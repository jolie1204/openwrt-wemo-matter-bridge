#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include <pthread.h>
#include <stdlib.h>
#include <ixml.h>

#include "wemo_engine.h"

static struct wemo_engine_callback we_callback;

static int socket_fd = -1;

static char *getFirstDocumentItem(IXML_Document *doc, const char *item)
{
	IXML_NodeList *nodeList = NULL;
	IXML_Node *textNode = NULL;
	IXML_Node *tmpNode = NULL;
	char *ret = NULL;

	nodeList = ixmlDocument_getElementsByTagName(doc, (char *)item);
	if (nodeList) {
		tmpNode = ixmlNodeList_item(nodeList, 0);
		if (tmpNode) {
			textNode = ixmlNode_getFirstChild(tmpNode);
			if (!textNode) {
				ret = strdup("");
				goto epilogue;
			}
			if (!ixmlNode_getNodeValue(textNode)) {
				ret = strdup("");
				goto epilogue;
			} else {
				ret = strdup(ixmlNode_getNodeValue(textNode));
			}
		} else {
			goto epilogue;
		}
	}

epilogue:
	if (nodeList) {
		ixmlNodeList_free(nodeList);
	}

	return ret;
}

static int we_handleGetInformationResult(char *result, struct we_dev_information *info)
{
    IXML_Document *Device = NULL;
    char *result_item = NULL;

    Device = ixmlParseBuffer(result);

    memset(info, 0, sizeof(struct we_dev_information));
    if ((result_item = getFirstDocumentItem(Device, "firmwareVersion"))) {
        /* string */
        printf("%s: firmwareVersion : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "iconVersion"))) {
        /* integer */
        printf("%s: : iconVersion : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "iconPort"))) {
        /* integer */
        printf("%s: : iconPort : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "macAddress"))) {
        /* string */
        printf("%s: : macAddress : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "binaryState"))) {
        /* integer */
        printf("%s: : binaryState : %s\n", __FUNCTION__, result_item);
        info->binaryState = atoi(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "hwVersion"))) {
        /* integer */
        printf("%s: : hwVersion : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "deviceCurrentTime"))) {
        /* long integer */
        printf("%s: : deviceCurrentTime : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "productName"))) {
        /* string */
        printf("%s: : productName : %s\n", __FUNCTION__, result_item);
        if (strlen(result_item)) {
            info->productName = result_item;
        }
        else {
            free(result_item);
        }
    }
    if ((result_item = getFirstDocumentItem(Device, "FriendlyName"))) {
        /* string */
        printf("%s: : FriendlyName : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "currentFWUpdateState"))) {
        /* integer */
        printf("%s: : currentFWUpdateState : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "brightness"))) {
        /* integer */
        printf("%s: : brightness : %s\n", __FUNCTION__, result_item);
        info->brightness = atoi(result_item);
        free(result_item);
    }
    else {
        printf("%s: : not a dimmer, brightness : -1\n", __FUNCTION__);
        info->brightness = -1;
    }
    if ((result_item = getFirstDocumentItem(Device, "fader"))) {
        /* string */
        printf("%s: : fader : %s\n", __FUNCTION__, result_item);
        if (strlen(result_item)) {
            info->fader = result_item;
        }
        else {
            free(result_item);
        }
    }
    if ((result_item = getFirstDocumentItem(Device, "OverTemp"))) {
        /* integer */
        printf("%s: : OverTemp : %s\n", __FUNCTION__, result_item);
        info->OverTemp = atoi(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "nightMode"))) {
        /* integer */
        printf("%s: : nightMode : %s\n", __FUNCTION__, result_item);
        info->nightMode = atoi(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "startTime"))) {
        /* long integer */
        printf("%s: : startTime : %s\n", __FUNCTION__, result_item);
        info->startTime = atol(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "endTime"))) {
        /* long integer */
        printf("%s: : endTime : %s\n", __FUNCTION__, result_item);
        info->endTime = atol(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "nightModeBrightness"))) {
        /* integer */
        printf("%s: : nightModeBrightness : %s\n", __FUNCTION__, result_item);
        info->nightModeBrightness = atoi(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "CountdownEndTime"))) {
        /* long integer */
        printf("%s: : CountdownEndTime : %s\n", __FUNCTION__, result_item);
        info->CountdownEndTime = atol(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "longPressRuleDeviceCnt"))) {
        /* integer */
        printf("%s: : longPressRuleDeviceCnt : %s\n", __FUNCTION__, result_item);
        info->longPressRuleDeviceCnt = atoi(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "longPressRuleDeviceUdn"))) {
        /* string */
        printf("%s: : longPressRuleDeviceUdn : %s\n", __FUNCTION__, result_item);
        if (strlen(result_item)) {
            info->longPressRuleDeviceUdn = result_item;
        }
        else {
            free(result_item);
        }
    }
    if ((result_item = getFirstDocumentItem(Device, "longPressRuleAction"))) {
        /* integer */
        printf("%s: : longPressRuleAction : %s\n", __FUNCTION__, result_item);
        info->longPressRuleAction = atoi(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "longPressRuleState"))) {
        /* integer */
        printf("%s: : longPressRuleState : %s\n", __FUNCTION__, result_item);
        info->longPressRuleState = atoi(result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "dbVersion"))) {
        /* integer */
        printf("%s: : dbVersion : %s\n", __FUNCTION__, result_item);
        free(result_item);
    }
    if ((result_item = getFirstDocumentItem(Device, "hushMode"))) {
        /* string */
        printf("%s: : hushMode : %s\n", __FUNCTION__, result_item);
        if (strlen(result_item)) {
            info->hushMode = result_item;
        }
        else {
            free(result_item);
        }
    }

    if (Device)
        ixmlDocument_free(Device);

    return 0;
}

