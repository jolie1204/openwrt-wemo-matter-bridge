#include "wemo_bridge/endpoint_registry.h"

#include <sqlite3.h>

#include <filesystem>
#include <string>

namespace wemo_bridge {

namespace {

constexpr uint16_t kFirstDynamicEndpointId = 2;
constexpr const char * kCreateTablesSql    =
    "CREATE TABLE IF NOT EXISTS endpoint_map ("
    "  udn TEXT PRIMARY KEY,"
    "  endpoint_id INTEGER NOT NULL UNIQUE"
    ");"
    "CREATE TABLE IF NOT EXISTS bridge_meta ("
    "  key TEXT PRIMARY KEY,"
    "  value TEXT NOT NULL"
    ");";

bool ExecSql(sqlite3 * db, const char * sql)
{
    char * error_msg = nullptr;
    const int rc     = sqlite3_exec(db, sql, nullptr, nullptr, &error_msg);
    if (error_msg != nullptr)
    {
        sqlite3_free(error_msg);
    }
    return rc == SQLITE_OK;
}

std::optional<uint16_t> QueryEndpointId(sqlite3 * db, const std::string & udn)
{
    sqlite3_stmt * stmt = nullptr;
    const char * sql    = "SELECT endpoint_id FROM endpoint_map WHERE udn = ?1;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, udn.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<uint16_t> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        result = static_cast<uint16_t>(sqlite3_column_int(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return result;
}

uint16_t QueryNextEndpointId(sqlite3 * db)
{
    sqlite3_stmt * stmt = nullptr;
    const char * sql    = "SELECT value FROM bridge_meta WHERE key = 'next_endpoint_id';";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return kFirstDynamicEndpointId;
    }

    uint16_t next_id = kFirstDynamicEndpointId;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char * value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (value != nullptr)
        {
            next_id = static_cast<uint16_t>(std::stoi(value));
        }
    }
    sqlite3_finalize(stmt);
    return next_id;
}

bool UpsertNextEndpointId(sqlite3 * db, uint16_t next_id)
{
    sqlite3_stmt * stmt = nullptr;
    const char * sql    =
        "INSERT INTO bridge_meta(key, value) VALUES('next_endpoint_id', ?1) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    const std::string next_id_str = std::to_string(next_id);
    sqlite3_bind_text(stmt, 1, next_id_str.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool InsertMapping(sqlite3 * db, const std::string & udn, uint16_t endpoint_id)
{
    sqlite3_stmt * stmt = nullptr;
    const char * sql    = "INSERT INTO endpoint_map(udn, endpoint_id) VALUES(?1, ?2);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    sqlite3_bind_text(stmt, 1, udn.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, endpoint_id);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace

EndpointRegistry::EndpointRegistry(std::string path) : mPath(std::move(path)) {}

std::optional<uint16_t> EndpointRegistry::Lookup(const std::string & udn) const
{
    std::filesystem::path db_path(mPath);
    if (db_path.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(db_path.parent_path(), ec);
    }

    sqlite3 * db = nullptr;
    if (sqlite3_open(mPath.c_str(), &db) != SQLITE_OK)
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        return std::nullopt;
    }

    if (!ExecSql(db, kCreateTablesSql))
    {
        sqlite3_close(db);
        return std::nullopt;
    }

    const auto endpoint_id = QueryEndpointId(db, udn);
    sqlite3_close(db);
    return endpoint_id;
}

std::optional<uint16_t> EndpointRegistry::GetOrAssign(const std::string & udn)
{
    std::filesystem::path db_path(mPath);
    if (db_path.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(db_path.parent_path(), ec);
    }

    sqlite3 * db = nullptr;
    if (sqlite3_open(mPath.c_str(), &db) != SQLITE_OK)
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        return std::nullopt;
    }

    if (!ExecSql(db, kCreateTablesSql) || !ExecSql(db, "BEGIN IMMEDIATE TRANSACTION;"))
    {
        sqlite3_close(db);
        return std::nullopt;
    }

    const auto existing = QueryEndpointId(db, udn);
    if (existing.has_value())
    {
        ExecSql(db, "COMMIT;");
        sqlite3_close(db);
        return existing;
    }

    const uint16_t endpoint_id = QueryNextEndpointId(db);
    const uint16_t next_id     = static_cast<uint16_t>(endpoint_id + 1);

    if (!InsertMapping(db, udn, endpoint_id) || !UpsertNextEndpointId(db, next_id))
    {
        ExecSql(db, "ROLLBACK;");
        sqlite3_close(db);
        return std::nullopt;
    }

    ExecSql(db, "COMMIT;");
    sqlite3_close(db);
    return endpoint_id;
}

} // namespace wemo_bridge
