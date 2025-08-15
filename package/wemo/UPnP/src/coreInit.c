/***************************************************************************
*
*
* coreInit.c
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
#include "global.h"
#include <stdlib.h>
#include "fcntl.h"
#include "wemodefs.h"
#include "fw_rev.h"
#include <ithread.h>
#include "types.h"
#include "upnp.h"
#include "controlledevice.h"
#include "plugin_ctrlpoint.h"
#include "wifiHndlr.h"
#include "fwDl.h"
#include "utils.h"
#include "utlist.h"
#include "osUtils.h"
#include "httpsWrapper.h"
#include "logger.h"
#include "itc.h"
#include "gpio.h"
#include "WemoDB.h"

#ifdef PRODUCT_WeMo_Dimmer
#include "plugin_wasp.h"
#endif

#include "rule.h"
#include "watchDog.h"
#ifdef SIMULATED_OCCUPANCY
#include "simulatedOccupancy.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include "thready_utils.h"
#include <sys/syscall.h>
#include "thready_utils.h"
#include <sys/syscall.h>
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

#define WEMO_VERSION_GEMTEK_PROD   "WeMo_version"
#define BELKIN_DAEMON_SUCCESS "Belkin_daemon_success"
#define LAN_IPADDR "lan_ipaddr"
#define MY_FW_VERSION "my_fw_version"
#define FW_UPGRD_MEM_THR_VALUE "5600"

#define SIGNED_PUBKEY_FILE_NAME  "WeMoPubKey.asc"
#define IMPORT_KEY_PARAM_NAME "import_pkey_name"

void disconnectFromRouter();

void CheckAttributes();

//extern char g_szBootArgs[SIZE_128B];
extern int g_PowerStatus;
extern void libNvramInit();
pthread_attr_t sysRestore_attr;

#define RALINK_GENERAL_READ             0x50

extern struct Command *front, *rear;
extern unsigned int g_queue_counter;
extern pthread_mutex_t g_queue_mutex;
extern pthread_attr_t wdLog_attr;
pthread_t sendremoteupdstatus_thread = -1;

extern void StartInetMonitorThread();

char g_szHomeId[SIZE_20B];
char g_szPluginPrivatekey[MAX_PKEY_LEN];
char g_routerMac[MAX_MAC_LEN];
char g_routerSsid[MAX_ESSID_LEN];
char g_szSmartDeviceId[SIZE_256B];
char g_szSmartPrivateKey[MAX_PKEY_LEN];
char g_szPluginCloudId[SIZE_16B];
int gDstSupported = 1;
int ghwVersion=1;
char g_NotificationStatus[MAX_RES_LEN];
extern SERVERENV g_ServerEnvType;
char g_serverEnvIPaddr[SIZE_32B];
char g_turnServerEnvIPaddr[SIZE_32B];

char g_szRestoreState[MAX_RES_LEN];

pthread_t logFile_thread = -1;

extern void* UDS_MonitorAndStartClientDataPath(void *arg);
extern int gRulesInitialized;
#ifdef PRODUCT_WeMo_Dimmer
extern int gNTPTimeSet;
#endif

extern int g_bWiredEthernet;

/*
 * Here's where we used to just have the thread exit.  Which sometimes
 * just leaves the application in a semi-functional state.  Better to
 * just shut down and let wemoApp get restarted.  That way, any leaked
 * resources get freed and we have a chance to recover.
 */
static void handleSigSegv(int signum,siginfo_t *pInfo,void *pVoid)
{
    uint32_t *up = (uint32_t *)&signum;
#if defined (__mips__)
    static int Recursed = 0;

    if(Recursed++ == 0) {
        APP_LOG("WiFiApp",LOG_ALERT,"[%d:%s] SIGNAL SIGSEGV [%d] RECEIVED, bad address: %p, fault address: %08x, ra: %08x",
                (int)syscall(SYS_gettid), tu_get_my_thread_name(), (unsigned int) signum, pInfo->si_addr, up[46], up[110]);
    } else {
        // Just wait for the grim reaper
        for( ; ; ) {
            sleep(100000);
        }
    }
#else
    APP_LOG("WiFiApp",LOG_ALERT,"[%d:%s] SIGNAL SIGSEGV [%d] RECEIVED, aborting...",
            (int)syscall(SYS_gettid), tu_get_my_thread_name(), signum);
#endif
    //pthread_exit(NULL);

    /* exit application on encountering SIGSEGV as per Tina's mail */
    abort();
}
static void handleSigAbrt(int signum)
{
    APP_LOG("WiFiApp",LOG_ERR,"[%d:%s] SIGNAL SIGABRT [%d] RECEIVED, Aborting.", (int)syscall(SYS_gettid), tu_get_my_thread_name(), signum);
    // It appears the authors' intention was to have the application
    // exit at this point.  That is the documented behavior of
    // exit().  Unfortunately, real-world testing shows taht only the
    // thread exits.  This is a surprise.  Fortunately, merely
    // returning from the abort signal handler also causes the process
    // to exit and in this case it actually does.
    //exit(0);
}


