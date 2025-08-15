/***************************************************************************
*
*
* plugin_ctrlpoint.c
*
* Created by Belkin International, Software Engineering on XX/XX/XX.
* Copyright (c) 2012-2013 Belkin International, Inc. and/or its affiliates. All rights reserved.
*
* Belkin International, Inc. retains all right, title and interest (including all
* intellectual property rights) in and to this computer program, which is
* protected by applicable intellectual property laws.  Unless you have obtained
* a separate written license from Belkin International, Inc., you are not authorized
* to utilize all or a part of this computer program for any purpose (including
* reproduction, distribution, modification, and compilation into object code)
* and you must immediately destroy or return to Belkin International, Inc
* all copies of this computer program.  If you are licensed by Belkin International, Inc., your
* rights to utilize this computer program are limited by the terms of that license.
*
* To obtain a license, please contact Belkin International, Inc.
*
* This computer program contains trade secrets owned by Belkin International, Inc.
* and, unless unauthorized by Belkin International, Inc. in writing, you agree to
* maintain the confidentiality of this computer program and related information
* and to not disclose this computer program and related information to any
* other person or entity.
*
* THIS COMPUTER PROGRAM IS PROVIDED AS IS WITHOUT ANY WARRANTIES, AND BELKIN INTERNATIONAL, INC.
* EXPRESSLY DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING THE WARRANTIES OF
* MERCHANTIBILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE, AND NON-INFRINGEMENT.
*
*
***************************************************************************/
#include "wemodefs.h"
#include "controlledevice.h"
#include "plugin_device.h"
#include "plugin_ctrlpoint.h"
#include "plugin_cmd.h"
#include "rule.h"
#include "gpio.h"
#include "httpsWrapper.h"

#include "utils.h"
#include "osUtils.h"
#ifdef _OPENWRT_
#include "belkin_api.h"
#else
#include "gemtek_api.h"
#endif
#include "upnpCommon.h"
#include "wifiHndlr.h"
#include "sigGen.h"
#include <sys/syscall.h>
#include <stdbool.h>

#ifdef SIMULATED_OCCUPANCY
#include "simulatedOccupancy.h"
#endif
#include "thready_utils.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

extern int g_eDeviceType;
extern char	g_szUDN[];
extern void UnlockDeviceSync(void);
pthread_mutex_t DeviceListMutex;
pthread_mutex_t SignNotifyMutex;
pthread_mutex_t HomeidListMutex;
int gProcessData = true;

#define STATE_RELAY_OVERRIDE_OFF 0x02

#define STATE_RELAY_OVERRIDE_ON 0x03

#define DOWNLOAD_DELTA 30 /**30 sec time gap*/

char gMacAddr[MAX_MAC_LEN];
char gSerialNo[MAX_MAC_LEN];
char g_homeid[SIZE_64B];
char g_deviceid[SIZE_64B];
char g_signature[SIGNATURE_LEN];
int ctrlpt_handle = -1;
extern char g_routerMac[MAX_MAC_LEN];
extern char g_routerSsid[MAX_ESSID_LEN];
extern char g_szRestoreState[MAX_RES_LEN];
char PluginDeviceType[] = "urn:Belkin:service:basicevent:1";
char gHomeIdList[SIZE_256B];
char gSignatureList[SIZE_2048B];

char* g_szSensorServiceList =
    "urn:Belkin:service:basicevent:1"
#ifdef PRODUCT_WeMo_Insight
    ";urn:Belkin:service:insight:1"
#endif

#ifdef PRODUCT_WeMo_Maker
    ";urn:Belkin:service:deviceevent:1"
#endif
    ;

int default_timeout = 1801;
int ctrlptstatusid;

int g_iRediscoverHandle = 0x00;

extern char	g_szUDN[SIZE_UDN];

int g_deviceindex = 0;
int g_CtrlPointDelete = 0x0;
static int nRA = 0x0;

pCtrlPluginDeviceNode g_pGlobalPluginDeviceList = NULL;

extern char g_szHomeId[SIZE_20B];
extern char g_szPluginPrivatekey[SIZE_50B];
extern char g_szSmartDeviceId[SIZE_256B];
extern char g_szSmartPrivateKey[SIZE_50B];

#define MAX_SERVICE_UPDATE_TIME			120	//10 hours?
#define MAX_SERVICE_UPDATE_TIME_NEW		10
extern unsigned short gpluginRemAccessEnable;
pthread_t s_RediscoverHandle 	= -1;
static pthread_attr_t tmp_hmattr;
static pthread_t s_GetHomeIdHandle 		= -1;

static int	s_cntRediscoveryCounter = 0x00;
#define		MAX_REDISCOVERY_TIMES	10		//10 * 120 s = 1200 seconds 20 minutes

inline void initHomeIdListLock()
{
    osUtilsCreateLock(&HomeidListMutex);
}

inline void  AcquireHomeIdListLock()
{
    osUtilsGetLock(&HomeidListMutex);
}

inline void  ReleaseHomeIdListLock()
{
    osUtilsReleaseLock(&HomeidListMutex);
}

void *CtrlPtRediscoverTask(void *args)
{
    bool bRunning = true;

    tu_set_my_thread_name( __FUNCTION__ );
    s_cntRediscoveryCounter = 0x00;

    while( bRunning ) {
        int rnd = 0;
        char homeIdList[SIZE_256B];
        char signaturelist[SIZE_2048B];

        memset(homeIdList, 0, sizeof(homeIdList));
        memset(signaturelist, 0, sizeof(signaturelist));

        rnd = rand() % 60;
        if (0x00 == rnd)
            rnd= 10;

        pluginUsleep(rnd*1000000);

        if(gProcessData) {
            /* send data gathered from devices in last iteration */
            AcquireHomeIdListLock();
            if(strlen(g_szHomeId) && (0 != atoi(g_szHomeId)) && strlen(gHomeIdList) && !strcmp(g_szRestoreState, "0")) {
                authSign *assign = NULL;

                /* create our signature */
                assign = createAuthSignature(g_szWiFiMacAddress, g_szSerialNo, g_szPluginPrivatekey);
                if (!assign) {
                    APP_LOG("UPNP: DEVICE", LOG_ERR, "\n Signature Structure returned NULL\n");
                }


                /* Append our home id and signature in the lists before sending */
                snprintf(homeIdList, sizeof(homeIdList)-1, "%s-%s", gHomeIdList, g_szHomeId);
                if(assign) {
                    APP_LOG("UPNP: DEVICE", LOG_HIDE, "###############################Self Signature: <%s>", assign->signature);

                    snprintf(signaturelist, sizeof(signaturelist)-1, "%s-%s", gSignatureList, assign->signature);

                    free(assign);
                } else {
                    /* Insert dummy signature to keep syntax sane */
                    snprintf(signaturelist, sizeof(signaturelist)-1, "%s-%s", gSignatureList, "DUMMY");
                }

                /* reset the home id  and signature list */
                memset(gHomeIdList, 0, sizeof(gHomeIdList));
                memset(gSignatureList, 0, sizeof(gSignatureList));
            }
            ReleaseHomeIdListLock();

        }

        pluginUsleep(MAX_SERVICE_UPDATE_TIME*1000000);

        if (s_cntRediscoveryCounter >= MAX_REDISCOVERY_TIMES) {
            //- Reset
            APP_LOG("UPNP: Device", LOG_DEBUG, "##################### to clean up device list ###################");
            s_cntRediscoveryCounter = 0x00;
            nRA = 0x0;
            CtrlPointRemoveAll(0);
            /* Range Extender fix: We have to stop discovering and sending information to cloud now */
            gProcessData = false;
            APP_LOG("UPNP: Device", LOG_DEBUG, "Stop home merge processing");
            {
                if((0x00 == atoi(g_szRestoreState)) && (0x00 != strlen(g_szHomeId))  && (0x00 != strlen(g_szPluginPrivatekey))) {
#ifdef SIMULATED_OCCUPANCY
                    if((!gRuleHandle[e_AWAY_RULE].ruleCnt) && (!gpSimulatedDevice)  && !gLongPressRuleActive)
#endif
                        if(!gRuleHandle[e_SENSOR_RULE].ruleCnt)
                                {
                            APP_LOG("UPNP: Device", LOG_DEBUG, "Breaking the loop...");
                            bRunning = false;
                            continue;
                        }
                }
            }
        }

        s_cntRediscoveryCounter++;
        APP_LOG("UPNP: Device", LOG_DEBUG, "##################### Control point to re-discover: %d/%d ###################",
                s_cntRediscoveryCounter, MAX_REDISCOVERY_TIMES);

        if( UPNP_E_SUCCESS != CtrlPointDiscoverDevices() ) {
            gProcessData = false;
            bRunning = false;
            continue;
        }

        if(gProcessData == true && !strcmp(g_szRestoreState,"0")) {
            /* sleep for a while to allow device discovery and creation of device list */
            pluginUsleep(MAX_DISCOVER_TIMEOUT*1000000);
        }
    }
    /* Stop Thread */
    /* stop control point too for a switch which is registered */
    APP_LOG("UPNP: Device", LOG_DEBUG, "Stop plugin ctrlpoint for SWITCH");
    /* move this statement here, because StopDiscoverTask in StopPluginCtrlPoint was causing this thread to get cancelled without stopping plugin control point */
    s_RediscoverHandle = -1;
    StopPluginCtrlPoint();

    return NULL;
}

/**
 *
 *
 *
 *
 *
 *
 *
 */
void RunDiscoverTask()
{
    if (-1 != s_RediscoverHandle) {
        APP_LOG("UPNP: Device", LOG_ERR, "############Rediscover handle already created################");
        return;
    }

    pthread_create(&s_RediscoverHandle, NULL, CtrlPtRediscoverTask, NULL);
    if (-1 != s_RediscoverHandle) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "Rediscover thread created successfully");
        pthread_detach(s_RediscoverHandle);
    } else {
        APP_LOG("UPNP: Device", LOG_ERR, "#######Rediscover thread can not be created#########");
    }
}

void StopDiscoverTask()
{
    if (-1 != s_RediscoverHandle) {
        ithread_cancel(s_RediscoverHandle);
        s_RediscoverHandle = -1;
    }
}


