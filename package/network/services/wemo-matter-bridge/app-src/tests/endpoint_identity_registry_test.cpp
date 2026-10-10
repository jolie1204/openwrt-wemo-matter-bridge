#include "wemo_bridge/endpoint_identity_registry.h"
#include <sqlite3.h>
#include <sys/stat.h>
#include <cassert>
#include <iostream>
#include <string>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char ** argv)
{
    assert(argc == 2);
    const std::string path = argv[1];
    using wemo_bridge::EndpointIdentityRegistry;
    assert(!EndpointIdentityRegistry(path).GetOrAssign("uuid:missing", "GEN-no-recreation"));
    assert(access(path.c_str(), F_OK) != 0);
    assert(EndpointIdentityRegistry::InitializeEmpty(path));
    assert(!EndpointIdentityRegistry::InitializeEmpty(path));
    EndpointIdentityRegistry registry(path);
    assert(registry.GetOrAssign("UUID:fixture-a", "GEN-original-a") == "GEN-original-a");
    assert(registry.GetOrAssign("uuid:fixture-b", "GEN-original-b") == "GEN-original-b");
    EndpointIdentityRegistry reboot(path);
    assert(reboot.GetOrAssign("uuid:fixture-b", "GEN-new-b") == "GEN-original-b");
    assert(reboot.GetOrAssign("uuid:fixture-a", "GEN-new-a") == "GEN-original-a");
    assert(!reboot.GetOrAssign("uuid:fixture-collision", "GEN-original-a"));
    assert(!reboot.GetOrAssign("invalid", "valid"));
    assert(!reboot.GetOrAssign("uuid:", "valid"));
    assert(!reboot.GetOrAssign("uuid:space bad", "valid"));
    assert(!reboot.GetOrAssign("uuid:null", std::string("abc\0def", 7)));
    assert(!reboot.GetOrAssign("uuid:oversize", std::string(33, 'a')));
    struct stat st {};
    assert(stat(path.c_str(), &st) == 0 && (st.st_mode & 0077) == 0);
    assert(!EndpointIdentityRegistry(path + "/impossible").GetOrAssign("uuid:unwritable", "valid"));

    sqlite3 * reader = nullptr;
    assert(sqlite3_open(path.c_str(), &reader) == SQLITE_OK);
    assert(sqlite3_exec(reader, "BEGIN; SELECT * FROM endpoint_identity;", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(!reboot.GetOrAssign("uuid:commit-busy", "GEN-must-not-escape"));
    assert(sqlite3_exec(reader, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(reader);
    assert(reboot.GetOrAssign("uuid:commit-busy", "GEN-after-failure") == "GEN-after-failure");

    sqlite3 * db = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    // Import of exact, privately captured legacy identities preserves their value.
    assert(sqlite3_exec(db, "INSERT INTO endpoint_identity VALUES('uuid:legacy-seed','GEN-exact-before-update');", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(reboot.GetOrAssign("uuid:legacy-seed", "GEN-new-candidate") == "GEN-exact-before-update");
    assert(sqlite3_exec(db, "INSERT INTO endpoint_identity VALUES('uuid:duplicate','GEN-original-a');", nullptr, nullptr, nullptr) == SQLITE_CONSTRAINT);
    assert(sqlite3_exec(db, "INSERT INTO endpoint_identity VALUES('uuid:empty','');", nullptr, nullptr, nullptr) == SQLITE_CONSTRAINT);
    assert(sqlite3_exec(db, "PRAGMA ignore_check_constraints=ON; UPDATE endpoint_identity SET unique_id='' WHERE udn='uuid:fixture-a';", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(!reboot.GetOrAssign("uuid:fixture-a", "GEN-do-not-replace-corruption"));
    sqlite3_close(db);
    assert(chmod(path.c_str(), 0644) == 0);
    assert(!reboot.GetOrAssign("uuid:public-file", "GEN-not-private"));
    const auto corruptPath = path + ".bad-schema";
    assert(sqlite3_open(corruptPath.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "CREATE TABLE endpoint_identity(udn TEXT,unique_id TEXT);"
                           "INSERT INTO endpoint_identity VALUES('uuid:owner-a','GEN-duplicate'),('uuid:owner-b','GEN-duplicate');", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    assert(chmod(corruptPath.c_str(), 0600) == 0);
    EndpointIdentityRegistry corrupt(corruptPath);
    assert(!corrupt.GetOrAssign("uuid:owner-a", "GEN-candidate"));
    assert(!corrupt.GetOrAssign("uuid:owner-c", "GEN-duplicate"));
    assert(!corrupt.GetOrAssign("uuid:owner-d", "GEN-would-insert"));
    assert(sqlite3_open(corruptPath.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt * count = nullptr;
    assert(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM endpoint_identity;", -1, &count, nullptr) == SQLITE_OK);
    assert(sqlite3_step(count) == SQLITE_ROW && sqlite3_column_int(count, 0) == 2);
    sqlite3_finalize(count);sqlite3_close(db);
    const auto triggerPath = path + ".trigger";
    assert(EndpointIdentityRegistry::InitializeEmpty(triggerPath));
    EndpointIdentityRegistry triggered(triggerPath);
    assert(triggered.GetOrAssign("uuid:trigger-initial", "GEN-initial") == "GEN-initial");
    assert(sqlite3_open(triggerPath.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "CREATE TRIGGER rewrite_identity AFTER INSERT ON endpoint_identity BEGIN UPDATE endpoint_identity SET unique_id='GEN-rewritten' WHERE udn=NEW.udn; END;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    assert(!triggered.GetOrAssign("uuid:trigger-initial", "GEN-candidate"));
    assert(!triggered.GetOrAssign("uuid:trigger-new", "GEN-must-not-escape"));
    const auto zeroPath = path + ".zero";
    const int zero = open(zeroPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    assert(zero >= 0);close(zero);
    assert(!EndpointIdentityRegistry(zeroPath).GetOrAssign("uuid:lost", "GEN-no-regeneration"));
    assert(stat(zeroPath.c_str(), &st) == 0 && st.st_size == 0);
    const auto invalidSeed = path + ".invalid-seed";
    assert(EndpointIdentityRegistry::InitializeEmpty(invalidSeed));
    assert(sqlite3_open(invalidSeed.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "INSERT INTO endpoint_identity VALUES('UUID:noncanonical','GEN-existing');", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(!EndpointIdentityRegistry(invalidSeed).GetOrAssign("uuid:other", "GEN-new"));
    assert(sqlite3_exec(db, "DELETE FROM endpoint_identity; INSERT INTO endpoint_identity VALUES(CAST('uuid:blob' AS BLOB),'GEN-existing');", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(!EndpointIdentityRegistry(invalidSeed).GetOrAssign("uuid:other", "GEN-new"));
    assert(sqlite3_exec(db, "DELETE FROM endpoint_identity; INSERT INTO endpoint_identity VALUES('uuid:blob',CAST('GEN-existing' AS BLOB));", nullptr, nullptr, nullptr) == SQLITE_OK);
    assert(!EndpointIdentityRegistry(invalidSeed).GetOrAssign("uuid:other", "GEN-new"));
    sqlite3_close(db);
    assert(unlink(triggerPath.c_str()) == 0);
    assert(!EndpointIdentityRegistry(triggerPath).GetOrAssign("uuid:trigger-initial", "GEN-must-not-rotate"));
    assert(access(triggerPath.c_str(), F_OK) != 0);
    std::cout << "PASS: persistent canonical UDN identities, seed preservation, collisions, corruption and COMMIT errors fail closed\n";
}