extern int gPrintThreadList;
/* signal handler for wemoApp */
static void handleSigPipe(int signum)
{
    APP_LOG("WiFiApp",LOG_ALERT,"SIGNAL SIGPIPE [%d] RECEIVED ..",signum);
    return;
}

void setSignalHandlers(void)
{
    struct sigaction act, oldact;
    sigset_t block_mask;

    APP_LOG("WiFiApp",LOG_DEBUG," RTMIN: %d, RTMAX: %d", SIGRTMIN, SIGRTMAX);
    sigemptyset (&block_mask);

    sigaddset (&block_mask, SIGINT);
    sigaddset (&block_mask, SIGQUIT);

    act.sa_flags = (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART | SA_SIGINFO);
    act.sa_sigaction = handleSigSegv;
    act.sa_mask = block_mask;
    if(sigaction(SIGSEGV, &act, &oldact)) {
        APP_LOG("WiFiApp",LOG_ERR,
                "sigaction failed... errno: %d", errno);
    } else {
        if(oldact.sa_handler == SIG_IGN)
            APP_LOG("WiFiApp",LOG_DEBUG,"oldact RTMIN: SIGIGN");

        if(oldact.sa_handler == SIG_DFL)
            APP_LOG("WiFiApp",LOG_DEBUG,"oldact RTMIN: SIGDFL");
    }
    act.sa_flags = (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART);
    act.sa_handler = handleSigAbrt;
    act.sa_mask = block_mask;
    if(sigaction(SIGABRT, &act, &oldact)) {
        APP_LOG("WiFiApp",LOG_ERR,
                "sigaction failed... errno: %d", errno);
    } else {
        if(oldact.sa_handler == SIG_IGN)
            APP_LOG("WiFiApp",LOG_DEBUG,"oldact RTMIN: SIGIGN");

        if(oldact.sa_handler == SIG_DFL)
            APP_LOG("WiFiApp",LOG_DEBUG,"oldact RTMIN: SIGDFL");
    }
    act.sa_flags = (SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART);
    act.sa_handler = handleSigPipe;
    act.sa_mask = block_mask;
    if(sigaction(SIGPIPE, &act, &oldact)) {
        APP_LOG("WiFiApp",LOG_ERR,
                "sigaction failed... errno: %d", errno);
    } else {
        if(oldact.sa_handler == SIG_IGN)
            APP_LOG("WiFiApp",LOG_DEBUG,"oldact RTMIN: SIGIGN");

        if(oldact.sa_handler == SIG_DFL)
            APP_LOG("WiFiApp",LOG_DEBUG,"oldact RTMIN: SIGDFL");
    }
}

#define MAX_CHANNELS 11
int selectStrongestSigApChannel()
{
    PMY_SITE_SURVEY pAvlAPList;
    /* 4 doesn't mean anything. chan value should be in 1 - 13 */
    int chan = 4;
    int count = 0, i;
    int signal = 0;

    pAvlAPList = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
    if(!pAvlAPList) {
        APP_LOG("UPNP", LOG_DEBUG,"Malloc Failed...");
        return -1;
    }

    APP_LOG("UPNP", LOG_DEBUG,"Get List...");
    EnableSiteSurvey(NULL);
    getCurrentAPList(pAvlAPList, &count);
    APP_LOG("UPNP", LOG_DEBUG,"List Size <%d>...",count);

    for (i = 0; i < count; i++) {
        int ap_signal = atoi((char *)pAvlAPList[i].signal);
        if (ap_signal > signal) {
            signal = ap_signal;
            chan = (atoi((char *)pAvlAPList[i].channel));
        }
    }

    /* if chan we got is out of range (1-13), then just set it to some value between 1-13 */
    if ((chan < 1) || (chan > 13)) {
        chan = 4;
    }
    APP_LOG("UPNP", LOG_DEBUG,"Selected channel: <%d>",chan);

    char pCommand[SIZE_64B],chBuf[SIZE_16B];
    int ret;

    memset(pCommand, 0, SIZE_64B);
    memset(chBuf, 0, SIZE_16B);
    strncpy(pCommand, "Channel=",sizeof(pCommand)-1);
    snprintf(chBuf,sizeof(chBuf),"%d",chan);
    strncat(pCommand,chBuf,sizeof(pCommand) - strlen(pCommand) - 1);
    ret = wifiSetCommand (pCommand,INTERFACE_AP);
    if(ret < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
        free (pAvlAPList);
        return FAILURE;
    }
    APP_LOG("UPNP", LOG_DEBUG, "################### ra0 is on Channel: %d ###################", chan);

    free (pAvlAPList);
    return 0;
}