/**
 *	Get device index in the device list for send command
 *
 *
 *
 *
 *
 *
 *
 ******************************************************************/
int GetDeviceIndexByUDN(const char* udn)
{

    pCtrlPluginDeviceNode tmpdevnode = g_pGlobalPluginDeviceList;
    int isFound = 0x00;

    int index = 0x00;
    if (tmpdevnode) {
        while(tmpdevnode) {
            if (0x00 == strcmp(udn, tmpdevnode->UDN)) {
                APP_LOG("UPnPCtrPt", LOG_DEBUG, "Device %s found", udn);
                isFound = 0x01;
                index++;
                break;
            } else {
                tmpdevnode = tmpdevnode->next;
                index++;
            }
        }
    } else {
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "No living devices found");
    }

    if (!isFound)
        index = 0x00;

    return index;

}

char* GetUDNByDeviceIndex(int nIndex)
{

    pCtrlPluginDeviceNode tmpdevnode = g_pGlobalPluginDeviceList;

    int index = 0x01;
    if (tmpdevnode) {
        while(tmpdevnode) {
            if (index == nIndex) {
                APP_LOG("UPnPCtrPt", LOG_DEBUG, "Device %s found",  tmpdevnode->UDN);
                return (tmpdevnode->UDN);
            } else {
                tmpdevnode = tmpdevnode->next;
                index++;
            }
        }
    } else {
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "No living devices found");
    }

    return NULL;

}

int GetDeviceIndexNumber(void* arg)
{

    pCtrlPluginDeviceNode tmpdevnode = g_pGlobalPluginDeviceList;
    int index = 0x00;

    if (tmpdevnode) {
        while(tmpdevnode) {
            index++;
            tmpdevnode = tmpdevnode->next;
        }
    } else {
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "No devices found");
    }

    return index;

}

int CtrlPointDiscoverDevices(void)
{
    int rect = -1;

    if (-1 == ctrlpt_handle) {
        APP_LOG("UPnPCtrPt",LOG_ERR, "###### control point not created");
        return 0x01;
    }

    rect = UpnpSearchAsync(ctrlpt_handle, MAX_DISCOVER_TIMEOUT, PluginDeviceType, NULL);
    if( UPNP_E_SUCCESS != rect ) {
        APP_LOG("UPnPCtrPt",LOG_ERR, "Error sending search request%d", rect );
        return UPNP_E_INVALID_DEVICE;
    }

    return UPNP_E_SUCCESS;
}

void EnableContrlPointRediscover(int isEnable)
{
    if (isEnable)
        RunDiscoverTask();
}

int StartPluginCtrlPoint(char* if_name, unsigned short port)
{
    int rect;
    if(-1 != ctrlpt_handle) {
        return UPNP_E_SUCCESS;
    }
    initDeviceSync();
    initSignNotify();

    rect = UpnpInit2(if_name, port, g_szUDN_1);

    if((UPNP_E_SUCCESS != rect) && UPNP_E_INIT != rect) {
        APP_LOG("UPnPCtrPt",LOG_ERR, "StartCtrlPoint: UpnpInit() Error: %d", rect);
        UpnpFinish();
        return UPNP_E_INIT_FAILED;
    }

    if( 0 == port ) {
        port = UpnpGetServerPort();
    }

    APP_LOG("UPnPCtrPt",LOG_DEBUG, "UPnP Initialized interface = %s port = %u",
            if_name, port);

    APP_LOG("UPnPCtrPt",LOG_DEBUG, "Registering Control Point: sensor" );

    rect = UpnpRegisterClient((Upnp_FunPtr) CtrlPointCallbackEventHandler,
                              NULL, &ctrlpt_handle );

    if( UPNP_E_SUCCESS != rect && UPNP_E_ALREADY_REGISTERED != rect) {
        APP_LOG("UPnPCtrPt",LOG_ERR, "Error registering callback: %d", rect);
        UpnpFinish();
        return UPNP_E_INIT_FAILED;
    }

    APP_LOG("UPnPCtrPt",LOG_ERR, "Register sensor client success");
    if ((g_eDeviceType == DEVICE_SENSOR) || (g_eDeviceTypeTemp == DEVICE_INSIGHT)) {
        //- Only send out when device is sensor, socket will be via adv
        CtrlPointDiscoverDevices();
    }

    system("route del -net 127.0.0.0 netmask 255.0.0.0");
    g_CtrlPointDelete = 0x0;/**This is indicate control point remove 1 means delete is going on*/
    return UPNP_E_SUCCESS;
}


void CtrlPointPrintDevice()
{
    if (0x00 == g_pGlobalPluginDeviceList) {

        APP_LOG("UPnPCtrPt",LOG_DEBUG, "No device in discovery list");
        return;
    }

    pCtrlPluginDeviceNode tmpNode =  g_pGlobalPluginDeviceList;
    int index = 0x00;
    while (0x00 != tmpNode) {
        index++;
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "Device %d: %s\n", index, tmpNode->UDN);
        tmpNode = tmpNode->next;
    }

}


void *ResendRemoteEnableRequestThreadProc(void *args)
{
    int iVal = 60, exp = 1, retry_iVal = 0, iCnt = 1;
    tu_set_my_thread_name( __FUNCTION__ );
    while(1) {
        retry_iVal = iVal * exp;
        /* re-try time is 60 60 60 60 60 120 180 240 300 300 300 */
        if(retry_iVal < MAX_RETRY_INTERVAL && iCnt > INIT_RETRY_INTERVAL) {
            exp++;
        } else {
            iCnt++;
        }
        APP_LOG("UPnPCtrPt",LOG_ERR, "RE-TRYING in <%d> seconds..", retry_iVal);
        pluginUsleep(retry_iVal * 1000000);
        char *pluginKey = GetBelkinParameter (DEFAULT_PLUGIN_PRIVATE_KEY);
        if ((pluginKey && (0x00 == strlen(pluginKey)) && (0x00 == atoi(g_szRestoreState)))
            ||(pluginKey && (0x00 != strlen(pluginKey)) && (0x01 == atoi(g_szRestoreState)))) {
            PluginCtrlPointShareHWInfo(g_deviceindex);
        }

    }
    return NULL;
}

void *GetHomeIdThreadProc(void *args)
{
    tu_set_my_thread_name( __FUNCTION__ );
    while(1) {
        pluginUsleep(300000000);
        char *homeId = GetBelkinParameter (DEFAULT_HOME_ID);
        if (0x00 != homeId && (0x00 == strlen(homeId))) {
            PluginCtrlPointGetHomeId(g_deviceindex, 0x00);
            APP_LOG("UPNP: Device", LOG_DEBUG, "GetHomeIdThreadProc thread: PluginCtrlPointGetHomeId request send successfully");
        }
    }
    return NULL;
}

void GetHomeIdTaskThread(void)
{
    if (-1 != s_GetHomeIdHandle) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "############GetHomeIdTaskThread handle already created################");
        return;
    }

    pthread_attr_init(&tmp_hmattr);
    pthread_attr_setdetachstate(&tmp_hmattr,PTHREAD_CREATE_DETACHED);
    pthread_create(&s_GetHomeIdHandle, &tmp_hmattr, GetHomeIdThreadProc, NULL);
    if (-1 != s_GetHomeIdHandle) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "GetHomeIdTaskThread thread created successfully");
    } else {
        APP_LOG("UPNP: Device", LOG_DEBUG, "#######GetHomeIdTaskThread thread can not be created#########");
    }
}

void GetMacAddrResponseTask(UpnpActionComplete *gm_args)
{
    if(gm_args == 0x00) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "GetMacAddrResponseTask: gm_args is NULL");
        return;
    }
    UpnpActionComplete *a_event = (UpnpActionComplete *)gm_args;
    IXML_Document *result = UpnpActionComplete_get_ActionResult(a_event);
    char* paramValue = Util_GetFirstDocumentItem(result, "MacAddr");
    if (0x00 != paramValue && (0x00 != strlen(paramValue))) {
        APP_LOG("UPnPCtrPt",LOG_ERR, "MacAddr:%s\n", paramValue);
        memset(gMacAddr, 0, sizeof(gMacAddr));
    }
    char* paramValue1 = Util_GetFirstDocumentItem(result, "SerialNo");
    if (0x00 != paramValue1 && (0x00 != strlen(paramValue1))) {
        APP_LOG("UPnPCtrPt", LOG_ERR, "SerialNo:%s\n", paramValue1);
        memset(gSerialNo, 0, sizeof(gSerialNo));
    }
    char* paramValue2 = Util_GetFirstDocumentItem(result, "PluginUDN");
    if (0x00 != paramValue2 && (0x00 != strlen(paramValue2))) {
        APP_LOG("UPnPCtrPt", LOG_ERR, "PluginUDN:%s\n", paramValue2);
    }

    FreeXmlSource(paramValue);
    FreeXmlSource(paramValue1);
    FreeXmlSource(paramValue2);
}

