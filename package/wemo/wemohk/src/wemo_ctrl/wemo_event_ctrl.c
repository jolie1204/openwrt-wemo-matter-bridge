#include <sqlite3.h>
#include "wemo_event_ctrl.h"
#include "wemo_device_db.h"
#include "wemo_ipc_server.h"
#include "logger.h"

extern sqlite3 *ctrlpt_state_db;

int wemoCtrlPointGetPower( int devnum )
{
    return wemoCtrlPointSendActionEx(WEMO_SERVICE_BASICEVENT, devnum,
                                     "GetBinaryState", NULL, NULL, 0, 0);
}

int wemoCtrlPointGetLevel(int devnum)
{
    return wemoCtrlPointSendActionEx(WEMO_SERVICE_BASICEVENT, devnum,
                                     "GetBinaryState", NULL, NULL, 0, 0);
}

int wemoCtrlPointRetrieveState(int wemo_id, struct we_state *state_data)
{
    struct wemoDeviceNode *devnode;
    IXML_Document *actionNode = NULL;
    int rc = CTRLPT_SUCCESS;
    int dev_id = -1;

    if ((dev_id = wemoCtrlPointGetDevID(wemo_id)) == -1) {
        fprintf(stderr, "device %d not found\n", wemo_id);

        wemo_dev_statedb_update_online(ctrlpt_state_db, wemo_id, 0);

        struct we_state state_buffer;
        state_buffer.is_online = 0;
        state_buffer.state = 0;
        state_buffer.level = -1;
        APP_LOG("WEMOHK", LOG_DEBUG, "sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                          wemo_id,
                          state_buffer.is_online,
                          state_buffer.state,
                          state_buffer.level);

        wemo_ipc_send_event(wemo_id, &state_buffer);

        return CTRLPT_ERROR;
    }
    else {
        ithread_mutex_lock( &DeviceListMutex );
        rc = wemoCtrlPointGetDevice( dev_id, &devnode );
        if( CTRLPT_SUCCESS == rc ) {
            actionNode = UpnpMakeAction("GetBinaryState",
                                        wemoServiceType[WEMO_SERVICE_BASICEVENT],
                                        0,
                                        NULL);

            rc = UpnpSendActionAsync(ctrlpt_handle,
                                     devnode->device.wemoService[WEMO_SERVICE_BASICEVENT].ControlURL,
                                     wemoServiceType[WEMO_SERVICE_BASICEVENT],
                                     NULL,
                                     actionNode,
                                     (Upnp_FunPtr) wemoCtrlPointCallbackEventHandler,
                                     NULL);
            if( rc != UPNP_E_SUCCESS ) {
                APP_LOG("WEMOHK", LOG_DEBUG,  "Error in UpnpSendActionAsync -- %d", rc );
                rc = CTRLPT_ERROR;
            }
        }
        ithread_mutex_unlock( &DeviceListMutex );

        if( actionNode )
            ixmlDocument_free( actionNode );
    }
    return rc;
}

int wemoCtrlPointSendPowerOn(int devnum, int async)
{
    char *state[] = { "BinaryState" };
    char *value[] = { "1" };

    return wemoCtrlPointSendActionEx(WEMO_SERVICE_BASICEVENT, devnum,
                                     "SetBinaryState", state, value, 1, async);
}

int wemoCtrlPointSendPowerOff(int devnum, int async)
{
    char *state[] = { "BinaryState" };
    char *value[] = { "0" };

    return wemoCtrlPointSendActionEx(WEMO_SERVICE_BASICEVENT, devnum,
                                     "SetBinaryState", state, value, 1, async);
}

int wemoCtrlPointSetLevel(int devnum, int level, int async)
{
    char *brightness[] = {"brightness"};
    char value[4];
    char *param;

    if ((level < 0) || (level > 100)) {
        return CTRLPT_ERROR;
    }

    sprintf(value, "%d", level);

    param = value;
    return wemoCtrlPointSendActionEx(WEMO_SERVICE_BASICEVENT, devnum,
                                     "SetBinaryState", brightness, &param, 1, async);
}