extern void initdevConfiglock();

void restoreRelayState()
{
    FILE * pRelayFile = 0x00;
#if defined(PRODUCT_WeMo_Insight)
    char* szRelayPath = "/proc/RELAY_LED";
#else
    char* szRelayPath = "/proc/GPIO9";
#endif
    char szflag[4];
    int command = 0;
    char* pResult = NULL;

    pRelayFile = fopen(szRelayPath, "r");
    if (pRelayFile == 0x00) {
        APP_LOG("InitializePowerLEDState:", LOG_DEBUG, "Error on Open file for read: %s ", szRelayPath);
        return;
    }

    memset(szflag, 0x00, sizeof(szflag));
    pResult = fgets(szflag, sizeof(szflag), pRelayFile);
    if (pResult) {
        fclose(pRelayFile);
        command = atoi(szflag);
        LockLED();
        setPower(!command);
        SetCurBinaryState(!command);
        UnlockLED();
        APP_LOG("InitializePowerLEDState", LOG_DEBUG, "BOOT TIME BINARY STATE: %d , power set : %d", command, !command);
    } else {
        APP_LOG("InitializePowerLEDState", LOG_DEBUG, "READ GPIO RETURNED NULL");
        fclose(pRelayFile);
    }
}

int startCtrlPoint()
{
    if(DEVICE_SENSOR == g_eDeviceType) {
        APP_LOG("CtrlPt", LOG_DEBUG, "Sensor Start Plugin Control Point");
        return TRUE;
    } else if (DEVICE_SOCKET == g_eDeviceType) {
        if ((0x00 == atoi(g_szRestoreState)) && (0x00 == strlen(g_szHomeId) ) && (0x00 == strlen(g_szPluginPrivatekey))) {
            APP_LOG("CtrlPt", LOG_DEBUG, "Socket Start Plugin Control Point");
            return TRUE;
        }
    } else if ( (DEVICE_CROCKPOT == g_eDeviceType) || (DEVICE_SBIRON == g_eDeviceType) ||
                (DEVICE_MRCOFFEE == g_eDeviceType) || (DEVICE_PETFEEDER == g_eDeviceType) ||
                (DEVICE_SMART == g_eDeviceType)
              ) {
        if ((0x00 == atoi(g_szRestoreState)) && (0x00 == strlen(g_szHomeId) ) && (0x00 == strlen(g_szPluginPrivatekey))) {
            APP_LOG("CtrlPt", LOG_DEBUG, "Socket Start Plugin Control Point");
            return TRUE;
        }
    }
    return FAILURE;
}

void HandleHardRestoreAndCtrlPoint(int forceStart, char* if_name)
{
    if(forceStart || startCtrlPoint()) {
        APP_LOG("CtrlPt", LOG_DEBUG, "Starting Plugin Ctrl point, forceStart: %d", forceStart);
        int ret=StartPluginCtrlPoint(if_name, 0x00);
        if(UPNP_E_INIT_FAILED==ret) {
            APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
            APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
            resetSystem();
        }
        EnableContrlPointRediscover(TRUE);
    }

}