#ifdef SIMULATED_OCCUPANCY
void ProcessGetSimulatedRuleDataResponse(UpnpActionComplete *args)
{
    char *index = NULL;
    char *state = NULL;
    char *remtimetotoggle = NULL;
    char *udn = NULL;
    SimulatedDevData *devicedata=NULL;
    SimulatedDevData *tmpdevdata=NULL;
    int 	found = 0;
    char *strtok_r_temp;

    APP_LOG("UPnPCtrPt", LOG_DEBUG, "ProcessGetSimulatedRuleDataResponse...");
    if(args == 0x00) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "Argument is NULL");
        return;
    }

    if(gpSimulatedDevice == NULL) {	/* Checking gpSimulatedDevice as this can be updated from other threads too */
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "No simulated rule data");
        return;
    }

    UpnpActionComplete *a_event = (UpnpActionComplete *)args;
    IXML_Document *result = UpnpActionComplete_get_ActionResult(a_event);

    char* paramValue = Util_GetFirstDocumentItem(result, "RuleData");
    if (0x00 != paramValue && (0x00 != strlen(paramValue))) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "RuleData received:%s", paramValue);

        /* Process response - format: index|binarystate|remtimetotoggle| */

        index = strtok_r(paramValue, "|",&strtok_r_temp);
        state = strtok_r(NULL, "|",&strtok_r_temp);
        remtimetotoggle = strtok_r(NULL, "|",&strtok_r_temp);
        udn = strtok_r(NULL, "|",&strtok_r_temp);

        APP_LOG("UPnPCtrPt",LOG_DEBUG, "Parsed deviceindex:%s, binarystate: %s, remaing time to toggle: %s and udn: %s", index, state, remtimetotoggle, udn);
        LockSimulatedOccupancy();
        if(gpSimulatedDevice)
            tmpdevdata = gpSimulatedDevice->pUpnpRespInfo;
        while (tmpdevdata) {
            if (strcmp(tmpdevdata->sDevInfo.UDN, udn) == 0) {
                found = 1;
                break;
            }

            if (!tmpdevdata->next)
                break;

            tmpdevdata = tmpdevdata->next;
        }

        if (!found) {
            devicedata = (SimulatedDevData*)ZALLOC(sizeof(SimulatedDevData));
            if (0x00 == devicedata) {
                APP_LOG("UPnPCtrPt", LOG_ERR, "Error: Can not allocate device memory\n");
                FreeXmlSource(paramValue);
                UnlockSimulatedOccupancy();
                system("reboot");
            } else {
                APP_LOG("UPnPCtrPt", LOG_ERR, "Allocated %d bytes for dev UDN: %s", sizeof(SimulatedDevData), udn);
            }

            strncpy(devicedata->sDevInfo.UDN, udn, sizeof(devicedata->sDevInfo.UDN)-1);
            devicedata->sDevInfo.devIndex = atoi(index);
            devicedata->binaryState = atoi(state);
            devicedata->remTimeToToggle = atoi(remtimetotoggle);
            devicedata->next = NULL;

            if (!tmpdevdata) {
                if(gpSimulatedDevice)
                    gpSimulatedDevice->pUpnpRespInfo = devicedata;
            } else {
                tmpdevdata->next = devicedata;
            }

            APP_LOG("UPnPCtrPt", LOG_DEBUG, " ############### Simulated device data added: %s ####################\
			    Device data index: %d \
			    Device data binary state: %d \
			    Device data remaining time to toggle: %d \
			    ",devicedata->sDevInfo.UDN,
                    devicedata->sDevInfo.devIndex,
                    devicedata->binaryState,
                    devicedata->remTimeToToggle
                   );
        } else {
            tmpdevdata->sDevInfo.devIndex = atoi(index);
            tmpdevdata->binaryState = atoi(state);
            tmpdevdata->remTimeToToggle = atoi(remtimetotoggle);
            APP_LOG("UPnPCtrPt", LOG_DEBUG, " ############### Simulated device data updated: %s ####################\
			    Device data index: %d \
			    Device data binary state: %d \
			    Device data remaining time to toggle: %d \
			    ",tmpdevdata->sDevInfo.UDN,
                    tmpdevdata->sDevInfo.devIndex,
                    tmpdevdata->binaryState,
                    tmpdevdata->remTimeToToggle
                   );
        }

        if(gpSimulatedDevice)
            (gpSimulatedDevice->upnpRespTotalCount)++;	//upnp action response count
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "******** SIMULATED DEVICE UPNP RESPONSE COUNT is: %d ********", (gpSimulatedDevice->upnpRespTotalCount));
        UnlockSimulatedOccupancy();

    } else {
        APP_LOG("UPnPCtrPt", LOG_ERR, "Invalid response");
    }

    FreeXmlSource(paramValue);
    APP_LOG("UPnPCtrPt", LOG_DEBUG, "ProcessGetSimulatedRuleDataResponse done...");
}
#endif

void updateInsightHomeSettingsResponse(UpnpActionComplete *args)
{

    if(args == 0x00) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "Argument is NULL");
        return;
    }
    UpnpActionComplete *a_event = (UpnpActionComplete *)args;
    IXML_Document *result = UpnpActionComplete_get_ActionResult(a_event);

    char* paramValue = Util_GetFirstDocumentItem(result, "Currency");
    if (0x00 != paramValue && (0x00 != strlen(paramValue))) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "Currency received:%s", paramValue);
    }

    FreeXmlSource(paramValue);

    paramValue = Util_GetFirstDocumentItem(result, "EnergyPerUnitCost");
    if (0x00 != paramValue && (0x00 != strlen(paramValue))) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "EnergyPerUnitCost received:%s", paramValue);
    }

    FreeXmlSource(paramValue);

    paramValue = Util_GetFirstDocumentItem(result, "EnergyPerUnitCostVersion");
    if (0x00 != paramValue && (0x00 != strlen(paramValue))) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "EnergyPerUnitCostVersion received:%s", paramValue);
    }

    FreeXmlSource(paramValue);

    return;
}

void GetHomeIdResponseTask(UpnpActionComplete *a_event)
{
    IXML_Document *result = UpnpActionComplete_get_ActionResult(a_event);
    char* paramValue = Util_GetFirstDocumentItem(result, "HomeId");
    if (0x00 != paramValue && (0x00 != strlen(paramValue))) {
        APP_LOG("UPnPCtrPt",LOG_HIDE, "HomeId:%s\n", paramValue);
        if(0x0 == strcmp(paramValue, "failure")) {
            char *homeId = GetBelkinParameter (DEFAULT_HOME_ID);
            if (0x00 != homeId && (0x00 == strlen(homeId))) {
                if (-1 == s_GetHomeIdHandle) {
                    GetHomeIdTaskThread();
                }
            }
        } else {
            if (-1 != s_GetHomeIdHandle) {
                pthread_cancel(s_GetHomeIdHandle);
                s_GetHomeIdHandle = -1;
            }
            char* paramValue1 = Util_GetFirstDocumentItem(result, "DeviceId");
            if (0x00 != paramValue1 && (0x00 != strlen(paramValue1))) {
                APP_LOG("UPnPCtrPt", LOG_HIDE, "DeviceId:%s\n", paramValue1);
                SetBelkinParameter (DEFAULT_SMART_DEVICE_ID,paramValue1);
                SaveSetting();
            }
            if(!nRA) {
                char *szHomeId = paramValue;
                memset(g_homeid, 0x0, sizeof(g_homeid));
                strncpy(g_homeid, szHomeId, sizeof(g_homeid)-1);
                char *szDeviceId = paramValue1;
                memset(g_deviceid, 0x0, sizeof(g_deviceid));
                strncpy(g_deviceid, szDeviceId, sizeof(g_deviceid)-1);
                PluginCtrlPointShareHWInfo(g_deviceindex);
                ++nRA;
            }

            FreeXmlSource(paramValue1);
        }
        FreeXmlSource(paramValue);
    }

}

void CtrlPointProcessControlAction(UpnpActionComplete *a_event)
{
    IXML_Document *result = UpnpActionComplete_get_ActionResult(a_event);
    char *localName = result->n.firstChild->localName;

    if(localName)
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "Node value: %s", localName);

    if (0x00 == strcmp(localName, "GetMacAddrResponse")) {
        GetMacAddrResponseTask(a_event);
    }
#ifdef SIMULATED_OCCUPANCY
    else if (0x00 == strcmp(localName, "GetSimulatedRuleDataResponse")) {
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "Process Get Simulated Rule Data Response case...");
        ProcessGetSimulatedRuleDataResponse(a_event);
    }
#endif
}

void subscribeDeviceServices(IXML_Document *DescDoc, const char *location,CtrlPointPluginDevice *deviceNode)
{
    int   service;
    int   ret = 1;
    char *serviceId;
    char *eventURL;
    char *controlURL;
    Upnp_SID eventSID;
    int TimeOut = default_timeout;

    for(service = 0; service < PLUGIN_MAX_SERVICES; service++) {
        eventSID[0] = 0;
        serviceId = NULL;
        eventURL = NULL;
        controlURL = NULL;
        if(0x00 != strstr(g_szSensorServiceList,  CtrleeDeviceServiceType[service])) {
            if(Util_FindAndParseService(DescDoc, location, CtrleeDeviceServiceType[service], &serviceId, &eventURL, &controlURL)) {
                ret = UpnpSubscribe(ctrlpt_handle, eventURL,&TimeOut,eventSID);
                if(ret == UPNP_E_SUCCESS) {
                    if(NULL != serviceId) {
                        SAFE_STRCPY(deviceNode->services[service].ServiceId, serviceId);
                        FreeXmlSource(serviceId);
                    }
                    if(NULL != CtrleeDeviceServiceType[service]) {
                        SAFE_STRCPY(deviceNode->services[service].ServiceType, CtrleeDeviceServiceType[service]);
                    }
                    if(NULL != controlURL) {
                        SAFE_STRCPY(deviceNode->services[service].ControlURL, controlURL);
                        FreeXmlSource(controlURL);
                    }
                    if(NULL != eventURL) {
                        SAFE_STRCPY(deviceNode->services[service].EventURL, eventURL);
                        FreeXmlSource(eventURL);
                    }
                    if(strlen(eventSID) > 0) {
                        SAFE_STRCPY(deviceNode->services[service].SID, eventSID);
                        APP_LOG("UPnPCtrPt",LOG_DEBUG, "Subscribed to service:%s, sid: %s", CtrleeDeviceServiceType[service], deviceNode->services[service].SID);
                    }
                } else {
                    APP_LOG("UPnPCtrPt", LOG_ERR, "####### Error Subscribing to EventURL -- %s\n", CtrleeDeviceServiceType[service]);
                }
            }
        }
    }
}
/**
 *	Function:
 *		CtrlPointProcessDeviceDiscovery
 *
 * Description:
 *		Extension of function "CtrlPointProcessDeviceDiscovery" adding the advertisement process
 *		The original one still kept but not called any more in case need to revert to original one
 *
 **/
