#include "wemo_bridge/wemo_adapter_openwemo.h"
#include "wemo_bridge/motion_sensor_state.h"
#include <cassert>
#include <cstring>
#include <iostream>
extern "C" {
#define export we_export_param
#include "wemo_engine.h"
#undef export
}
static we_device_list devices{};
static int status=WE_STATUS_OK, writes=0;
static event_callback_t callback=nullptr;
extern "C" {
int we_init(){return 1;}
int we_discover(int){return 1;}
int we_set_ipc_target(const char*,int){return 1;}
int we_list_devices(we_device_list *out){*out=devices;return status;}
int we_set_action(int,we_state*){++writes;return 1;}
int we_change_name(int,we_name_change*){return 1;}
int we_register_event_callback(event_callback_t cb){callback=cb;return 1;}
}
int main(){
 auto initial=wemo_bridge::ApplyMotionSensorSnapshot(13,true,false);assert(!initial.reachable&&!initial.occupied);
 auto motion=wemo_bridge::ApplyMotionSensorSnapshot(5,true,false);assert(motion.reachable&&motion.occupied);
 for(int invalid:{0,1,3,4,7,9,11,12,13,-1,999}) { auto unavailable=wemo_bridge::ApplyMotionSensorSnapshot(invalid,true,true);assert(!unavailable.reachable&&unavailable.occupied); }
 auto offline=wemo_bridge::ApplyMotionSensorSnapshot(2,false,true);assert(!offline.reachable&&offline.occupied);
 auto idle=wemo_bridge::ApplyMotionSensorSnapshot(2,true,true);assert(idle.reachable&&!idle.occupied);
 devices.count=5; const int types[]={WEMO_MINI,WEMO_LIGHT,WEMO_DIMMER,WEMO_SENSOR,WEMO_NETCAM_SENSOR};
 for(int i=0;i<devices.count;i++){auto &d=devices.items[i];d.wemo_id=i+1;d.device_type=types[i];d.level=-1;d.is_online=1;std::snprintf(d.udn,sizeof d.udn,"fixture-%d",i);}
 devices.items[2].level=50; devices.items[4].motion_snapshot=5; devices.items[4].state=1;devices.items[4].level=75;
 wemo_bridge::WemoAdapterOpenWemo adapter("127.0.0.1:1"); auto list=adapter.ListCachedDevices();assert(list.size()==5);
 assert(list[0].is_plug && !list[0].is_sensor);assert(!list[1].is_sensor);assert(list[2].supports_level);
 assert(!list[3].is_sensor && !list[3].is_plug); // retain prior generic sensor endpoint behavior
 assert(list[4].is_sensor && !list[4].is_plug && !list[4].supports_level && list[4].motion_snapshot==5);
 assert(!adapter.SetOnOff("fixture-4",true));assert(!adapter.SetLevelPercent("fixture-4",50));assert(writes==0);
 assert(adapter.SetOnOff("fixture-0",true));assert(writes==1);
 wemo_bridge::WemoStateEvent event{};
 adapter.RegisterStateCallback([&](const auto &e){event=e;});assert(callback);
 we_state state{};state.is_online=1;state.state=1;state.level=-1;state.motion_snapshot=9;
 callback(5,&state);assert(event.motion_snapshot==9);
 status=WE_STATUS_INVALID;list=adapter.ListCachedDevices();assert(list.size()==5);
 for(const auto &d:list)assert(d.is_online == !d.is_sensor);assert(list[4].motion_snapshot==1);
 status=WE_STATUS_OK;devices.count=0;assert(adapter.ListCachedDevices().empty());
 std::cout<<"PASS: NetCam-only classification, readonly sensor, plug/light regressions, atomic push and IPC failure preserves topology\n";
}