void startUPnP(int forceEnableCtrlPoint)
{
    char* ip_address = NULL;
    unsigned int port = 0;
    int ret=-1;
    //-How to make sure it connects to home network?
    if(getCurrentClientState()) {
        ip_address = wifiGetIP(INTERFACE_CLIENT);
        APP_LOG("UPNP", LOG_DEBUG, "################### UPNP on router: %s ###################", ip_address);
        ret=ControlleeDeviceStart(GetLanDeviceName(), port, desc_doc_name, web_dir_path);
        if(( ret != UPNP_E_SUCCESS ) && ( ret != UPNP_E_INIT ) ) {
            APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
            APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
            resetSystem();
        }

        UpdateUPnPNetworkMode(UPNP_INTERNET_MODE);
        HandleHardRestoreAndCtrlPoint(forceEnableCtrlPoint, GetLanDeviceName());

        // We will set the LED to normal, since the device is
        // fully operational at this point.
#if defined(PRODUCT_WeMo_LightV2)
        SetWiFiLED(RGB_SWITCH_OFF);
#elif defined(PRODUCT_WeMo_SNSV2)
        SetWiFiLED(0x04);
#endif
    } else {
        ip_address = wifiGetIP(INTERFACE_AP);
        APP_LOG("UPNP", LOG_DEBUG, "################### UPNP on local:%s ###################", ip_address);
        ret=ControlleeDeviceStart(INTERFACE_AP, port, desc_doc_name, web_dir_path);
        if(( ret != UPNP_E_SUCCESS ) && ( ret != UPNP_E_INIT ) ) {
            APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
            APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
            resetSystem();
        }
        UpdateUPnPNetworkMode(UPNP_LOCAL_MODE);
        EnableSiteSurvey(NULL);
    }
}

void initCoreThreads()
{
    //Check remote access and initialize NAT
    char *fwUpURLStr = NULL;
    FirmwareUpdateInfo fwUpdInf;
    fwUpURLStr = GetBelkinParameter("FirmwareUpURL");
    if(fwUpURLStr && strlen(fwUpURLStr)!=0) {
        memset(&fwUpdInf, 0x00, sizeof(fwUpdInf));
        strncpy(fwUpdInf.firmwareURL, fwUpURLStr, sizeof(fwUpdInf.firmwareURL)-1);
        StartFirmwareUpdate(fwUpdInf);
    }
}

#if !defined(PRODUCT_WeMo_Dimmer) && !defined(PRODUCT_WeMo_SNSV2) && !defined(PRODUCT_WeMo_Light)
void *systemRestore(void *args)
{
    int restoreValue = 0;
    int err = 0, fd = 0;

    tu_set_my_thread_name( __FUNCTION__ );

    if( (fd = open("/dev/gpio", O_RDWR)) < 0 ) {
        APP_LOG("WiFiApp", LOG_DEBUG, "Open /dev/gpio failed");
        return (void *)-1;
    }

    while( 1 ) {
        err = ioctl(fd, RALINK_GENERAL_READ, (void *)&restoreValue);
        if( err < 0 ) {
            APP_LOG("WiFiApp", LOG_DEBUG, "Ralink Read byte failed ");
            close(fd);
            return (void *)err;
        }

        usleep(500000);

        if( restoreValue == 1 ) {
            //correctUbootParams();
            resetNetworkParams();
            ClearRuleFromFlash();
            usleep(500000);
            system("nvram restore");

            pluginUsleep(2000000);

            char *homeId = GetBelkinParameter(DEFAULT_HOME_ID);
            char *pluginKey = GetBelkinParameter(DEFAULT_PLUGIN_PRIVATE_KEY);
            if ((homeId && pluginKey) && (0x00 == strlen(homeId) ) && (0x00 == strlen(pluginKey)))
                setRemoteRestoreParam(0x0);
            else
                setRemoteRestoreParam(0x1);
            APP_LOG("UPNP", LOG_DEBUG, "System rebooting........");
            system("reboot -f");
            break;
        }
    }

    pthread_exit(NULL);
}
#endif