void CtrlPointProcessDeviceDiscovery(IXML_Document *DescDoc, UpnpDiscovery *d_event ,CtrlPointPluginDevice *deviceNode,int deviceIndex,int isAdv)
{
    //- Device information
    char *friendlyName = NULL;
    char presURL[SIZE_256B];
    char *baseURL = NULL;
    char *relURL = NULL;
    char *UDN = NULL;
    char *deviceType = NULL;
    int 	ret = 1;

    UDN                 = Util_GetFirstDocumentItem(DescDoc, "UDN");
    friendlyName        = Util_GetFirstDocumentItem(DescDoc, "friendlyName");
    baseURL             = Util_GetFirstDocumentItem(DescDoc, "URLBase");
    relURL              = Util_GetFirstDocumentItem(DescDoc, "presentationURL");
    deviceType          = Util_GetFirstDocumentItem(DescDoc, "deviceType");

    ret = UpnpResolveURL((baseURL ? baseURL : UpnpDiscovery_get_Location_cstr(d_event)), relURL, presURL);
    if (UPNP_E_SUCCESS != ret) {
        //- Comment out junk message
    }
    if (!UDN || !strlen(UDN)) {
        APP_LOG("UPnPCtrPt:discover",LOG_ERR, "UDN not found");
        goto exit_func;
    }
    if (!strcmp(UDN, g_szUDN)) {
        goto exit_func;
    }
    //- Access start here
    LockDeviceNode(&(deviceNode->lock));
    /**Only Modify the newly added list and Alive packet device which has exists previously*/
    //search and subscrible service
    APP_LOG("UPnPCtrPt", LOG_DEBUG, "Allocated %d bytes for dev UDN: %s", sizeof(CtrlPluginDeviceNode), UDN);
    if(isAdv && (!deviceNode->IsDeviceRequestUpdate)) {
        CtrlPointUnsubcribeNodeService(deviceNode);
    }
    subscribeDeviceServices(DescDoc, UpnpDiscovery_get_Location_cstr(d_event), deviceNode);
    //- Device Management
    strncpy(deviceNode->DescDocURL, UpnpDiscovery_get_Location_cstr(d_event), SIZE_256B - 1);        // <-----<<< DANGEROUS
    strncpy(deviceNode->FriendlyName, friendlyName, SIZE_256B - 1);  // <-----<<< DANGEROUS
    strncpy(deviceNode->PresURL, presURL, SIZE_256B - 1);            // <-----<<< DANGEROUS
    deviceNode->AdvrTimeOut = UpnpDiscovery_get_Expires(d_event);
    deviceNode->IsDeviceRequestUpdate = 0x00;
    APP_LOG("UPnPCtrPt", LOG_INFO, " ## Updated device(%s): \n\t%s \n\t%s \n\ttimeout: %d ##",
            deviceNode->DescDocURL, deviceNode->FriendlyName, UDN, deviceNode->AdvrTimeOut);

    g_deviceindex = deviceIndex;
    UnlockDeviceNode(&(deviceNode->lock));

exit_func:
    FreeXmlSource(UDN);
    FreeXmlSource(friendlyName);
    FreeXmlSource(baseURL);
    FreeXmlSource(relURL);
    FreeXmlSource(deviceType);
}
void RemoveDeviceByUDN(const char* szUDN)
{
    LockDeviceSync();

    pCtrlPluginDeviceNode curdevnode 	= g_pGlobalPluginDeviceList;
    pCtrlPluginDeviceNode prevdevnode 	= 0x00;
    if (0x00 == curdevnode) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "No device in the list, can not delete");
        UnlockDeviceSync();
        return;
    }

    //- First device
    if(0x00 == strcmp(curdevnode->UDN, szUDN)) {
        g_pGlobalPluginDeviceList = curdevnode->next;
        CtrlPointDeleteNode(curdevnode);

    } else {
        prevdevnode = curdevnode;
        curdevnode  = curdevnode->next;
        while(curdevnode) {
            if(0x00 == strcmp(curdevnode->UDN, szUDN)) {
                prevdevnode->next = curdevnode->next;
                CtrlPointDeleteNode(curdevnode);
                APP_LOG("UPnPCtrPt",LOG_DEBUG, "Find and remove device");
                break;
            }

            prevdevnode = curdevnode;
            curdevnode = curdevnode->next;
        }
    }

    UnlockDeviceSync();

}

void CtrlPointProcessDeviceByebye(UpnpDiscovery *d_event)
{
    CtrlPointPluginDevice *devicenode=NULL;
    int found=0;
    if (0x00 == strlen(UpnpDiscovery_get_DeviceID_cstr(d_event))) {
        APP_LOG("UPnPCtrPt",LOG_INFO, "\nNO UDN Present for the device BYE BYE");
        return;
    }
    LockDeviceSync();
    pCtrlPluginDeviceNode tmpdevnode = g_pGlobalPluginDeviceList;
    while (tmpdevnode) {
        if (!strcmp(tmpdevnode->UDN, UpnpDiscovery_get_DeviceID_cstr(d_event))) {
            APP_LOG("UPnPCtrPt", LOG_DEBUG,"\nThe UDN Map for BYE BYE of the existing device UDN for Deletion is =%s \n",tmpdevnode->UDN);
            devicenode=&(tmpdevnode->device);
            found=1;
            break;
        }
        tmpdevnode = tmpdevnode->next;
    }
    UnlockDeviceSync();
    if(found) {
        osUtilsGetLock(&(devicenode->lock));
        APP_LOG("UPnPCtrPt", LOG_DEBUG,"\nThe device going for BYE BYE delete is =%s \n", UpnpDiscovery_get_DeviceID_cstr(d_event));
        if(devicenode->descDoc) {
            APP_LOG("UPnPCtrPt", LOG_DEBUG,"\nThe device called for BYE BYE CtrlPointUnsubcribeNodeService is=%s \n",
                    UpnpDiscovery_get_DeviceID_cstr(d_event));
            CtrlPointUnsubcribeNodeService(devicenode);
            ixmlDocument_free(devicenode->descDoc);
            devicenode->descDoc = NULL;
        }
        osUtilsReleaseLock(&(devicenode->lock));
    }
}

pCtrlPluginDeviceNode GetDeviceNodeBySID(const char* SID)
{
    pCtrlPluginDeviceNode node = 0x00;

    node = g_pGlobalPluginDeviceList;
    while(node) {
        if ((0x00==strcmp(SID, node->device.services[PLUGIN_E_EVENT_SERVICE].SID))) {
            //APP_LOG("UPnPCtrPt", LOG_DEBUG, "device found: %04X:%s", node, node->device.UDN);
            break;
        } else {
            node = node->next;
        }
    }

    return node;
}

void ProcessUpNpNotify(UpnpEvent *event)
{
    char routerMac[MAX_MAC_LEN];
    char routerssid[MAX_ESSID_LEN];
    memset(routerMac, 0, sizeof(routerMac));
    memset(routerssid, 0, sizeof(routerssid));
    if (!event)
        return;

    LockDeviceSync();

    pCtrlPluginDeviceNode deviceNode = GetDeviceNodeBySID(UpnpEvent_get_SID_cstr(event));
    IXML_Document *changed_variables = UpnpEvent_get_ChangedVariables(event);

    if (0x00 == deviceNode) {
        APP_LOG("UPnPCtrPt", LOG_ERR, "Can not find device of SID: %s", UpnpEvent_get_SID_cstr(event));
        UnlockDeviceSync();
        return;
    }

    UnlockDeviceSync();

    char* state = Util_GetFirstDocumentItem(changed_variables, "UserAction");

    if ((0x00 != state) && (0x00 != strlen(state))) {
        int iState = atoi(state);

        APP_LOG("UPnPCtrPt", LOG_INFO, "state change: %d", iState);
    }

    char *nhomeid = Util_GetFirstDocumentItem(changed_variables, "HomeIdRequest");
    if ((0x00 != nhomeid) && (0x00 != strlen(nhomeid))) {
        memset(g_homeid, 0x0, sizeof(g_homeid));
        strncpy(g_homeid, nhomeid, sizeof(g_homeid)-1);
        APP_LOG("UPnPCtrPt", LOG_HIDE, "GOT HOME ID NOTIFY: <%s>", nhomeid);
    }

    char *ndeviceid = Util_GetFirstDocumentItem(changed_variables, "DeviceIdRequest");
    if ((0x00 != ndeviceid) && (0x00 != strlen(ndeviceid))) {
        memset(g_deviceid, 0x0, sizeof(g_deviceid));
        strncpy(g_deviceid, ndeviceid, sizeof(g_deviceid)-1);
        APP_LOG("UPnPCtrPt", LOG_INFO, "GOT DEVICE ID NOTIFY: <%s>", ndeviceid);
    }
#ifdef PRODUCT_WeMo_Insight
    char *nEnergyPerUnitCost = Util_GetFirstDocumentItem(changed_variables, "EnergyPerUnitCost");
    if ((0x00 != nEnergyPerUnitCost) && (0x00 != strlen(nEnergyPerUnitCost))) {
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "GOT ENERGY PER UNIT COST  NOTIFY: <%s>", nEnergyPerUnitCost);
        ProcessEnergyPerunitCostNotify(nEnergyPerUnitCost);
    }
    FreeXmlSource(nEnergyPerUnitCost);
#endif

    //- Clean up
    FreeXmlSource(state);
    FreeXmlSource(nhomeid);
    FreeXmlSource(ndeviceid);
}

