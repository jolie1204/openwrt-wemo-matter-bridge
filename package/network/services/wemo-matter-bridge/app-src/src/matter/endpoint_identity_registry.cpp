#include "wemo_bridge/endpoint_identity_registry.h"

#include <sqlite3.h>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <set>
#include <cstring>

namespace wemo_bridge {
namespace {
constexpr const char * kSchema =
    "CREATE TABLE IF NOT EXISTS endpoint_identity ("
    "udn TEXT NOT NULL PRIMARY KEY CHECK(length(CAST(udn AS BLOB)) BETWEEN 6 AND 256),"
    "unique_id TEXT NOT NULL UNIQUE CHECK(length(CAST(unique_id AS BLOB)) BETWEEN 1 AND 32)"
    ");";

bool Exec(sqlite3 * db, const char * sql)
{
    // No SQLite error strings: they may contain private UDN or identity values.
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool ValidUniqueId(const std::string & value)
{
    if (value.empty() || value.size() > 32) return false;
    for (unsigned char c : value) if (c < 33 || c > 126) return false;
    return true;
}

std::string NormalizeSchema(const std::string & sql)
{
    std::string normalized;
    for (unsigned char c : sql) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') continue;
        normalized += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    if (!normalized.empty() && normalized.back() == ';') normalized.pop_back();
    return normalized;
}

bool SchemaMatches(sqlite3 * db)
{
    sqlite3_stmt * statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT type,sql FROM sqlite_master WHERE name='endpoint_identity';", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_type(statement, 0) == SQLITE_TEXT &&
              sqlite3_column_type(statement, 1) == SQLITE_TEXT;
    if (ok) {
        const auto * type = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        const auto * sql = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
        std::string expected = NormalizeSchema(kSchema);
        expected.erase(expected.find("ifnotexists"), std::string("ifnotexists").size());
        ok = type && std::string(type) == "table" && sql && NormalizeSchema(sql) == expected &&
             sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    statement = nullptr;
    if (ok) {
        ok = sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='trigger' AND tbl_name='endpoint_identity' LIMIT 1;", -1, &statement, nullptr) == SQLITE_OK;
        if (ok) ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
    }
    return ok;
}

bool UniqueOwnershipMatches(sqlite3 * db, const std::string & uid, const std::optional<std::string> & owner)
{
    sqlite3_stmt * statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT udn FROM endpoint_identity WHERE unique_id=?1;", -1, &statement, nullptr) != SQLITE_OK)
        return false;
    bool ok = sqlite3_bind_text(statement, 1, uid.data(), static_cast<int>(uid.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    const int step = ok ? sqlite3_step(statement) : SQLITE_ERROR;
    if (owner) {
        ok = step == SQLITE_ROW && sqlite3_column_type(statement, 0) == SQLITE_TEXT;
        if (ok) {
            const auto * text = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
            const int size = sqlite3_column_bytes(statement, 0);
            ok = text && size > 0 && std::string(text, static_cast<size_t>(size)) == *owner &&
                 sqlite3_step(statement) == SQLITE_DONE;
        }
    } else ok = step == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

bool ValidateRows(sqlite3 * db)
{
    sqlite3_stmt * statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT udn,unique_id FROM endpoint_identity;", -1, &statement, nullptr) != SQLITE_OK) return false;
    bool ok = true;
    int step = SQLITE_ERROR;
    std::set<std::string> udns, identities;
    while (ok && (step = sqlite3_step(statement)) == SQLITE_ROW) {
        if (udns.size() >= 4096 || sqlite3_column_type(statement, 0) != SQLITE_TEXT || sqlite3_column_type(statement, 1) != SQLITE_TEXT) { ok = false; break; }
        const auto * udnText = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
        const auto * uidText = reinterpret_cast<const char *>(sqlite3_column_text(statement, 1));
        const int udnSize = sqlite3_column_bytes(statement, 0), uidSize = sqlite3_column_bytes(statement, 1);
        if (!udnText || !uidText || udnSize < 6 || udnSize > 256 || uidSize < 1 || uidSize > 32) { ok = false; break; }
        std::string udn(udnText, static_cast<size_t>(udnSize)), uid(uidText, static_cast<size_t>(uidSize));
        const auto canonical = CanonicalIdentityUdn(udn);
        ok = canonical && *canonical == udn && ValidUniqueId(uid) && udns.insert(udn).second && identities.insert(uid).second;
    }
    sqlite3_finalize(statement);
    return ok && step == SQLITE_DONE;
}

bool PreparePrivateFile(const std::string & path, bool create)
{
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return false;
    std::error_code error;
    const bool made = create && std::filesystem::create_directories(parent, error);
    if (error) return false;
    if (made && chmod(parent.c_str(), 0700) != 0) return false;
    struct stat st {};
    if (lstat(parent.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) return false;
    const int fd = create ? open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600) :
                            open(path.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return false;
    bool safe = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 0077) == 0;
    if (safe && !create) {
        char header[16] {};
        safe = st.st_size >= 100 && pread(fd, header, sizeof(header), 0) == static_cast<ssize_t>(sizeof(header)) &&
               std::memcmp(header, "SQLite format 3\0", sizeof(header)) == 0;
    }
    close(fd);
    return safe;
}
} // namespace

std::optional<std::string> CanonicalIdentityUdn(const std::string & udn)
{
    if (udn.size() < 6 || udn.size() > 256 || udn[4] != ':') return std::nullopt;
    const char prefix[] = "uuid";
    for (size_t i = 0; i < 4; ++i)
        if (udn[i] != prefix[i] && udn[i] != prefix[i] - ('a' - 'A')) return std::nullopt;
    for (unsigned char c : udn) if (c < 33 || c > 126) return std::nullopt;
    return "uuid:" + udn.substr(5);
}

EndpointIdentityRegistry::EndpointIdentityRegistry(std::string path) : mPath(std::move(path)) {}

bool EndpointIdentityRegistry::InitializeEmpty(const std::string & path)
{
    if (!PreparePrivateFile(path, true)) return false;
    sqlite3 * db = nullptr;
    bool ok = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr) == SQLITE_OK;
    if (ok) ok = Exec(db, "PRAGMA synchronous=EXTRA;") && Exec(db, kSchema) && SchemaMatches(db);
    if (db) sqlite3_close(db);
    if (!ok) unlink(path.c_str()); // Only the exclusively created provisioning file.
    return ok;
}

std::optional<std::string> EndpointIdentityRegistry::GetOrAssign(const std::string & udn, const std::string & candidate)
{
    const auto canonical = CanonicalIdentityUdn(udn);
    if (!canonical || !ValidUniqueId(candidate) || !PreparePrivateFile(mPath, false)) return std::nullopt;
    sqlite3 * db = nullptr;
    if (sqlite3_open_v2(mPath.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return std::nullopt;
    }
    std::optional<std::string> result;
    if (Exec(db, "PRAGMA synchronous=EXTRA;") && Exec(db, "BEGIN IMMEDIATE;"))
    {
        sqlite3_stmt * statement = nullptr;
        bool ok = SchemaMatches(db) && ValidateRows(db) && sqlite3_prepare_v2(db, "SELECT unique_id FROM endpoint_identity WHERE udn=?1;", -1, &statement, nullptr) == SQLITE_OK;
        int step = SQLITE_ERROR;
        if (ok) {
            ok = sqlite3_bind_text(statement, 1, canonical->data(), static_cast<int>(canonical->size()), SQLITE_TRANSIENT) == SQLITE_OK;
            if (ok) step = sqlite3_step(statement);
        }
        if (ok && step == SQLITE_ROW && sqlite3_column_type(statement, 0) == SQLITE_TEXT) {
            const auto * text = reinterpret_cast<const char *>(sqlite3_column_text(statement, 0));
            const int size = sqlite3_column_bytes(statement, 0);
            if (text && size > 0 && size <= 32) {
                std::string existing(text, static_cast<size_t>(size));
                if (ValidUniqueId(existing)) result = std::move(existing);
            }
            ok = result.has_value() && sqlite3_step(statement) == SQLITE_DONE && UniqueOwnershipMatches(db, *result, canonical);
        } else if (ok && step == SQLITE_DONE) {
            sqlite3_finalize(statement);
            statement = nullptr;
            ok = UniqueOwnershipMatches(db, candidate, std::nullopt) && sqlite3_prepare_v2(db, "INSERT INTO endpoint_identity(udn,unique_id) VALUES(?1,?2);", -1, &statement, nullptr) == SQLITE_OK;
            if (ok) ok = sqlite3_bind_text(statement, 1, canonical->data(), static_cast<int>(canonical->size()), SQLITE_TRANSIENT) == SQLITE_OK &&
                         sqlite3_bind_text(statement, 2, candidate.data(), static_cast<int>(candidate.size()), SQLITE_TRANSIENT) == SQLITE_OK;
            if (ok) ok = sqlite3_step(statement) == SQLITE_DONE;
            if (ok) result = candidate;
        } else ok = false;
        sqlite3_finalize(statement);
        // A failed COMMIT must never return even an existing identity.
        if (!ok || !Exec(db, "COMMIT;")) { Exec(db, "ROLLBACK;"); result.reset(); }
    }
    sqlite3_close(db);
    return result;
}
} // namespace wemo_bridge