void *ResetButtonTask(void *args);
void core_init_late(int forceEnableCtrlPoint)
{
    // setSignalHandlers();
#if !defined(PRODUCT_WeMo_SNSV2) && !defined(PRODUCT_WeMo_Light)
    pthread_t reset_thread;

    //Creating Network Reset thread, for monitoring 5 second reset button press
    createDetachedThread(&reset_thread,ResetButtonTask,NULL);
#endif

    g_szBuiltFirmwareVersion = FW_REV;
    g_szBuiltTime         = BUILD_TIME;

    {
        char buf[SIZE_256B];
        memset (buf,0,SIZE_256B);

        APP_LOG("UPNP",LOG_DEBUG, "Update NVRAM with FW Details..." );
        SetBelkinParameter(WEMO_VERSION_GEMTEK_PROD, FW_REV1);
        strncpy (buf,"\"",sizeof(buf)-1);
    }

    initDeviceUPnP();

    createPasswordKeyDataV3();

    if (DEVICE_SOCKET == g_eDeviceType) {
        initLED();
#if defined(LONG_PRESS_SUPPORTED)
        initLongPressLock();
#endif
#if defined(PRODUCT_WeMo_LightV2)
        // Disable disable power button for Ground Truth to prevent accidents
        // for the light switch 3-way, sync_state_task will take care of 
        // monitoring relay.
        if (get_nway() != 3) {
            pthread_create(&power_thread, NULL, PowerButtonTask, NULL);
            pthread_detach (power_thread);
        }
#endif
#if defined(PRODUCT_WeMo_SNSV2)
        pthread_create(&power_thread, NULL, PowerButtonTask, NULL);
        pthread_detach (power_thread);
#endif
#if REFACTORIT_LATER
        /* Disabled for DIMMER for now until HW is properly ready */
        pthread_create(&ButtonTaskMonitor_thread, NULL, ButtonTaskMonitorThread, NULL);
        pthread_detach (ButtonTaskMonitor_thread);
#endif
        pthread_create(&relay_thread, NULL, RelayControlTask, NULL);
        pthread_detach (relay_thread);
    }

    /* create restore thread before checking settings to speed up factory restore case */
#if !defined(PRODUCT_WeMo_Dimmer) && !defined(PRODUCT_WeMo_SNSV2) && !defined(PRODUCT_WeMo_Light)
    pthread_t systemRestore_thread;
    pthread_attr_init(&sysRestore_attr);
    pthread_attr_setdetachstate(&sysRestore_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&systemRestore_thread, &sysRestore_attr, systemRestore, NULL);
#endif

#ifdef PRODUCT_WeMo_Dimmer

    unsigned char u8Brightness = 100;
    bool curState = 0;
    /* load bulb type, if any */
    char *tmp = NULL;
    tmp = GetBelkinParameter(DIMMER_BULB_TYPE);
    if(tmp) {
        strncpy(gBulbType, tmp, sizeof(gBulbType)-1);
    }

    /*Load the hardware current brightness */
    if(SUCCESS == getWaspVariable(WASP_VAR_CURRENT_BRIGHTNESS, WASP_VARTYPE_UINT8, (void*)&u8Brightness)) {
        g_brightness = u8Brightness;
        APP_LOG("RemoteAccess",LOG_DEBUG, "g_brightness :%d", g_brightness);

        snprintf(g_fader, MAX_FADER_LENGTH, "300:-1:1:0:%u", g_brightness);
    }

    /* Load the current state */
    if(SUCCESS == getWaspVariable(WASP_VAR_ON_OFF, WASP_VARTYPE_BOOL, (void*)&curState)) {
        g_PowerStatus = curState;
        APP_LOG("RemoteAccess",LOG_DEBUG, "curState :%d", curState);
    }
    /* thread to poll the WASP to check the change in the value of variables */
    pthread_t waspPoll_thread;
    createDetachedThread(&waspPoll_thread, waspPollTask, NULL);
    /* thread to notify the WASP changes to the app if necessary */
    pthread_t waspNotify_thread;
    createDetachedThread(&waspNotify_thread, waspChangeNotify, NULL);

    loadNightModeConfiguration();
#ifndef __MIPSEL__   /* code for the simulation environment */
    /* initialize the WASP_VAR_CURRENT_BRIGHTNESS to 100*/
    unsigned char u8 = 100;
    setWaspVariable(WASP_VAR_CURRENT_BRIGHTNESS, WASP_VARTYPE_UINT8, (void*)&u8);

    /* initialize the WASP_VAR_FADE_REMAINING to 0 */
    unsigned short u16 = (unsigned short)(0);
    setWaspVariable(WASP_VAR_FADE_REMAINING, WASP_VARTYPE_UINT16, (void*)&u16);
#endif

#endif

#ifdef __WIRED_ETH__
    if(g_bWiredEthernet) {
        StartInetMonitorThread();
    } else
#endif
    {
        int retVal = FAILURE;

        retVal = initWiFiHandler ();
        APP_LOG("RemoteAccess",LOG_DEBUG, "initWiFiHandler retVal:%d\n", retVal);
        if(SUCCESS == retVal) {
            char* pIp = GetWanIPAddress ();
            APP_LOG("NetworkControl", LOG_DEBUG,"IP:%s",pIp);
            if( pIp && strlen(pIp) && (0 != strcmp(pIp, "0.0.0.0")) ) {
                APP_LOG("RemoteAccess",LOG_DEBUG, "Calling SetCurrentClientState with STATE_INTERNET_NOT_CONNECTED\n");
                SetCurrentClientState(STATE_INTERNET_NOT_CONNECTED);
                startUPnP(forceEnableCtrlPoint);
#ifdef PRODUCT_WeMo_Dimmer
                setAnimation(LED_STATE_CONNECTION_RESTABLISHED);
#endif
            }
        } else {
            selectStrongestSigApChannel();
            startUPnP(forceEnableCtrlPoint);

        }
    }
    APP_LOG("RemoteAccess",LOG_DEBUG, "***********initRule()***********\n");
    initRule();

#if defined(PRODUCT_WeMo_Dimmer)
    if(gNTPTimeSet) {
        /* If the HUSH_ANIMATION_END_TIME is set, the hush mode was active before the machine
            start. restore that */
        tmp = GetBelkinParameter(HUSH_ANIMATION_END_TIME);
        if(tmp && strlen(tmp) > 0) {
            /* selectedSuspendedOption is not needed in this case as the
               time will be based on already saved endTime */
            startHushMode(ACTIVE, 0);
        } else {
            APP_LOG("RemoteAccess",LOG_DEBUG, "Hush Mode is not active.");
        }
    }
#endif
#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    pthread_t ntc_thread;
    createDetachedThread(&ntc_thread,ntcTask,NULL);
#endif

    if (DEVICE_SENSOR== g_eDeviceType) {
        //        StartSensorTask();
        pthread_create(&SensorTaskMonitor_thread, NULL, SensorTaskMonitorThread, NULL);
        pthread_detach (SensorTaskMonitor_thread);
    }
    else {
        APP_LOG("UPNP", LOG_DEBUG, "################### No Device type found ###################");
    }

    initCoreThreads();
}