int IsDownLoadDescDoc(UpnpDiscovery *d_event,IXML_Document **DescDoc,CtrlPointPluginDevice **devicenode,int *deviceIndex,int isAdv)
{
    int ret = 0, found = 0;

    /**Global Device list access start here*/
    LockDeviceSync();
    pCtrlPluginDeviceNode tmpdevnode = g_pGlobalPluginDeviceList;
    while (tmpdevnode) {
        (*deviceIndex)++;
        if (!strcmp(tmpdevnode->UDN, UpnpDiscovery_get_DeviceID_cstr(d_event))) {
            (*devicenode)=&(tmpdevnode->device);
            found = 1;
            //UnlockDeviceSync();	// - unlock here to not release it if device not found
            break;
        }
        tmpdevnode = tmpdevnode->next;
    }

    if(found) {
        LockDeviceNode(&((*devicenode)->lock));/**Device Node access by putting lock*/
        if ((strcmp((*devicenode)->DescDocURL, UpnpDiscovery_get_Location_cstr(d_event)) == 0) &&
            ((*devicenode)->descDoc != NULL) && ((*devicenode)->IsDeviceRequestUpdate == 0)) {
            APP_LOG("UPnPCtrPt", LOG_DEBUG, "Device %s already in g_pGlobalPluginDeviceList, skip downloading...", tmpdevnode->UDN);
        }
        else {
            if ((*devicenode)->IsDeviceRequestUpdate == 1) {
                APP_LOG("UPnPCtrPt", LOG_DEBUG, "Device %s requested updated.", tmpdevnode->UDN);
            }
            else {
                APP_LOG("UPnPCtrPt", LOG_DEBUG, "Device %s already in g_pGlobalPluginDeviceList, but location changed.",
                        tmpdevnode->UDN);
            }
            if(((time(NULL)) - ((*devicenode)->timeStamp))>DOWNLOAD_DELTA) {
                if( ( ret = UpnpDownloadXmlDoc( UpnpDiscovery_get_Location_cstr(d_event), DescDoc ) ) != UPNP_E_SUCCESS) {
                    APP_LOG("UPnPCtrPt", LOG_ERR, "Failed to download xml document at %s (%s)-- %d\n", UpnpDiscovery_get_Location_cstr(d_event), tmpdevnode->UDN, ret);
                } else {
                    if((*devicenode)->descDoc ) {
                        ixmlDocument_free((*devicenode)->descDoc);/**Free the existing device doc 1st*/
                    }
                    (*devicenode)->descDoc=(*DescDoc);
                    (*devicenode)->timeStamp=time(NULL);
                    APP_LOG("UPnPCtrPt", LOG_DEBUG, "Updated XML Doc downloaded successfully for device:%s", tmpdevnode->UDN);

                    if(!isAdv) { /**This will only for device node exsits but get m-search respnse, so no need to update all value*/
                        (*devicenode)->AdvrTimeOut = UpnpDiscovery_get_Expires(d_event);
                        // ret = 1; /**Not required to update device list as it is a M-Search reply for existing Node*/
                    }
                }
            } else {
                (*DescDoc)=(*devicenode)->descDoc;
                ret=1;/**Not required to update device list as it already updated by previous thread*/
            }
        }
        UnlockDeviceNode(&((*devicenode)->lock));
    } else {
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "Downloading location : %s.", UpnpDiscovery_get_Location_cstr(d_event));
        if( ( ret = UpnpDownloadXmlDoc( UpnpDiscovery_get_Location_cstr(d_event), DescDoc ) ) != UPNP_E_SUCCESS) {
            APP_LOG("UPnPCtrPt", LOG_ERR, "Failed to download xml document -- %d\n",ret);
        } else {
            APP_LOG("UPnPCtrPt", LOG_DEBUG, "Adding new device : %s\n", UpnpDiscovery_get_DeviceID_cstr(d_event));
            /**Hold the Global Lock until new node is not created and then hold local node lock only */
            tmpdevnode=(CtrlPluginDeviceNode *)ZALLOC(sizeof(CtrlPluginDeviceNode));/**New Device node will get add at begining of linklist*/
            strncpy(tmpdevnode->UDN,UpnpDiscovery_get_DeviceID_cstr(d_event), sizeof(tmpdevnode->UDN)-1);
            tmpdevnode->device.timeStamp=time(NULL);
            tmpdevnode->next=g_pGlobalPluginDeviceList;
            g_pGlobalPluginDeviceList=tmpdevnode;
            initDeviceNodeLock(&(tmpdevnode->device.lock));
            (*devicenode)=&(tmpdevnode->device);
            LockDeviceNode(&(tmpdevnode->device.lock));
            //- Compiler behavior not so sure, so force reset to 0x00;
#if defined(SIMULATED_OCCUPANCY)
            tmpdevnode->Skip = 0x00;
#endif
            (*devicenode)->IsDeviceRequestUpdate = 0x01;/**To infrom node created 1st time*/
            (*devicenode)->descDoc=(*DescDoc);
            (*devicenode)->timeStamp=time(NULL);
            *deviceIndex=0x01;
            UnlockDeviceNode(&(tmpdevnode->device.lock));
        }
    }
    UnlockDeviceSync();

    return ret;
}

int CtrlPointCallbackEventHandler(Upnp_EventType EventType,
                                  void *Event,
                                  void *Cookie )
{
    int isAdv = 0x00;
    int deviceIndex = 0x00;
    CtrlPointPluginDevice *tmpdevnode=NULL;
    if(g_CtrlPointDelete) {
        /**This is indicate control point remove 1 means delete is going on*/
        APP_LOG("UPnPCtrPt", LOG_DEBUG,"Control point Node Delete is going on and flag g_CtrlPointDelete=%d",g_CtrlPointDelete);
        return 0;
    }
    switch ( EventType ) {
    case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
        //- PLEASE do NOT break here, let it fall through
        //- Adv and discovery response using the same API, but taking a bit difference
    {
        UpnpDiscovery *d_event = (UpnpDiscovery *)Event;
        /* Set the isAdv flag for any 1 advertisement message only */
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "Turning on adv flag on devid: %s\n", UpnpDiscovery_get_DeviceID_cstr(d_event));
        int rnd = (30 + (rand() % 30));
        /* delay the advertisement processing */
        sleep(rnd);
        isAdv=0x1;
    }

    case UPNP_DISCOVERY_SEARCH_RESULT: {
        UpnpDiscovery *d_event = (UpnpDiscovery *)Event;
        IXML_Document *DescDoc = NULL;

        if(UpnpDiscovery_get_ErrCode(d_event) != UPNP_E_SUCCESS ) {
            APP_LOG("UPnPCtrPt", LOG_ERR, "Error in Discovery Callback -- %d\n", UpnpDiscovery_get_ErrCode(d_event));
        }
        if(!IsDownLoadDescDoc(d_event, &DescDoc, &tmpdevnode, &deviceIndex, isAdv)) {
            if (DescDoc != NULL) {
                CtrlPointProcessDeviceDiscovery(DescDoc, d_event, tmpdevnode, deviceIndex, isAdv);
            }
        }

        break;
    }

    case UPNP_DISCOVERY_SEARCH_TIMEOUT:
        /*
           Nothing to do here...
         */
        break;

    case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE: {
        APP_LOG("UPnPCtrPt", LOG_INFO, "UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE received");

        UpnpDiscovery *d_event = (UpnpDiscovery *)Event;

        if(UpnpDiscovery_get_ErrCode(d_event) != UPNP_E_SUCCESS ) {
            APP_LOG("UPnPCtrPt", LOG_INFO, "UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE: -- %d\n", UpnpDiscovery_get_ErrCode(d_event));
            return UpnpDiscovery_get_ErrCode(d_event);
        }
        CtrlPointProcessDeviceByebye(d_event);

        break;
    }

    case UPNP_CONTROL_ACTION_COMPLETE: {
        UpnpActionComplete *a_event = (UpnpActionComplete *)Event;

        if (0x00 == a_event) {
            break;
        }

        if( UpnpActionComplete_get_ErrCode(a_event) != UPNP_E_SUCCESS ) {
            APP_LOG("UPnPCtrPt",LOG_ERR, "UPNP_CONTROL_ACTION_COMPLETE, failure [%d]\n", UpnpActionComplete_get_ErrCode(a_event));
        } else {

            APP_LOG("UPnPCtrPt",LOG_INFO, "UPNP_CONTROL_ACTION_COMPLETE, success");
            CtrlPointProcessControlAction(a_event);
        }
    }
    break;

    case UPNP_CONTROL_GET_VAR_COMPLETE: {
        break;
    }


    case UPNP_EVENT_RECEIVED: {

        UpnpEvent *e_event = (UpnpEvent *)Event;
        APP_LOG("UPnPCtrPt", LOG_DEBUG, "UPNP_EVENT_RECEIVED: ");
        ProcessUpNpNotify(e_event);

        break;
    }

    case UPNP_EVENT_SUBSCRIBE_COMPLETE: {

        APP_LOG("UPnPCtrPt",LOG_DEBUG, "UPNP_EVENT_SUBSCRIBE_COMPLETE: ");

        break;
    }


    case UPNP_EVENT_UNSUBSCRIBE_COMPLETE: {
        break;
    }
    case UPNP_EVENT_RENEWAL_COMPLETE: {

        break;
    }

    case UPNP_EVENT_AUTORENEWAL_FAILED:
    case UPNP_EVENT_SUBSCRIPTION_EXPIRED: {
        break;
    }

    /*
       ignore these cases, since this is not a device
     */
    case UPNP_EVENT_SUBSCRIPTION_REQUEST:
    case UPNP_CONTROL_GET_VAR_REQUEST:
    case UPNP_CONTROL_ACTION_REQUEST:
        break;
    }

    return 0;
}

/***********************************************************
 *
 *
 *
 *
 *
 *
 *
 *
 *************************************************************/
int PluginCtrlPointSetSensorEvent(int deviceIndex, const char* UDN, int eventStatus, const char* eventFriendlyName, int triggerDuration /*minutes*/)
{
    PluginCtrlPointSendAction(PLUGIN_E_SETUP_SERVICE, deviceIndex, "SetSensorEvent", 0x00, 0x00, 0x00);
    return 0x00;
}

#if 1
int PluginCtrlPointAddRule(int deviceIndex)
{
    PluginCtrlPointSendAction(PLUGIN_E_RULES_SERVICE, deviceIndex, "EditRule", 0x00, 0x00, 0x00);
    return 0;
}
#endif
int PluginCtrlPointGetFirmware(int deviceIndex)
{
    PluginCtrlPointSendAction(PLUGIN_E_FIRMWARE_SERVICE, deviceIndex, "GetFirmwareVersion", 0x00, 0x00, 0x00);
    return 0;
}

int PluginCtrlPointUpdateFirmware(int deviceIndex, const char* URL)
{
    char* paramNames[] = {"URL"};
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "URL:%s\n", URL);
    PluginCtrlPointSendAction(PLUGIN_E_FIRMWARE_SERVICE, deviceIndex, "UpdateFirmware", (const char **)paramNames, (char **)&URL, 0x01);
    return 0;
}

/***
 *
 *
 */