void *we_comm_task(void *args)
{
    fd_set rfds;
    int retval;
    ssize_t result = 0;
    struct we_ipc_hdr ipchdr;
    char ipc_data[IPC_DATA_MAX];

    while(1) {
        do {
            FD_ZERO(&rfds);
            FD_SET(socket_fd, &rfds);

            retval = select(socket_fd + 1, &rfds, NULL, NULL, NULL);
        } while (retval == -1 && errno == EINTR);
        if (retval > 0) {
            if (FD_ISSET(socket_fd, &rfds)) {
                /* The socket_fd has data available to be read */
                result = recv(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0);
                if (result == 0) {
                    /* This means the other side closed the socket */
                    goto thr_exit;
                }
                if (ipchdr.size > IPC_DATA_MAX) {
                    fprintf(stderr, "Invalid ipc_data size (%d)\n", ipchdr.size);
                    goto thr_exit;
                }
                result = recv(socket_fd, ipc_data, ipchdr.size, 0);
                if (result == 0)
                    goto thr_exit;
                switch(ipchdr.cmd) {
                case EVENT_SETUP:
                    break;
                case EVENT_CONNECTION_STATE:
                    if (we_callback.netstate_callback)
                        we_callback.netstate_callback(ipchdr.wemo_id, (struct we_network_status *)ipc_data);
                    break;
                case EVENT_STATE:
                    if (we_callback.event_callback) {
                        we_callback.event_callback(ipchdr.wemo_id, (struct we_state *)ipc_data);
                    }
                    break;
                case EVENT_NAME_CHANGE:
                    if (we_callback.name_change_callback) {
                        we_callback.name_change_callback(ipchdr.wemo_id, (struct we_name_change *)ipc_data);
                    }
                    break;
                case EVENT_NAME_VALUE:
                    if (we_callback.name_value_callback) {
                        we_callback.name_value_callback(ipchdr.wemo_id, (struct we_name_value *)ipc_data);
                    }
                    break;
                case EVENT_DEVICE_INFO:
                    if (we_callback.dev_info_callback) {
                        /* parse the XML payload */
                        struct we_dev_information info;
                        we_handleGetInformationResult(ipc_data, &info);
                        we_callback.dev_info_callback(ipchdr.wemo_id, &info);

                        if (info.productName)
                            free(info.productName);
                        if (info.fader)
                            free(info.fader);
                        if (info.hushMode)
                            free(info.hushMode);
                        if (info.longPressRuleDeviceUdn)
                            free(info.longPressRuleDeviceUdn);
                    }
                    break;
                case EVENT_INSIGHT_HOME_SETTINGS:
                    if (we_callback.insight_home_settings_callback) {
                        we_callback.insight_home_settings_callback(ipchdr.wemo_id, (struct we_insight_home_settings *)ipc_data);
                    }
                    break;
                default:
                    fprintf(stderr, "%s: Unknown EVENT %d\n", __FUNCTION__, ipchdr.cmd);
                    break;
                }
            }
        }
        else {
            /* An error ocurred, just print it to stdout */
            printf("Error on select(): %s", strerror(errno));
        }
    }
thr_exit:
    close(socket_fd);
    socket_fd = -1;

    return NULL;
}