int wemoCtrlPointSetDimmer(int devnum, int state, int level, int async)
{
    char *set_dimmer[] = {"BinaryState", "brightness"};
    char *value[] = {"", ""};
    char state_str[4];
    char value_str[4];

    snprintf(state_str, 4, "%d", state);
    snprintf(value_str, 4, "%d", level);

    value[0] = state_str;
    value[1] = value_str;
    return wemoCtrlPointSendActionEx(WEMO_SERVICE_BASICEVENT, devnum,
                                     "SetBinaryState", set_dimmer, (char **) value, 2, async);
}

int wemoCtrlPointTriggerAction(int wemo_id, struct we_state *state_data, int async)
{
    int dev_id = -1;
    struct we_state state_buffer;

    if ((dev_id = wemoCtrlPointGetDevID(wemo_id)) == -1) {
        fprintf(stderr, "device %d not found\n", wemo_id);

        wemo_dev_statedb_update_online(ctrlpt_state_db, wemo_id, 0);

        state_buffer.is_online = 0;
        state_buffer.state = 0;
        state_buffer.level = -1;
        APP_LOG("WEMOHK", LOG_DEBUG, "sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                          wemo_id,
                          state_buffer.is_online,
                          state_buffer.state,
                          state_buffer.level);

        wemo_ipc_send_event(wemo_id, &state_buffer);
        return CTRLPT_ERROR;
    }
    else {
        if (state_data->level != -1) {
            if (wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL) == state_data->level) {
                if ((wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_BINARY) == state_data->state) ||
                    (state_data->state == -1)) {
                    state_buffer.is_online = 1;
                    state_buffer.state = wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_BINARY);
                    state_buffer.level = state_data->level;
                    APP_LOG("WEMOHK", LOG_DEBUG, "sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                                      wemo_id,
                                      state_buffer.is_online,
                                      state_buffer.state,
                                      state_buffer.level);

                    wemo_ipc_send_event(wemo_id, &state_buffer);
        }
        else {
                    if (state_data->state == 1) {
                        wemoCtrlPointSendPowerOn(dev_id, async);
        }
                    else if (state_data->state == 0) {
                        wemoCtrlPointSendPowerOff(dev_id, async);
    }

                }
    }
    else {
        if (state_data->state != -1) {
                    wemoCtrlPointSetDimmer(dev_id, state_data->state, state_data->level, async);
            }
            else {
                    wemoCtrlPointSetLevel(dev_id, state_data->level, async);
            }
        }
        }
        else if (state_data->state != -1) {
            if (wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_BINARY) == state_data->state) {
                state_buffer.is_online = 1;
                state_buffer.state = state_data->state;
                state_buffer.level = wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL);
                APP_LOG("WEMOHK", LOG_DEBUG, "sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                                  wemo_id,
                                  state_buffer.is_online,
                                  state_buffer.state,
                                  state_buffer.level);

                wemo_ipc_send_event(wemo_id, &state_buffer);
            }
            else {
                if (state_data->state) {
                    wemoCtrlPointSendPowerOn(dev_id, async);
                }
                else {
                    wemoCtrlPointSendPowerOff(dev_id, async);
                }
            }
        }
    }
    return CTRLPT_SUCCESS;
}

int wemoCtrlPointSetHKSetupState(int wemo_id, struct we_hksetup_state *state)
{
    char *arguments[] = { "HKSetupDone" };
    char *value[1];
    int dev_id;
    int ret = 0;

    if ((dev_id = wemoCtrlPointGetDevID(wemo_id)) == -1) {
        APP_LOG("WEMOHK", LOG_DEBUG, "device %d not found", wemo_id);

        return CTRLPT_ERROR;
    }

    value[0] = (char *) malloc(sizeof(int));
    sprintf(value[0], "%d", state->hksetup_state);

    ret = wemoCtrlPointSendActionEx(WEMO_SERVICE_BASICEVENT, dev_id,
                                  "setHKSetupState", arguments, (char **) value, 1, 0);
    free(value[0]);
    return ret;
}