int PluginCtrlPointSyncTime(int deviceIndex, int utc, int TimeZone, int dstEnable)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "calling PluginCtrlPointGetApList: %d device", deviceIndex);

    char* paramsList[] = {"utc", "TimeZone", "dst"};

    char*  values[3];
    values[0] = (char*)MALLOC(SIZE_16B+1);
    values[1] = (char*)MALLOC(SIZE_16B+1);
    values[2] = (char*)MALLOC(SIZE_16B+1);

    snprintf(values[0], SIZE_16B, "%d", utc);
    snprintf(values[1], SIZE_16B, "%d", TimeZone);
    snprintf(values[2], SIZE_16B, "%d", dstEnable);

    PluginCtrlPointSendAction(PLUGIN_E_TIME_SYNC_SERVICE, deviceIndex, "SyncTime", (const char **)paramsList, values, 3);

    free(values[0]);
    free(values[1]);
    free(values[2]);

    return 0x00;
}

int PluginCtrlPointShareHWInfo(int deviceIndex)
{
    char *paramNames[] = {"Mac", "Serial", "Udn","RestoreState","HomeId","PluginKey"};
    char*  values[6];
    values[0] = (char*)ZALLOC(SIZE_128B);
    values[1] = (char*)ZALLOC(SIZE_128B);
    values[2] = (char*)ZALLOC(SIZE_128B);
    values[3] = (char*)ZALLOC(SIZE_128B);
    values[4] = (char*)ZALLOC(SIZE_128B);
    values[5] = (char*)ZALLOC(SIZE_128B);

    snprintf(values[0], SIZE_128B, "%s", g_szWiFiMacAddress);
    snprintf(values[1], SIZE_128B, "%s", g_szSerialNo);
    snprintf(values[2], SIZE_128B, "%s", g_szUDN);
    snprintf(values[3], SIZE_128B, "%s", g_szRestoreState);
    snprintf(values[4], SIZE_128B, "%s", g_szHomeId);
    snprintf(values[5], SIZE_128B, "%s", g_szPluginPrivatekey);

    APP_LOG("UPnPCtrPt",LOG_HIDE, "PluginCtrlPointShareHWInfo:values[0]=%s\n",values[0]);
    APP_LOG("UPnPCtrPt",LOG_HIDE, "PluginCtrlPointShareHWInfo:values[1]=%s\n",values[1]);
    APP_LOG("UPnPCtrPt",LOG_HIDE, "PluginCtrlPointShareHWInfo:values[2]=%s\n",values[2]);
    APP_LOG("UPnPCtrPt",LOG_HIDE, "PluginCtrlPointShareHWInfo:values[3]=%s\n",values[3]);
    APP_LOG("UPnPCtrPt",LOG_HIDE, "PluginCtrlPointShareHWInfo:values[4]=%s\n",values[4]);
    APP_LOG("UPnPCtrPt",LOG_HIDE, "PluginCtrlPointShareHWInfo:values[5]=%s\n",values[5]);

    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "ShareHWInfo", (const char **)paramNames, values, 0x06);

    free(values[0]);
    free(values[1]);
    free(values[2]);
    free(values[3]);
    free(values[4]);
    free(values[5]);

    return 0x00;
}

int PluginCtrlPointCloseAp(int devnum)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "calling PluginCtrlPointCloseAp: %d device", devnum);
    PluginCtrlPointSendAction(PLUGIN_E_SETUP_SERVICE, devnum, "CloseSetup", 0x00, 0x00, 0x00);

    return UPNP_E_SUCCESS;
}

int PluginCtrlPointGetMetaInfo(int devnum)
{
    PluginCtrlPointSendAction(PLUGIN_E_METAINFO_SERVICE, devnum, "GetMetaInfo", 0x00, 0x00, 0x00);
    return 0;
}

int PluginGetNetworkStatus(int devnum)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "calling PluginGetNetworkStatus: %d device", devnum);
    PluginCtrlPointSendAction(PLUGIN_E_SETUP_SERVICE, devnum, "GetNetworkStatus", 0x00, 0x00, 0x00);

    return UPNP_E_SUCCESS;
}

int PluginCtrlPointGetApList(int devnum)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "calling PluginCtrlPointGetApList: %d device", devnum);

    PluginCtrlPointSendAction(PLUGIN_E_SETUP_SERVICE, devnum, "GetApList", 0x00, 0x00, 0x00);

    return UPNP_E_SUCCESS;
}

#define MAX_NETWORK_PARAMS 5
int  PluginCtrlPointConnectHomeNetwork(int deviceIndex, int channel, const char* ssid, const char* Auth, const char* Encrypt, const char* password)
{
    APP_LOG("UPnPCtrPt",LOG_HIDE, "channel: %d, ssid: %s, auth: %s, encrypto: %s, password: %s\n", channel, ssid, Auth, Encrypt, password);

    char *paramNames[] = {"ssid", "auth", "encrypt", "password", "channel"};
    char* values[MAX_NETWORK_PARAMS];
    values[0x00] = (char*)ZALLOC(SIZE_32B);
    values[0x01] = (char*)ZALLOC(SIZE_32B);
    values[0x02] = (char*)ZALLOC(SIZE_32B);
    values[0x03] = (char*)ZALLOC(SIZE_32B);
    values[0x04] = (char*)ZALLOC(SIZE_32B);

    strncpy(values[0], ssid, SIZE_32B-1);
    strncpy(values[1], Auth, SIZE_32B-1);
    strncpy(values[2], Encrypt, SIZE_32B-1);
    strncpy(values[3], password, SIZE_32B-1);

    snprintf(values[4], SIZE_32B, "%d", channel);

    PluginCtrlPointSendAction(PLUGIN_E_SETUP_SERVICE, deviceIndex, "ConnectHomeNetwork", (const char **)paramNames, values, MAX_NETWORK_PARAMS);

    free(values[0x00]);
    free(values[0x01]);
    free(values[0x02]);
    free(values[0x03]);
    free(values[0x04]);

    APP_LOG("UPnPCtrPt",LOG_DEBUG, "PluginCtrlPointConnectHomeNetwork: command request\n");

    return UPNP_E_SUCCESS;
}


int PluginCtrlPointGetDevice(int devnum, pCtrlPluginDeviceNode *devnode)
{
    int count = devnum;
    pCtrlPluginDeviceNode tmpdevnode = NULL;

    if (count) {
        tmpdevnode = g_pGlobalPluginDeviceList;
    } else {
        return PLUGIN_ERROR;
    }

    while (--count && tmpdevnode) {
        tmpdevnode = tmpdevnode->next;
    }

    if (tmpdevnode)
        *devnode = tmpdevnode;
    else
        APP_LOG("UPnPCtrPt", LOG_ERR, "PluginCtrlPointGetDevice: can not find device");

    return PLUGIN_SUCCESS;
}


int PluginCtrlPointGetSimulatedRuleData(char *deviceUdn)
{
    int deviceIndex = 0;

    LockDeviceSync();
    deviceIndex = GetDeviceIndexByUDN((const char *)deviceUdn);
    UnlockDeviceSync();

    //APP_LOG("UPnPCtrPt", LOG_DEBUG, "    --- upnp device index:%d \n", deviceIndex);
    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "GetSimulatedRuleData", 0x00, 0x00, 0x00);

    return UPNP_E_SUCCESS;
}


int PluginCtrlPointSendAction(int service, int devnum, const char *actionname, const char **param_name, char **param_val, int param_count)
{
    pCtrlPluginDeviceNode 	devnode = NULL;
    IXML_Document 			*actionNode = NULL;

    int rect = UPNP_E_SUCCESS;
    int param;

    char szTmpEventURL[SIZE_256B];
    memset(szTmpEventURL, 0x00, sizeof(szTmpEventURL));

    //-Lock and unlock quickly so that no delay
    LockDeviceSync();

    rect = PluginCtrlPointGetDevice(devnum, &devnode);
    if (PLUGIN_SUCCESS == rect) {
        if (0x00 != devnode)
            strncpy(szTmpEventURL, devnode->device.services[service].ControlURL, sizeof(szTmpEventURL)-1);
    }

    UnlockDeviceSync();

    if (0x00 != strlen(szTmpEventURL)) {
        if (0 == param_count) {
            actionNode = UpnpMakeAction(actionname, CtrleeDeviceServiceType[service], 0, NULL);
        } else {
            for (param = 0; param < param_count; param++) {
                rect = UpnpAddToAction(&actionNode, actionname,
                                       CtrleeDeviceServiceType[service], param_name[param], param_val[param]);

                if (0x00 != rect) {
                    APP_LOG("UPnPCtrPt", LOG_ERR, "UpnpAddToAction: can not add action to list");
                }
            }
        }

        if(CtrleeDeviceServiceType[service] != NULL) {
            rect = UpnpSendActionAsync(ctrlpt_handle, szTmpEventURL, CtrleeDeviceServiceType[service], NULL, actionNode,
                                       (Upnp_FunPtr) CtrlPointCallbackEventHandler, NULL);

            if (rect != UPNP_E_SUCCESS) {
                APP_LOG("UPnPCtrPt",LOG_ERR, "Error in UpnpSendActionAsync -- %d\n", rect);
                rect = 0x01;
            }

            //TODO: This call can crash if service on which action it trying to send is not subscribed by any control point
        }

    } else {
        APP_LOG("UPnPCtrPt",LOG_ERR, "PluginCtrlPointSendAction: can not find device in device table");
    }

    if (actionNode)
        ixmlDocument_free(actionNode);

    return rect;
}