int we_init()
{
    struct sockaddr_un name;
    pthread_t we_ipc_thread;

    memset(&we_callback, 0, sizeof(struct wemo_engine_callback));

    if ((socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        return 0;
    }
    name.sun_family = AF_UNIX;
    strcpy (name.sun_path, SOCKET_NAME);
    if (connect(socket_fd, (const struct sockaddr *)&name, SUN_LEN(&name)) == -1) {
        return 0;
    }

    if(!pthread_create(&we_ipc_thread, NULL, we_comm_task, NULL)) {
        pthread_detach(we_ipc_thread);
    }
    else {
        return 0;
    }

    return 1;
}

int we_register_event_callback(void (*callback) (int wemo_id, struct we_state *data))
{
    we_callback.event_callback = callback;
    return 1;
}

int we_register_netstate_callback(void (*callback) (int wemo_id, struct we_network_status *data))
{
    we_callback.netstate_callback = callback;
    return 1;
}

int we_register_name_change_callback(void (*callback) (int wemo_id, struct we_name_change *data))
{
    we_callback.name_change_callback = callback;
    return 1;
}

int we_register_name_value_callback(void (*callback) (int wemo_id, struct we_name_value *data))
{
    we_callback.name_value_callback = callback;
    return 1;
}

int we_register_dev_info_callback(void (*callback) (int wemo_id, struct we_dev_information *data))
{
    we_callback.dev_info_callback = callback;
    return 1;
}

int we_register_insight_home_settings_callback(void (*callback) (int wemo_id, struct we_insight_home_settings *data))
{
    we_callback.insight_home_settings_callback = callback;
    return 1;
}

int we_get_action (int wemo_id, struct we_state *we_state_data)
{
    struct we_ipc_hdr ipchdr;
    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_GET;
        ipchdr.size = sizeof(struct we_state);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_action failure!\n");
            return 0;
        }
        if (send(socket_fd, we_state_data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "we_get_action failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
	return 1;
}

int we_set_action (int wemo_id, struct we_state *we_state_data)
{
    struct we_ipc_hdr ipchdr;
    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_SET;
        ipchdr.size = sizeof(struct we_state);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_set_action failure!\n");
            return 0;
        }
        if (send(socket_fd, we_state_data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "we_set_action failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
	return 1;
}

int we_del_action (int wemo_id, struct we_state *we_state_data)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_DELETE;
        ipchdr.size = sizeof(struct we_state);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_del_action failure!\n");
            return 0;
        }
        if (send(socket_fd, we_state_data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "we_del_action failure!\n");
            return 0;
        }
    } else {
        return 0;
    }
    return 1;
}

