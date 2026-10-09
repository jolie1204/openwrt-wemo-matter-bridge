#include "wemo_bridge/endpoint_registry.h"
#include "wemo_bridge/sensor_endpoint_id_guard.h"
#include <sqlite3.h>
#include <cassert>
#include <iostream>
#include <memory>
#include "wemo_bridge/published_slot.h"
int main(int argc,char **argv) {
 struct Slot { int wemo_id=0; bool is_sensor=false; std::unique_ptr<int> device; };
 Slot slot;slot.wemo_id=7;slot.is_sensor=true;slot.device=std::make_unique<int>(1);
 assert(wemo_bridge::PublishedSlotMatches(slot,7));
 wemo_bridge::ClearUnpublishedSlot(slot);
 assert(!slot.is_sensor && slot.wemo_id==0 && !wemo_bridge::PublishedSlotMatches(slot,7));
 slot.wemo_id=7; // Defense in depth for a stale identity after failed publication.
 assert(!wemo_bridge::PublishedSlotMatches(slot,7));
 uint16_t legacyNext=17;
 { wemo_bridge::SensorEndpointIdGuard guard(legacyNext,true); guard.Assign(0x8000); assert(legacyNext==0x8000); }
 assert(legacyNext==17);
 auto failingRegistration=[&]() { wemo_bridge::SensorEndpointIdGuard guard(legacyNext,true);guard.Assign(0x8001);return false; };
 assert(!failingRegistration()); assert(legacyNext==17);
 { wemo_bridge::SensorEndpointIdGuard guard(legacyNext,false); ++legacyNext; }
 assert(legacyNext==18);
 assert(argc==2); const char *path=argv[1];
 wemo_bridge::EndpointRegistry registry(path,0x8000);
 assert(registry.GetOrAssign("fixture-a")==0x8000);
 assert(registry.GetOrAssign("fixture-b")==0x8001);
 wemo_bridge::EndpointRegistry restarted(path,0x8000);
 assert(restarted.GetOrAssign("fixture-b")==0x8001);
 assert(restarted.GetOrAssign("fixture-a")==0x8000);
 assert(restarted.GetOrAssign("fixture-c")==0x8002);
 sqlite3 *reader=nullptr; assert(sqlite3_open(path,&reader)==SQLITE_OK);
 assert(sqlite3_exec(reader,"BEGIN; SELECT * FROM endpoint_map;",nullptr,nullptr,nullptr)==SQLITE_OK);
 assert(!restarted.GetOrAssign("fixture-commit-busy")); // COMMIT blocked by reader; no ID may escape.
 assert(sqlite3_exec(reader,"ROLLBACK;",nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(reader);
 assert(restarted.GetOrAssign("fixture-after-failure")==0x8003); // aborted allocation rolled back.
 sqlite3 *db=nullptr; assert(sqlite3_open(path,&db)==SQLITE_OK);
 assert(sqlite3_exec(db,"UPDATE endpoint_map SET endpoint_id=2 WHERE udn='fixture-a'",nullptr,nullptr,nullptr)==SQLITE_OK);
 assert(!restarted.GetOrAssign("fixture-a"));
 assert(sqlite3_exec(db,"UPDATE bridge_meta SET value='65534'",nullptr,nullptr,nullptr)==SQLITE_OK);
 assert(!restarted.GetOrAssign("fixture-overflow"));
 assert(sqlite3_exec(db,"UPDATE bridge_meta SET value='garbage'",nullptr,nullptr,nullptr)==SQLITE_OK);
 assert(!restarted.GetOrAssign("fixture-corrupt")); sqlite3_close(db);
 std::cout<<"PASS: reserved sensor range, restart/order persistence, exhaustion/corrupt metadata fail closed\n";
}