int PluginCtrlPointSendActionAll(int service, const char *actionname, const char **param_name, char **param_val, int param_count)
{
    IXML_Document 			*actionNode = NULL;
    pCtrlPluginDeviceNode tmpdevnode = NULL;

    int rect = UPNP_E_SUCCESS;
    int param;

    char szTmpEventURL[SIZE_256B];

    //-Lock and unlock quickly so that no delay
    LockDeviceSync();

    tmpdevnode = g_pGlobalPluginDeviceList;

    while (tmpdevnode) {
#if defined(SIMULATED_OCCUPANCY)
        if(!(tmpdevnode->Skip))
#endif
        {
            memset(szTmpEventURL, 0x00, sizeof(szTmpEventURL));
            strncpy(szTmpEventURL, tmpdevnode->device.services[service].ControlURL, sizeof(szTmpEventURL)-1);

            if (0x00 != strlen(szTmpEventURL)) {
                if (0 == param_count) {
                    actionNode = UpnpMakeAction(actionname, CtrleeDeviceServiceType[service], 0, NULL);
                } else {
                    for (param = 0; param < param_count; param++) {
                        rect = UpnpAddToAction(&actionNode, actionname,
                                               CtrleeDeviceServiceType[service], param_name[param], param_val[param]);

                        if (0x00 != rect) {
                            APP_LOG("UPnPCtrPt", LOG_ERR, "UpnpAddToAction: can not add action to list");
                        }
                    }
                }

                if(CtrleeDeviceServiceType[service] != NULL) {
                    rect = UpnpSendActionAsync(ctrlpt_handle, szTmpEventURL, CtrleeDeviceServiceType[service], NULL, actionNode,
                                               (Upnp_FunPtr) CtrlPointCallbackEventHandler, NULL);

                    if (rect != UPNP_E_SUCCESS) {
                        APP_LOG("UPnPCtrPt",LOG_ERR, "Error in UpnpSendActionAsync -- %d\n", rect);
                        rect = 0x01;
                    } else {
                        APP_LOG("UPnPCtrPt",LOG_INFO, "UpnpSendActionAsync %s successful for %s", actionname, tmpdevnode->UDN);
                    }
                }

            } else {
                APP_LOG("UPnPCtrPt",LOG_ERR, "PluginCtrlPointSendAction: can not find device in device table");
            }

            if (actionNode) {
                ixmlDocument_free(actionNode);
                actionNode = NULL;
            }

        }
        tmpdevnode = tmpdevnode->next;
    }

    UnlockDeviceSync();

    return rect;
}

/************************************************************************
 * Function: deviceNodeInList
 *     Function to search if passed UDN exists in the control point device list
 *  Parameters:
 *     UDN - UDN to be looked up in the device list
 *     deviceList - Array of strings holding the device UDNs
 *     count - Number of strings in the deviceList
 *  Return:
 *     Returns SUCCESS or FAILURE based on whether UDN was found or not
************************************************************************/


int deviceNodeInList(char *UDN, char** deviceList, int count)
{
    int i=0;
    int found=0;

    APP_LOG("UPnPCtrPt", LOG_DEBUG, "Look up UDN %s", UDN);

    for(i=0; i<count; i++) {
        if(deviceList[i] && !strncmp(UDN, deviceList[i], SIZE_UDN)) {
            APP_LOG("UPnPCtrPt",LOG_DEBUG, "Found %s in devicelist at index %d", UDN, i);
            found=1;
            break;
        }
    }

    if(!found) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "%s not found in devicelist", UDN);
        return FAILURE;
    } else {
        return SUCCESS;
    }
}

/************************************************************************
 * Function: PluginCtrlPointSendActionToList
 *     Function to send UPnP action to list of UDNs
 *  Parameters:
 *     service - Service to which UPnP action belongs
 *     actionname - UPnP action to be called
 *     param_name - Paramter names for the UPnP action
 *     param_val - Paramter values for the UPnP action
 *     param_count - Number of Paramters passed
 *     deviceList - Array of strings containing the UDNs of target devices
 *     listCount - Number of target devices
 *  Return:
 *     Returns SUCCESS or FAILURE
************************************************************************/


int PluginCtrlPointSendActionToList(int service, const char *actionname, const char **param_name, char **param_val, int param_count, char** deviceList, int listCount)
{
    IXML_Document 			*actionNode = NULL;
    pCtrlPluginDeviceNode tmpdevnode = NULL;

    int rect = UPNP_E_SUCCESS;
    int param;

    char szTmpEventURL[SIZE_256B];

    //-Lock and unlock quickly so that no delay
    LockDeviceSync();

    tmpdevnode = g_pGlobalPluginDeviceList;

    while (tmpdevnode) {
        if(strncmp(tmpdevnode->UDN, g_szUDN_1, sizeof(tmpdevnode->UDN)) && (deviceNodeInList(tmpdevnode->UDN, deviceList, listCount) == SUCCESS)) {
            memset(szTmpEventURL, 0x00, sizeof(szTmpEventURL));
            strncpy(szTmpEventURL, tmpdevnode->device.services[service].ControlURL, sizeof(szTmpEventURL)-1);

            if (0x00 != strlen(szTmpEventURL)) {
                if (0 == param_count) {
                    actionNode = UpnpMakeAction(actionname, CtrleeDeviceServiceType[service], 0, NULL);
                } else {
                    for (param = 0; param < param_count; param++) {
                        rect = UpnpAddToAction(&actionNode, actionname,
                                               CtrleeDeviceServiceType[service], param_name[param], param_val[param]);

                        if (0x00 != rect) {
                            APP_LOG("UPnPCtrPt", LOG_ERR, "UpnpAddToAction: can not add action to list");
                            /* No point continuing with this device */
                            if (actionNode) {
                                ixmlDocument_free(actionNode);
                                actionNode = NULL;
                            }
                            tmpdevnode = tmpdevnode->next;
                            continue;
                        }
                    }
                }

                if(CtrleeDeviceServiceType[service] != NULL) {
                    rect = UpnpSendActionAsync(ctrlpt_handle, szTmpEventURL, CtrleeDeviceServiceType[service], NULL, actionNode,
                                               (Upnp_FunPtr) CtrlPointCallbackEventHandler, NULL);

                    if (rect != UPNP_E_SUCCESS) {
                        APP_LOG("UPnPCtrPt",LOG_ERR, "Error in UpnpSendActionAsync -- %d\n", rect);
                    } else {
                        APP_LOG("UPnPCtrPt",LOG_DEBUG, "UpnpSendActionAsync %s successful for %s", actionname, tmpdevnode->UDN);
                    }
                }

            } else {
                APP_LOG("UPnPCtrPt",LOG_ERR, "PluginCtrlPointSendAction: can not find device in device table");
            }

            if (actionNode) {
                ixmlDocument_free(actionNode);
                actionNode = NULL;
            }

        }
        tmpdevnode = tmpdevnode->next;
    }

    UnlockDeviceSync();

    return rect;
}

#ifdef SIMULATED_OCCUPANCY
int PluginCtrlPointSendActionSimulated(int service, const char *actionname)
{
    IXML_Document 	*actionNode = NULL;
    pCtrlPluginDeviceNode tmpdevnode = NULL;
    SimulatedDevInfo *simdeviceinf;
    int simdevicecnt = -1, i = 0;
    int rect = UPNP_E_SUCCESS;
    char szTmpEventURL[SIZE_256B] = {'\0',};
    char szUDN[SIZE_UDN] = {'\0',};

    if (0x00 == gpSimulatedDevice) {
        APP_LOG("UPnPCtrPt",LOG_DEBUG, "Simulated Occupancy is not there anymore. Returning!!");
        return 0x1;
    }

    LockSimulatedOccupancy();
    simdeviceinf = gpSimulatedDevice->pDevInfo;
    simdevicecnt = gpSimulatedDevice->totalCount;
    UnlockSimulatedOccupancy();

    APP_LOG("Rule", LOG_DEBUG, "action: %s simdevicecnt: %d", actionname, simdevicecnt);

    if(CtrleeDeviceServiceType[service] == NULL) {
        APP_LOG("UPnPCtrPt", LOG_ERR, "Undefined service type: %d", service);
        return 0x1;
    }

    for(i = 0; i < simdevicecnt; i++) {
        memset(szUDN, 0, sizeof(szUDN));

        if(strstr(simdeviceinf[i].UDN, "uuid:Bridge") != NULL) {
            strncpy(szUDN, simdeviceinf[i].UDN, BRIDGE_UDN_LEN);
        } else if(strstr(simdeviceinf[i].UDN, "uuid:Maker") != NULL) {
            strncpy(szUDN, simdeviceinf[i].UDN, MAKER_UDN_LEN);
            strncat(szUDN, ":sensor:switch", sizeof(szUDN)-strlen(szUDN)-1);
        } else
            strncpy(szUDN, simdeviceinf[i].UDN, sizeof(szUDN)-1);

        APP_LOG("UPNP: Device", LOG_DEBUG, "Input UDN: %s converted UDN: %s", simdeviceinf[i].UDN, szUDN);

        LockDeviceSync();
        tmpdevnode = g_pGlobalPluginDeviceList;

        while (tmpdevnode) {
            if(strcmp(tmpdevnode->UDN, szUDN) == 0) { //a simulated device
                APP_LOG("UPNP: Device", LOG_DEBUG, "device: %s in global list matched with device: %s in simulated list", tmpdevnode->UDN, simdeviceinf[i].UDN);

                memset(szTmpEventURL, 0x00, sizeof(szTmpEventURL));
                strncpy(szTmpEventURL, tmpdevnode->device.services[service].ControlURL, sizeof(szTmpEventURL)-1);

                if (0x00 != strlen(szTmpEventURL)) {
                    rect = UpnpAddToAction(&actionNode, actionname,
                                           CtrleeDeviceServiceType[service], (const char *)"UDN", (const char *)simdeviceinf[i].UDN);

                    if (0x00 != rect) {
                        APP_LOG("UPnPCtrPt", LOG_ERR, "UpnpAddToAction: can not add action to list");
                    } else {
                        rect = UpnpSendActionAsync(ctrlpt_handle, szTmpEventURL, CtrleeDeviceServiceType[service], NULL, actionNode,
                                                   (Upnp_FunPtr) CtrlPointCallbackEventHandler, NULL);

                        if (rect != UPNP_E_SUCCESS) {
                            APP_LOG("UPnPCtrPt",LOG_ERR, "Error in UpnpSendActionAsync -- %d\n", rect);
                            rect = 0x01;
                        } else {
                            APP_LOG("UPnPCtrPt",LOG_ERR, "UpnpSendActionAsync %s successful for %s", actionname, simdeviceinf[i].UDN);
                        }
                    }
                } else {
                    APP_LOG("UPnPCtrPt",LOG_ERR, "PluginCtrlPointSendAction: can not find device in device table");
                }

                if (actionNode) {
                    ixmlDocument_free(actionNode);
                    actionNode = NULL;
                }


            }
            tmpdevnode = tmpdevnode->next;
        }
        UnlockDeviceSync();
    }

    return rect;
}
#endif

//------------------------------------------------------Now for new service ---------------------------------