void core_init_early()
{
    char* fwUpgradeMemThr = NULL;
    //    setSignalHandlers();
#ifdef __WIRED_ETH__
    if(WiredEthernetUp(0) == -1) {
        g_bWiredEthernet = 1;
        APP_LOG("REMOTEACCESS",LOG_DEBUG,"Wired Ethernet mode");
    }
#endif

#ifdef PRODUCT_WeMo_Dimmer
    int ret = 0;

// Wait for up to 30 seconds for WASP to become ready.
 // The MCU firmware of 1 to 3 processors may need to be updated on 
// the first boot after a firmware update.
    do {
        int i;
        WaspVariable Var;

        for(i = 0; i < 300; i++) {
            // initialize WASP interface to HW
            if((ret = WASP_Init()) == WASP_OK) {
                APP_LOG("WiFiApp", LOG_DEBUG, "WASP_Init successful.");
                break;
            }
            pluginUsleep(100000);  // wait .1 seconds, then try again
        }

        if(ret != WASP_OK) {
            // wasp initialization failed. Exit wemoApp
            APP_LOG("WiFiApp", LOG_CRIT, "WASP_Init failed - %s. Aborting...",
                    WASP_strerror(ret));
            resetSystem();
        }

        for(; i < 300; i++) {
            memset(&Var,0,sizeof(Var));
            Var.ID = WASP_VAR_DEVID;
            Var.State = VAR_VALUE_CACHED;
            WASP_GetVariable(&Var);
            if((ret = WASP_GetVariable(&Var)) == WASP_OK) {
                APP_LOG("WiFiApp", LOG_DEBUG, "WASP is ready.");
                break;
            }
            pluginUsleep(100000);  // wait .1 seconds, then try again
        }

        if(ret != WASP_OK) {
            // wasp initialization failed. Exit wemoApp
            APP_LOG("WiFiApp",LOG_CRIT,"WASP_GetVariable failed - %s. Aborting...",
                    WASP_strerror(ret));
            resetSystem();
        }
    } while(FALSE);

    initAttrNotifyLockDimmer();

    initWASPLock();
#endif
    /* initialize the lock used during call to
       SetAwayRuleTask */
    initLongPressAwayLock();

    initFWUpdateStateLock();
    initHomeIdListLock();
    setCurrFWUpdateState(FM_STATUS_DEFAULT);    //setting to default
    webAppInit(0); //call it when no other thread exists in wemoApp

    SetAppSSID();
#if !defined(PRODUCT_WeMo_Dimmer) && !defined(PRODUCT_WeMo_SNSV2) && !defined(PRODUCT_WeMo_LightV2)
    if(!g_bWiredEthernet) {
        SetAppSSIDCommand();
    }
#endif

    //pluginOpenLog (PLUGIN_LOGS_FILE, CONSOLE_LOGS_SIZE);
    APP_LOG("WiFiApp",LOG_DEBUG, "\n #########Removing /tmp/*.starting files\n");
    system("rm -f /tmp/*.starting");
    fwUpgradeMemThr = GetBelkinParameter("FW_UPGRD_MemThr");
    /* if firmware upgrade parameter is not available set default value */
    if ((0x00 == fwUpgradeMemThr) || (0x00 == strlen(fwUpgradeMemThr)))
        SetBelkinParameter("FW_UPGRD_MemThr", FW_UPGRD_MEM_THR_VALUE);

    SetBelkinParameter(IMPORT_KEY_PARAM_NAME, SIGNED_PUBKEY_FILE_NAME);

    initWatchDog();

    //- reset possible
    SetBelkinParameter(SETTIME_SEC, "");

    /* check parameters */
    CheckAttributes();

#if defined(SIMULATED_OCCUPANCY)
    simulatedOccupancyInit();
#endif
    /* initIPC should be called before setNotificationStatus */
    initIPC();

    /*Create a file to keep the WeMoApp start time info */
    system("date >/tmp/WeMoAppUpTime");

#define NVRAM_FILE_NAME "/tmp/Belkin_settings/nvram_settings.sh"
// WEMO-46992: delete legacy file that could leak sensitive information
    unlink(NVRAM_FILE_NAME);
}

