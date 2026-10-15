#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../wemo_ctrl/wemo_ctrl.c"
#include "../wemo_ctrl/wemo_ipc_server.c"
extern char wemo_device_db[], wemo_state_db[];
static void snapshot(const char *udn, const char *mode, const char *availability, const char *binary) {
    char xml[1024];
    snprintf(xml,sizeof xml,"<e:propertyset xmlns:e='urn:schemas-upnp-org:event-1-0'><e:property><BinaryState>%s</BinaryState></e:property><e:property><MotionState>%s</MotionState></e:property><e:property><DetectionAvailable>%s</DetectionAvailable></e:property></e:propertyset>",binary,mode,availability);
    IXML_Document *doc=ixmlParseBuffer(xml); assert(doc);
    wemoStateUpdate((char *)udn,WEMO_SERVICE_BASICEVENT,doc,GlobalDeviceList->device.wemoService[WEMO_SERVICE_BASICEVENT].VariableStrVal);
    ixmlDocument_free(doc);
}
int main(int argc,char **argv) {
    assert(argc==2);
    snprintf(wemo_device_db,128,"%s/device.db",argv[1]);
    snprintf(wemo_state_db,128,"%s/state.db",argv[1]);
    assert(wemo_dev_db_init(&ctrlpt_dev_db,&ctrlpt_state_db)==DB_SUCCESS);
    ithread_mutex_init(&DeviceListMutex,NULL);
    const char *xml="<root><device><deviceType>urn:Belkin:device:NetCamSensor:1</deviceType><friendlyName>Fixture</friendlyName><manufacturer>Belkin</manufacturer><modelName>NetCam</modelName><UDN>uuid:NetCamSensor-fixture</UDN><serviceList><service><serviceType>urn:Belkin:service:basicevent:1</serviceType><serviceId>urn:Belkin:serviceId:basicevent1</serviceId><SCPDURL>/eventservice.xml</SCPDURL><controlURL>/upnp/control/basicevent1</controlURL><eventSubURL>/upnp/event/basicevent1</eventSubURL></service></serviceList></device></root>";
    IXML_Document *doc=ixmlParseBuffer(xml); assert(doc);
    wemoCtrlPointAddDevice(doc,"http://127.0.0.1:9/setup.xml",300);
    ixmlDocument_free(doc); assert(GlobalDeviceList);
    const char *udn=GlobalDeviceList->device.UDN;
    int id=wemo_dev_db_retrieve_id(ctrlpt_dev_db,(char*)udn); assert(id>0);
    assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==0);
    sqlite3_stmt *stmt=NULL;
    assert(sqlite3_prepare_v2(ctrlpt_dev_db,"SELECT device_type FROM wemo_device",-1,&stmt,NULL)==SQLITE_OK);
    assert(sqlite3_step(stmt)==SQLITE_ROW); assert(sqlite3_column_int(stmt,0)==WEMO_NETCAM_SENSOR); sqlite3_finalize(stmt);
    /* Rediscovery revives a retired row without allocating a new identity,
     * both while the node is cached and after it has expired from memory. */
    for (int expired=0; expired<2; expired++) {
        if (expired) {
            assert(wemoCtrlPointRemoveDevice("uuid:NetCamSensor-fixture")==CTRLPT_SUCCESS);
            assert(GlobalDeviceList==NULL);
        }
        assert(sqlite3_exec(ctrlpt_dev_db,
            "UPDATE wemo_device SET retired=1, retired_at=1, last_seen=1",
            NULL,NULL,NULL)==SQLITE_OK);
        wemo_dev_statedb_update_online(ctrlpt_state_db,id,0);
        doc=ixmlParseBuffer(xml); assert(doc);
        wemoCtrlPointAddDevice(doc,"http://127.0.0.2:9/setup.xml",300);
        ixmlDocument_free(doc);
        assert(GlobalDeviceList && GlobalDeviceList->next==NULL);
        udn=GlobalDeviceList->device.UDN;
        assert(wemo_dev_db_retrieve_id(ctrlpt_dev_db,(char*)udn)==id);
        assert(sqlite3_prepare_v2(ctrlpt_dev_db,
            "SELECT wemo_id,retired,retired_at,last_seen,ipaddr FROM wemo_device",
            -1,&stmt,NULL)==SQLITE_OK);
        assert(sqlite3_step(stmt)==SQLITE_ROW);
        assert(sqlite3_column_int(stmt,0)==id);
        assert(sqlite3_column_int(stmt,1)==0 && sqlite3_column_int(stmt,2)==0);
        assert(sqlite3_column_int64(stmt,3)>1);
        assert(!strcmp((const char*)sqlite3_column_text(stmt,4),"127.0.0.2"));
        assert(sqlite3_step(stmt)==SQLITE_DONE); sqlite3_finalize(stmt);
        struct we_device_list revived;
        assert(wemo_ctrl_list_devices(&revived)==WE_STATUS_OK);
        assert(revived.count==1 && revived.items[0].wemo_id==id && revived.items[0].is_online==1);
    }
    /* Disk is intentionally old while list must match the just-pushed cache. */
    sqlite3_exec(ctrlpt_state_db, "INSERT OR REPLACE INTO state_capability(wemo_id,cap,value) SELECT wemo_id,3,2 FROM state", NULL,NULL,NULL);
    snapshot(udn,"motion","1","1");
    struct we_device_list cached_list;
    assert(wemo_ctrl_list_devices(&cached_list)==WE_STATUS_OK);
    assert(cached_list.count==1 && cached_list.items[0].motion_snapshot==5);
    const char *omissions[]={"<propertyset><FriendlyName>x</FriendlyName></propertyset>","<propertyset><BinaryState>0</BinaryState></propertyset>"};
    int observed_seen=wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SEEN);
    for (unsigned j=0;j<2;j++) {
        IXML_Document *partial=ixmlParseBuffer(omissions[j]);assert(partial);
        wemoStateUpdate((char*)udn,WEMO_SERVICE_BASICEVENT,partial,GlobalDeviceList->device.wemoService[WEMO_SERVICE_BASICEVENT].VariableStrVal);
        ixmlDocument_free(partial);
        assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==5);
        assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SEEN)==observed_seen);
    }
    GlobalDeviceList->device.motion_action_legacy=1;
    IXML_Document *legacy_event=ixmlParseBuffer(omissions[1]);
    wemoStateUpdate((char*)udn,WEMO_SERVICE_BASICEVENT,legacy_event,GlobalDeviceList->device.wemoService[WEMO_SERVICE_BASICEVENT].VariableStrVal);
    ixmlDocument_free(legacy_event);
    assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==12);
    snapshot(udn,"motion","1","1"); assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==5);
    snapshot(udn,"unavailable","0","1"); assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==9);
    snapshot(udn,"paused","0","0"); assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==7);
    snapshot(udn,"idle","1","0"); assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==2);
    UpnpActionComplete *response=UpnpActionComplete_new(); assert(response);
    UpnpActionComplete_strcpy_CtrlUrl(response,GlobalDeviceList->device.wemoService[WEMO_SERVICE_BASICEVENT].ControlURL);
    UpnpActionComplete_set_ActionRequest(response,UpnpMakeAction("GetMotionStatus",wemoServiceType[WEMO_SERVICE_BASICEVENT],0,NULL));
    UpnpActionComplete_set_ErrCode(response,401);
    wemoCtrlPointCallbackEventHandler(UPNP_CONTROL_ACTION_COMPLETE,response,NULL); assert(GlobalDeviceList->device.motion_action_legacy==1);
    assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==0);
    assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SEEN)==0);
    UpnpActionComplete_delete(response);
    snapshot(udn,"motion","1","1");
    response=UpnpActionComplete_new();
    UpnpActionComplete_strcpy_CtrlUrl(response,GlobalDeviceList->device.wemoService[WEMO_SERVICE_BASICEVENT].ControlURL);
    UpnpActionComplete_set_ActionRequest(response,UpnpMakeAction("GetFriendlyName",wemoServiceType[WEMO_SERVICE_BASICEVENT],0,NULL));
    UpnpActionComplete_set_ErrCode(response,401);
    GlobalDeviceList->device.motion_action_legacy=0;
    wemoCtrlPointCallbackEventHandler(UPNP_CONTROL_ACTION_COMPLETE,response,NULL);
    assert(!GlobalDeviceList->device.motion_action_legacy);
    assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==5);
    UpnpActionComplete_delete(response);
    snapshot(udn,"future","1","1"); assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==1);
    strcpy(GlobalDeviceList->device.deviceType,"urn:Belkin:device:controllee:1");
    snapshot(udn,"motion","1","1"); assert(wemo_dev_db_get_capability(ctrlpt_state_db,id,CAP_MOTION_SNAPSHOT)==1);
    const char *models[]={"Socket","LightSwitch","Dimmer","Insight","Sensor"};
    const int types[]={WEMO_MINI,WEMO_LIGHT,WEMO_DIMMER,WEMO_INSIGHT,WEMO_SENSOR};
    for (unsigned i=0;i<sizeof types/sizeof *types;i++) {
        struct wemoDevice fixture={0}; snprintf(fixture.UDN,sizeof fixture.UDN,"uuid:fixture-%u",i);
        strcpy(fixture.modelName,models[i]); strcpy(fixture.firmwareVersion,"unknown");
        strcpy(fixture.deviceType,"urn:Belkin:device:controllee:1");
        wemo_dev_db_insert(ctrlpt_dev_db,&fixture);
        assert(sqlite3_prepare_v2(ctrlpt_dev_db,"SELECT device_type FROM wemo_device WHERE UDN=?",-1,&stmt,NULL)==SQLITE_OK);
        sqlite3_bind_text(stmt,1,fixture.UDN,-1,SQLITE_STATIC);
        assert(sqlite3_step(stmt)==SQLITE_ROW && sqlite3_column_int(stmt,0)==types[i]); sqlite3_finalize(stmt);
    }
    char oversized[2048],huge[400]; memset(huge,'x',sizeof huge-1);huge[sizeof huge-1]=0;
    snprintf(oversized,sizeof oversized,"<root><device><deviceType>urn:Belkin:device:NetCamSensor:1</deviceType><friendlyName>Fixture</friendlyName><manufacturer>Belkin</manufacturer><modelName>NetCam</modelName><UDN>%s</UDN></device></root>",huge);
    doc=ixmlParseBuffer(oversized);assert(doc);wemoCtrlPointAddDevice(doc,"http://127.0.0.1:9/setup.xml",300);ixmlDocument_free(doc);
    assert(GlobalDeviceList->next==NULL); // oversized identity cannot alias an existing device
    wemo_dev_db_finish(ctrlpt_dev_db,ctrlpt_state_db);
    puts("PASS: real discovery + GENA callback, sparse metadata, exact type/length guards, legacy fallback and plug/light/sensor regressions");
}