int PluginCtrlPointSetFriendlyName(int deviceIndex, const char* name)
{
    APP_LOG("UPnPCtrPt",LOG_INFO, "name: %s", name);
    char *paramNames[] = {"FriendlyName"};

    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "SetFriendlyName", (const char **)&paramNames, (char **)&name, 1);
    return 0;
}

int PluginCtrlPointSensorEventInd(int deviceIndex, int nowAction, int duration, int endAction, const char* udn)
{

    APP_LOG("UPnPCtrPt:Sensor", LOG_DEBUG, "######## %s called", __FUNCTION__);


    char *paramNames[] = {"BinaryState", "Duration", "EndAction", "UDN"};
    char *values[4];
    values[0x00] = (char*)MALLOC(SIZE_8B+1);
    values[0x01] = (char*)MALLOC(SIZE_8B+1);
    values[0x02] = (char*)MALLOC(SIZE_8B+1);
    values[0x03] = (char*)MALLOC(SIZE_256B+1);
    snprintf(values[0x00], SIZE_8B, "%d", nowAction);
    snprintf(values[0x01], SIZE_8B, "%d", duration);
    snprintf(values[0x02], SIZE_8B, "%d", endAction);
    snprintf(values[0x03], SIZE_256B, "%s", udn);

    APP_LOG("UPnPCtrPt:Sensor", LOG_DEBUG, "start action: %d, command duration: %d, stop action: %d, udn: %s",
            nowAction, duration, endAction, udn);

    int rect = PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex,
                                         "SetBinaryState",
                                         (const char **)&paramNames, (char **)&values, 4);

    free(values[0x00]);
    free(values[0x01]);
    free(values[0x02]);
    free(values[0x03]);

    return rect;

}


int PluginCtrlPointChangeBinaryState (int deviceIndex, int newValue)
{
    char values[8];
    memset(values, 0x00, 8);
    snprintf(values, 8, "%d", newValue);

    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "SetBinaryState", 0x00, 0x00, 0x00);
    return 0;
}

/***
 *
 ***********************************************************************************************/
int PluginCtrlPointSetBinaryState (int deviceIndex, const char** name)
{

    //- TODO: ##################################
    int hasDevice = 0x00;
    LockDeviceSync();
    if (g_pGlobalPluginDeviceList)
        hasDevice = 0x01;
    UnlockDeviceSync();

    if (hasDevice) {
        char *paramNames[] = {"BinaryState"};
        PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "SetBinaryState", (const char **)&paramNames, (char **)name, 1);
    } else {
        APP_LOG("UPnPCtrPt:Sensor", LOG_ERR, "No device is discovery list");
    }
    return 0;
}


int PluginCtrlPointGetFriendlyName(int deviceIndex, const char* name)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "Get friendly name ......");

    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "GetFriendlyName", 0x00, 0x00, 0x00);
    return 0;
}
int PluginCtrlPointGetBinary (int deviceIndex, const char* name)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "name: %s", name);

    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "GetBinaryState", 0x00, 0x00, 0x00);
    return 0;
}
int PluginCtrlPointGetHomeId(int deviceIndex, const char* name)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "Get Home Id ......");

    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "GetHomeId", 0x00, 0x00, 0x00);
    return 0;
}
int PluginCtrlPointGetDeviceId(int deviceIndex, const char* name)
{
    APP_LOG("UPnPCtrPt",LOG_DEBUG, "Get Device Id ......");

    PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "GetDeviceId", 0x00, 0x00, 0x00);
    return 0;
}


void *AutoCtrlPointTestLoop(void *args)
{
    static int k = 0x01;
    pluginUsleep(30000000);
    while (1) {
        pluginUsleep(10000000);
        APP_LOG("UPnPCtrPt:AutoCtrlPointTestLoop", LOG_DEBUG, "###### Auto test command send ####");

        PluginCtrlPointChangeBinaryState(1, k % 2);
        k++;
    }
    return NULL;
}

void initDeviceSync()
{
    osUtilsCreateLock(&DeviceListMutex);
}


void LockDeviceSync()
{
    osUtilsGetLock(&DeviceListMutex);
}

void UnlockDeviceSync()
{
    osUtilsReleaseLock(&DeviceListMutex);
}

void initSignNotify()
{
    osUtilsCreateLock(&SignNotifyMutex);
}

void LockSignNotify()
{
    osUtilsGetLock(&SignNotifyMutex);
}

void UnlockSignNotify()
{
    osUtilsReleaseLock(&SignNotifyMutex);
}
void initDeviceNodeLock(pthread_mutex_t *theLock)
{
    osUtilsCreateLock(theLock);
}
void deinitDeviceNodeLock(pthread_mutex_t *theLock)
{
    osUtilsDestroyLock(theLock);
}
void LockDeviceNode(pthread_mutex_t *theLock)
{
    osUtilsGetLock(theLock);
}

void UnlockDeviceNode(pthread_mutex_t *theLock)
{
    osUtilsReleaseLock(theLock);
}
int StopPluginCtrlPoint(void)
{
    if (-1 == ctrlpt_handle)
        return 0x00;

    StopDiscoverTask();

    APP_LOG("UPnPCtrPt", LOG_DEBUG, "StopPluginCtrlPoint Called");
    CtrlPointRemoveAll(1);
    UpnpUnRegisterClient( ctrlpt_handle );
    ctrlpt_handle = -1;
    g_CtrlPointDelete = 0x0;/**This is indicate control point remove 1 means delete is going on*/
    APP_LOG("UPnPCtrPt", LOG_DEBUG, "StopPluginCtrlPoint: UpnpUnRegisterClient DONE");
    APP_LOG("UPnPCtrPt", LOG_DEBUG, "StopPluginCtrlPoint: UpnpFinish DONE");

    return PLUGIN_SUCCESS;
}

int CtrlPointRemoveAll(int stopctrlptflag)
{
    LockDeviceSync();

    pCtrlPluginDeviceNode curdevnode;
    pCtrlPluginDeviceNode next;

    curdevnode = g_pGlobalPluginDeviceList;

    while (curdevnode) {
        if(!stopctrlptflag) {
            curdevnode->device.IsDeviceRequestUpdate = 0x01;	//- Reset, requested update when next discovery respone comes
        }
        CtrlPointDeleteNode(curdevnode);
        next = curdevnode->next;
        if(stopctrlptflag) {
            APP_LOG("UPnPCtrPt", LOG_DEBUG, "Deleting node of dev UDN: %s", curdevnode->UDN);
            g_CtrlPointDelete = 0x01;/**This is indicate control point remove 1 means delete is going on*/
            LockDeviceNode(&(curdevnode->device.lock));/**Device Node access by putting lock*/
            if(curdevnode->device.descDoc) {
                ixmlDocument_free(curdevnode->device.descDoc);/**Free the existing device doc 1st*/
                curdevnode->device.descDoc = NULL;
            }
            UnlockDeviceNode(&(curdevnode->device.lock));
            deinitDeviceNodeLock(&(curdevnode->device.lock));
            free(curdevnode);
        }
        curdevnode = next;
    }

    APP_LOG("UPnPCtrPt", LOG_DEBUG, "CtrlPointRemoveAll done");
    if(stopctrlptflag) {
        g_pGlobalPluginDeviceList = NULL;
    }
    UnlockDeviceSync();

    return PLUGIN_SUCCESS;
}
int CtrlPointDeleteNode( CtrlPluginDeviceNode *node )
{
    APP_LOG("UPnPCtrPt", LOG_DEBUG, "CtrlPointDeleteNode Called");
    int rc, service;

    if (NULL == node) {
        APP_LOG("UPnPCtrPt", LOG_ERR, "ERROR: CtrlPointDeleteNode: Node is empty");
        return FAILURE;
    }

    for (service = 0; service < PLUGIN_MAX_SERVICES; service++) {
        if ((PLUGIN_E_EVENT_SERVICE == service)) {
            if (strcmp(node->device.services[service].SID, "") != 0) {
                rc = UpnpUnSubscribe(ctrlpt_handle, node->device.services[service].SID);
                if (UPNP_E_SUCCESS == rc) {
                    APP_LOG("UPnPCtrPt", LOG_NOTICE, "SUCCESS: Unsubscribed: service: %s :SID=%s",
                            node->device.services[service].ServiceType,
                            node->device.services[service].SID);
                } else {
                    APP_LOG("UPnPCtrPt", LOG_ERR, "####### ERROR: unsubscribed: service: %s SID=%s, RC=%d",
                            node->device.services[service].ServiceType, node->device.services[service].SID, rc);
                }
            }
        }
    }

    APP_LOG("UPnPCtrPt", LOG_DEBUG, "CtrlPointDeleteNode done\n");

    return PLUGIN_SUCCESS;
}
int CtrlPointUnsubcribeNodeService(CtrlPointPluginDevice *node )
{
    APP_LOG("UPnPCtrPt", LOG_DEBUG, "CtrlPointDeleteNode Called");
    int rc, service;

    if (NULL == node) {
        APP_LOG("UPnPCtrPt", LOG_ERR, "ERROR: CtrlPointDeleteNode: Node is empty");
        return FAILURE;
    }

    for (service = 0; service < PLUGIN_MAX_SERVICES; service++) {
        if ((PLUGIN_E_EVENT_SERVICE == service)) {
            if (strcmp(node->services[service].SID, "") != 0) {
                rc = UpnpUnSubscribe(ctrlpt_handle, node->services[service].SID);
                if (UPNP_E_SUCCESS == rc) {
                    APP_LOG("UPnPCtrPt", LOG_NOTICE, "SUCCESS: Unsubscribed: service: %s :SID=%s",
                            node->services[service].ServiceType,
                            node->services[service].SID);
                } else {
                    APP_LOG("UPnPCtrPt", LOG_ERR, "####### ERROR: unsubscribed: service: %s SID=%s, RC=%d",
                            node->services[service].ServiceType, node->services[service].SID, rc);
                }
            }
        }
    }

    APP_LOG("UPnPCtrPt", LOG_DEBUG, "CtrlPointDeleteNode done\n");

    return PLUGIN_SUCCESS;
}

int CtrlPointRediscoverCallBack(void)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "##################### Control point to re-discover ###################");

    CtrlPointDiscoverDevices();

    return 0x00;
}