int we_get_netstate(int wemo_id, struct we_network_status *network_status)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_CONNECTION_STATE;
        ipchdr.size = sizeof(struct we_network_status);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_netstate failure!\n");
            return 0;
        }
        if (send(socket_fd, network_status, ipchdr.size, 0) < 0) {
            fprintf(stderr, "we_get_netstate failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
	return 1;
}

int we_connect(int wemo_id, struct we_conn_data *conn_data)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_SETUP;
        ipchdr.size = sizeof(struct we_conn_data);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_connect failure!\n");
            return 0;
        }
        if (send(socket_fd, conn_data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "we_connect failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
	return 1;
}

int we_closesetup(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_CLOSESETUP;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_closesetup failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
	return 1;
}

int we_discover(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_DISCOVER;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_discover failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_firm_update(int wemo_id, struct we_firmware_data *firm_data)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_FIRMWARE_UPDATE;
        ipchdr.size = sizeof(struct we_firmware_data);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_firm_update failure!\n");
            return 0;
        }
        if (send(socket_fd, firm_data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "we_firm_update failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_set_hksetup_state(int wemo_id, struct we_hksetup_state *setup_state)
{
    struct we_ipc_hdr ipchdr;
    
    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_SET_HKSETUP_STATE;
        ipchdr.size = sizeof(struct we_hksetup_state);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_set_hksetup_state failure!\n");
            return 0;
        }
        if (send(socket_fd, setup_state, ipchdr.size, 0) < 0) {
            fprintf(stderr, "we_set_hksetup_state failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_change_name(int wemo_id, struct we_name_change *name_data)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_CHANGE_NAME;
        ipchdr.size = sizeof(struct we_name_change);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
        if (send(socket_fd, name_data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_set_name_value(int wemo_id, struct we_name_value *data)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_NAME_VALUE;
        ipchdr.size = sizeof(struct we_name_value);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
        if (send(socket_fd, data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_reset(int wemo_id, struct we_reset *reset_data)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_RESET;
        ipchdr.size = sizeof(struct we_reset);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
        if (send(socket_fd, reset_data, ipchdr.size, 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_restart_rule(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_RESTART_RULE;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_restart_rule failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_get_devinfo(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_GET_DEVINFO;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_devinfo failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_get_insightHomeSettings(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_GET_INSIGHTHOME_SETTINGS;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_insightHomeSettings failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_set_insightHomeSettings(int wemo_id, struct we_insight_home_settings *home_settings)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_SET_INSIGHTHOME_SETTINGS;
        ipchdr.size = sizeof(struct we_insight_home_settings);
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
        if (send(socket_fd, home_settings, ipchdr.size, 0) < 0) {
            fprintf(stderr, "%s failure!\n", __FUNCTION__);
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_get_insightParams(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_GET_INSIGHT_PARAMS;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_insightParams failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_set_powerThreshold(int wemo_id, struct we_insight_threshold *threshold)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_SET_POWER_THRESHOLD;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_insightParams failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_get_powerThreshold(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_GET_POWER_THRESHOLD;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_insightParams failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_get_dataExportInfo(int wemo_id)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_GET_DATA_EXPORTINFO;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_insightParams failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_schedule_dataExport(int wemo_id, struct we_insight_export *export)
{
    struct we_ipc_hdr ipchdr;

    if (socket_fd != -1) {
        ipchdr.wemo_id = wemo_id;
        ipchdr.cmd = CMD_SCHEDULE_DATA_EXPORT;
        ipchdr.size = 0;
        if (send(socket_fd, &ipchdr, sizeof(struct we_ipc_hdr), 0) < 0) {
            fprintf(stderr, "we_get_insightParams failure!\n");
            return 0;
        }
    }
    else {
        return 0;
    }
    return 1;
}

int we_end()
{
    fprintf(stderr, "we_end: cleaning up : ");
    if (socket_fd != -1) {
        fprintf(stderr, "close socket (%d)\n", socket_fd);
        close (socket_fd);
    }
    memset(&we_callback, 0, sizeof(struct wemo_engine_callback));
    return 1;
}