void CheckAttributes()
{
    memset(g_szHomeId, 0x00, sizeof(g_szHomeId));
    memset(g_szSmartDeviceId, 0x00, sizeof(g_szSmartDeviceId));
    memset(g_szSmartPrivateKey, 0x00, sizeof(g_szSmartPrivateKey));
    memset(g_szPluginPrivatekey, 0x00, sizeof(g_szPluginPrivatekey));
    memset(g_szPluginCloudId, 0x00, sizeof(g_szPluginCloudId));
    memset(g_szRestoreState, 0x0, sizeof(g_szRestoreState));
    memset(g_routerMac, 0x0, sizeof(g_routerMac));
    memset(g_routerSsid, 0x0, sizeof(g_routerSsid));
    memset(g_NotificationStatus, 0x0, sizeof(g_NotificationStatus));
    memset(g_turnServerEnvIPaddr, 0x00, sizeof(g_turnServerEnvIPaddr));
    memset(g_serverEnvIPaddr, 0x00, sizeof(g_serverEnvIPaddr));
#ifdef WeMo_INSTACONNECT
    memset(gBridgeNodeList, 0x00, sizeof(gBridgeNodeList));
#endif
    g_ServerEnvType = E_SERVERENV_PROD;

    char *homeId = GetBelkinParameter (DEFAULT_HOME_ID);
    if (0x00 != homeId && (0x00 != strlen(homeId))) {
        strncpy(g_szHomeId, homeId, sizeof(g_szHomeId)-1);
    }

    char *smartId = GetBelkinParameter (DEFAULT_SMART_DEVICE_ID);
    if (0x00 != smartId && (0x00 != strlen(smartId))) {
        strncpy(g_szSmartDeviceId, smartId, sizeof(g_szSmartDeviceId)-1);
    }

    char *smartKey = GetBelkinParameter (DEFAULT_SMART_PRIVATE_KEY);
    if (0x00 != smartKey && (0x00 != strlen(smartKey))) {
        strncpy(g_szSmartPrivateKey, smartKey, sizeof(g_szSmartPrivateKey)-1);
    }

    char *pluginKey =	GetBelkinParameter (DEFAULT_PLUGIN_PRIVATE_KEY);
    if (0x00 != pluginKey && (0x00 != strlen(pluginKey))) {
        strncpy(g_szPluginPrivatekey, pluginKey, sizeof(g_szPluginPrivatekey)-1);
    }

    char *pluginId = GetBelkinParameter (DEFAULT_PLUGIN_CLOUD_ID);
    if (0x00 != pluginId && (0x00 != strlen(pluginId))) {
        strncpy(g_szPluginCloudId, pluginId, sizeof(g_szPluginCloudId)-1);
    }

    char *pRouterMac = GetBelkinParameter (WIFI_ROUTER_MAC);
    if (0x00 != pRouterMac && (0x00 != strlen(pRouterMac))) {
        strncpy(g_routerMac, pRouterMac, sizeof(g_routerMac)-1);
    }

    char *pRouterSSID = GetBelkinParameter (WIFI_ROUTER_SSID);
    if (0x00 != pRouterSSID && (0x00 != strlen(pRouterSSID))) {
        strncpy(g_routerSsid, pRouterSSID, sizeof(g_routerSsid)-1);
    }

    char *lasttimezone = GetBelkinParameter(SYNCTIME_LASTTIMEZONE);
    if (0x00 != lasttimezone && (0x00 != strlen(lasttimezone))) {
        g_lastTimeZone = atof(lasttimezone);
        APP_LOG("UPNP", LOG_DEBUG,"setting g_lastTimeZone:%f from flash...success", g_lastTimeZone);
    } else {
        APP_LOG("UPNP", LOG_DEBUG,"Not setting g_lastTimeZone from flash... failure");
    }

    char *lastdstenable = GetBelkinParameter(LASTDSTENABLE);
    if (0x00 != lastdstenable && (0x00 != strlen(lastdstenable))) {
        gDstEnable = atoi(lastdstenable);
        APP_LOG("UPNP", LOG_DEBUG,"setting gDstEnable:%d from flash... success", gDstEnable);
    } else {
        APP_LOG("UPNP", LOG_DEBUG,"Not setting gDstEnable from flash... failure");
    }

    char *dstSupported = GetBelkinParameter(SYNCTIME_DSTSUPPORT);
    if (0x00 != dstSupported && (0x00 != strlen(dstSupported))) {
        gDstSupported = atoi(dstSupported);
        APP_LOG("UPNP", LOG_DEBUG,"setting gDstSupported:%d from flash... success", gDstSupported);
    } else {
        APP_LOG("UPNP", LOG_DEBUG,"Not setting gDstSupported from flash... failure");
        gDstSupported = 1;
    }
    //NOTIFICATION STATUS
    char *pNotificationStatus = GetBelkinParameter(NOTIFICATION_VALUE);
    if(pNotificationStatus) {
        if((0x0 == strlen(pNotificationStatus))) {
            APP_LOG("UPNP",LOG_DEBUG, "Setting Event Notification Status to 0......" );
            strncpy(g_NotificationStatus, "0", sizeof(g_NotificationStatus)-1);
        } else {
            strncpy(g_NotificationStatus, pNotificationStatus, sizeof(g_NotificationStatus)-1);
        }
    }

#ifdef WeMo_SMART_SETUP_V2
    char *customizedState = GetBelkinParameter(CUSTOMIZED_STATE);
    if((customizedState!= NULL) && (0x0 != strlen(customizedState)))
        g_customizedState = atoi(customizedState);
    else
        setCustomizedState(DEVICE_UNCUSTOMIZED);
    APP_LOG("UPNP",LOG_DEBUG, "Customized state saved is: %d", g_customizedState);
#endif
    char *hw_version = GetBelkinParameter("hwVersion");
    if(hw_version && strlen(hw_version) != 0) {
        ghwVersion = atoi(hw_version);
    }

#if defined(DEBUG_ENABLE)
    char *pUploadEnable = GetBelkinParameter(LOG_UPLOAD_ENABLE);
    if((NULL != pUploadEnable) && (((0x00 != strlen(pUploadEnable)) && (atoi(pUploadEnable) == 0)) || (0x00 == strlen(pUploadEnable)))) {
        APP_LOG("UPNP", LOG_DEBUG,"Setting log upload to cloud variable on flash");
        SetBelkinParameter(LOG_UPLOAD_ENABLE, "1");
    } else
        APP_LOG("UPNP", LOG_DEBUG,"Log upload to cloud variable already set on flash");
#endif
    return;
}

