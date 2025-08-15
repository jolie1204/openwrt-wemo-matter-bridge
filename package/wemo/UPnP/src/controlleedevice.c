/***************************************************************************
 *
 *
 * controlleedevice.c
 *
 * Created by Belkin International, Software Engineering on May 27, 2011
 * Copyright (c) 2012-2014 Belkin International, Inc. and/or its affiliates. All rights reserved.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>

#include <ithread.h>
#include <upnp.h>
#include <sys/time.h>
#include <math.h>
#include "global.h"
#include "wemodefs.h"
#include "fw_rev.h"
#include "logger.h"
#include "wifiHndlr.h"
#include "controlledevice.h"
#include "gpio.h"
#include "belkin_api.h"
#include "new_upgrade.h"
#include "itc.h"
#include "pktStructs.h"
#include "rule.h"
#include "plugin_ctrlpoint.h"
#include "utils.h"
#include "utlist.h"
#include "mxml.h"
#include "sigGen.h"
/* apple homekit setup key payload generation */
#include "base_encode.h"
#include "watchDog.h"
#include "upnpCommon.h"
#include "osUtils.h"
#include "httpsWrapper.h"
#ifdef PRODUCT_WeMo_Insight
#include "insight.h"
#include "InsightHandler.h"
#endif

#include "smartSetupUPnPHandler.h"

#ifdef SIMULATED_OCCUPANCY
#include "simulatedOccupancy.h"
#endif

#include "thready_utils.h"
#include "rulesdb_utils.h"

#include "libhkstore.h"
#include "secret.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

/* serial number length*/
#define MAX_SERIAL_LEN  14
#define SERIAL_TYPE_INDEX 6 //- The seventh digital indicating device type
#define SUB_DEVICE_TYPE_INDEX 8 //- The ninth digital indicating sub device type
//[WEMO-26944]
#define NUM_SECONDS_IN_HOUR 60*60

extern int webAppFileDownload(char *url, char *outfilename);
extern void StopDownloadRequest(void);
extern void nat_trav_destroy(void *);
extern void initBugsense(void);

pthread_mutex_t longPressAwayLock;
int g_isRemoteAccessByApp = 0x00;
int g_OldApp = 0x00;
pthread_mutex_t g_remoteAccess_mutex;
pthread_cond_t g_remoteAccess_cond;

pthread_t timesyncThread=-1;

pthread_attr_t timesync_attr;
ProxyRemoteAccessInfo *g_pxRemRegInf = NULL;

pthread_attr_t updateFw_attr;
pthread_t fwUpMonitorthread=-1;
pthread_attr_t firmwareUp_attr;
pthread_t firmwareUpThread=-1;
pthread_attr_t wdLog_attr;
extern pthread_t logFile_thread;
int currFWUpdateState=0;
unsigned long int gFwDownloadTimeStamp=0;
extern int gTimeZoneUpdated;

//Increase this time Download time to 20 minutes for now until new strategy/design is created.
#define MAX_FW_DL_TIME_OUT  20*60
char* ip_address 	= NULL;
char* desc_doc_name 	= NULL;
char* web_dir_path 	= NULL;

int g_isTimeSyncByMobileApp = 0x00;
int gWebIconVersion=0;
extern int ghwVersion;
extern int gLastAuthVal;

int   g_lastDstStatus = 0x00;

char  g_server_ip[SIZE_32B];
unsigned short g_server_port;
char gUserKey[PASSWORD_MAX_LEN];

extern char g_szApSSID[MAX_APSSID_LEN];
extern char g_routerMac[MAX_MAC_LEN];
extern char g_routerSsid[MAX_ESSID_LEN];
extern int gNTPTimeSet;
extern char g_szRestoreState[SIZE_2B];
extern int ctrlpt_handle;
extern int gBootReconnect;
extern int gStopDownloadFW;
extern int gSignalStrength;

unsigned int szDeviceID;
extern unsigned long int g_poweronStartTime;

static char* DEFAULT_SERIAL_NO = "0123456789";
static int   DEFAULT_SERIAL_TAILER_SIZE = 3;
static pthread_attr_t reset_attr;
static pthread_t reset_thread = -1;

int g_eDeviceType = DEVICE_UNKNOWN;	//- Device type indentifier
int g_eDeviceTypeTemp = DEVICE_UNKNOWN;	//- Device type indentifier
int g_ra0DownFlag = 0;
#if defined(PRODUCT_WeMo_Insight)
char g_SendInsightParams =0;
int g_isDSTApplied = 0;
unsigned int g_StateLog = 0;
#endif
int gSetupRequested = 0;
int gAppCalledCloseAp = 0;

char g_szWiFiMacAddress[SIZE_64B];
char g_szFriendlyName[SIZE_256B];

extern char g_szSerialNo[SIZE_64B];
extern char g_szProdVarType[SIZE_16B];

char g_szUDN[SIZE_UDN];
char g_szUDN_1[SIZE_UDN];
char g_szFirmwareVersion[SIZE_64B];
char g_szSkuNo[SIZE_64B];


char g_szFirmwareURLSignature[MAX_FW_URL_SIGN_LEN];

static  pthread_t ithPowerSensorMonitorTask = -1;
volatile static  int sPowerDuration 	= 0x00;
volatile static  int sPowerEndAction = -1;
extern int g_IsLastUserActionOn;


char* g_szBuiltFirmwareVersion = 0x00;
char* g_szBuiltTime = 0x00;
pthread_attr_t dst_main_attr;
pthread_t dstmainthread = -1;
int gDstEnable=0;
extern int gDstSupported;

int g_security_enforce = 1;

int g_iDstNowTimeStatus	= 0x00;
int gpluginStatusTS = 0;
pthread_mutex_t gFWUpdateStateLock;
pthread_mutex_t gSiteSurveyStateLock;

//char g_szBootArgs[SIZE_128B];

extern char g_serverEnvIPaddr[SIZE_32B];
extern char g_turnServerEnvIPaddr[SIZE_32B];
extern SERVERENV g_ServerEnvType;

extern int gSmartSetup;

char g_szActuation[SIZE_128B];
char g_szClientType[SIZE_128B];
char g_szRemote[SIZE_8B];

#define	    CONTROLLEE_DEVICE_STOP_WAIT_TIME	   5*1000000
volatile int gRestartRuleEngine=0;

RemoteAccessInfo *pgAppRegInfo = NULL;

#ifdef PRODUCT_WeMo_Dimmer
SNightModeConfiguration *gpsNightMode = NULL;
int gNightModeActive = 0;
char gBulbType[SIZE_16B]= {0};

#define BULB_TYPE_COUNT 3
/* Bulb Preset Values for MinLevel/MaxLevel/TurnOnLevel */
static const char
DimmerBulbPreset [BULB_TYPE_COUNT][4][SIZE_16B] = {
    /*BulbType*/     /*MinLevel*/  /*MaxLevel*/  /*TurnOnLevel*/
    {"CFL",            "28" ,         "240",         "28"},
    {"INCANDESCENT",   "6" ,          "255",         "6"},
    {"LED",            "15" ,          "240",         "15"}
};
#endif

//--------------- Global Definition ------------ //- WiFi setup callback list
PluginDeviceUpnpAction g_Wifi_Setup_Actions[] = {
    {"GetApList", GetApList, 0},
    {"GetNetworkList", GetNetworkList, 0},
    {"ConnectHomeNetwork", ConnectHomeNetwork, 0},
    {"GetNetworkStatus", GetNetworkStatus, 0},
    {"SetSensorEvent", SetBinaryState, 1},
    {"TimeSync", SyncTime, 0},
    {"CloseSetup", CloseSetup, 0},
    {"StopPair", StopPair, 0},
};

PluginDeviceUpnpAction g_time_sync_Actions[] = {
    {"TimeSync", SyncTime, 0},
    {"GetTime", 0x00, 0}
};

//- Basic event callback list
PluginDeviceUpnpAction g_basic_event_Actions[] = {
#ifdef PRODUCT_WeMo_Insight
    {"SetInsightHomeSettings", SetInsightHomeSettings, 1},
    {"GetInsightHomeSettings", GetInsightHomeSettings, 0},
    {"UpdateInsightHomeSettings", UpdateInsightHomeSettings, 1},
#endif
    {"SetBinaryState", SetBinaryState, 1},
    {"SetMultiState", 0x00, 1},
    {"GetBinaryState", GetBinaryState, 0},
    {"GetFriendlyName", GetFriendlyName, 0},
    {"ChangeFriendlyName", SetFriendlyName, 1},
    {"GetDeviceId", GetDeviceId, 0},
    {"GetMacAddr", GetMacAddr, 0},
    {"GetSerialNo", GetSerialNo, 0},
    {"GetPluginUDN", GetPluginUDN, 0},
    {"ShareHWInfo", GetShareHWInfo, 0},
    {"SetDeviceId", SetDeviceId, 1},
    {"GetIconURL", GetIcon, 0},
    {"ReSetup", ReSetup, 1},
    {"SetLogLevelOption", SetLogLevelOption, 1},
    {"GetLogFileURL", GetLogFilePath, 0},
    {"GetWatchdogFile", GetWatchdogFile, 0},
    {"GetSignalStrength", SignalStrengthGet, 0},
    {"SetServerEnvironment", SetServerEnvironment, 1},
    {"GetServerEnvironment", GetServerEnvironment, 0},
    {"GetIconVersion", GetIconVersion, 0},
    {"SetIconVersion", SetIconVersion, 0},
#if defined(PRODUCT_WeMo_Light) && !defined(PRODUCT_WeMo_Dimmer)
    {"SetNightLightStatus", SetNightLightStatus, 1},
    {"GetNightLightStatus", GetNightLightStatus, 0},
#endif
#ifdef SIMULATED_OCCUPANCY
    {"GetSimulatedRuleData", GetSimulatedRuleData, 1},
    {"NotifyManualToggle", NotifyManualToggle, 0},
#endif
#ifdef PRODUCT_WeMo_Dimmer
    {"ConfigureNightMode",  ConfigureNightMode, 1},
    {"GetNightModeConfiguration",  GetNightModeConfiguration, 1},
    {"Calibrate", Calibrate, 1},
    {"SetBulbType", SetBulbType, 1},
    {"ConfigureDimmingRange",  ConfigureDimmingRange, 1},
    {"SimulateOverTemp",  SimulateOverTemp, 1},
    {"TestLEDs", TestLEDs, 1},
    {"CheckResetButtonState", CheckResetButtonState, 1},
#endif
#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
    {"ConfigureHushMode", ConfigureHushMode, 1},
    {"identifyDevice", identifyDevice, 1},
    {"setDummyMode", setDummyMode, 1},
#endif

#ifdef LONG_PRESS_SUPPORTED
    {"SimulateLongPress",  SimulateLongPress, 1},
#endif
#ifdef SIMULATED_OCCUPANCY
    {"SetAwayRuleTask", SetAwayRuleTask, 1},
#endif
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    {"GetGPIO",  GetGPIO, 0},
    {"SetGPIO",  SetGPIO, 1},
    {"SimulateOverTemp",  SimulateOverTemp, 1},
#endif
    {"GetHKSetupInfo", GetHKSetupInfo, 0},
    {"SetSetupDoneStatus", SetSetupDoneStatus, 1},
    {"GetSetupDoneStatus", GetSetupDoneStatus, 0},
    {"setHKSetupState", setHKSetupState, 1},
    {"getHKSetupState", getHKSetupState, 0},
    {"resetHKConfig", resetHKConfig, 1},
    {"StartIperf", StartIperf, 1},
    {"StopIperf",  StopIperf, 1},
    {"setAutoFWUpdate", setAutoFWUpdate, 1},
    {"SetEnforceSecurity", SetEnforceSecurity, 1},
    {"GetEnforceSecurity", GetEnforceSecurity, 0},
    {"removeHomekitData", removeHomekitData, 1},
};

PluginDeviceUpnpAction g_Rules_Actions[] = {
    {"UpdateWeeklyCalendar", UpdateWeeklyCalendar, 1},
    {"EditWeeklycalendar", EditWeeklycalendar, 1},

    {"GetRulesDBPath", GetRulesDBPath, 0},
    {"SetRulesDBVersion", SetRulesDBVersion, 1},
    {"GetRulesDBVersion", GetRulesDBVersion, 0},
    {"FetchRules", FetchRules, 1},
    {"StoreRules", StoreRules, 1},
#if defined(PRODUCT_WeMo_Insight) || defined(PRODUCT_WeMo_SNS)
    {"SetRuleID", SetRuleID, 1},
    {"DeleteRuleID", DeleteRuleID, 1},
#endif
#ifdef SIMULATED_OCCUPANCY
    {"SimulatedRuleData", SimulatedRuleData, 1},
#endif
    {"RestartRuleEngine", RestartRuleEngine, 1},
};



//- Firmware update callback list

PluginDeviceUpnpAction g_firmware_event_Actions[] = {
    {"UpdateFirmware", UpdateFirmware, 1},
    {"GetFirmwareVersion", GetFirmwareVersion, 0},
};

PluginDeviceUpnpAction g_metaInfo_Actions[] = {
    {"GetMetaInfo", GetMetaInfo, 0},
    {"GetExtMetaInfo", GetExtMetaInfo, 0},
};

PluginDeviceUpnpAction g_deviceInfo_Actions[] = {
    {"GetDeviceInformation", GetDeviceInformation, 0},
    {"GetInformation", GetInformation, 0},
};

#ifdef PRODUCT_WeMo_Insight
PluginDeviceUpnpAction g_insight_Actions[] = {
    {"GetInsightParams",GetInsightParams, 0},
    {"GetPowerThreshold",GetPowerThreshold, 0},
    {"SetPowerThreshold",SetPowerThreshold, 1},
    {"SetAutoPowerThreshold",SetAutoPowerThreshold, 1},
    {"ResetPowerThreshold",ResetPowerThreshold, 1},
    {"ScheduleDataExport", ScheduleDataExport, 1},
    {"GetDataExportInfo", GetDataExportInfo, 0},
};
#endif

PluginDeviceUpnpAction g_smart_setup_Actions[] = {
    {"PairAndRegister",PairAndRegister, 0},
};

PluginDeviceUpnpAction g_manufacture_Actions[] = {
    {"GetManufactureData", GetManufactureData, 0},
};



char *CtrleeDeviceServiceType[] = {"urn:Belkin:service:WiFiSetup:1",
                                   "urn:Belkin:service:timesync:1",
                                   "urn:Belkin:service:basicevent:1",
                                   "urn:Belkin:service:firmwareupdate:1",
                                   "urn:Belkin:service:rules:1",
                                   "urn:Belkin:service:metainfo:1",
#ifdef PRODUCT_WeMo_Insight
                                   "urn:Belkin:service:insight:1",
#endif
                                   "urn:Belkin:service:bridge:1",
                                   "urn:Belkin:service:deviceinfo:1",
                                   "urn:Belkin:service:smartsetup:1",
                                   "urn:Belkin:service:manufacture:1"

                                  };

char* szServiceTypeInfo[] = {"PLUGIN_E_SETUP_SERVICE",
                             "PLUGIN_E_TIME_SYNC_SERVICE",
                             "PLUGIN_E_EVENT_SERVICE",
                             "PLUGIN_E_FIRMWARE_SERVICE",
                             "PLUGIN_E_RULES_SERVICE",
                             "PLUGIN_E_METAINFO_SERVICE",
#ifdef PRODUCT_WeMo_Insight
                             "PLUGIN_E_INSIGHT_SERVICE",
#endif
                             "PLUGIN_E_BRIDGE_SERVICE",
                             "PLUGIN_E_DEVICEINFO_SERVICE",
                             "PLUGIN_E_SMART_SETUP_SERVICE",
                             "PLUGIN_E_MANUFACTURE_SERVICE"

                            };

PluginDevice SocketDevice = {-1, PLUGIN_MAX_SERVICES};


char* s_szNtpServer = "192.43.244.18";	//in Default, use North America
#define DEFAULT_REGION_INDEX 5

tTimeZone g_tTimeZoneList[] = {
    {-12.0, 	1, "(GMT-12:00) Enewetak, Kwajalein"},
    {-11.0, 	2, "(GMT-11:00) Midway Island, Samoa"},
    {-10.0, 	3, "(GMT-10:00) Hawaii"},
    {-9.5, 		4, "GMT-09:30) Marquesas Islands"},
    {-9.0, 		5, "(GMT-09:00) Alaska"},
    {-8.0, 		6, "(GMT-08:00) Pacific Time (US & Canada); Tijuana"},
    {-7.0, 		8, "(GMT-07:00) Mountain Time (US & Canada)"},
    {-6.0, 		9, "(GMT-06:00) Central Time (US & Canada)"},
    {-5.0, 		13, "(GMT-05:00) Eastern Time (US & Canada)"},
    {-4.5, 		15, "(GMT-04:30) Venezuela, Caracas"},
    {-4.0, 		16, "(GMT-04:00) Atlantic Time (Canada)"},
    {-3.5, 		19, "(GMT-03:30) Newfoundland"},
    {-3.0, 		20, "(GMT-03:00) Brasilia"},
    {-2.5, 		21, "(GMT-02:30) St. John's, Canada"},
    {-2.0, 		22, "(GMT-02:00) Mid-Atlantic"},
    {-1.0, 		23, "(GMT-01:00) Azores"},
    {0.0, 		26, "(GMT) Greenwich Mean Time: Lisbon, London,Dublin, Edinburgh"},
    {1.0, 		31, "(GMT+01:00) Paris, Sarajevo, Skopje"},
    {2.0, 		35, "(GMT+02:00) Cairo"},
    {3.0, 		40, "(GMT+03:00) Moscow, St. Petersburg,Volgograd, Kazan"},
    {3.5, 		42, "(GMT+03:30) Iran"},
    {4.0, 		43, "(GMT+04:00) Abu Dhabi, Muscat, Tbilisi"},
    {4.5, 		44, "(GMT+04:30) Kabul, Afghanistan"},
    {5.0, 		46, "(GMT+05:00) Islamabad, Karachi"},
    {5.5, 		47, "(GMT+05:30) India, Sri Lanka"},
    {5.75, 		48, "(GMT+05:45) Nepal"},
    {6.0, 		49, "(GMT+06:00) Almaty, Dhaka"},
    {6.5, 		50, "((GMT+06:30) Cocos Islands, Myanmar"},
    {7.0, 		51, "(GMT+07:00) Bangkok, Jakarta, Hanoi"},
    {8.0, 		52, "(GMT+08:00) Beijing, Chongqing, Urumqi, Hong Kong, Perth, Singapore, Taipei"},
    {9.0, 		54, "(GMT+09:00) Toyko, Osaka, Sapporo"},
    {9.5, 		55, "(GMT+09:30) Northern Territory, South Australia"},
    {10.0, 		56, "(GMT+10:00) Brisbane"},
    {10.5, 		60, "(GMT+10:30) Lord Howe Island"},
    {11.0, 		62, "(GMT+11:00) Magada"},
    {11.5, 		63, "(GMT+11:30) Norfolk Island"},
    {12.0, 		64, "(GMT+12:00) Fiji, Kamchatka, Marshall Is."},
    {12.75, 	66, "(GMT+12:45) Chatham Islands"},
    {13.0, 		67, "(GMT+13:00) Tonga"},
    {14.0, 		68, "(GMT+14:00) Line Islands"}
};

char* gDevTypeStringArr[] = {
    "controllee", /* Default - unknown case */
    "controllee", /* DEVICE_SOCKET */
    "sensor", /* DEVICE_SENSOR */
    "wemo_baby", /* DEVICE_BABYMON */
    "stream", /* DEVICE_STREAMING - NOT USED */
    "bridge", /* DEVICE_BRIDGE - NOT USED */
    "insight", /* DEVICE_INSIGHT */
    "wemo_crockpot", /* DEVICE_CROCKPOT */
    "lightswitch", /* DEVICE_LIGHTSWITCH */
    "NetCamSensor", /* DEVICE_NETCAM */
    LINKSYSWNC_NAME, /* DEVICE_LINKSYS_WNC_CAM */
    "sbiron", /* DEVICE_SBIRON */
    "mrcoffee", /* DEVICE_MRCOFFEE */
    "petfeeder", /* DEVICE_PETFEEDER */
    "smart", /* DEVICE_SMART */
    "maker", /* DEVICE_MAKER */
    "EchoWater", /* DEVICE_ECHO */
    "dimmer",     /* DEVICE_DIMMER */
    "lightswitch", /* DEVICE_LIGHTSWITCH V2*/
    "lightswitch", /* DEVICE_LIGHTSWITCH 3 Way*/
};

char* gDevUDNStringArr[] = {
    "Socket", /* Default - unknown case */
    "Socket", /* DEVICE_SOCKET */
    "Sensor", /* DEVICE_SENSOR */
    "wemo_baby", /* DEVICE_BABYMON */
    "stream", /* DEVICE_STREAMING - NOT USED */
    "Bridge", /* DEVICE_BRIDGE - For WeMo-Bridge-LEDLight */
    "Insight", /* DEVICE_INSIGHT */
    "wemo_crockpot", /* DEVICE_CROCKPOT */
    "Lightswitch", /* DEVICE_LIGHTSWITCH */
    "NetCamSensor", /* DEVICE_NETCAM */
    LINKSYSWNC_NAME, /* DEVICE_LINKSYS_WNC_CAM */
    "Sbiron",       /* DEVICE_SBIRON */
    "Mrcoffee",     /* DEVICE_MRCOFFEE */
    "Petfeeder", /* DEVICE_PETFEEDER */
    "Smart",        /* DEVICE_SMART */
    "Maker", /* DEVICE_MAKER */
    "EchoWater", /* DEVICE_ECHO */
    "Dimmer",     /* DEVICE_Dimmer */
    "Lightswitch", /* DEVICE_LIGHTSWITCH V2*/
    "Lightswitch", /* DEVICE_LIGHTSWITCH 3 way*/
};

char* gDefFriendlyName[] = {
#ifdef PRODUCT_WeMo_SNSV2
    DEFAULT_SOCKETV2_FRIENDLY_NAME, /* Default - unknown case */
    DEFAULT_SOCKETV2_FRIENDLY_NAME, /* DEVICE_SOCKET */
#else
    DEFAULT_SOCKET_FRIENDLY_NAME, /* Default - unknown case */
    DEFAULT_SOCKET_FRIENDLY_NAME, /* DEVICE_SOCKET */
#endif
    DEFAULT_SENSOR_FRIENDLY_NAME, /* DEVICE_SENSOR */
    DEFAULT_BABY_FRIENDLY_NAME, /* DEVICE_BABYMON */
    DEFAULT_STREAMING_FRIENDLY_NAME, /* DEVICE_STREAMING - NOT USED */
    DEFAULT_BRIDGE_FRIENDLY_NAME, /* DEVICE_BRIDGE - NOT USED */
    DEFAULT_INSIGHT_FRIENDLY_NAME, /* DEVICE_INSIGHT */
    DEFAULT_CROCKPOT_FRIENDLY_NAME, /* DEVICE_CROCKPOT */
    DEFAULT_LIGHTSWITCH_FRIENDLY_NAME, /* DEVICE_LIGHTSWITCH */
    DEFAULT_NETCAM_FRIENDLY_NAME, /* DEVICE_NETCAM */
    DEFAULT_LINKSYSWNC_FRIENDLY_NAME, /* DEVICE_LINKSYSWNC */
    DEFAULT_SBIRON_FRIENDLY_NAME, /* DEVICE_SBIRON */
    DEFAULT_MRCOFFEE_FRIENDLY_NAME, /* DEVICE_MRCOFFEE */
    DEFAULT_PETFEEDER_FRIENDLY_NAME,/* DEVICE_PETFEEDER */
    DEFAULT_SMART_FRIENDLY_NAME,/* DEVICE_SMART */
    DEFAULT_MAKER_FRIENDLY_NAME,/* DEVICE_MAKER */
    DEFAULT_ECHO_FRIENDLY_NAME,/* DEVICE_ECHO*/
    DEFAULT_DIMMER_FRIENDLY_NAME,/* DEVICE_DIMMER*/
    DEFAULT_LIGHTSWITCHV2_FRIENDLY_NAME, /* DEVICE_LIGHTSWITCH V2 */
    DEFAULT_LIGHTSWITCH3WAY_FRIENDLY_NAME, /* DEVICE_LIGHTSWITCH 3 way */
};

const char *VarTypes[] = {
    "Invalid!",
    "ENUM",
    "PERCENT",
    "TEMP",
    "TIME32",
    "TIME16",
    "TIMEBCD",
    "BOOL",
    "BCD_DATE",
    "DATETIME",
    "STRING",
    "BLOB",
    "UINT8",
    "INT8",
    "UINT16",
    "INT16",
    "UINT32",
    "INT32",
    "TIME_M16"
};

const char *Usages[] = {
    "Invalid!",
    "FIXED",
    "MONITORED",
    "DESIRED",
    "CONTROLLED"
};

const char *ValStates[] = {
    "Not set",        // VAR_VALUE_UNKNOWN
    "Cached value",   // VAR_VALUE_CACHED:
    "Live value",     // VAR_VALUE_LIVE
    "Set Value",      // VAR_VALUE_SET
    "Value being Set" // VAR_VALUE_SETTING
};

char* gDeviceClientType[] = {
    ":a3b6-41e8-afb5-a3430cea2dcd", /* Default - unknown case */
    ":a3b6-41e8-afb5-a3430cea2dcd", /* DEVICE_SOCKET */
    ":1a08-463e-8cbb-e4ea74e427ed", /* DEVICE_SENSOR */
    ":", /* DEVICE_BABYMON */
    ":", /* DEVICE_STREAMING : NOT USED */
    ":52f8-406e-a5a6-5c5a2254868c", /* DEVICE_BRIDGE - NOT USED */
    ":5e7e-40a7-8bf4-539d1dc2ce42", /* DEVICE_INSIGHT */
    ":b136dbb7-5880-48f1-a347-084347ad22d9", /* DEVICE_CROCKPOT */
    ":ccbb-4346-a240-70bcf8f53632", /* DEVICE_LIGHTSWITCH */
    ":4f76-477f-a7ca-fd56a8f85390", /* DEVICE_NETCAM */
    ":", /* DEVICE_LINKSYS_WNC_CAM */
    ":", /* DEVICE_SBIRON */
    ":66bf-445d-8404-994bc95055bf", /* DEVICE_MRCOFFEE */
    ":", /* DEVICE_PETFEEDER */
    ":", /* DEVICE_SMART */
    "3310c367-c39b-4966-8b0e-07cbd3cbdde7", /* DEVICE_MAKER */
    ":e19a9d16-1136-4519-a1ce-0732e2b2d848", /* DEVICE_ECHO*/
    ":ccbb-4346-a240-70bcf8f53632", /* DEVICE_DIMMER */
    ":ccbb-4346-a240-70bcf8f53632", /* DEVICE_LIGHTSWITCHV2 */
    ":ccbb-4346-a240-70bcf8f53632", /* DEVICE_LIGHTSWITCH3WAY */
};


productNameTbl g_Modelcode_Productname[]= {
    {"AirPurifier","AirPurifier"},
    {"wemo_baby","BabyMonitor"},
    {"Bridge","Bridge"},
    {"Classic A60 RGBW","FlexBulb"},
    {"Classic A60 TW","TemperatureBulb"},
    {"CoffeeMaker","CoffeeMaker"},
    {"Connected A-19 60W Equivalent","Lighting"},
    {"crockpot","crockpot"},
    {"Flex RGBW","FlexBulb"},
    {"Gardenspot RGB","ColorBulb"},
    {"HeaterA","HeaterA"},
    {"HeaterB","HeaterB"},
    {"Humidifier"," Humidifier"},
    {"Insight","Insight"},
    {"iQBR30","Lighting"},
    {"LCT001","Lighting"},
    {"LGDWL","Lighting"},
    {"LIGHTIFY A19 Tunable White","TemperatureBulb"},
    {"LIGHTIFY Flex RGBW","FlexBulb"},
    {"LIGHTIFY Gardenspot RGB","ColorBulb"},
    {"Lightswitch","Lightswitch"},
    {"LWB004","Lighting"},
    {"Maker","Maker"},
    {"MZ100","Lighting"},
    {"NetCam","NetCam"},
    {"NetCamHDv1","NetCamHDv1"},
    {"NetCamHDv2","NetCamHDv2"},
    {"Sensor","Sensor"},
    {"smart","smart"},
    {"Socket","Socket"},
    {"Surface Light TW","Lighting"},
    {"Water","Water"},
    {"ZLL Light","Lighting"},
    {"F7C038","DWSensor"},
    {"F7C039","Fob"},
    {"F7C040","AlarmSensor"},
    {"F7C041","PIR"},
    {"Dimmer","Dimmer"},
    {"Lightswitch", "LightswitchV2"},
    {"Lightswitch", "Lightswitch3Way"},
};

#define TOTAL_PRODUCT_NAME  sizeof(g_Modelcode_Productname) / sizeof(g_Modelcode_Productname[0])
#define DEFAULT_CASE_SENSOR  "Sensor"
#define DEFAULT_CASE_LIGHT  "lighting"

//--------------- Local Definition -------------

UpnpDevice_Handle device_handle = -1;
/*
	 The amount of time (in seconds) before advertisements
	 will expire
 */
int default_advr_expire = 86400;//24 * 60 *60;

//- In default, in local network
int g_IsUPnPOnInternet = FALSE;
//- In dedault, not in setup mode
int g_IsDeviceInSetupMode = FALSE;

extern char g_szHomeId[SIZE_20B];
extern char g_szSmartDeviceId[SIZE_256B];
extern char g_szSmartPrivateKey[MAX_PKEY_LEN];
extern char g_szPluginPrivatekey[MAX_PKEY_LEN];
extern char g_szPluginCloudId[SIZE_16B];
static pthread_mutex_t s_upnp_param_mutex;

static char gPrevIP[SIZE_20B]= {'\0',};
static char gPrevPort[SIZE_8B]= {'\0',};

//- store socket override status
#define	MAX_OVERRIDEN_STATUS_LEN	512
char szOverridenStatus[MAX_OVERRIDEN_STATUS_LEN];

static char *Wemo46751(char *szTimeZone);

char *GetWemoMacAddress(void)
{
    return g_szWiFiMacAddress;
}

char *GetWemoFriendlyName(void)
{
    return g_szFriendlyName;
}

char *GetWemoFirmwareVersion(void)
{
    return g_szFirmwareVersion;
}

char *GetWemoDeviceUDN(void)
{
    return g_szUDN_1;
}

const char *getProductName(char *modelCode)
{
    int index;
    if( (NULL == modelCode) || (0 == strlen(modelCode)))
        /**Handle NULL Case and Empty string Case of model Code specical case for Home Sensor*/
    {
        APP_LOG("UPNP: DEVICE", LOG_DEBUG,"###########modelCode Empty or NULL##############");
        goto EXIT;
    }
    for( index = 0; index < TOTAL_PRODUCT_NAME; index++ ) {
        if( !strcmp(modelCode,g_Modelcode_Productname[index].modelCode) ) {
            SetBelkinParameter("productName", (char *) g_Modelcode_Productname[index].productName);
            return g_Modelcode_Productname[index].productName;
        }
    }
EXIT:
    /**This is added for default case as for wiki page,  it
    *  may be extend further as soon as wiki page edited.
    */
    return DEFAULT_CASE_SENSOR;
}

void getModelCode(char *modelCode)
{
    strncpy(modelCode,getDeviceUDNString(),SIZE_32B-1);
}
void initUPnPThreadParam()
{
    ithread_mutexattr_t attr;
    ithread_mutexattr_init(&attr);
    ithread_mutexattr_setkind_np( &attr, ITHREAD_MUTEX_RECURSIVE_NP );
    pthread_mutex_init(&s_upnp_param_mutex, &attr);
    ithread_mutexattr_destroy(&attr);
}

void initFWUpdateStateLock()
{
    osUtilsCreateLock(&gFWUpdateStateLock);
}

void initSiteSurveyStateLock()
{
    osUtilsCreateLock(&gSiteSurveyStateLock);
}

void initLongPressAwayLock()
{
    osUtilsCreateLock(&longPressAwayLock);
}

int	 IsTimeUpdateByMobileApp()
{
    int ret = 0x00;
    pthread_mutex_lock(&s_upnp_param_mutex);
    ret = g_isTimeSyncByMobileApp;
    pthread_mutex_unlock(&s_upnp_param_mutex);

    return ret;
}

void UpdateMobileTimeSync(int newState)
{
    pthread_mutex_lock(&s_upnp_param_mutex);
    g_isTimeSyncByMobileApp = newState;
    pthread_mutex_unlock(&s_upnp_param_mutex);
}

void UPnPTimeSyncStatusNotify()
{
    char* szParamNames[] = {"TimeSyncRequest"};
    char* szParamValues[] = {"0"};
    int event_type = PLUGIN_E_EVENT_SERVICE;

    UpnpNotify(device_handle, SocketDevice.service_table[event_type].UDN,
               SocketDevice.service_table[event_type].ServiceId, (const char **)szParamNames, (const char **)szParamValues, 0x01);

    APP_LOG("UPNP: DEVICE", LOG_DEBUG, "###############################Notification: time sync request sent");

    //- add the time zone and dst push notification to sensor, short term solution
    //- Below for NetCam, but should in SNS to support the timezone push notification
    char* tzIndex = GetBelkinParameter("timezone_index");
    if ((0x00 != tzIndex) && (0x00 != strlen(tzIndex))) {
        char* szTimeZone[] = {"TimeZoneNotification"};
        char* szTimeZoneValue[1] = {0x00};
        szTimeZoneValue[0] = (char*)ZALLOC(SIZE_4B);
        snprintf(szTimeZoneValue[0], SIZE_4B, "%s", tzIndex);
        UpnpNotify(device_handle, SocketDevice.service_table[event_type].UDN,
                   SocketDevice.service_table[event_type].ServiceId, (const char **)szTimeZone, (const char **)szTimeZoneValue, 0x01);

        free(szTimeZoneValue[0]);
    }

}

void UPnPSetHomeDeviceIdNotify()
{
    char* szParamValues[] = {g_szHomeId, g_szSmartDeviceId};
    char* szParamNames[] = {"HomeIdRequest", "DeviceIdRequest"};

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)szParamNames, (const char **)szParamValues, 0x02);

    APP_LOG("UPNP: DEVICE", LOG_HIDE, "###############################Notification: HomeId - szParamValues[0]: <%s> and DeviceId - szParamValues[1]: <%s> sent", szParamValues[0], szParamValues[1]);

}

char* getDeviceTypeString(void)
{
    APP_LOG("UPNP: DEVICE", LOG_ALERT, "Device type: %d and temp type: %d", g_eDeviceType, g_eDeviceTypeTemp);
    if(g_eDeviceTypeTemp) {
        APP_LOG("UPNP: DEVICE", LOG_ALERT, "Device type string: %s", gDevTypeStringArr[g_eDeviceTypeTemp]);
        return gDevTypeStringArr[g_eDeviceTypeTemp];
    } else {
        APP_LOG("UPNP: DEVICE", LOG_ALERT, "Device type string: %s", gDevTypeStringArr[g_eDeviceType]);
        return gDevTypeStringArr[g_eDeviceType];
    }

}

char* getDeviceUDNString(void)
{
    APP_LOG("UPNP: DEVICE", LOG_ALERT, "Device type: %d and temp type: %d", g_eDeviceType, g_eDeviceTypeTemp);
    if(g_eDeviceTypeTemp) {
        APP_LOG("UPNP: DEVICE", LOG_ALERT, "Device UDN: %s", gDevUDNStringArr[g_eDeviceTypeTemp]);
        return gDevUDNStringArr[g_eDeviceTypeTemp];
    } else {
        APP_LOG("UPNP: DEVICE", LOG_ALERT, "Device UDN: %s", gDevUDNStringArr[g_eDeviceType]);
        return gDevUDNStringArr[g_eDeviceType];
    }

}

int getClientType(void)
{
    int index=0;

    if(g_eDeviceTypeTemp) {
        index = g_eDeviceTypeTemp;
    } else {
        index = g_eDeviceType;
    }

    memset(g_szClientType, 0, sizeof(g_szClientType));

    strncpy(g_szClientType, g_szFirmwareVersion,sizeof(g_szClientType) - 1);
    strncat(g_szClientType, gDeviceClientType[index], sizeof(g_szClientType) - strlen(g_szClientType) - 1);

    APP_LOG("UPNP: DEVICE", LOG_DEBUG, "Idx: %d, Client Type: %s, len: %d", index, g_szClientType, strlen(g_szClientType));

    return SUCCESS;
}

void updateXmlHwVersion(char *szBuff)
{

    char *nvram_hwversion = NULL;

    nvram_hwversion = GetBelkinParameter("hwVersion");
    if (strlen(nvram_hwversion) == 0) {
        ghwVersion = 1;
    }
    else {
        ghwVersion = atoi(nvram_hwversion);
    }
    sprintf(szBuff, "<hwVersion>v%d</hwVersion>\n", ghwVersion);
    APP_LOG("UPNP: DEVICE", LOG_DEBUG, "Device Hw version: %s", szBuff);
}

void updateXmlDeviceTag(char *szBuff)
{

    char *pDevString;

    pDevString = getDeviceTypeString();

    sprintf(szBuff, "<deviceType>urn:Belkin:device:%s:1</deviceType>\n", pDevString);
    APP_LOG("UPNP: DEVICE", LOG_DEBUG, "Device type tag: %s", szBuff);
}


void updateXmlUDNTag(char *szBuff, char *szBuff1)
{

    char *pDevString;
    char *nvram_udn = NULL;

    nvram_udn = GetBelkinParameter("DeviceUDN");
    if ((nvram_udn == NULL) || (strlen(nvram_udn) == 0)) {
        pDevString = getDeviceUDNString();

        sprintf(szBuff1, "uuid:%s-1_0-%s", pDevString, g_szSerialNo);
        sprintf(szBuff, "<UDN>%s</UDN>\n", szBuff1);
        SetBelkinParameter("DeviceUDN", szBuff1);
    }
    else {
        sprintf(szBuff1, "%s", nvram_udn);
        sprintf(szBuff, "<UDN>%s</UDN>\n", nvram_udn);
    }
    APP_LOG("UPNP: DEVICE", LOG_DEBUG, "Device UDN tag: %s", szBuff);
}


int UpdateXML2Factory()
{
    FILE* pfReadStream  = 0x00;
    FILE* pfWriteStream = 0x00;
    char szBuff[SIZE_256B];
    char szBuff1[SIZE_256B];

    //- Open file to write
    //gautam: update the Insight and LS Makefile to copy Insightsetup.xml and Lightsetup.xml in /sbin/web/ as setup.xml
    pfReadStream 	= fopen("/sbin/web/setup.xml", "r");
    pfWriteStream = fopen("/tmp/Belkin_settings/setup.xml", "w");

    if (0x00 == pfReadStream || 0x00 == pfWriteStream) {
        APP_LOG("UPNP: DEVICE", LOG_ERR, "UpdateXML2Factory: open files handles failure");

        if(pfReadStream)
            fclose(pfReadStream);

        if(pfWriteStream)
            fclose(pfWriteStream);

        return PLUGIN_UNSUCCESS;
    }

    while (!feof(pfReadStream)) {
        memset(szBuff, 0x00, sizeof(szBuff));

        fgets(szBuff, SIZE_256B, pfReadStream);

            if (strstr(szBuff, "<deviceType>")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                updateXmlDeviceTag(szBuff);
            } else if (strstr(szBuff, "<hwVersion>")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                updateXmlHwVersion(szBuff);
            } else if (strstr(szBuff, "<UDN>")) {
                //- reset it again
                memset(szBuff, 0x00, sizeof(szBuff));
                memset(szBuff1, 0x00, sizeof(szBuff1));
                updateXmlUDNTag(szBuff, szBuff1);

                memset(g_szUDN, 0x00, sizeof(g_szUDN));
                strncpy(g_szUDN, szBuff1, sizeof(g_szUDN)-1);
                strncpy(g_szUDN_1, szBuff1, sizeof(g_szUDN_1)-1);

                APP_LOG("UPNP: DEVICE", LOG_DEBUG, "Device UDN: %s", g_szUDN_1);
            } else if (strstr(szBuff, "<serialNumber>")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                snprintf(szBuff, SIZE_256B, "<serialNumber>%s</serialNumber>\n", g_szSerialNo);
            } else if (strstr(szBuff, "modelName")) {
#ifdef PRODUCT_WeMo_SNS
                memset(szBuff, 0x00, sizeof(szBuff));
                snprintf(szBuff, SIZE_256B, "<modelName>%s</modelName>\n", getDeviceUDNString());
#endif
            } else if (strstr(szBuff, "friendlyName")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                snprintf(szBuff, SIZE_256B, "<friendlyName>%s</friendlyName>\n", g_szFriendlyName);
            } else if (strstr(szBuff, "firmwareVersion")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                snprintf(szBuff, SIZE_256B, "<firmwareVersion>%s</firmwareVersion>\n", g_szFirmwareVersion);
            } else if (strstr(szBuff, "macAddress")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                snprintf(szBuff, SIZE_256B, "<macAddress>%s</macAddress>\n", g_szWiFiMacAddress);
            } else if (strstr(szBuff, "iconVersion")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                int port = 0;

                port = UpnpGetServerPort();
                snprintf(szBuff, SIZE_256B, "<iconVersion>%d|%d</iconVersion>\n", gWebIconVersion, port);
            } else if (strstr(szBuff, "binaryState")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                int state = 0;

                state = GetCurBinaryState();
                snprintf(szBuff, SIZE_256B, "<binaryState>%d</binaryState>\n", state);
#if defined(PRODUCT_WeMo_Dimmer)
            } else if (strstr(szBuff, "brightness")) {
                memset(szBuff, 0x00, sizeof(szBuff));
                int brightness = 0;

                brightness = getBrightness();
                snprintf(szBuff, SIZE_256B, "<brightness>%d</brightness>\n", brightness);
#endif
            } else if (strstr(szBuff, "hkSetupCode")) {
                char *setupCode = NULL;
                memset(szBuff, 0x00, sizeof(szBuff));

                setupCode = HomekitstoreGet("SETUP_CODE");

                if ((setupCode == NULL) || (strlen(setupCode) == 0)) {
                    snprintf(szBuff, SIZE_256B, "<hkSetupCode>NOT AVAILABLE YET</hkSetupCode>\n");
                }
                else {
                    snprintf(szBuff, SIZE_256B, "<hkSetupCode>%s</hkSetupCode>\n", setupCode);
                }
            }

        fwrite(szBuff, 1, strlen(szBuff), pfWriteStream);

    }

    fclose(pfReadStream);
    fclose(pfWriteStream);

    APP_LOG("UPNP", LOG_DEBUG, "Replace set up XML successfully\n");
    APP_LOG("UPNP", LOG_DEBUG, "After create xml");
    return 0x00;

}

void GetMacAddress()
{
    char* szMac = utilsRemDelimitStr(GetMACAddress(),":");
    memset(g_szWiFiMacAddress, 0x00, sizeof(g_szWiFiMacAddress));

    strncpy(g_szWiFiMacAddress, szMac, sizeof(g_szWiFiMacAddress)-1);
    free(szMac);
    APP_LOG("STARTUP", LOG_DEBUG, "MAC:%s", g_szWiFiMacAddress);
}


void GetFirmware()
{
    //char* szPreviousVserion = GetBelkinParameter("FirmwareVersion");

    char* szPreviousVserion = g_szBuiltFirmwareVersion;

    memset(g_szFirmwareVersion, 0x00, sizeof(g_szFirmwareVersion));
    if (0x00 == szPreviousVserion || 0x00 == strlen(szPreviousVserion)) {
        snprintf(g_szFirmwareVersion, sizeof(g_szFirmwareVersion), "%s", DEFAULT_FIRMWARE_VERSION);
    } else {
        snprintf(g_szFirmwareVersion, sizeof(g_szFirmwareVersion), "%s", szPreviousVserion);
    }

    APP_LOG("Bootup", LOG_DEBUG, "Firmware:%s, built time: %s", g_szFirmwareVersion, g_szBuiltTime?g_szBuiltTime:"Unknown");

}
void GetSkuNo()
{
    char* szPreviousSkuNo   = GetBelkinParameter("SkuNo");
    memset(g_szSkuNo, 0x00, sizeof(g_szSkuNo));
    if (0x00 == szPreviousSkuNo || 0x00 == strlen(szPreviousSkuNo)) {
        snprintf(g_szSkuNo, sizeof(g_szSkuNo), "%s", DEFAULT_SKU_NO);
    } else {
        snprintf(g_szSkuNo, sizeof(g_szSkuNo), "%s", szPreviousSkuNo);
    }


    APP_LOG("STARTUP", LOG_DEBUG, "SKU:%s", g_szSkuNo);

}

char* getDefaultFriendlyName()
{
    if(g_eDeviceTypeTemp) {
        APP_LOG("UPNP: DEVICE", LOG_DEBUG, "Name : %s", gDefFriendlyName[g_eDeviceTypeTemp]);
        return gDefFriendlyName[g_eDeviceTypeTemp];
    } else {
        APP_LOG("UPNP: DEVICE", LOG_DEBUG, "Name: %s", gDefFriendlyName[g_eDeviceType]);
        return gDefFriendlyName[g_eDeviceType];
    }


}

void GetDeviceFriendlyName()
{
    char *pszFriendlyName = NULL;

    memset(g_szFriendlyName, 0x00, sizeof(g_szFriendlyName));
    char *szFriendlyName = GetDeviceConfig("FriendlyName");

    if ((0x00 != szFriendlyName) && (0x00 != strlen(szFriendlyName))) {
        strncpy(g_szFriendlyName, szFriendlyName, sizeof(g_szFriendlyName)-1);
    } else {
        pszFriendlyName = getDefaultFriendlyName();

        if(0 == strcmp(pszFriendlyName, DEFAULT_BABY_FRIENDLY_NAME)) {
            char *pProdVar = GetBelkinParameter (PRODUCT_VARIANT_NAME);

            if(pProdVar!= NULL && (strlen(pProdVar) > 0))
                snprintf(g_szFriendlyName, sizeof(g_szFriendlyName), "%sBaby", pProdVar);
            else
                strncpy(g_szFriendlyName, pszFriendlyName, sizeof(g_szFriendlyName)-1);
        } else
            strncpy(g_szFriendlyName, pszFriendlyName, sizeof(g_szFriendlyName)-1);
    }

    APP_LOG("Startup", LOG_DEBUG, "Friendly Name: %s", g_szFriendlyName);
}

/**
 * GetDeviceType: This function identifies the device type
 *                based on the device serial number
 *
 * Following is the description of the serial no schema
 *  Supplier ID | Yr of Mfg | Wk of Mfg | Product | Unique Seq Id
 *            22|12|38|K01|FFFFF
 * K = Relay based products
 *      K01- Switch 1.0US; K11-Switch 1.0 WW; K12-Insight; K13 - Light Switch; K14 - Crockpot
 * L = Sensors
 *      L01 - Motion Sensor 1.0 US; L11 - Motion Sensor 1.0 WW;
 * B = Bridges
 * M = Monitors
 * V = Video
 * S = Smart
 */

void GetDeviceType()
{
    char DevSerial[SIZE_64B]= {'\0'};

    APP_LOG("UPNP", LOG_ALERT, "serial no: %s", g_szSerialNo);
    strncpy(DevSerial, g_szSerialNo, sizeof(DevSerial)-1);

    if (0x00 == strlen(g_szSerialNo)) {
        APP_LOG("UPNP", LOG_ERR, "Device type unknow, please see the manufacture reference");
        g_eDeviceType = DEVICE_SOCKET;
        g_eDeviceTypeTemp = DEVICE_UNKNOWN;
    } else {
        switch(DevSerial[SERIAL_TYPE_INDEX]) {
        case 'K': {
            //- Switch
            if ('1' == DevSerial[SERIAL_TYPE_INDEX+2]) {
                APP_LOG("UPNP", LOG_DEBUG, "DEVICE: SOCKET");
                g_eDeviceType = DEVICE_SOCKET;
                g_eDeviceTypeTemp = DEVICE_UNKNOWN;
            } else if ('2' == DevSerial[SERIAL_TYPE_INDEX+2]) {
                APP_LOG("UPNP", LOG_DEBUG, "DEVICE: INSIGHT");
                g_eDeviceType = DEVICE_SOCKET;
                g_eDeviceTypeTemp = DEVICE_INSIGHT;
            } else if ('3' == DevSerial[SERIAL_TYPE_INDEX+2]) {
                APP_LOG("UPNP", LOG_DEBUG, "DEVICE: LIGHTSWITCH");
                g_eDeviceType = DEVICE_SOCKET;
                g_eDeviceTypeTemp = DEVICE_LIGHTSWITCH;
            } else if ('5' == DevSerial[SERIAL_TYPE_INDEX+2]) {
                APP_LOG("UPNP", LOG_DEBUG, "DEVICE: DIMMER LIGHTSWITCH");
                g_eDeviceType = DEVICE_SOCKET;
                g_eDeviceTypeTemp = DEVICE_DIMMER;
            }

        }
        break;

        case 'L': {
            APP_LOG("UPNP", LOG_DEBUG, "DEVICE: SENSOR");
            g_eDeviceType = DEVICE_SENSOR;
            g_eDeviceTypeTemp = DEVICE_UNKNOWN;
        }
        break;

        case 'M': {
            APP_LOG("UPNP", LOG_DEBUG, "DEVICE: BABYMON");
            g_eDeviceType = DEVICE_SOCKET;
            g_eDeviceTypeTemp = DEVICE_BABYMON;
        }
        break;

        case 'B': {
            APP_LOG("UPNP", LOG_DEBUG, "DEVICE: BRIDGE");
            g_eDeviceType = DEVICE_SOCKET;
            g_eDeviceTypeTemp = DEVICE_BRIDGE;
        }
        break;



        case 'V':
            /* Note that Netcam devices are built by partners and do not
             * have factory generated serial numbers.  The serial number
             * passed here is generated at run time by GetSerialNumber()
             * in WeMo_NetCam/belkin_api/belkin_api.c  */
            switch( DevSerial[SUB_DEVICE_TYPE_INDEX] ) {
            case '1':
            case '2':
            default:
                /* NetCam sensor, still as a sensor */
                APP_LOG("UPNP", LOG_DEBUG, "DEVICE: NetCam SENSOR");
                g_eDeviceType = DEVICE_SENSOR;
                g_eDeviceTypeTemp = DEVICE_NETCAM;
                break;
            case '3':
                // Linksys camera (aka Linksys WNC)
                APP_LOG("UPNP", LOG_DEBUG, "DEVICE: Linksys SENSOR");
                g_eDeviceType = DEVICE_SENSOR;
                g_eDeviceTypeTemp = DEVICE_LINKSYS_WNC_CAM;
                break;
            }
            // printf( "DEVTYPE: %s@%s:%d g_eDeviceType:%d, g_eDeviceTypeTemp:%d\n",
            //         __FILE__, __FUNCTION__, __LINE__,
            //         g_eDeviceType, g_eDeviceTypeTemp );
            break;

        default: {
            /* Default device type is Socket/Switch */
            APP_LOG("UPNP", LOG_DEBUG, "DEVICE: Default SOCKET");
            g_eDeviceType = DEVICE_SOCKET;
            g_eDeviceTypeTemp = DEVICE_UNKNOWN;
            APP_LOG("UPNP", LOG_ALERT, "Serial No: %s, DEVICE type: %d and temp type: %d", DevSerial, g_eDeviceType, g_eDeviceTypeTemp);
        }
        break;
        }
    }
}

void serverEnvIPaddrInit(void)
{
    memset(g_serverEnvIPaddr, 0x0, sizeof(g_serverEnvIPaddr));
    memset(g_turnServerEnvIPaddr, 0x0, sizeof(g_turnServerEnvIPaddr));
    g_ServerEnvType = E_SERVERENV_PROD;
}

int compareIconSize()
{
    struct stat sb1,sb2;
    char *defaultIconpath = NULL;
    if ((DEVICE_SOCKET == g_eDeviceType))
        defaultIconpath = "/etc/icon.jpg";
    else
        defaultIconpath = "/etc/sensor.jpg";
    char *customizedIconpath = "/tmp/Belkin_settings/icon.jpg";
    int defaultIconSize = 0, customizedIconSize = 0;
    if((stat(defaultIconpath,&sb1)) == 0);
    defaultIconSize = sb1.st_size;
    if((stat(customizedIconpath,&sb2)) == 0);
    customizedIconSize = sb2.st_size;
    /*Default icon should be greater in size than customized icon as App compresses the customized
                                               icon before applying it. */
    if(defaultIconSize >= customizedIconSize)
        return 1;
    else
        return 0;
}

void initDeviceUPnP()
{
    char *iconVer;
    int retVal = 0;  /* retVal will be set to 1 if size of /etc/icon.jpg(or /etc/sensor.jpg) is greater than
				    /tmp/Belkin_settings/icon.jpg */

    //- Get device type
    /** Move serial request to top since it is the king element*/
    initSerialRequest();
    GetDeviceType();

    GetMacAddress();
    GetSkuNo();
    GetFirmware();
    GetDeviceFriendlyName();

    /* load icon version */
    iconVer = GetBelkinParameter(ICON_VERSION_KEY);
    if(iconVer && strlen(iconVer)) {
        gWebIconVersion = atoi(iconVer);
    }
    APP_LOG("UPNP", LOG_DEBUG, "saved icon version: %d", gWebIconVersion);

    retVal = compareIconSize(); //Comparing default and customized icons
    //Copy all xml file from etc to MTD
    system("rm -rf /tmp/Belkin_settings/*.xml");

    system("cp -f /sbin/web/* /tmp/Belkin_settings");

#if defined(PRODUCT_WeMo_LightV2)
    system("cp /tmp/usb_device.xml /tmp/Belkin_settings/usb_device.xml");
#endif

    //gautam: update the Insight and LS Makefile to copy Insightsetup.xml and Lightsetup.xml in /sbin/web/ as setup.xml

    //Change some tags of setup.xml binding to device related

    //-Check existence of icon file
    FILE* pFileIcon = fopen("/tmp/Belkin_settings/icon.jpg", "r");
    if (0x00 == pFileIcon || (retVal && (!gWebIconVersion))) {
        //-Icon not existing, copy the factory one
        // Also checking iconsize and icon version to update the new 3x icons(WEMO-46393).
        APP_LOG("UPNP", LOG_DEBUG, "icon not found, using the default one");
        if ((DEVICE_SOCKET == g_eDeviceType) || (DEVICE_CROCKPOT == g_eDeviceType)) {
            //gautam: update the Insight and LS Makefile to copy Insight.png and Light.png in /etc/ as icon.jpg
            system("cp /etc/icon.jpg /tmp/Belkin_settings");
        } else {
            system("cp /etc/sensor.jpg /tmp/Belkin_settings/icon.jpg");
        }
    } else {
        fclose(pFileIcon);
        pFileIcon = 0x00;
    }

    UpdateXML2Factory();

    initUPnPThreadParam();
    serverEnvIPaddrInit();

    getClientType();
    /* not the ideal place but to make integration transparent for all Applications */
    //initBugsense();
}


int ControlleeDeviceCallbackEventHandler( Upnp_EventType EventType,
        void *Event,
        void *Cookie)
{

    switch (EventType) {

    case UPNP_EVENT_SUBSCRIPTION_REQUEST:
        PluginDeviceHandleSubscriptionRequest((UpnpSubscriptionRequest *)Event);
        break;

    case UPNP_CONTROL_GET_VAR_REQUEST:
        break;

    case UPNP_CONTROL_ACTION_REQUEST: {
        UpnpActionRequest* pEvent = (UpnpActionRequest *)Event;
        CtrleeDeviceHandleActionRequest(pEvent);
    }
    break;

    case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
    case UPNP_DISCOVERY_SEARCH_RESULT:
    case UPNP_DISCOVERY_SEARCH_TIMEOUT:
    case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE:
    case UPNP_CONTROL_ACTION_COMPLETE:
    case UPNP_CONTROL_GET_VAR_COMPLETE:
    case UPNP_EVENT_RECEIVED:
    case UPNP_EVENT_RENEWAL_COMPLETE:
    case UPNP_EVENT_SUBSCRIBE_COMPLETE:
    case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
        break;

    default:
        APP_LOG("UPNP",LOG_ERR, "Error in ControlleeDeviceCallbackEventHandler: unknown event type %d\n",
                EventType );
    }

    return (0);
}

int CtrleeDeviceHandleActionRequest(UpnpActionRequest *pActionRequest)
{
    IXML_Document *request = NULL;
    IXML_Document *result = NULL;
    int rect = UPNP_E_SUCCESS;
    int loop = 0x00;
    char *errorString = NULL;
    int secure = 1;

    if (0x00 == pActionRequest
        || !UpnpActionRequest_get_DevUDN_cstr(pActionRequest)
        || 0x00 == UpnpString_get_Length(UpnpActionRequest_get_DevUDN(pActionRequest))) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        return 0x01;
    }

    for (loop = 0x00; loop < PLUGIN_MAX_SERVICES; loop++) {
        //- to locate service containing this command
        if (0x00 == strcmp(UpnpActionRequest_get_DevUDN_cstr(pActionRequest), SocketDevice.service_table[loop].UDN) &&
            0x00 == strcmp(UpnpActionRequest_get_ServiceID_cstr(pActionRequest), SocketDevice.service_table[loop].ServiceId)) {
            break;
        }
    }

    request = UpnpActionRequest_get_ActionRequest(pActionRequest);

	UpnpActionRequest_set_ErrCode(pActionRequest, 0);
	UpnpActionRequest_set_ActionResult(pActionRequest, NULL);

    if (PLUGIN_MAX_SERVICES == loop) {
        APP_LOG("UPNP",LOG_ERR, "Action service not found: %s",
                UpnpActionRequest_get_ServiceID_cstr(pActionRequest));
        rect = UPNP_E_INVALID_SERVICE;
        return rect;
    }

    //- Service found, and to locate the action name and callback function
    {
        int cntActionNo = SocketDevice.service_table[loop].cntTableSize;
        int index = 0x00;

        if (0x00 == cntActionNo) {
            APP_LOG("UPNP",LOG_ERR, "No device action found in actions table");
            return rect;
        }


        for (index = 0x00; index < cntActionNo; index++) {
            PluginDeviceUpnpAction* pTable = SocketDevice.service_table[loop].ActionTable;
            if (0x00 != pTable) {
                if (0x00 == strcmp(UpnpActionRequest_get_ActionName_cstr(pActionRequest),
                                   (pTable + index)->actionName)) {
                    secure = (pTable + index)->secure;
                    APP_LOG("UPNP",LOG_DEBUG, "Action found: %s",
                            UpnpActionRequest_get_ActionName_cstr(pActionRequest));
                    break;
                }
            } else {
                APP_LOG("UPNP",LOG_ERR, "Action table not set");
                return rect;
            }
        }

        if (cntActionNo == index) {
            APP_LOG("UPNP",LOG_ERR, "Action not found: %s", UpnpActionRequest_get_ActionName_cstr(pActionRequest));
            return 0x01;
        }

        {
            PluginDeviceUpnpAction* pAction = SocketDevice.service_table[loop].ActionTable + index;
            if (0x00 == pAction) {
                APP_LOG("UPNP",LOG_ERR, "Action entry empty");
                return 0x01;
            }

            if (pAction->pUpnpAction) {
                char ip_addr[NI_MAXHOST] = {0};
                if (getnameinfo((struct sockaddr *)UpnpActionRequest_get_CtrlPtIPAddr(pActionRequest), sizeof(struct sockaddr_storage), ip_addr, sizeof(ip_addr), NULL, 0, NI_NUMERICHOST)) {
                    return 1;
                }

                if (strcmp(GetWanIPAddress(), ip_addr)) {
                    char *enforced_string = NULL;
                    enforced_string = GetBelkinParameter("Enforce_Security");
                    if(enforced_string && strlen(enforced_string) != 0) {
                        g_security_enforce = atoi(enforced_string);
                    }
                    else {
                        /* disable for default, until time comes */
                        g_security_enforce = 0;
                    }

                    if (g_security_enforce) {
                        if (secure) {
                            char *auth = Util_GetFirstDocumentItem(request, "Authenticator");
                            char *seq = Util_GetFirstDocumentItem(request, "SEQ");
                            if (auth) {
                                APP_LOG("UPNPDevice", LOG_DEBUG, "authenticator received: %s", auth);
                            }
                            else {
                                APP_LOG("UPNPDevice", LOG_DEBUG, "security enforced but no Authenticator received: %s", auth);
                                return 1;
                            }
                            if (seq) {
                                APP_LOG("UPNPDevice", LOG_DEBUG, "seq received: %s", seq);
                            }
                            else {
                                APP_LOG("UPNPDevice", LOG_DEBUG, "security enforced but no SEQ received: %s", auth);
                                return 1;
                            }
                            if (!validate_secret(g_szWiFiMacAddress,
                                                 g_szSerialNo,
                                                 (char *)UpnpActionRequest_get_ActionName_cstr(pActionRequest),
                                                 atoi(seq),
                                                 auth)) {
                                APP_LOG("UPNPDevice", LOG_DEBUG, "security enforced but Authenticator invalid: %s", auth);
                                return 1;
                            }
                        }
                    }
                }
                pAction->pUpnpAction(pActionRequest, request, &result, (const char **)&errorString);
                UpnpActionRequest_set_ActionResult(pActionRequest, result);
            }
            else
                APP_LOG("UPNP", LOG_WARNING, "Action name found: %s, but callback entry not set", UpnpString_get_String(UpnpActionRequest_get_ActionName(pActionRequest)));
        }


    }

    //- Get service type

    return rect;

}

int ControlleeDeviceStop()
{

    int ret=-1;
    if (-1 == device_handle)
        return 0x00;

    ret=UpnpUnRegisterRootDevice(device_handle);
    if((UPNP_E_SUCCESS != ret) && (UPNP_E_FINISH != ret)) {
        APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset, ret:%d ###################", ret);
        resetSystem();
    }
    device_handle = -1;
    UpnpFinish();

    memset(g_server_ip, 0x00, sizeof(g_server_ip));
    g_server_port = 0x00;
    APP_LOG("UPNP", LOG_DEBUG, "UPNP is to stop for setup");

    return 0x00;

}

//static struct VirtualDirCallbacks VirtualCallBack;
//---------------------------- POST FILE -------------------
int PostFile(UpnpWebFileHandle fileHnd, char *buf, int buflen, const void *cookie, const void *request_cookie)
{
    APP_LOG("UPNP: DEVICE", LOG_DEBUG, "to write data: %d bytes\n", buflen);
    return 0x00;
}

int OpenWebFile(char *filename, enum UpnpOpenFileMode Mode, const void *cookie, const void *request_cookie)
{
    APP_LOG("UPNP DEVICE", LOG_DEBUG, "File is to open\n");
    return 0x00;
}

int GetFileInfo(const char *filename, UpnpFileInfo *info, const void *cookie, const void **request_cookie)
{
    APP_LOG("UPNP DEVICE", LOG_DEBUG, "Get File inform called\n");
    return 0x00;
}

#include "upnpdebug.h"

int ControlleeDeviceStart(char *if_name,
                          unsigned short port,
                          char *desc_doc_name,
                          char *web_dir_path)
{
    int ret = UPNP_E_SUCCESS;
    char desc_doc_url[MAX_FW_URL_LEN];

    //shutdown the previous instance of UPNP, if any
    ControlleeDeviceStop();
    if(strcmp(if_name, INTERFACE_AP) == 0)
        port = 49152;
    else
        port = 49153;

    APP_LOG("UPNP",LOG_DEBUG, "Initializing UPnP Sdk on interface = %s port = %u", if_name, port);

    ret = UpnpInit2(if_name, port, g_szUDN_1);
    if( ( ret != UPNP_E_SUCCESS ) && ( ret != UPNP_E_INIT ) ) {
        APP_LOG("UPNP",LOG_CRIT, "Error with UpnpInit2 -- %d\n", ret );
        UpnpFinish();
        return ret;
    }

    ip_address = UpnpGetServerIpAddress();

    strncpy(g_server_ip, ip_address, sizeof(g_server_ip)-1);

    port = g_server_port = UpnpGetServerPort();

    APP_LOG("UPNP",LOG_CRIT, "UPnP Initialized ipaddress= %s port = %u",
            ip_address, port );

    if( desc_doc_name == NULL ) {
        //gautam: update the Insight and LS Makefile to copy Insightsetup.xml and Lightsetup.xml in /sbin/web/ as setup.xml
        desc_doc_name = "setup.xml";

    }

    if( web_dir_path == NULL ) {
        web_dir_path = DEFAULT_WEB_DIR;
    }

    snprintf( desc_doc_url, MAX_FW_URL_LEN, "http://%s:%d/%s", ip_address, port, desc_doc_name );

    UpdateXML2Factory();
    AsyncSaveData();

    APP_LOG("UPNP",LOG_DEBUG, "Specifying the webserver root directory -- %s\n",
            web_dir_path );
    if( ( ret =
              UpnpSetWebServerRootDir( web_dir_path ) ) != UPNP_E_SUCCESS ) {
        APP_LOG("UPNP",LOG_ERR, "Error specifying webserver root directory -- %s: %d\n",
                web_dir_path, ret );
        UpnpFinish();
        return ret;
    }

    APP_LOG("UPNP",LOG_DEBUG,
            "Registering the RootDevice\n"
            "\t with desc_doc_url: %s\n",
            desc_doc_url );


    UpnpEnableWebserver(TRUE);

    UpnpVirtualDir_set_OpenCallback((VDCallback_Open)OpenWebFile);
    UpnpVirtualDir_set_GetInfoCallback((VDCallback_GetInfo)GetFileInfo);
    UpnpVirtualDir_set_WriteCallback((VDCallback_Write)PostFile);
    UpnpVirtualDir_set_ReadCallback(NULL);
    UpnpVirtualDir_set_SeekCallback(NULL);
    UpnpVirtualDir_set_CloseCallback(NULL);

    ret = UpnpAddVirtualDir("./", NULL, NULL);
    if (UPNP_E_SUCCESS != ret) {
        APP_LOG("UPNP", LOG_ERR, "Add virtual directory failure");
    } else {
        APP_LOG("UPNP", LOG_DEBUG, "Add virtual directory success");
    }


    if( ( ret = UpnpRegisterRootDevice( desc_doc_url,
                                        (Upnp_FunPtr) ControlleeDeviceCallbackEventHandler,
                                        NULL, &device_handle ) )
        != UPNP_E_SUCCESS ) {
        APP_LOG("UPNP",LOG_ERR, "Error registering the rootdevice : %d\n", ret );
        UpnpFinish();
        return ret;
    } else {
        APP_LOG("UPNP",LOG_DEBUG, "RootDevice Registered with device_handle:%d\nInitializing State Table\n", device_handle);
        ControlleeDeviceStateTableInit(desc_doc_url);
        APP_LOG("UPNP",LOG_DEBUG, "State Table Initialized\n");

        /*
         * When restart UpnpInit, send the SIGUSR1 signal to wemo_ctrl.
         * When wemo_ctrl received SIGUSR1, execute UpnpSearchAsync()
         */
        //APP_LOG("UPNP",LOG_DEBUG, "Sending SIGUSR1 to wemo_ctrl....");
        system("touch /tmp/upnp.init");
        //        system("killall -SIGUSR1 wemo_ctrl");

        if( ( ret =
                  UpnpSendAdvertisement( device_handle, default_advr_expire ) )
            != UPNP_E_SUCCESS ) {
            APP_LOG("UPNP",LOG_ERR, "Error sending advertisements : %d\n", ret );
            UpnpFinish();
            return ret;
        }

        APP_LOG("UPNP",LOG_DEBUG, "Advertisements Sent\n");
    }

    return UPNP_E_SUCCESS;
}

int ControlleeDeviceStateTableInit(char *DescDocURL)
{
    IXML_Document *DescDoc = NULL;
    int ret = UPNP_E_SUCCESS;
    char *servid = NULL;
    char *evnturl = NULL;
    char *ctrlurl = NULL;
    char *udn = NULL;

    /*Download description document */
    if (UpnpDownloadXmlDoc(DescDocURL, &DescDoc) != UPNP_E_SUCCESS) {
        APP_LOG("UPNP",LOG_DEBUG, "Controllee device table initialization -- Error Parsing %s\n",
                DescDocURL);
        ret = UPNP_E_INVALID_DESC;
        ixmlDocument_free(DescDoc);

        return ret;
    } else {
        APP_LOG("UPNP",LOG_DEBUG, "Down load %s success", DescDocURL);
    }

    udn = Util_GetFirstDocumentItem(DescDoc, "UDN");
    memset(g_szUDN, 0x00, sizeof(g_szUDN));

    if (udn) {
        APP_LOG("UPNP",LOG_DEBUG, "UDN: %s\n", udn);
        strncpy(g_szUDN, udn, sizeof(g_szUDN)-1);
    } else {
        APP_LOG("UPNP",LOG_ERR, "UDN: reading failure");
    }

    //-Add setup service here
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_SETUP_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_SETUP_SERVICE]);
    }

    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);
    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;


    //- Add Sync time service
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_TIME_SYNC_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_TIME_SYNC_SERVICE]);

    }

    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);
    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;

    //- Add basic event service
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_EVENT_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE]);
    }
    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);

    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;

    //- Add firmware update service
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_FIRMWARE_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_FIRMWARE_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;


        return ret;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_FIRMWARE_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_FIRMWARE_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_FIRMWARE_SERVICE]);
    }
    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);

    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;
    //- Add rule service
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;


        return ret;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_RULES_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_RULES_SERVICE]);
    }

    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);

    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;

    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);


    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;
    //- Add Meta service here
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_METAINFO_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_METAINFO_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;


        return ret;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_METAINFO_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_METAINFO_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_METAINFO_SERVICE]);
    }

    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);
    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;

#ifdef PRODUCT_WeMo_Insight
    //- Add Insight service here
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_INSIGHT_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_INSIGHT_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;


        return ret;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_INSIGHT_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_INSIGHT_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_INSIGHT_SERVICE]);
    }

    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);
    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;
#endif

    //- Add Device Information service here
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;


        return ret;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_DEVICEINFO_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_DEVICEINFO_SERVICE]);
    }

    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);
    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;

    //- Add smart setup service here
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_SMART_SETUP_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_SMART_SETUP_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;


        return ret;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_SMART_SETUP_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_SMART_SETUP_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_SMART_SETUP_SERVICE]);
    }
    FreeXmlSource(servid);
    FreeXmlSource(evnturl);
    FreeXmlSource(ctrlurl);
    servid = NULL;
    evnturl = NULL;
    ctrlurl = NULL;

//- Add Manufacture Information service here
    if (!Util_FindAndParseService(DescDoc, DescDocURL, CtrleeDeviceServiceType[PLUGIN_E_MANUFACTURE_SERVICE], &servid, &evnturl, &ctrlurl)) {
        APP_LOG("UPNP",LOG_ERR, "%s -- Error: Could not find Service: %s\n", __FILE__, CtrleeDeviceServiceType[PLUGIN_E_MANUFACTURE_SERVICE]);

        ret = UPNP_E_INVALID_DESC;
        goto FreeServiceResource;
    } else {
        CtrleeDeviceSetServiceTable(PLUGIN_E_MANUFACTURE_SERVICE, udn, servid,
                                    CtrleeDeviceServiceType[PLUGIN_E_MANUFACTURE_SERVICE],
                                    &SocketDevice.service_table[PLUGIN_E_MANUFACTURE_SERVICE]);
    }

FreeServiceResource: {
        ixmlDocument_free(DescDoc);
        FreeXmlSource(servid);
        FreeXmlSource(evnturl);
        FreeXmlSource(ctrlurl);
        FreeXmlSource(udn);

        servid = NULL;
        evnturl = NULL;
        ctrlurl = NULL;
    }


    return ret;
}

int CtrleeDeviceSetServiceTable(int serviceType,
                                const char* UDN,
                                const char* serviceId,
                                const char* szServiceType,
                                pPluginService pService)
{
    strncpy(pService->UDN, UDN, sizeof(pService->UDN)-1);
    strncpy(pService->ServiceId, serviceId, sizeof(pService->ServiceId)-1);
    strncpy(pService->ServiceType, szServiceType, sizeof(pService->ServiceType)-1);

    CtrleeDeviceSetActionTable(serviceType, &SocketDevice.service_table[serviceType]);

    return 0x00;
}

#define PARAMS_BINARY_STATE {"BinaryState"}
#if defined (PRODUCT_WeMo_Insight)
#  define BASICEVENT_PARAM PARAMS_BINARY_STATE
#  define VALUES_NUM_ITEMS (1)
#  define VALUE_1_SIZE (100)
#elif defined (PRODUCT_WeMo_Dimmer)
#  define BASICEVENT_PARAM {"BinaryState", "Brightness", "Fader"}
#  define VALUES_NUM_ITEMS (3)
#  define VALUE_1_SIZE (4)
#  define VALUE_2_SIZE (8)
#  define VALUE_3_SIZE (32)
#else
#  define BASICEVENT_PARAM PARAMS_BINARY_STATE
#  define VALUE_1_SIZE (4)
#  define VALUES_NUM_ITEMS (1)
#endif

#define FWUPDATE_PARAM {"FirmwareUpdateStatus"}
#define RULE_PARAM {"RulesDBVersion"}
#define NETWORKSTATUS_PARAM {"NetworkStatus"}

int PluginDeviceHandleSubscriptionRequest(UpnpSubscriptionRequest *sr_event)
{
    if (!sr_event) {
        APP_LOG("UPNPDevice", LOG_ERR,"Service subscription: parameter error, request stop");
        return 0x01;
    }

    /* These 2 variables are defined differently depending on the
     * product:
     * - Paramters(sic) gets different initialization data.
     * - values[] has varying length (1 or 2) and different sized item(s)
     */

    char *basicevent_parameters[] = BASICEVENT_PARAM;
    char *fwupdate_parameters[] = FWUPDATE_PARAM;
    char *rule_parameters[] = RULE_PARAM;
    char *networkstatus_parameters[] = NETWORKSTATUS_PARAM;
    char **parameters = NULL;
    char *values[VALUES_NUM_ITEMS];
    char param_count = 0;

    values[0] = CALLOC( 1, VALUE_1_SIZE );
#if defined (PRODUCT_WeMo_Dimmer)
    values[1] = CALLOC( 1, VALUE_2_SIZE );
    values[2] = CALLOC( 1, VALUE_3_SIZE );
#endif

    if (!strcmp(UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event),
                "urn:Belkin:serviceId:basicevent1")) {
        int curState = 0;

        if (DEVICE_SOCKET == g_eDeviceType) {
            //- Power state
            LockLED();
            curState = GetCurBinaryState();
            UnlockLED();
        }
        else {
            //-Sensor state
            curState = GetSensorState();
        }

#ifdef PRODUCT_WeMo_Insight
        snprintf(values[0], VALUE_1_SIZE, "%d|%u|%u|%u|%u|%u|%u|%u|%u|%0.f",
                 curState, g_StateChangeTS, g_ONFor, g_TodayONTimeTS,
                 g_TotalONTime14Days, g_HrsConnected, g_AvgPowerON,
                 g_PowerNow, g_AccumulatedWattMinute, g_KWH14Days);
#elif defined (PRODUCT_WeMo_Dimmer)
        snprintf(values[0], VALUE_1_SIZE, "%d", curState);
        snprintf(values[1], VALUE_2_SIZE, "%d", getBrightness());
        getFader(values[2]);
#else
        snprintf(values[0], VALUE_1_SIZE, "%d", curState);
#endif
        parameters = basicevent_parameters;
        param_count = VALUES_NUM_ITEMS;
    }
    else if (!strcmp(UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event),
                     "urn:Belkin:serviceId:rules1")) {
        char *version = NULL;
        version = GetDeviceConfig(RULE_DB_VERSION_KEY);
        snprintf(values[0], VALUE_1_SIZE, "%s", strlen(version) ? version : "0");
        parameters = rule_parameters;
        param_count = 1;
    }
    else if (!strcmp(UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event),
               "urn:Belkin:serviceId:firmwareupdate1")) {
        snprintf(values[0], VALUE_1_SIZE, "%d", getCurrFWUpdateState());
        parameters = fwupdate_parameters;
        param_count = 1;
    }
    else if (!strcmp(UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event),
               "urn:Belkin:serviceId:WiFiSetup1")) {
        snprintf(values[0], VALUE_1_SIZE, "%d", getCurrentClientState());
        parameters = networkstatus_parameters;
        param_count = 1;
    }
    else {
        parameters = NULL;
        param_count = 0;
    }

    UpnpAcceptSubscription(device_handle,
                           UpnpSubscriptionRequest_get_UDN_cstr(sr_event),
                           UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event),
                           (const char **)parameters,
                           (const char **)values,
                           param_count,
                           UpnpSubscriptionRequest_get_SID_cstr(sr_event));

    if(values[0]) {
        free(values[0]);
    }
#if defined (PRODUCT_WeMo_Dimmer)
    if(values[1]) {
        free(values[1]);
    }
    if (values[2]) {
        free(values[2]);
    }
#endif
    APP_LOG("UPNPDevice", LOG_DEBUG,"Service subscription: %s: success",
            UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event));

    if (strstr(UpnpSubscriptionRequest_get_ServiceId_cstr(sr_event), "basicevent")) {
        {
            if(g_OldApp) {
                UPnPTimeSyncStatusNotify();
                g_OldApp=0;
            }
        }
#ifdef PRODUCT_WeMo_Insight
        APP_LOG("UPNPDevice", LOG_DEBUG,"INSIGHT HOME SETTINGS NOTIFY...");
        //send notification on get request
        SendHomeSettingChangeMsg();
#endif
    }

    return (0x00);
}

int CtrleeDeviceSetActionTable(PLUGIN_SERVICE_TYPE serviceType, pPluginService pOut)
{
    switch (serviceType) {
    case PLUGIN_E_SETUP_SERVICE:
        pOut->ActionTable  = g_Wifi_Setup_Actions;
        pOut->cntTableSize = sizeof(g_Wifi_Setup_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_SETUP_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;
    case PLUGIN_E_TIME_SYNC_SERVICE:
        pOut->ActionTable  = g_time_sync_Actions;
        pOut->cntTableSize = sizeof(g_time_sync_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_TIME_SYNC_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;
    case PLUGIN_E_EVENT_SERVICE:
        pOut->ActionTable  = g_basic_event_Actions;
        pOut->cntTableSize = sizeof(g_basic_event_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_EVENT_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;
    case PLUGIN_E_FIRMWARE_SERVICE:
        pOut->ActionTable  = g_firmware_event_Actions;
        pOut->cntTableSize = sizeof(g_firmware_event_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_FIRMWARE_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;


    case PLUGIN_E_RULES_SERVICE:
        pOut->ActionTable  = g_Rules_Actions;
        pOut->cntTableSize = sizeof(g_Rules_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_RULES_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);

        break;

    case PLUGIN_E_METAINFO_SERVICE:
        pOut->ActionTable  = g_metaInfo_Actions;
        pOut->cntTableSize = sizeof(g_metaInfo_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_METAINFO_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;

    case PLUGIN_E_DEVICEINFO_SERVICE:
        pOut->ActionTable  = g_deviceInfo_Actions;
        pOut->cntTableSize = sizeof(g_deviceInfo_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_DEVICEINFO_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;

    case PLUGIN_E_MANUFACTURE_SERVICE:
        pOut->ActionTable 	= g_manufacture_Actions;
        pOut->cntTableSize 	= sizeof(g_manufacture_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_MANUFACTURE_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;

#ifdef PRODUCT_WeMo_Insight
    case PLUGIN_E_INSIGHT_SERVICE:
        pOut->ActionTable  = g_insight_Actions;
        pOut->cntTableSize = sizeof(g_insight_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_INSIGHT_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;
#endif

    case PLUGIN_E_SMART_SETUP_SERVICE:
        pOut->ActionTable  = g_smart_setup_Actions;
        pOut->cntTableSize = sizeof(g_smart_setup_Actions)/sizeof(PluginDeviceUpnpAction);
        APP_LOG("UPNP", LOG_DEBUG, "PLUGIN_E_SMART_SETUP_SERVICE: service: %s Action Table set: %d", pOut->ServiceType, pOut->cntTableSize);
        break;
    default:
        APP_LOG("UPNP", LOG_ERR, "WRONG service ID");
        break;
    }

    return UPNP_E_SUCCESS;
}

/**
 *
 *
 *
 *
 *
 *
 *
 * ************************************************************************/
#define 	AP_LIST_BUFF_SIZE	3*SIZE_1024B

#if 0
void* siteSurveyPeriodic(void *args)
{
    int count=0;

    if (pAvlAPList)
        free(pAvlAPList);

    /* buffer allocated is as per the WIFI driver. Refer: cmm_info.c*/
    /* Memory allocated for 64 entries */
    pAvlAPList = (PMY_SITE_SURVEY) MALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);

    if(!pAvlAPList) {
        APP_LOG("UPNPDevice",LOG_ERR,"Malloc Failed...");
        resetSystem();
    }

    while(1) {
        if(g_ra0DownFlag == 1)
            break;

        osUtilsGetLock(&gSiteSurveyStateLock);
        memset(pAvlAPList, 0x0, sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
        getCurrentAPList (pAvlAPList, &count);
        g_cntApListNumber = count;
        osUtilsReleaseLock(&gSiteSurveyStateLock);

        pluginUsleep(10000000); //10 secs
    }

    if (pAvlAPList)
        free(pAvlAPList);
    pAvlAPList = 0x00;

    APP_LOG("UPNPDevice",LOG_DEBUG,"******* SiteSurvey Thread exiting ************");
    pthread_exit(0);
}
#endif

int GetApList(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szAplistBuffer[AP_LIST_BUFF_SIZE];
    char szApEntry[MAX_RESP_LEN];
    int i=0, count=0;
    int listCnt=0;
    PMY_SITE_SURVEY pAvlAPList = NULL;

    memset(szAplistBuffer, 0x0, AP_LIST_BUFF_SIZE);

    pAvlAPList = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
    if(!pAvlAPList) {
        APP_LOG("UPNPDevice",LOG_ERR,"Malloc Failed...");
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);
        UpnpAddToActionResponse(out, "GetApList", CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],"ApList", "FAILURE");

        return FAILURE;
    }

    EnableSiteSurvey(NULL);

    APP_LOG("UPNPDevice",LOG_DEBUG,"Get List...");
    getCurrentAPList (pAvlAPList, &count);

    listCnt = count;

    for (i=0; i < count; i++) {
        if((strstr(pAvlAPList[i].ssid, ",") != NULL) || (strstr(pAvlAPList[i].ssid, "|") != NULL)) {
            listCnt--;
            APP_LOG("UPNP: DEVICE",LOG_DEBUG, "Updated listcnt: %d for SSID: %s", listCnt, pAvlAPList[i].ssid);
        }
    }

    APP_LOG("UPNP: DEVICE",LOG_DEBUG, "count: %d, listcnt: %d\n", count, listCnt);

    snprintf(szAplistBuffer, sizeof(szAplistBuffer), "Page:1/1/%d$\n", listCnt);

    for (i=0; i < count; i++) {
        if((strstr(pAvlAPList[i].ssid, ",") == NULL) && (strstr(pAvlAPList[i].ssid, "|") == NULL)) {
            memset(szApEntry, 0x00, sizeof(szApEntry));

            snprintf(szApEntry, sizeof(szApEntry), "%s|%d|%d|%s,\n",
                     pAvlAPList[i].ssid,
                     atoi((const char *)pAvlAPList[i].channel),
                     atoi((const char *)pAvlAPList[i].signal),
                     pAvlAPList[i].security
                    );
            strncat(szAplistBuffer, szApEntry, sizeof(szAplistBuffer)-strlen(szAplistBuffer)-1);
        } else
            APP_LOG("UPNP: DEVICE",LOG_DEBUG, "Skipping entry %d for SSID: %s", i, pAvlAPList[i].ssid);
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetApList", CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],"ApList", szAplistBuffer);

    APP_LOG("UPNP: DEVICE",LOG_DEBUG, "%s\n", szAplistBuffer);

    if(pAvlAPList) {
        free(pAvlAPList);
        pAvlAPList = NULL;
    }

    return UPNP_E_SUCCESS;
}

int GetNetworkList(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szAplistBuffer[AP_LIST_BUFF_SIZE];
    char szApEntry[MAX_RESP_LEN];
    int i=0, ssidLen = 0;
    int count=0;
    PMY_SITE_SURVEY pAvlAPList = NULL;

    memset(szAplistBuffer, 0x0, AP_LIST_BUFF_SIZE);

    pAvlAPList = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
    if(!pAvlAPList) {
        APP_LOG("UPNPDevice",LOG_ERR,"Malloc Failed...");
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);
        UpnpAddToActionResponse(out, "GetNetworkList", CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],"NetworkList", "FAILURE");

        return FAILURE;
    }

    EnableSiteSurvey(NULL);

    APP_LOG("UPNPDevice",LOG_DEBUG,"Get Network List...");
    getCurrentAPList (pAvlAPList, &count);

    snprintf(szAplistBuffer, sizeof(szAplistBuffer), "Page:1/1/%d$\n", count);

    for (i=0; i < count; i++) {
        memset(szApEntry, 0x00, sizeof(szApEntry));

        ssidLen = strlen(pAvlAPList[i].ssid);
        snprintf(szApEntry, sizeof(szApEntry), "%d|%s|%d|%d|%s|\n",
                 ssidLen,
                 pAvlAPList[i].ssid,
                 atoi((const char *)pAvlAPList[i].channel),
                 atoi((const char *)pAvlAPList[i].signal),
                 pAvlAPList[i].security
                );
        strncat(szAplistBuffer, szApEntry, sizeof(szAplistBuffer)-strlen(szAplistBuffer)-1);
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetNetworkList", CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],"NetworkList", szAplistBuffer);

    APP_LOG("UPNP: DEVICE",LOG_DEBUG, "%s\n", szAplistBuffer);

    if(pAvlAPList) {
        free(pAvlAPList);
        pAvlAPList = NULL;
    }

    return UPNP_E_SUCCESS;
}



/*
 * Unsets the u-boot env variable 'boot_A_args'.
 *
 * Will work only on the OPENWRT boards.
 * Gemtek boards follow different procedure to handle the scenario.
 *
 * Done for the Story: 2187, To restore the state of the switch before power failure
 */
#if 0
void correctUbootParams()
{
    char sysString[SIZE_128B];
    int bootArgsLen = 0;

    memset(sysString, '\0', SIZE_128B);
    bootArgsLen = strlen(g_szBootArgs);

    if(bootArgsLen) {
        snprintf(sysString, sizeof(sysString), "fw_setenv boot_A_args %s", g_szBootArgs);
        system(sysString);
    }
}
#endif

void AsyncControlleeDeviceStop()
{
    pMessage msg = createMessage(META_CONTROLLEE_DEVICE_STOP, 0x00, 0x00);
    SendMessage2App(msg);
}

void* resetThread(void *arg)
{
    int resetType = *(int *)arg;
    tu_set_my_thread_name( __FUNCTION__ );

    free(arg);

    switch(resetType) {
    case META_SOFT_RESET:
        APP_LOG("ResetThread", LOG_ALERT, "Processing META_SOFT_RESET");
#ifdef PRODUCT_WeMo_Insight
        ClearUsageData();
#endif
        APP_LOG("UPNP",LOG_DEBUG, "***resetThread:ClearRuleFromFlash()***\n");
        ClearRuleFromFlash();
        UpdateXML2Factory();
        AsyncSaveData();
        break;

    case META_FULL_RESET:
        APP_LOG("ResetThread", LOG_ALERT, "Processing META_FULL_RESET");
        APP_LOG("ResetThread", LOG_ALERT, "Sending SIGUSR1 to wemo_remote process to delete cloud entry");
        system("killall -SIGUSR1 wemo_remote");
#ifdef __ORESETUP__
        /* Remove saved IP from flash */
        UnSetBelkinParameter ("wemo_ipaddr");
        /* Unsetting IconVersion */
        UnSetBelkinParameter (ICON_VERSION_KEY);
        gWebIconVersion=0;
        StopInetTask();
        AsyncControlleeDeviceStop();
        APP_LOG("ITC: meta", LOG_DEBUG, "CALL DEREGISTER!!!");
#else
#ifdef PRODUCT_WeMo_Dimmer
        /* set animation to reflect the LED_STATE_FACTORY_RESTORE state. */
        setAnimation(LED_STATE_FACTORY_RESTORE);
#endif
        StopInetTask();
        AsyncControlleeDeviceStop();
        APP_LOG("ITC: meta", LOG_DEBUG, "CALL DEREGISTER!!!");
        /* Remove saved IP from flash */
        UnSetBelkinParameter ("wemo_ipaddr");
        /* Unsetting IconVersion */
        UnSetBelkinParameter (ICON_VERSION_KEY);
        gWebIconVersion=0;
#endif
        pluginUsleep(CONTROLLEE_DEVICE_STOP_WAIT_TIME);
        ResetToFactoryDefault(0);

        break;

    case META_REMOTE_RESET:
        APP_LOG("ResetThread", LOG_ALERT, "Processing META_REMOTE_RESET");
        UnSetBelkinParameter (DEFAULT_HOME_ID);
        memset(g_szHomeId, 0x00, sizeof(g_szHomeId));
        UnSetBelkinParameter (DEFAULT_PLUGIN_PRIVATE_KEY);
        memset(g_szPluginPrivatekey, 0x00, sizeof(g_szPluginPrivatekey));
        UnSetBelkinParameter (RESTORE_PARAM);
        memset(g_szRestoreState, 0x0, sizeof(g_szRestoreState));
        /* server environment settings cleanup and nat client destroy */
        break;
#ifdef PRODUCT_WeMo_Insight
    case META_CLEAR_USAGE:
        APP_LOG("ResetThread", LOG_ALERT, "Processing META_CLEAR_USAGE");
        ClearUsageData();
        break;
#endif
    case META_WIFI_SETTING_RESET:
        APP_LOG("ResetThread", LOG_ALERT, "Processing META_WIFI_SETTING_RESET");
        StopInetTask();
        /* remove IOT related variables */
        UnSetBelkinParameter ("iotHost");
        UnSetBelkinParameter ("iotRegion");
        UnSetBelkinParameter ("cloudHost");
        UnSetBelkinParameter ("cloudEnv");
        UnSetBelkinParameter ("deviceID");
        UnSetBelkinParameter ("deviceToken");
        UnSetBelkinParameter("GroupId");
        UnSetBelkinParameter("setupToken");
        UnSetBelkinParameter("provision_complete");
/*
        UnSetBelkinParameter("HomeKitSetup");
        UnSetBelkinParameter("HKSetupState");
        UnSetBelkinParameter("close_setup");
*/
        system("rm -rf /tmp/Belkin_settings/.data");
        system("rm -f /tmp/wac.stop");
        system("rm -f /tmp/upnp.init");
        /* Let ADK know wifi reset happened */
        /* will be cleaned in wifiHndler.c:saveData after next setup */
        SetBelkinParameter("wifi_reset_happened", "1");
        resetWiFiSettings();
        break;

    }
    reset_thread = -1;
    pthread_exit(0);
    return 0;
}

int ExecuteReset(int resetIndex)
{
    int *resetType = (int *)MALLOC(sizeof(int));
    int retVal;

    if(!resetType) {
        APP_LOG("UPnP: Device",LOG_ERR, "Memory could not be allocated for resetType");
        return SUCCESS;
    }

    if(0x01 == resetIndex)
        *resetType = META_SOFT_RESET;
    else if(0x02 == resetIndex)
        *resetType = META_FULL_RESET;
    else if(0x03 == resetIndex)
        *resetType = META_REMOTE_RESET;
#ifdef PRODUCT_WeMo_Insight
    else if(0x04 == resetIndex)
        *resetType = META_CLEAR_USAGE;
#endif
    else if(0x05 == resetIndex)
        *resetType = META_WIFI_SETTING_RESET;
    else
        return FAILURE;

    /* first of all remove the reset thread, if running */
    if(reset_thread != -1) {
        if((retVal = pthread_cancel(reset_thread)) == 0) {
            reset_thread = -1;
            APP_LOG("UPnP: Device",LOG_DEBUG,"reset thread removed successfully....");
        } else {
            APP_LOG("UPnP: Device",LOG_ERR,"reset thread removal failed [%d] ....",retVal);
        }
    } else
        APP_LOG("UPnP: Device",LOG_DEBUG,"reset thread doesn't exist. Creating reset thread....");

    pthread_attr_init(&reset_attr);
    pthread_attr_setdetachstate(&reset_attr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&reset_thread,&reset_attr,
                            (void*)&resetThread, (void *)resetType);

    if(retVal < SUCCESS) {
        APP_LOG("UPnP: Device",LOG_CRIT, "RESET Thread not created");
        return SUCCESS;
    }

    return SUCCESS;
}

#ifndef __ORESETUP__
pthread_mutex_t g_remoteDeReg_mutex;
pthread_cond_t g_remoteDeReg_cond;
#define WAIT_DREG_TIMEOUT	10
#endif

int ReSetup(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int retVal = UPNP_E_SUCCESS;

    APP_LOG("UPNPDevice: ReSetup", LOG_DEBUG, "%s", __FUNCTION__);

    if (0x00 == pActionRequest || 0x00 == request) {
        return 0x01;
    }
    char* paramValue = Util_GetFirstDocumentItem(request, "Reset");

    if (paramValue)
        APP_LOG("UPNPDevice", LOG_DEBUG, "trying reset plugin to: %s", paramValue);

    int resettype = atoi(paramValue);

#ifdef __ORESETUP__
    /* Clear Rules info */
    if(resettype == 0x01) {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);

        UpnpAddToActionResponse(out, "ReSetup",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "success");

        APP_LOG("UPNPDevice", LOG_DEBUG, "Reset Plugin-> (Mem Partitions) Rules:  done: %d", resettype);

        ExecuteReset(0x01);
    }
    /* Clear All info */
    else if(resettype == 0x02) {
        UpnpActionRequest_set_ErrCode(pActionRequest, SUCCESS);
        UpnpAddToActionResponse(out, "ReSetup",CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "success");
        ExecuteReset(0x02);
    }
    /* Clear Remote info */
    else if(resettype == 0x03) {
        UpnpActionRequest_set_ErrCode(pActionRequest, SUCCESS);
        UpnpAddToActionResponse(out, "ReSetup",CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "success");
        ExecuteReset(0x03);
    }
#ifdef PRODUCT_WeMo_Insight
    else if(resettype == 0x04) {
        UpnpActionRequest_set_ErrCode(pActionRequest, SUCCESS);
        UpnpAddToActionResponse(out, "ReSetup",CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "success");
        ExecuteReset(0x04);
    }
#endif
    else if(resettype == 0x05) {
        UpnpActionRequest_set_ErrCode(pActionRequest, SUCCESS);
        UpnpAddToActionResponse(out, "ReSetup",CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "success");
        ExecuteReset(0x05);
    } else {
        APP_LOG("UPNPDevice", LOG_ERR, "Reset Plugin not done: %d", resettype);

        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);

        UpnpAddToActionResponse(out, "ReSetup", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "unsuccess");
    }
#else
    retVal = ExecuteReset(resettype);
    if (retVal < SUCCESS) {
        APP_LOG("UPNPDevice", LOG_ERR, "Reset Plugin not done: %d", resettype);
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "ReSetup", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "unsuccess");
    } else {
        if (resettype == 0x02) {
            APP_LOG("REMOTEACCESS", LOG_DEBUG, "SHOULD HANDLE DEREGISTRATION FROM CLOUD for type %d", resettype);
        }
        UpnpActionRequest_set_ErrCode(pActionRequest, SUCCESS);
        UpnpAddToActionResponse(out, "ReSetup",CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Reset", "success");
    }
#endif
    return UPNP_E_SUCCESS;
}

void StopPowerMonitorTimer()
{
    int ret = 0x01;
    if (-1 != ithPowerSensorMonitorTask) {
        ret = ithread_cancel(ithPowerSensorMonitorTask);

        if (0x00 != ret) {
            APP_LOG("UPNP: Rule", LOG_DEBUG, "######################## ithread_cancel: Can not stop power monitor task thread ##############################");
        }

        ithPowerSensorMonitorTask = -1;

    }
}

void UpdatePowerMonitorTimer(int duration, int endAction)
{
    int ret = 0x01;

    if (0x00 == duration)
        return;
    sPowerDuration  = duration;
    sPowerEndAction = endAction;

    if (-1 == ithPowerSensorMonitorTask) {
        ret = pthread_create(&ithPowerSensorMonitorTask, 0x00, PowerSensorMonitorTask, 0x00);
        if (0x00 == ret) {
            ret = pthread_detach(ithPowerSensorMonitorTask);
            if (0x00 != ret) {
                APP_LOG("UPNP: Rule", LOG_DEBUG, "######################## pthread_detach: Can not detach power monitor task thread ##############################");
            }
        } else {
            APP_LOG("UPNP: Rule", LOG_DEBUG, "######################## pthread_create: Can not create power monitor task thread ##############################");
            resetSystem();
        }
    } else {
        APP_LOG("UPNP: sensor rule", LOG_DEBUG, "Sensor event, monitoring thread running until %d seconds:", sPowerDuration);
    }
}

void UPnPActionUpdate(int curState)
{
    pMessage msg = 0x00;

    if (0x00 == curState) {
        msg = createMessage(UPNP_ACTION_MESSAGE_OFF_IND, 0x00, 0x00);
    } else if (0x01 == curState) {
        msg = createMessage(UPNP_ACTION_MESSAGE_ON_IND, 0x00, 0x00);
    }

    SendMessage2App(msg);
}


void UPnPInternalToggleUpdate(int curState)
{
    pMessage msg = 0x00;
    if (0x00 == curState) {
        msg = createMessage(UPNP_MESSAGE_OFF_IND, 0x00, 0x00);
    } else if (0x01 == curState) {
        msg = createMessage(UPNP_MESSAGE_ON_IND, 0x00, 0x00);
    }
#ifdef PRODUCT_WeMo_Insight
    else if (POWER_SBY == curState) {
        msg = createMessage(UPNP_MESSAGE_SBY_IND, 0x00, 0x00);
    }
#endif

    //gautam:  Relay thread does nothing but sends it to main thread: unnecessary latency
    SendMessage2App(msg);
}

int SetBinaryState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "SetBinaryState: command paramter invalid");
        return 0x01;
    }
    if (DEVICE_SENSOR == g_eDeviceType) {
        APP_LOG("UPNPDevice", LOG_ERR, "Sensor device, command not support");
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);

        UpnpAddToActionResponse(out, "SetBinaryState",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "BinaryState", "unsuccess");

        return 0x00;
    }
    int ret = FAILURE;
    int ret1 = SUCCESS;
    int countdownRuleLastMinStatus = 0;
    int toState = GetCurBinaryState();

    char* paramValue = Util_GetFirstDocumentItem(request, "BinaryState");
    char* paramDuration = Util_GetFirstDocumentItem(request, "Duration");
    char* paramEndAction = Util_GetFirstDocumentItem(request, "EndAction");
    int attrSet = 0;
    int toBrightness = -1;
    bool sensorTrigger = false;
#ifdef PRODUCT_WeMo_Dimmer
    char *paramBrightness = Util_GetFirstDocumentItem(request, "brightness");
    char *paramFader = Util_GetFirstDocumentItem(request, "fader");
    int bret =FAILURE;
    int fret = FAILURE;
#endif
#ifdef PRODUCT_WeMo_Insight
    char attrValue[SIZE_100B];
#else
    char attrValue[SIZE_32B];
#endif
    memset(attrValue, 0x00, sizeof(attrValue));

    if(paramValue && strlen(paramValue)) {
        attrSet|= ATTR_STATE;
        //Set plugin device status as requested
        toState  = atoi(paramValue);
        APP_LOG("UPNPDevice", LOG_DEBUG, "to request state change to %d", toState);
    }

#ifdef PRODUCT_WeMo_Dimmer
    if(paramBrightness && strlen(paramBrightness)) {
        attrSet|= ATTR_BRIGHTNESS;
        toBrightness = atoi(paramBrightness);
        APP_LOG("UPNPDevice", LOG_DEBUG, "to request brightness change to %d", toBrightness);
    }


    if(paramFader && strlen(paramFader)) {
        if(attrSet) {
            APP_LOG("UPNPDevice", LOG_DEBUG, "fader with state/brightness isn't permitted.");
            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
            UpnpAddToActionResponse(out, "SetBinaryState",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "BinaryState", "Error");

            FreeXmlSource(paramValue);
            FreeXmlSource(paramBrightness);
            FreeXmlSource(paramFader);
            FreeXmlSource(paramDuration);
            FreeXmlSource(paramEndAction);
            return 0x00;
        }
        attrSet|= ATTR_FADER;
        /* fader start is considered as an intervention. calling notifyManualToggle
           to cancel any on-going Away Task */
#ifdef SIMULATED_OCCUPANCY
        if(LONG_PRESS_AWAY_ACTIVE ||
           (gRuleHandle[e_AWAY_RULE].ruleCnt && (gpSimulatedDevice && gpSimulatedDevice->ruleEndTime))) {
            notifyManualToggle();
        }
#endif
        APP_LOG("UPNPDevice", LOG_DEBUG, "to request fader change to %s", paramFader);
        /* play the sleep timer animation */
        setAnimation(LED_STATE_SLEEP_TIMER);
        fret = setFader(paramFader, true);
    }
#endif

    if(!attrSet) {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
        UpnpAddToActionResponse(out, "SetBinaryState",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "BinaryState", "Error");

        FreeXmlSource(paramValue);
#ifdef PRODUCT_WeMo_Dimmer
        FreeXmlSource(paramBrightness);
        FreeXmlSource(paramFader);
#endif
        FreeXmlSource(paramDuration);
        FreeXmlSource(paramEndAction);
        return 0x00;
    }

    if ((0x00 != paramDuration) && (0x00 != strlen(paramDuration)) && (0x00 != paramEndAction) && (0x00 != strlen(paramEndAction))) {
        /* Sensor triggered */
        APP_LOG("UPNPDevice", LOG_DEBUG, "Sensor trigger");
        setActuation(ACTUATION_SENSOR_RULE);
        sensorTrigger = true;
    } else {
        /* App triggered */
        APP_LOG("UPNPDevice", LOG_DEBUG, "App trigger");
        setActuation(ACTUATION_MANUAL_APP);
        //setRemote("0");
    }

#ifdef PRODUCT_WeMo_Dimmer
    if(!(attrSet & ATTR_FADER))
#endif
    {
        APP_LOG("UPNPDevice", LOG_DEBUG, "************to state:%d, g_powerStatus:%d", toState,g_PowerStatus);
        ret1 = processAction (toState, CONTROL_LOCAL, attrSet, toBrightness, sensorTrigger);

        if(attrSet & ATTR_STATE)
            ret = (ret1 & ATTR_STATE)?FAILURE:SUCCESS;
#ifdef PRODUCT_WeMo_Dimmer
        if(attrSet & ATTR_BRIGHTNESS)
            bret = (ret1 & ATTR_BRIGHTNESS)?FAILURE:SUCCESS;
#endif
    }

#ifdef PRODUCT_WeMo_Dimmer
    if (SUCCESS == ret || SUCCESS == bret || SUCCESS == fret)
#else
    if (SUCCESS == ret)
#endif

    {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);
        if((attrSet & ATTR_STATE) && SUCCESS == ret) {
#if !defined(PRODUCT_WeMo_Dimmer)
            UPnPInternalToggleUpdate(toState);
#endif
#ifdef PRODUCT_WeMo_Insight
            if (g_InitialMonthlyEstKWH) {
                snprintf(attrValue, SIZE_100B, "%d|%u|%u|%u|%u|%u|%u|%u|%u|%d",
                         toState,g_StateChangeTS,g_ONFor,g_TodayONTimeTS ,g_TotalONTime14Days,g_HrsConnected,g_AvgPowerON,g_PowerNow,g_AccumulatedWattMinute,g_InitialMonthlyEstKWH);
            } else {
                snprintf(attrValue, SIZE_100B, "%d|%u|%u|%u|%u|%u|%u|%u|%u|%0.f",
                         toState,g_StateChangeTS,g_ONFor,g_TodayONTimeTS ,g_TotalONTime14Days,g_HrsConnected,g_AvgPowerON,g_PowerNow,g_AccumulatedWattMinute,g_KWH14Days);
            }
            APP_LOG("UPNP", LOG_DEBUG, "Local Binary State Insight Parameters: %s", attrValue);
#else
            snprintf (attrValue, sizeof (attrValue), "%d", toState);
#endif
            UpnpAddToActionResponse(out, "SetBinaryState",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "BinaryState", attrValue);
            /* Countdown Time Notification */
            memset (attrValue, 0, sizeof (attrValue));
            snprintf (attrValue, sizeof (attrValue), "%lu", gCountdownEndTime);
            APP_LOG ("UPNP", LOG_DEBUG, "Countdown Time: %s", attrValue);

            UpnpAddToActionResponse (out,
                                     "SetBinaryState",
                                     CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                     "CountdownEndTime", attrValue);

            /* Device current time */
            memset (attrValue, 0, sizeof (attrValue));
            snprintf (attrValue, sizeof (attrValue), "%lu", GetUTCTime());

            APP_LOG ("UPNP", LOG_DEBUG, "Countdown Time: %s", attrValue);

            UpnpAddToActionResponse (out,
                                     "SetBinaryState",
                                     CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                     "deviceCurrentTime", attrValue);
        }
#ifdef PRODUCT_WeMo_Dimmer
        if((attrSet & ATTR_BRIGHTNESS) && (SUCCESS == bret)) {
            snprintf (attrValue, sizeof (attrValue), "%d", getBrightness());
            UpnpAddToActionResponse(out, "SetBinaryState",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "brightness", attrValue);
        }
        if((attrSet & ATTR_FADER) && SUCCESS == fret) {
            getFader(attrValue);
            UpnpAddToActionResponse(out, "SetBinaryState",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Fader", attrValue);
        }
#endif


    } else {
        if((attrSet & ATTR_STATE) && FAILURE == ret) {
#ifdef PRODUCT_WeMo_Insight
            if (g_InitialMonthlyEstKWH) {
                snprintf(attrValue, SIZE_100B, "%d|%u|%u|%u|%u|%u|%u|%u|%u|%d",
                         toState,g_StateChangeTS,g_ONFor,g_TodayONTimeTS ,g_TotalONTime14Days,g_HrsConnected,g_AvgPowerON,g_PowerNow,g_AccumulatedWattMinute,g_InitialMonthlyEstKWH);
            } else {
                snprintf(attrValue, SIZE_100B, "%d|%u|%u|%u|%u|%u|%u|%u|%u|%0.f",
                         toState,g_StateChangeTS,g_ONFor,g_TodayONTimeTS ,g_TotalONTime14Days,g_HrsConnected,g_AvgPowerON,g_PowerNow,g_AccumulatedWattMinute,g_KWH14Days);
            }
            APP_LOG("UPNP", LOG_DEBUG, "Local Binary State Insight Parameters on failure: %s", attrValue);
#else
            snprintf(attrValue, sizeof(attrValue), "%d", GetCurBinaryState());
#endif
            /* WEMO-33379: Error response should be sent even when countdown rule is in last minute */
            if (gRuleHandle[e_COUNTDOWN_RULE].ruleCnt &&
                countdownRuleLastMinStatus) {
                APP_LOG ("UPNPDevice", LOG_DEBUG, "Countdown timer was in last minute");
            }
            UpnpAddToActionResponse(out, "SetBinaryState",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "BinaryState", attrValue);
            /* Countdown Time Notification */
            memset (attrValue, 0, sizeof (attrValue));
            snprintf (attrValue, sizeof (attrValue), "%lu", gCountdownEndTime);
            APP_LOG ("UPNP", LOG_DEBUG, "Countdown Time: %s", attrValue);

            UpnpAddToActionResponse (out,
                                     "SetBinaryState",
                                     CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                     "CountdownEndTime", attrValue);

            /* Device current time */
            memset (attrValue, 0, sizeof (attrValue));
            snprintf (attrValue, sizeof (attrValue), "%lu", GetUTCTime());

            APP_LOG ("UPNP", LOG_DEBUG, "Countdown Time: %s", attrValue);

            UpnpAddToActionResponse (out,
                                     "SetBinaryState",
                                     CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                     "deviceCurrentTime", attrValue);
            APP_LOG("UPNPDevice", LOG_ERR, "None of the three attributes(state, brightness, fader) modified");
        }
#ifdef PRODUCT_WeMo_Dimmer
        if((attrSet & ATTR_BRIGHTNESS) && (FAILURE == bret)) {
            snprintf (attrValue, sizeof (attrValue), "%d", getBrightness());
            UpnpAddToActionResponse(out, "SetBinaryState",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "brightness", attrValue);
        }
        if((attrSet & ATTR_FADER) && FAILURE == fret) {
            getFader(attrValue);
            UpnpAddToActionResponse(out, "SetBinaryState",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Fader", attrValue);
        }
#endif

    }

    if ((0x00 != paramDuration) && (0x00 != strlen(paramDuration)) && (0x00 != paramEndAction) && (0x00 != strlen(paramEndAction))) {
        //- JIRA: WEMO-4605: to check the ON logic from user and 8029
        if((IsLastUserActionOn()) &&(toState == POWER_ON)) {
            APP_LOG("Rule", LOG_DEBUG, "Last user action to ON, no timer");
            StopPowerMonitorTimer();
            //- do nothing here
        } else {
            int PowerDuration  = atoi(paramDuration);
            int PowerEndAction = atoi(paramEndAction);
            APP_LOG("UPnP: Sensor rule", LOG_DEBUG, "duration: %d, endAction: %d", PowerDuration, PowerEndAction);
            if(PowerDuration) {
                APP_LOG("UPNPDevice", LOG_DEBUG, "Sensor event, create management thread");
                UpdatePowerMonitorTimer(PowerDuration, PowerEndAction);
            }
        }
    } else {
        //- Action from phone, so notify the sensor
        SetLastUserActionOnState(toState);
        if (0x00 == ret) {
            StopPowerMonitorTimer();
        }
    }

    FreeXmlSource(paramValue);
    FreeXmlSource(paramDuration);
    FreeXmlSource(paramEndAction);
#ifdef PRODUCT_WeMo_Dimmer
    FreeXmlSource(paramBrightness);
    FreeXmlSource(paramFader);
#endif
    return UPNP_E_SUCCESS;
}

int SetLogLevelOption (pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int lvl;
    int opt;

    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_ERR, "SetLogLevelOption: command paramter invalid");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }
    char* szLevel = Util_GetFirstDocumentItem(request, "Level");
    if((szLevel == NULL) || (strlen(szLevel) == 0)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetLogLevelOption", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"Level", "Parameter Error");
        APP_LOG("UPNP: Device", LOG_ERR,"Set Log Level parameter: failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }
    char* szOption = Util_GetFirstDocumentItem(request, "Option");
    if((szOption == NULL) || (strlen(szOption) == 0)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetLogLevelOption", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"Option", "Parameter Error");
        APP_LOG("UPNP: Device", LOG_ERR,"Set Log Option parameter: failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    lvl = atoi(szLevel);
    opt = atoi(szOption);
    APP_LOG("UPNP: Device", LOG_DEBUG,"Setting Log level: %d and option: %d", lvl, opt);

    loggerSetLogLevel (lvl, opt);
    return UPNP_E_SUCCESS;
}

int GetBinaryState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int curState = 0x00;

    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "GetBinaryState: paramters error");
        return 0x01;
    }

    if (DEVICE_SOCKET == g_eDeviceType) {
        LockLED();
        curState = GetCurBinaryState();
        if(curState) {
            APP_LOG("UPNPDevice", LOG_ERR,"Switch State: ON");
        } else {
            APP_LOG("UPNPDevice", LOG_ERR,"Switch State: OFF");
        }
        UnlockLED();
    } else if (DEVICE_SENSOR == g_eDeviceType) {
        LockSensor();
        curState = GetSensorState();
        UnlockSensor();

        if(curState) {
            APP_LOG("UPNPDevice", LOG_ERR,"Motion Detected: TRUE");
        } else {
            APP_LOG("UPNPDevice", LOG_ERR,"Motion Detected: FALSE");
        }
    }

    char szCurState[SIZE_4B];
    memset(szCurState, 0x00, sizeof(szCurState));

    snprintf(szCurState, sizeof(szCurState), "%d", curState);
APP_LOG("UPNPDevice", LOG_DEBUG, "setting action result...");
    UpnpAddToActionResponse(out, "GetBinaryState",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "BinaryState", szCurState);
#ifdef PRODUCT_WeMo_Dimmer
    char brightnessValue[SIZE_32B];
    memset(brightnessValue, 0, sizeof(brightnessValue));
    snprintf(brightnessValue, sizeof(brightnessValue), "%d", getBrightness());

    char faderValue[MAX_FADER_LENGTH];
    memset(faderValue, 0, MAX_FADER_LENGTH);
    getFader(faderValue);

    UpnpAddToActionResponse(out, "GetBinaryState",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "brightness", brightnessValue);
    UpnpAddToActionResponse(out, "GetBinaryState",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "fader", faderValue);
    APP_LOG("UPNPDevice", LOG_DEBUG, "GetBinaryState state:%s brightness:%s fader:%s", szCurState, brightnessValue, faderValue);
#else
    APP_LOG("UPNPDevice", LOG_DEBUG, "GetBinaryState: %s", szCurState);
#endif

    IsOverriddenStatus();
    return UPNP_E_SUCCESS;
}

int StopPair(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "Parameter error");
        return 0x00;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "StopPair",
                            CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],"status", "success");

#ifdef __MIPSEL__
    system("ifconfig apcli0 down");
    pluginUsleep(500000);
    system("ifconfig apcli0 up");
#else
    system("ifconfig br-lan down");
    pluginUsleep(500000);
    system("ifconfig br-lan up");
#endif

    APP_LOG("UPNP", LOG_DEBUG, "WiFi pairing stopped");
    return 0;
}

int ConnectHomeNetwork(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int channel;
    int rect = 0x00;
    char* paramValue = 0x00;
    paramValue = Util_GetFirstDocumentItem(request, "channel");

    gAppCalledCloseAp=0;
    gBootReconnect=0;
    /* reset smart setup presence */
    gSmartSetup = 0;

    APP_LOG("UPNPDevice", LOG_CRIT,"%s", __FUNCTION__);
    if(isSetupRequested()) {
        APP_LOG("UPNPDevice", LOG_ERR, "#### Setup request already executed ######");
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);
        UpnpAddToActionResponse(out, "ConnectHomeNetwork", CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE], "status", "unsuccess");
        return 0x01;
    }

    setSetupRequested(1);

    UpdateUPnPNetworkMode(UPNP_LOCAL_MODE);

    channel = atoi(paramValue);

    char* ssid 		= Util_GetFirstDocumentItem(request, "ssid");
    char* auth 		= Util_GetFirstDocumentItem(request, "auth");
    char* encrypt	= Util_GetFirstDocumentItem(request, "encrypt");
    char* password;

    if(strcmp(auth,"OPEN"))
        password = Util_GetFirstDocumentItem(request, "password");
    else
        password = "NOTHING";

    /* Save the password in a global variable - to be used later by WifiConn thread */
    memset(gUserKey, 0, sizeof(gUserKey));
    memcpy(gUserKey, password, sizeof(gUserKey));

    APP_LOG("UPNPDevice",LOG_HIDE,"Attempting to connect home network: %s, channel: %d, auth:%s, encrypt:%s, password:%s",
            ssid, channel, auth, encrypt, password);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "ConnectHomeNetwork", CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],"PairingStatus", "Connecting");

    APP_LOG("UPNPDevice",LOG_DEBUG,"connect to selected network: %s", ssid);
    rect = threadConnectHomeNetwork(channel, ssid, auth, encrypt, password);

    if(strcmp(auth,"OPEN"))
        FreeXmlSource(password);
    FreeXmlSource(paramValue);
    FreeXmlSource(ssid);
    FreeXmlSource(auth);
    FreeXmlSource(encrypt);

    return rect;

}

int GetNetworkStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int WiFiClientCurrState = 0x00;
    char szStatus[SIZE_16B];
    memset(szStatus, 0x00, sizeof(szStatus));

    if (0x00 == pActionRequest) {
        APP_LOG("UPNP: Device", LOG_ERR, "%s: request parameter error", __FUNCTION__);

        return PLUGIN_ERROR_E_NETWORK_ERROR;
    }

    WiFiClientCurrState = getCurrentClientState();

    snprintf(szStatus, sizeof(szStatus), "%d", WiFiClientCurrState);

    APP_LOG("UPNPDevice", LOG_ERR,"NetworkStatus: %d:%s", WiFiClientCurrState, szStatus);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetNetworkStatus",
                            CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE],"NetworkStatus", szStatus);

    return UPNP_E_SUCCESS;
}


int GetDeviceTime(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    return UPNP_E_SUCCESS;
}

int IsApplyTimeSync(int utc, double timeZone, int dst)
{
    int rect = 0x00;

    /* 1. 24 Hours
     * 2. Time Zone difference.
     */

    APP_LOG("UPNPDevice", LOG_ERR,"TimeSync: %d:%f:%d:%f:%d", utc,timeZone,g_lastTimeSync,g_lastTimeZone,dst);
    if(timeZone != (g_lastTimeZone+dst)) {
        rect = 0x01;
    }

    if(utc > (g_lastTimeSync+86400)) {
        rect = 0x01;
    }

    {
        time_t Now = time(NULL);
        struct tm Tm;

        (void) gmtime_r(&Now,&Tm);
        if(Tm.tm_year + 1900 < 2015) {
            // Time not set yet
            rect = 0x01;
        }
    }

    return rect;
}

/***
 *  Function to get timezone_index value
 *	from flash
 *
 ******************************************/

int getTimeZoneIndexFromFlash(void)
{
    int Index = 0;
    char *timeZIdx = NULL;

    timeZIdx = GetBelkinParameter("timezone_index");
    if(timeZIdx && strlen(timeZIdx) != 0) {
        Index = atoi(timeZIdx);
    }

    APP_LOG("UPNP", LOG_DEBUG,"Index: %d", Index);
    return Index;
}

/***
 *
 *
 *
 *
 *
 *
 ******************************************/
int SyncTime(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int rect = 0;
    char dstenable[SIZE_2B];

    memset(dstenable, 0x0, sizeof(dstenable));

    int isLocalDstSupported = LOCAL_DST_SUPPORTED_ON;

    if (0x00 == pActionRequest) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_TIME_SYNC);
        UpnpAddToActionResponse(out, "SyncTime", CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE],"status", "Parameters Error!");
        return PLUGIN_ERROR_E_TIME_SYNC;
    }

    char* szUtc 		= Util_GetFirstDocumentItem(request, "UTC");
    char* szTimeZone	= Util_GetFirstDocumentItem(request, "TimeZone");
    char* szDst			= Util_GetFirstDocumentItem(request, "dst");
    //- The read the local phone, dst is supported now or not,
    char* szIsLocalDst	= Util_GetFirstDocumentItem(request, "DstSupported");
    char ltimezone[SIZE_16B];

    int startTime = time(0);

    memset(ltimezone, 0x0, sizeof(ltimezone));

    APP_LOG("UPNP: Device",LOG_DEBUG,"set time: utc: %s, timezone: %s, dst: %s", szUtc, szTimeZone, szDst);

    if (0x00 == szUtc || 0x00 == szTimeZone || 0x00 == szDst) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_TIME_SYNC);

        /* block is entered even when one of the parameters is NULL, free valid memory */
        FreeXmlSource(szUtc);
        FreeXmlSource(szTimeZone);
        FreeXmlSource(szDst);
        FreeXmlSource(szIsLocalDst);

        UpnpAddToActionResponse(out, "TimeSync", CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE],"status", "failure");
        APP_LOG("UPNP: Device", LOG_DEBUG, "paramters error");

        return 0x01;
    }

    szTimeZone = Wemo46751(szTimeZone);

    float TimeZone = 0.0;

    int utc 			= atoi(szUtc);

    //- atof not working well under this compiler, so calculated manually
    if (szTimeZone[0x00] == '-') {
        TimeZone 	= atoi(szTimeZone);
        if (0x00!= strstr(szTimeZone, ".5")) {
            TimeZone -= 0.5;
        }
        //[WEMO-26944] - szTimeZone could be rounded up to ".8"
        else if ((0x00 != strstr(szTimeZone, ".75")) || (0x00 != strstr(szTimeZone, ".8"))) {
            TimeZone += 0.25;
        }
    } else {
        TimeZone 	= atoi(szTimeZone);
        if (0x00!= strstr(szTimeZone, ".5")) {
            TimeZone += 0.5;
        }
        //[WEMO-26944] - szTimeZone could be rounded up to ".8"
        else if ((0x00 != strstr(szTimeZone, ".75")) || (0x00 != strstr(szTimeZone, ".8"))) {
            TimeZone += 0.75;
        }
    }


    int dst			= atoi(szDst);

    if (!IsApplyTimeSync(utc, TimeZone, dst)) {
        //- UPnP response here with failure
        APP_LOG("UPNP: Device", LOG_DEBUG, ": *****NOT APPLYING TIME SYNC");
        UpnpAddToActionResponse(out, "TimeSync", CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE],"status", "failure");
        FreeXmlSource(szUtc);
        FreeXmlSource(szTimeZone);
        FreeXmlSource(szDst);
        FreeXmlSource(szIsLocalDst);

        return 0x01;
    }

    if(gDstEnable != !dst) {
        gDstEnable=!dst;
        APP_LOG("UPNP: Device", LOG_DEBUG, ": ###### Setting DST value %d",gDstEnable);
        snprintf(dstenable, sizeof(dstenable), "%d", gDstEnable);
        SetBelkinParameter(LASTDSTENABLE, dstenable);
    }

    //- Get the local Dst supported flag
    if ( (0x00 != szIsLocalDst) &&
         0x00!= strlen(szIsLocalDst) ) {
        isLocalDstSupported = atoi(szIsLocalDst);
        gDstSupported = isLocalDstSupported;
        SetBelkinParameter(SYNCTIME_DSTSUPPORT, szIsLocalDst);

        if(!gDstSupported) {
            UnSetBelkinParameter(LASTDSTENABLE);
        }
    }

    //[WEMO-26944] - Adjust TimeZone in case Chatham Isl DST ON
    int iTimeZoneIndex = 0;
    int adjTime = 0;
    if (TimeZone == NZ_TIMEZONE_2 + 1) {
        //No index for timezone NZ_TIMEZONE_2 + 1, so adjust system-time when calling SetTime()
        iTimeZoneIndex = GetTimeZoneIndex(TimeZone - 1);
        adjTime = NUM_SECONDS_IN_HOUR;
    } else {
        iTimeZoneIndex = GetTimeZoneIndex(TimeZone);
        adjTime = 0;
    }
    APP_LOG("UPNP: Device",LOG_DEBUG,"set time: utc: %d, timezone: %f, additional time: %d, dst: %d", utc, TimeZone, adjTime, dst);

    pluginUsleep(500000);

    int lapTime = time(0);
    int timeDiff = lapTime-startTime;

    if(timeDiff > 0) {
        //APP_LOG("UPNP: Device",LOG_DEBUG,"additional Time: %d", timeDiff);
        utc += timeDiff;
    }
    APP_LOG("UPNP: Device",LOG_DEBUG,"setTime Adjusted utc: %d, timezoneIndex: %d, dst: %d", utc, iTimeZoneIndex, dst);

    if(timeDiff > 10) {
        // Sanity check.  We asked to sleep for .5 seconds above so we MAY
        // have actually overslept by a little, but more than 10 seconds
        // isn't believable, what probably happened was that we got two
        // Timesync requests from the App and the first one set the time
        // so ... bail
        APP_LOG("UPNP: DEVICE",LOG_ERR,"timeDiff: %d, bailing",timeDiff);
    } else {
        rect = SetTimeAndTZ((time_t) utc,szTimeZone,dst,gDstSupported);
    }

    if (Gemtek_Success == rect) {
        UpdateMobileTimeSync(0x01);		//-Set flag to true
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_E_SUCCESS);

        gNTPTimeSet = 1;
        g_lastTimeSync = utc;

        if (DST_ON == dst) {
            // calculate the absolute one
            {
                g_lastTimeZone = TimeZone - 1;
            }
        } else {
            //- Keep unchange
            g_lastTimeZone = TimeZone;
        }
        snprintf(ltimezone, sizeof(ltimezone), "%f", g_lastTimeZone);

        UpnpAddToActionResponse(out, "TimeSync", CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE],"status", "success");
        APP_LOG("UPNP: Device", LOG_CRIT,"set time: utc: %s, timezone: %s, dst: %s g_lastTimeZone:%f success", szUtc, szTimeZone, szDst,g_lastTimeZone);

        //- only if isLocalDstSupported true, the calculate and toggle?
        if (isLocalDstSupported) {
            //- Arizona and Hawii will not?
        } else
            gTimeZoneUpdated = 1;

        AsyncSaveData();
#ifdef PRODUCT_WeMo_Insight
        APP_LOG("UPNP: Device", LOG_ERR,"Restarting Data export scheduler on time sync");
        g_ReStartDataExportScheduler = 1;
        ReStartDataExportScheduler();
#endif

        /* Device time changed, restart rules engine */
        APP_LOG("UPNP: Device", LOG_DEBUG,"Restarting rule engine on time sync");
        gRestartRuleEngine = RULE_ENGINE_RELOAD;
#ifdef PRODUCT_WeMo_Dimmer
        /* check if the hush mode is active but the thread is not
           running. */
        checkAndStartHushMode();
#endif
    } else {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_TIME_SYNC);
        UpnpAddToActionResponse(out, "TimeSync", CtrleeDeviceServiceType[PLUGIN_E_TIME_SYNC_SERVICE],"status", "failure");
        APP_LOG("UPNP: Device", LOG_ERR,"set time: utc: %s, timezone: %s, dst: %s failure", szUtc, szTimeZone, szDst);
    }

    FreeXmlSource(szUtc);
    FreeXmlSource(szTimeZone);
    FreeXmlSource(szDst);
    FreeXmlSource(szIsLocalDst);

    return  UpnpActionRequest_get_ErrCode(pActionRequest);
}

int DeviceActionResponse(pUPnPActionRequest pActionRequest,
                         const char* responseName,
                         const char* serviceType,
                         const char* variabName,
                         const char *variableValue
                        )
{
    IXML_Document *out = UpnpActionRequest_get_ActionResult(pActionRequest);
    UpnpAddToActionResponse(&out, responseName, serviceType, variabName, variableValue);
    return 0;
}

/**
 * Get Friendly Name:
 * 	Callback to get device friendly name
 *
 *
 * *****************************************************************************************************************/
int GetFriendlyName(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s, called", __FUNCTION__);
    char *szFriendlyName = GetDeviceConfig("FriendlyName");

    APP_LOG("UPNP: Device", LOG_DEBUG, "Read name from flash: %s", szFriendlyName);

    if (pActionRequest == 0x00 || pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_ERR, "UPNP parameter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetFriendlyName", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"FriendlyName", g_szFriendlyName);


    APP_LOG("UPNP: Device", LOG_DEBUG,"Get friendly name: %s", g_szFriendlyName);
    return UPNP_E_SUCCESS;
}

int SetDeviceId(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    return UPNP_E_SUCCESS;
}

int GetDeviceId(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    return UPNP_E_SUCCESS;
}

int GetShareHWInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s, called", __FUNCTION__);
    char* szMac = NULL;
    char* szSerial = NULL;
    char* szUdn = NULL;
    char* szRestoreState = NULL;
    char* szHomeId = NULL;
    char* szPvtKey = NULL;


    if (pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_ERR, "UPNP parameter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }
    /* to restrict this request if there is already one in process */
    if(g_pxRemRegInf) {
        APP_LOG("UPNP: Device", LOG_ERR, "proxy registration already in process");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    /* allocate proxy registration request structure */
    g_pxRemRegInf = (ProxyRemoteAccessInfo *)CALLOC(1, sizeof(ProxyRemoteAccessInfo));
    if(g_pxRemRegInf == NULL) {
        APP_LOG("UPNP",LOG_ERR, "proxy RemRegInf mem allocation FAIL");
        return FAILURE;
    }

    szMac = Util_GetFirstDocumentItem(request, "Mac");
    if(szMac!=NULL) {
        if (0x00 == strlen(szMac)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_REMOTE_ACCESS);
            UpnpAddToActionResponse(out, "ShareHWInfo", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"Mac", "Parameter Error");
            APP_LOG("UPNP: Device", LOG_ERR,"Proxy Mac parameter failure");
            return PLUGIN_ERROR_E_REMOTE_ACCESS;
        }
    }

    szSerial = Util_GetFirstDocumentItem(request, "Serial");
    if(szSerial!=NULL) {
        if (0x00 == strlen(szSerial)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_REMOTE_ACCESS);
            UpnpAddToActionResponse(out, "ShareHWInfo", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"Serial", "Parameter Error");
            APP_LOG("UPNP: Device", LOG_ERR,"Proxy Serial number parameter failure");
            FreeXmlSource(szMac);
            return PLUGIN_ERROR_E_REMOTE_ACCESS;
        }
    }

    szUdn = Util_GetFirstDocumentItem(request, "Udn");
    if(szUdn!=NULL) {
        if (0x00 == strlen(szUdn)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_REMOTE_ACCESS);
            UpnpAddToActionResponse(out, "ShareHWInfo", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"Udn", "Parameter Error");
            APP_LOG("UPNP: Device", LOG_ERR,"Proxy Udn parameter failure");
            FreeXmlSource(szMac);
            FreeXmlSource(szSerial);
            return PLUGIN_ERROR_E_REMOTE_ACCESS;
        }
    }

    szRestoreState = Util_GetFirstDocumentItem(request, "RestoreState");
    if(szRestoreState!=NULL) {
        if (0x00 == strlen(szRestoreState)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_REMOTE_ACCESS);
            UpnpAddToActionResponse(out, "ShareHWInfo", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"RestoreState", "Parameter Error");
            APP_LOG("UPNP: Device", LOG_ERR,"Proxy Restore State parameter failure");
            FreeXmlSource(szMac);
            FreeXmlSource(szSerial);
            FreeXmlSource(szUdn);
            return PLUGIN_ERROR_E_REMOTE_ACCESS;
        }
    }

    szHomeId = Util_GetFirstDocumentItem(request, "HomeId");
    if(szHomeId!=NULL) {
        if (0x00 == strlen(szHomeId)) {
            APP_LOG("UPNP: Device", LOG_ERR,"Proxy Home Id parameter failure");
            FreeXmlSource(szHomeId);
            szHomeId = 0x00;
        }
    }

    szPvtKey = Util_GetFirstDocumentItem(request, "PluginKey");
    if(szPvtKey!=NULL) {
        if (0x00 == strlen(szPvtKey)) {
            APP_LOG("UPNP: Device", LOG_ERR,"Proxy Pvt Key parameter failure");
            FreeXmlSource(szPvtKey);
            szPvtKey = 0x00;
        }
    }

    strncpy(g_pxRemRegInf->proxy_macAddress, szMac, sizeof(g_pxRemRegInf->proxy_macAddress)-1);
    strncpy(g_pxRemRegInf->proxy_serialNumber, szSerial, sizeof(g_pxRemRegInf->proxy_serialNumber)-1);
    strncpy(g_pxRemRegInf->proxy_pluginUniqueId, szUdn, sizeof(g_pxRemRegInf->proxy_pluginUniqueId)-1);
    strncpy(g_pxRemRegInf->proxy_restoreState, szRestoreState, sizeof(g_pxRemRegInf->proxy_restoreState)-1);
    if((szHomeId!= NULL) && (strlen(szHomeId)> 0x00))
        strncpy(g_pxRemRegInf->proxy_homeId, szHomeId, sizeof(g_pxRemRegInf->proxy_homeId)-1);
    if((szPvtKey!= NULL) && (strlen(szPvtKey)> 0x00))
        strncpy(g_pxRemRegInf->proxy_privateKey, szPvtKey, sizeof(g_pxRemRegInf->proxy_privateKey)-1);

    APP_LOG("UPNP: Device", LOG_DEBUG,"Proxy MAC: <%s> SERIAL: <%s> UDN: <%s>", g_pxRemRegInf->proxy_macAddress, g_pxRemRegInf->proxy_serialNumber, g_pxRemRegInf->proxy_pluginUniqueId);

    FreeXmlSource(szMac);
    FreeXmlSource(szSerial);
    FreeXmlSource(szUdn);
    FreeXmlSource(szRestoreState);
    if(szHomeId!=NULL)
        FreeXmlSource(szHomeId);
    if(szPvtKey!=NULL)
        FreeXmlSource(szPvtKey);

    return UPNP_E_SUCCESS;
}

int GetMacAddr(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szPluginUDN[SIZE_UDN];
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s, called", __FUNCTION__);

    if (pActionRequest == 0x00 || pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_ERR, "UPNP parameter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }
    char *szMacAddr = g_szWiFiMacAddress;
    if(szMacAddr != NULL) {
        if (0x00 == strlen(szMacAddr)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            APP_LOG("UPNP: Device", LOG_ERR,"Failure Get Mac Addr: %s", "");
            return PLUGIN_ERROR_E_BASIC_EVENT;
        }
    }
    char *szSerialNo = g_szSerialNo;
    if(szSerialNo != NULL) {
        if (0x00 == strlen(szSerialNo)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            APP_LOG("UPNP: Device", LOG_ERR,"Failure Get Serial: %s", "");
            return PLUGIN_ERROR_E_BASIC_EVENT;
        }
    }
    memset(szPluginUDN, 0x00, sizeof(szPluginUDN));
    strncpy(szPluginUDN, g_szUDN, sizeof(szPluginUDN)-1);
    if(szPluginUDN != NULL) {
        if (0x00 == strlen(szPluginUDN)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            APP_LOG("UPNP: Device", LOG_ERR,"Failure Get UDN: %s", "");
            return PLUGIN_ERROR_E_BASIC_EVENT;
        }
    }

    APP_LOG("UPNP: Device", LOG_DEBUG, "Read  from flash Mac Addr: %s Serial No: %s UDN: %s\n", szMacAddr, szSerialNo, szPluginUDN);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetMacAddr", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"MacAddr", szMacAddr);
    UpnpAddToActionResponse(out, "GetSerialNo", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"SerialNo", szSerialNo);
    UpnpAddToActionResponse(out, "GetPluginUDN", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"PluginUDN", szPluginUDN);

    return UPNP_E_SUCCESS;
}
int GetSerialNo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    return UPNP_E_SUCCESS;
}
int GetPluginUDN(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    return UPNP_E_SUCCESS;
}

/**
 * SetIcon:
 * 	Callback to Get icon URL
 *
 *
 * *****************************************************************************************************************/
int SetFriendlyName(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (pActionRequest == 0x00 || pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"Set friendly name: paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    char* szFriendlyName = Util_GetFirstDocumentItem(request, "FriendlyName");
    if ((0x00 == strlen(szFriendlyName)) || (strlen(szFriendlyName) > 255)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "ChangeFriendlyName", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"status", "Parameters Error");
        APP_LOG("UPNP: Device", LOG_ERR,"Set friendly name: failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "ChangeFriendlyName", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "FriendlyName", szFriendlyName);

    strncpy(g_szFriendlyName, szFriendlyName, sizeof(g_szFriendlyName)-1);
    APP_LOG("UPNP: Device", LOG_DEBUG,"Set friendly name: %s", szFriendlyName);

    SetBelkinParameter("FriendlyName", g_szFriendlyName);
    UpdateXML2Factory();

    NameChangeNotify(szFriendlyName);
    FreeXmlSource(szFriendlyName);

    /* Issue 2894 */
    AsyncSaveData();

    return UPNP_E_SUCCESS;

}

/**
 * GetIcon:
 * 	Callback to Get icon URL
 *
 *
 * *****************************************************************************************************************/
int GetIcon(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szIconURL[MAX_FW_URL_LEN];
    memset(szIconURL, 0x00, sizeof(szIconURL));

    //-Return the icon path of the device
    if (strlen(g_server_ip) > 0x00) {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);
        snprintf(szIconURL, sizeof(szIconURL), "http://%s:%d/icon.jpg", g_server_ip, g_server_port);
        UpnpAddToActionResponse(out, "GetIconURL", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "URL", szIconURL);
    } else {
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);
        UpnpAddToActionResponse(out, "GetIconURL", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "URL", "");
    }

    APP_LOG("UPNP: Device", LOG_DEBUG,"GetIcon: %s", szIconURL);

    return UPNP_E_SUCCESS;
}

/**
 * GetIconVersion:
 * 	Callback to Get Icon Version
 *
 *
 * *****************************************************************************************************************/
int GetIconVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szIconVersion[SIZE_4B];

    memset(szIconVersion, 0, SIZE_4B);

    if (pActionRequest == 0x00 || pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"GetIconVersion : parameter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    snprintf(szIconVersion, sizeof(szIconVersion), "%d", gWebIconVersion);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    APP_LOG("UPNP: Device", LOG_DEBUG, "Icon version:%s", szIconVersion);

    UpnpAddToActionResponse(out, "GetIconVersion", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "IconVersion", szIconVersion);

    return UPNP_E_SUCCESS;
}

/**
 * SetIconVersion:
 * 	Callback to Set Icon Version
 *
 *
 * *****************************************************************************************************************/
int SetIconVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char *szIconVersion = NULL;

    szIconVersion = Util_GetFirstDocumentItem(request, "IconVersion");

    if ((szIconVersion == NULL) || (strlen(szIconVersion) == 0)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetIconVersion", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"status", "Parameters Error");
        APP_LOG("UPNP: Device", LOG_ERR,"SetIconVersion: failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SetIconVersion", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "IconVersion", szIconVersion);

    gWebIconVersion = atoi(szIconVersion);

    APP_LOG("UPNP: Device", LOG_DEBUG,"SetIconVersion: %d", gWebIconVersion);

    UpdateXML2Factory();

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SetIconVersion", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "IconVersion", szIconVersion);
    FreeXmlSource(szIconVersion);

    return UPNP_E_SUCCESS;
}

typedef struct time_sync_args {
    char szUtc[SIZE_16B];
    char szTimeZone[SIZE_8B];
    char szDst[SIZE_4B];
    char szIsLocalDst[SIZE_4B];
} STimeSyncArgs;

int timeSyncThread(void *tmp)
{
    int isLocalDstSupported = LOCAL_DST_SUPPORTED_ON;
    int rect = 0;
    float TimeZone = 0.0;
    STimeSyncArgs *args = (STimeSyncArgs *)tmp;
    //- The read the local phone, dst is supported now or not,
    char ltimezone[SIZE_16B];
    int utc = 0;
    int startTime = time(0);
    memset(ltimezone, 0x0, sizeof(ltimezone));

    if(!args) {
        APP_LOG("UPNP: Device", LOG_ERR, "Invalid argument");
        timesyncThread = -1;
        return FAILURE;
    }

    char *szTimeZone = args->szTimeZone;
    char *szUtc = args->szUtc;
    char *szDst = args->szDst;
    char *szIsLocalDst = args->szIsLocalDst;

    utc = atoi(szUtc);

    //- atof not working well under this compiler, so calculated manually
    if (szTimeZone[0x00] == '-') {
        TimeZone 	= atoi(szTimeZone);
        if (0x00!= strstr(szTimeZone, ".5")) {
            TimeZone -= 0.5;
        }
        //[WEMO-26944] - szTimeZone could be rounded up to ".8"
        else if ((0x00 != strstr(szTimeZone, ".75")) || (0x00 != strstr(szTimeZone, ".8"))) {
            TimeZone += 0.25;
        }
    } else {
        TimeZone 	= atoi(szTimeZone);
        if (0x00!= strstr(szTimeZone, ".5")) {
            TimeZone += 0.5;
        }
        //[WEMO-26944] - szTimeZone could be rounded up to ".8"
        else if ((0x00 != strstr(szTimeZone, ".75")) || (0x00 != strstr(szTimeZone, ".8"))) {
            TimeZone += 0.75;
        }
    }


    int dst			= atoi(szDst);

    if (!IsApplyTimeSync(utc, TimeZone, dst)) {
        //- UPnP response here with failure
        APP_LOG("UPNP: Device", LOG_DEBUG, ": *****NOT APPLYING TIME SYNC");
        free(args);
        timesyncThread = -1;
        return 0x01;
    }

    //- Get the local Dst supported flag
    if ( (0x00 != szIsLocalDst) &&
         0x00!= strlen(szIsLocalDst) ) {
        isLocalDstSupported = atoi(szIsLocalDst);
        gDstSupported = isLocalDstSupported;
        SetBelkinParameter(SYNCTIME_DSTSUPPORT, szIsLocalDst);

        if(!gDstSupported) {
            UnSetBelkinParameter(LASTDSTENABLE);
        }
    }

    //[WEMO-26944] - Adjust TimeZone in case Chatham Isl DST ON
    int iTimeZoneIndex = 0;
    int adjTime = 0;
    if (TimeZone == NZ_TIMEZONE_2 + 1) {
        //No index for timezone NZ_TIMEZONE_2 + 1, so adjust system-time when calling SetTime()
        iTimeZoneIndex = GetTimeZoneIndex(TimeZone - 1);
        adjTime = NUM_SECONDS_IN_HOUR;
    } else {
        iTimeZoneIndex = GetTimeZoneIndex(TimeZone);
        adjTime = 0;
    }
    APP_LOG("UPNP: Device",LOG_DEBUG,"set time: utc: %d, timezone: %f, additional time: %d, dst: %d", utc, TimeZone, adjTime, dst);

    pluginUsleep(500000);

    int lapTime = time(0);
    int timeDiff = lapTime-startTime;

    if(timeDiff > 0) {
        //APP_LOG("UPNP: Device",LOG_DEBUG,"additional Time: %d", timeDiff);
        utc += timeDiff;
    }
    APP_LOG("UPNP: Device",LOG_DEBUG,"setTime Adjusted utc: %d, timezoneIndex: %d, dst: %d", utc, iTimeZoneIndex, dst);

    if(timeDiff > 10) {
        // Sanity check.  We asked to sleep for .5 seconds above so we MAY
        // have actually overslept by a little, but more than 10 seconds
        // isn't believable, what probably happened was that we got two
        // Timesync requests from the App and the first one set the time
        // so ... bail
        APP_LOG("UPNP: DEVICE",LOG_ERR,"timeDiff: %d, bailing",timeDiff);
    } else {
        rect = SetTimeAndTZ((time_t) utc,szTimeZone,dst,gDstSupported);
    }
    if (Gemtek_Success == rect) {
        UpdateMobileTimeSync(0x01);		//-Set flag to true

        gNTPTimeSet = 1;
        g_lastTimeSync = utc;

        if (DST_ON == dst) {
            // calculate the absolute one
            {
                g_lastTimeZone = TimeZone - 1;
            }
        } else {
            //- Keep unchange
            g_lastTimeZone = TimeZone;
        }

        snprintf(ltimezone, sizeof(ltimezone), "%f", g_lastTimeZone);
        APP_LOG("UPNP: Device", LOG_CRIT,"setting LastTimeZone to %s", ltimezone);
        SetBelkinParameter (SYNCTIME_LASTTIMEZONE, ltimezone);

        APP_LOG("UPNP: Device", LOG_CRIT,"set time: utc: %s, timezone: %s, dst: %s g_lastTimeZone:%f success", szUtc, szTimeZone, szDst,g_lastTimeZone);

        //- only if isLocalDstSupported true, the calculate and toggle?
        if (isLocalDstSupported) {
            //- Arizona and Hawii will not?
        } else
            gTimeZoneUpdated = 1;

        AsyncSaveData();
#ifdef PRODUCT_WeMo_Insight
        APP_LOG("UPNP: Device", LOG_ERR,"Restarting Data export scheduler on time sync");
        g_ReStartDataExportScheduler = 1;
        ReStartDataExportScheduler();
#endif
        /* Device time changed, restart rules engine */
        APP_LOG("UPNP: Device", LOG_DEBUG,"Restarting rule engine on time sync");
        gRestartRuleEngine = RULE_ENGINE_RELOAD;
#ifdef PRODUCT_WeMo_Dimmer
        /* check if the hush mode is active but the thread is not
           running. */
        checkAndStartHushMode();
#endif
    } else {
        APP_LOG("UPNP: Device", LOG_ERR,"set time: utc: %s, timezone: %s, dst: %s failure", szUtc, szTimeZone, szDst);
    }

    free(args);
    timesyncThread = -1;
    APP_LOG("UPNP: Device", LOG_DEBUG, "Time sync thread exiting");
    return SUCCESS;
}


int handleTimeSync(pUPnPActionRequest pActionRequest, char *func)
{
    int retVal = FAILURE;
    IXML_Document *out = UpnpActionRequest_get_ActionResult(pActionRequest);
    IXML_Document *request = UpnpActionRequest_get_ActionRequest(pActionRequest);
    char dstenable[SIZE_2B];
    memset(dstenable, 0x0, sizeof(dstenable));

    if (0x00 == pActionRequest) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_TIME_SYNC);
        UpnpAddToActionResponse(&out, func, CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],"status", "Parameters Error!");
        return 0x01;
    }

    char* szUtc 		= Util_GetFirstDocumentItem(request, "UTC");
    char* szTimeZone	= Util_GetFirstDocumentItem(request, "TimeZone");
    char* szDst			= Util_GetFirstDocumentItem(request, "dst");
    char* szIsLocalDst	= Util_GetFirstDocumentItem(request, "DstSupported");
    STimeSyncArgs *args=NULL;

    if (0x00 == szUtc || 0x00 == szTimeZone || 0x00 == szDst || 0x00 == szIsLocalDst ||
        0x00 == strlen(szUtc) || 0x00 == strlen(szTimeZone) || 0x00 == strlen(szDst) || 0x00 == strlen(szIsLocalDst)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_TIME_SYNC);

        UpnpAddToActionResponse(&out, "TimeSync", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],"status", "failure");

        if(szUtc)
            FreeXmlSource(szUtc);

        if(szDst)
            FreeXmlSource(szDst);

        if(szTimeZone)
            FreeXmlSource(szTimeZone);

        if(szIsLocalDst)
            FreeXmlSource(szIsLocalDst);

        APP_LOG("UPNP: Device", LOG_DEBUG, "Old app case, send time sync request");
        //   			UPnPTimeSyncStatusNotify();
        g_OldApp=1;

        return PLUGIN_ERROR_E_TIME_SYNC;
    }

    szTimeZone = Wemo46751(szTimeZone);

    APP_LOG("UPNP: Device",LOG_DEBUG,"set time: utc: %s, timezone: %s, dst: %s, szIsLocalDst: %s", szUtc, szTimeZone, szDst, szIsLocalDst);

    int dst = atoi(szDst);
    if(gDstEnable != !dst) {
        gDstEnable=!dst;
        APP_LOG("UPNP: Device", LOG_DEBUG, ": ###### Setting DST value %d",gDstEnable);
        snprintf(dstenable, sizeof(dstenable), "%d", gDstEnable);
        SetBelkinParameter(LASTDSTENABLE, dstenable);
    }

    args = (STimeSyncArgs*)ZALLOC(sizeof(STimeSyncArgs));

    strncpy(args->szUtc, szUtc, sizeof(args->szUtc)-1);
    strncpy(args->szTimeZone, szTimeZone, sizeof(args->szTimeZone)-1);
    strncpy(args->szDst, szDst, sizeof(args->szDst)-1);
    strncpy(args->szIsLocalDst, szIsLocalDst, sizeof(args->szIsLocalDst)-1);

    pthread_attr_init(&timesync_attr);
    pthread_attr_setdetachstate(&timesync_attr, PTHREAD_CREATE_DETACHED);

    if(timesyncThread == -1)
        retVal = pthread_create(&timesyncThread, &timesync_attr, (void*)&timeSyncThread, (void *)args);

    if(retVal < SUCCESS) {
        APP_LOG("UPnPApp",LOG_ERR, "****TimeSync Thread not Created: %s****", strerror(errno));
    }

    if(szUtc)
        FreeXmlSource(szUtc);

    if(szDst)
        FreeXmlSource(szDst);

    if(szTimeZone)
        FreeXmlSource(szTimeZone);

    if(szIsLocalDst)
        FreeXmlSource(szIsLocalDst);

    UpnpAddToActionResponse(&out, "TimeSync", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],"status", "success");
    return  SUCCESS;
}

/**
 * GetInformation:
 * 	Callback to Get the device information in XML format
 *
 *
 * *****************************************************************************************************************/
int GetInformation(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char *pszBuff = NULL;
    int port = 0, state = 0;
    char modelCode[SIZE_32B]=" ";
    int bufSize = SIZE_1024B;
    int fwUpdateState = getCurrFWUpdateState();
    char *pRuleDbVersion=NULL;

    handleTimeSync(pActionRequest, "GetInformation");

#if defined(LONG_PRESS_SUPPORTED)
    int longPressRuleCount = (gpsLongPressRule != NULL)?gpsLongPressRule->count:0;
    char *longPressRuleUdn = (longPressRuleCount != 0)?gpsLongPressRule->pszUDNList:" ";
    /* possible long press action values=> 0:OFF, 1:ON, 2:Toggle, 3:AwayMode , -1:Invalid */
    int longPressRuleAction = (gpsLongPressRule != NULL)?gpsLongPressRule->action:-1;
    int longPressRuleState = (gpsLongPressRule != NULL)?gpsLongPressRule->state:-1;

    bufSize += strlen(longPressRuleUdn) + 1;
#endif

    pszBuff = (char *) MALLOC(bufSize);

    port = UpnpGetServerPort();
    state = GetCurBinaryState();
    getModelCode(modelCode);

    char tmp[SIZE_256B];
    snprintf(pszBuff, bufSize-1,"<Device><DeviceInformation><firmwareVersion>%s</firmwareVersion><iconVersion>%d</iconVersion><iconPort>%d</iconPort><macAddress>%s</macAddress><binaryState>%d</binaryState><hwVersion>v%d</hwVersion><deviceCurrentTime>%lu</deviceCurrentTime><productName>%s</productName>", g_szFirmwareVersion, gWebIconVersion, port, g_szWiFiMacAddress, state,ghwVersion,GetUTCTime(), getProductName(modelCode));

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp),"<FriendlyName>%s</FriendlyName><currentFWUpdateState>%d</currentFWUpdateState>", g_szFriendlyName, fwUpdateState);
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);

    /* WEMO-48532:Include fwUpdateState and firmware download timestamp(in case of FM_STATUS is not DEFAULT) the response */
    if((fwUpdateState != FM_STATUS_DEFAULT) && gFwDownloadTimeStamp) {
        char fwTimeStamp[SIZE_64B];
        memset(fwTimeStamp, 0, sizeof(fwTimeStamp));
        snprintf(fwTimeStamp, sizeof(fwTimeStamp),"<FWDownloadTimeStamp>%lu</FWDownloadTimeStamp>", gFwDownloadTimeStamp);
        strncat(pszBuff, fwTimeStamp, bufSize-strlen(pszBuff)-1);
    }

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    memset(tmp, 0, sizeof(tmp));
    if(isOverHeat2ndWarning()==TRUE) {
        snprintf(tmp, sizeof(tmp),"<OverTemp>%d</OverTemp>", getOverHeatState()?2:0);
    } else {
        snprintf(tmp, sizeof(tmp),"<OverTemp>%d</OverTemp>", getOverHeatState());
    }
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);
#endif
#ifdef PRODUCT_WeMo_Dimmer
    char faderValue[MAX_FADER_LENGTH];
    memset(faderValue, 0, MAX_FADER_LENGTH);
    getFader(faderValue);
    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp),"<brightness>%d</brightness><fader>%s</fader><OverTemp>%d</OverTemp>", getBrightness(), faderValue, getOverHeatState());
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);

    if(strlen(gBulbType)) {
        snprintf(tmp, sizeof(tmp),"<bulbType>%s</bulbType>", gBulbType);
        strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);
    }

    int nightMode=DEFAULT_NIGHT_MODE_STATUS;
    int brightness=DEFAULT_NIGHT_MODE_BRIGHTNESS;
    int startTime=DEFAULT_NIGHT_MODE_START_TIME;
    int endTime=DEFAULT_NIGHT_MODE_END_TIME;
    /* send the night configuration */
    if(gpsNightMode) {
        nightMode = gpsNightMode->nightMode;
        startTime = gpsNightMode->startTime;
        endTime = gpsNightMode->endTime;
        brightness = gpsNightMode->brightness;
    }
    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp),"<NightModeConfiguration><nightMode>%u</nightMode><startTime>%u</startTime><endTime>%u</endTime><nightModeBrightness>%u</nightModeBrightness></NightModeConfiguration>", nightMode, startTime, endTime, brightness);
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);
#endif

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "<CountdownEndTime>%lu</CountdownEndTime>", gCountdownEndTime);
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);

#if defined(LONG_PRESS_SUPPORTED)
    snprintf(pszBuff+strlen(pszBuff), bufSize-strlen(pszBuff)-1, "<longPressRuleDeviceCnt>%d</longPressRuleDeviceCnt><longPressRuleDeviceUdn>%s</longPressRuleDeviceUdn><longPressRuleAction>%d</longPressRuleAction><longPressRuleState>%d</longPressRuleState>", longPressRuleCount, longPressRuleUdn, longPressRuleAction, longPressRuleState);
#endif

    /* Fetch the rule Db version */
    pRuleDbVersion = GetDeviceConfig (RULE_DB_VERSION_KEY);
    if ((NULL == pRuleDbVersion) || (0 == strlen (pRuleDbVersion))) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: RuleDbVersion Error", __LINE__);
        pRuleDbVersion = "0";
    }

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "<dbVersion>%s</dbVersion>", pRuleDbVersion);
#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "<hushMode>%s</hushMode>", g_hushAnimParam);
#endif
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);

    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp,"</DeviceInformation></Device>", sizeof(tmp));
    strncat(pszBuff, tmp, bufSize-strlen(pszBuff)-1);

    if (*out != NULL) {
        ixmlDocument_free(*out);
    }
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    APP_LOG("UPNP: Device", LOG_DEBUG, "Device information: %s, len:%d", pszBuff, strlen(pszBuff));

    UpnpAddToActionResponse(out, "GetInformation", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE], "Information", pszBuff);

    free(pszBuff);

    return UPNP_E_SUCCESS;
}

/**
 * GetDeviceInformation:
 * 	Callback to Get the device information
 *
 *
 * *****************************************************************************************************************/

int GetDeviceInformation(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szBuff[MAX_BUF_LEN];
    int port = 0, state = 0;
    char *payload = NULL;
    char *setupCode = NULL;
    char *setup_state_param = NULL;
    char *provisioned_by = NULL;
    char setup_state[4] = {0};
    int code_state = false;

    handleTimeSync(pActionRequest, "GetDeviceInformation");

    port = UpnpGetServerPort();

    state = GetCurBinaryState();

    if (*out != NULL) {
        ixmlDocument_free(*out);
    }
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    memset(szBuff, 0x0, MAX_BUF_LEN);

    snprintf(szBuff, MAX_BUF_LEN,"%s|%s|%d|%d|%d|%s", g_szWiFiMacAddress, g_szFirmwareVersion, gWebIconVersion, port, state, g_szFriendlyName);

    do {
        char setup_key[16];
        char *setupId = NULL;

        setupCode = HomekitstoreGet("SETUP_CODE");

        if (setupCode == NULL) {
            code_state = false;
            APP_LOG("UPNP: HOMEKIT", LOG_ERR, "Failed to retrieve SETUP_CODE from hkstore");
            break;
        }

        memset(setup_key, 0, sizeof(setup_key));
        if(sscanf(setupCode,"%3s-%2s-%3s", setup_key, &setup_key[3], &setup_key[5]) != 3) {
            code_state = false;
            APP_LOG("UPNP: HOMEKIT", LOG_ERR, "Wrong SETUP_CODE format: %s", setupCode);
            break;
        }

        setupId = HomekitstoreGet("SETUP_ID");
        if ((setupId == NULL) || (strlen(setupId) != 4)) {
            code_state = false;
            APP_LOG("UPNP: HOMEKIT", LOG_INFO, "Failed to retrieve SETUP_ID from hkstore");
            break;
        }

#if defined(PRODUCT_WeMo_SNSV2)
        payload = get_setup_payload(0, atoi(setup_key), setupId);
#endif
#if defined(PRODUCT_WeMo_Dimmer)
        payload = get_setup_payload(1, atoi(setup_key), setupId);
#endif
#if defined(PRODUCT_WeMo_LightV2)
        payload = get_setup_payload(2, atoi(setup_key), setupId);
#endif
        code_state = true;
    } while(false);

    APP_LOG ("UPNP: Device", LOG_HIDE, "DeviceInformation: %s", szBuff);
    UpnpAddToActionResponse (out,
                             "GetDeviceInformation",
                             CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],
                             "DeviceInformation",
                             szBuff);
    if (code_state) {
        UpnpAddToActionResponse (out,
                                 "GetDeviceInformation",
                                 CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],
                                 "Payload",
                                 payload);
    }

    setup_state_param = GetBelkinParameter("HKSetupState");

    if (setup_state_param == NULL || strlen(setup_state_param) == 0) {
        snprintf(setup_state, sizeof(setup_state), "%d", 0);
    } else {
        snprintf(setup_state, sizeof(setup_state), "%s", setup_state_param);
    }

    UpnpAddToActionResponse (out,
                             "GetDeviceInformation",
                             CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],
                             "HKSetupState",
                             setup_state);

    /* Adding Countdown Time */
    APP_LOG ("UPNP: Device", LOG_DEBUG, "CountdownTime: %lu",
             gCountdownEndTime);
    memset (szBuff, 0x0, MAX_BUF_LEN);
    snprintf (szBuff, MAX_BUF_LEN, "%lu", gCountdownEndTime);

    UpnpAddToActionResponse (out,
                             "GetDeviceInformation",
                             CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],
                             "CountdownTime", szBuff);

    /* Add ProvisionedBy */
    provisioned_by = GetBelkinParameter("ProvisionedBy");
    if (strlen(provisioned_by)) {
        APP_LOG ("UPNP: Device", LOG_DEBUG, "ProvisionedBy: %s", provisioned_by);
        UpnpAddToActionResponse (out,
                                 "GetDeviceInformation",
                                 CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],
                                 "ProvisionedBy", provisioned_by);
    }
    else {
        APP_LOG ("UPNP: Device", LOG_DEBUG, "ProvisionedBy: not set, returning empty");
        UpnpAddToActionResponse (out,
                                 "GetDeviceInformation",
                                 CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],
                                 "ProvisionedBy", "");
    }

    if (payload) {
        free(payload);
    }
    return UPNP_E_SUCCESS;
}

/**
 * GetLogFilePath:
 * 	Callback to Get log file URL
 *
 *
 * *****************************************************************************************************************/
int GetLogFilePath(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szLogFileURL[MAX_FW_URL_LEN];
    memset(szLogFileURL, 0x00, sizeof(szLogFileURL));

    //-Return the log file path of the device
    if (strlen(g_server_ip) > 0x00) {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);
        snprintf(szLogFileURL, sizeof(szLogFileURL), "http://%s:%d/PluginLogs.txt", g_server_ip, g_server_port);
        UpnpAddToActionResponse(out, "GetLogFileURL", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "LOGURL", szLogFileURL);
    } else {
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);
        UpnpAddToActionResponse(out, "GetLogFileURL", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "LOGURL", "");
    }

    APP_LOG("UPNP: Device", LOG_DEBUG,"Log File URL: %s", szLogFileURL);

    return UPNP_E_SUCCESS;
}
pthread_t CloseApWaiting_thread = -1;

/**
 * GetWatchdogFile:
 * 	Callback to Get Watchdog Log file
 *
 *
 * *****************************************************************************************************************/
int GetWatchdogFile(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (pActionRequest == NULL) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "UPNP parameter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetWatchdogFile", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "WDFile", "Sending");

    APP_LOG("UPNP: Device", LOG_DEBUG,"GetWatchdogFile: Sending the wdLogFile");

    APP_LOG("UPNP: Device",LOG_DEBUG, "***************LOG Thread created***************\n");

    return UPNP_E_SUCCESS;
}

/**
 * Get Signal Strength:
 * 	Callback to get device present signal Strength
 *
 *
 * *****************************************************************************************************************/
int SignalStrengthGet(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szSignalSt[MAX_FW_URL_LEN];
    memset(szSignalSt, 0x00, sizeof(szSignalSt));

    APP_LOG("UPNP: Device", LOG_DEBUG, "%s, called", __FUNCTION__);

    if (pActionRequest == NULL) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "UPNP parameter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    /*Update signal strength*/
    if(!gSignalStrength) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "Update signal strength!");
        chksignalstrength();
    }

    snprintf(szSignalSt, sizeof(szSignalSt), "%d", gSignalStrength);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetSignalStrength", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"SignalStrength", szSignalSt);


    APP_LOG("UPNP: Device", LOG_DEBUG,"Get Signal Strength: %s", szSignalSt);
    return UPNP_E_SUCCESS;
}


/**
 * SetServerEnvironment:
 * 	Callback to Set Server Environment IP
 *
 *
 * *****************************************************************************************************************/

int SetServerEnvironment(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char ServerEnvType[SIZE_8B] = {0};
    if (pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"Set Server Environment: paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    char* szserverEnvIPaddr = Util_GetFirstDocumentItem(request, "ServerEnvironment");
    char* szturnserverEnvIPaddr = Util_GetFirstDocumentItem(request, "TurnServerEnvironment");
    char* szserverEnvType = Util_GetFirstDocumentItem(request, "ServerEnvironmentType");

    if (0x00 == strlen(szserverEnvIPaddr)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"ServerEnvironment", "Parameter Error");
        APP_LOG("UPNP: Device", LOG_ERR,"Set Server Environment: parameter error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    if (0x00 == strlen(szturnserverEnvIPaddr)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"TurnServerEnvironment", "Parameter Error");
        APP_LOG("UPNP: Device", LOG_ERR,"Set Turn Server Environment: parameter error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    if (0x00 == strlen(szserverEnvType)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"ServerEnvironmentType", "Parameter Error");
        APP_LOG("UPNP: Device", LOG_ERR,"Set Server Environment: parameter error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "ServerEnvironment", "success");
    UpnpAddToActionResponse(out, "SetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "TurnServerEnvironment", "success");
    UpnpAddToActionResponse(out, "SetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "ServerEnvironmentType", "success");

    /* cleanup old environment & remote settings and destroy nat client sesstion*/
    APP_LOG("UPNP: Device actual", LOG_DEBUG,"server Environment not set \n");
    ExecuteReset(0x03);
    serverEnvIPaddrInit();

    strncpy(g_serverEnvIPaddr, szserverEnvIPaddr, sizeof(g_serverEnvIPaddr)-1);
    strncpy(g_turnServerEnvIPaddr, szturnserverEnvIPaddr, sizeof(g_turnServerEnvIPaddr)-1);
    g_ServerEnvType = (SERVERENV)atoi(szserverEnvType);

    FreeXmlSource(szserverEnvIPaddr);
    FreeXmlSource(szserverEnvType);
    FreeXmlSource(szturnserverEnvIPaddr);

    snprintf(ServerEnvType, sizeof(ServerEnvType), "%d", g_ServerEnvType);
    SetBelkinParameter(CLOUD_SERVER_ENVIRONMENT, g_serverEnvIPaddr);
    SetBelkinParameter(CLOUD_SERVER_ENVIRONMENT_TYPE, ServerEnvType);
    SetBelkinParameter(CLOUD_TURNSERVER_ENVIRONMENT, g_turnServerEnvIPaddr);
    AsyncSaveData();

    return UPNP_E_SUCCESS;

}

/**
 * GetServerEnvironment:
 * 	Callback to Get Server Environment IP
 *
 *
 * *****************************************************************************************************************/
int GetServerEnvironment(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char ServerEnvType[SIZE_8B] = {0};
    if (pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "Get Server Environment: paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    APP_LOG("UPNP: Device", LOG_HIDE, "Server Environment IP is: %s \n turn server IP is: %s \n server environment type: %d \n", g_serverEnvIPaddr, g_turnServerEnvIPaddr, g_ServerEnvType);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"ServerEnvironment", g_serverEnvIPaddr);
    UpnpAddToActionResponse(out, "GetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"TurnServerEnvironment", g_turnServerEnvIPaddr);

    snprintf(ServerEnvType, sizeof(ServerEnvType), "%d", g_ServerEnvType);
    UpnpAddToActionResponse(out, "GetServerEnvironment", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"ServerEnvironmentType", ServerEnvType);


    return UPNP_E_SUCCESS;
}

/**
 *
 *
 *
 *
 *
 ******************************************/
#define	MAX_AP_CLOSE_WAITING_TIME	10
void *CloseApWaitingThread(void *args)
{
    //- Close Setup here

    int k = 0x00;
    int isSetup = 0x00;
    char routerMac[MAX_MAC_LEN];
    char routerssid[MAX_ESSID_LEN];
    tu_set_my_thread_name( __FUNCTION__ );
#ifdef PRODUCT_WeMo_Insight
    //InitOnSetup();
    char SetUpCompleteTS[SIZE_32B];
    memset(SetUpCompleteTS, 0, sizeof(SetUpCompleteTS));
    if(!g_SetUpCompleteTS) {
        g_SetUpCompleteTS = GetUTCTime();
        sprintf(SetUpCompleteTS, "%lu", g_SetUpCompleteTS);
        SetBelkinParameter(SETUP_COMPLETE_TS, SetUpCompleteTS);
        AsyncSaveData();
    }
    APP_LOG("ITC: network", LOG_ERR,"UPnP  updated on setup complete g_SetUpCompleteTS---%lu, SetUpCompleteTS--------%s:", g_SetUpCompleteTS, SetUpCompleteTS);
#endif
    //- Stop WiFi pairing thread if necessary
    StopWiFiPairingTask();
    memset(routerMac, 0, sizeof(routerMac));
    memset(routerssid, 0, sizeof(routerssid));

    while (k++ < MAX_AP_CLOSE_WAITING_TIME) {
        ip_address = NULL;
        ip_address = wifiGetIP(INTERFACE_CLIENT);

        if ((0x01 == getCurrentClientState()) || (0x03 == getCurrentClientState())) {
            isSetup = 0x01;
            break;
        } else {
            APP_LOG("UPNP: setup", LOG_DEBUG, "###### Network not connected yet, how this could be ?########");
            APP_LOG("UPNP: setup", LOG_CRIT, "Network not connected yet, how this could be ?");
        }

        pluginUsleep(1000000);
    }
    if (isSetup) {
        ControlleeDeviceStop();

#ifdef MT7628_AIRPLAY_SUPPORT
        system("iwpriv ra0 set airplayEnable=0");
#endif
        system("ifconfig ra0 down");
        g_ra0DownFlag = 1; //RA0 interface is Down
        ip_address = NULL;

        ip_address = wifiGetIP(INTERFACE_CLIENT);

        if (ip_address && (0x00 != strcmp(ip_address, DEFAULT_INVALID_IP_ADDRESS))) {
            //-Start new UPnP in client AP new address
            APP_LOG("UPNP: Device", LOG_HIDE,"start new UPnP session on %d: %s", g_eDeviceType, ip_address);
            UpdateUPnPNetworkMode(UPNP_INTERNET_MODE);
            //gautam: update the Insight and LS Makefile to copy Insightsetup.xml and Lightsetup.xml in /sbin/web/ as setup.xml
            int ret=ControlleeDeviceStart(GetLanDeviceName(), 0x00, "setup.xml", "/tmp/Belkin_settings");
            if(( ret != UPNP_E_SUCCESS ) && ( ret != UPNP_E_INIT ) ) {
                APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
                APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
                resetSystem();
            }
            getRouterEssidMac (routerssid, routerMac, INTERFACE_CLIENT);

            /* Range Extender fix: Irrespective of device type, start the control point */
            ret=StartPluginCtrlPoint(GetLanDeviceName(), 0x00);
            if(UPNP_E_INIT_FAILED==ret) {
                APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
                APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
                resetSystem();
            }
            EnableContrlPointRediscover(TRUE);

            if(0x00 == atoi(g_szRestoreState)) {
                if(((strlen(g_szHomeId) == 0x0) && (strlen(g_szPluginPrivatekey) == 0x0)) || \
                   ( (strlen(g_routerMac) == 0x00) && (strlen(g_routerSsid) == 0x00) )) {
                    APP_LOG("REMOTEACCESS", LOG_DEBUG, "Remote Access is not Enabled... ");
                } else {
                    if( (strcmp(g_routerMac, routerMac) != 0) && (strlen (g_routerSsid) > 0) ) {
                        APP_LOG("REMOTEACCESS", LOG_DEBUG, "router is not same.. sensor");
                    } else {
                        APP_LOG("REMOTEACCESS", LOG_DEBUG, "Remote Access is Enabled and router is same... \n");
                    }
                }
            }

        } else {
            APP_LOG("UPNP: Device", LOG_ERR,"IP address is not correct");
            CloseApWaiting_thread = -1;
            return (void *)0x01;
        }
    } else {
        APP_LOG("UPNP: Device", LOG_ERR, "Network is not connected, setup not closed");
    }

    AsyncSaveData();
    CloseApWaiting_thread = -1;
    return NULL;
}


/* CloseSetup:
 * 	Close the setup so AP will be dropp and UPnP FINISH and restart again
 *
 *
 *
 *
 *
 *
 *********************************************************************************************************************/
int CloseSetup(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{

    APP_LOG("UPNP: Device", LOG_DEBUG,"%s", __FUNCTION__);
    gAppCalledCloseAp=1;

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "CloseSetup", CtrleeDeviceServiceType[PLUGIN_E_SETUP_SERVICE], "status", "success");

    pthread_create(&CloseApWaiting_thread, NULL, CloseApWaitingThread, NULL);
    APP_LOG("UPNP: Device", LOG_DEBUG, "AP closing in %d seconds .......", MAX_AP_CLOSE_WAITING_TIME);

    return UPNP_E_SUCCESS;
}

void setCurrFWUpdateState(int state)
{
    osUtilsGetLock(&gFWUpdateStateLock);
    currFWUpdateState = state;
    osUtilsReleaseLock(&gFWUpdateStateLock);
    APP_LOG("UPNP",LOG_DEBUG, "currFWUpdateState updated: %d", currFWUpdateState);
}

int getCurrFWUpdateState(void)
{
    int state;
    osUtilsGetLock(&gFWUpdateStateLock);
    state = currFWUpdateState;
    osUtilsReleaseLock(&gFWUpdateStateLock);
    //APP_LOG("UPNP",LOG_DEBUG, "currFWUpdateState updated: %d", state);
    return state;
}


/* UpdateFirmware:
 * 	update firmware notification from APP
 * 	"NewFirmwareVersion"[szVersion]
 *	       "ReleaseDate"[szReleaseDate]
 *                  "URL"[szURL]
 *	       "Signature"[szSignature]
 *
 ********************************************************************************/
int UpdateFirmware(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int state = -1;
    FirmwareUpdateInfo fwUpdInf;

    APP_LOG("UPNP: Device", LOG_CRIT,"%s", __FUNCTION__);

    //-Read out all paramters from APP
    char* szNewFirmwareVersion = Util_GetFirstDocumentItem(request, "NewFirmwareVersion");
    char* szReleaseDate = Util_GetFirstDocumentItem(request, "ReleaseDate");
    char* szURL = Util_GetFirstDocumentItem(request, "URL");
    char* szSignature = Util_GetFirstDocumentItem(request, "Signature");
    char* szDownloadStartTime = Util_GetFirstDocumentItem(request, "DownloadStartTime");
    char* szWithUnsignedImage = Util_GetFirstDocumentItem(request, "WithUnsignedImage");

    if((0x00 == szURL) || (0x00 == strlen(szURL))) {
        APP_LOG("Firmware Update",LOG_ERR, "URL empty, command not executed");
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_E_INVALID_PARAM);
        UpnpAddToActionResponse(out, "UpdateFirmware",
                                CtrleeDeviceServiceType[PLUGIN_E_FIRMWARE_SERVICE], "status", "failure");
        goto on_return;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_E_SUCCESS);
    UpnpAddToActionResponse(out, "UpdateFirmware",
                            CtrleeDeviceServiceType[PLUGIN_E_FIRMWARE_SERVICE], "status", "success");

    state = getCurrFWUpdateState();
    if( (state == FM_STATUS_DOWNLOADING) || (state == FM_STATUS_DOWNLOAD_SUCCESS) ||
        (state == FM_STATUS_UPDATE_STARTING) ) {
        APP_LOG("UPnPApp",LOG_ERR, "************Firmware Update Already in Progress...");
        goto on_return;
    }

    memset(&fwUpdInf, 0x00, sizeof(FirmwareUpdateInfo));

    if(szDownloadStartTime)
        fwUpdInf.dwldStartTime = atoi(szDownloadStartTime)*60; //Seconds

    if((0x00 != szWithUnsignedImage) && (0x00 != strlen(szWithUnsignedImage)))
        fwUpdInf.withUnsignedImage = atoi(szWithUnsignedImage); // 1 = using unsigned image

    strncpy(fwUpdInf.firmwareURL, szURL, sizeof(fwUpdInf.firmwareURL)-1);
    StartFirmwareUpdate(fwUpdInf);

on_return:
    FreeXmlSource(szNewFirmwareVersion);
    FreeXmlSource(szReleaseDate);
    FreeXmlSource(szURL);
    FreeXmlSource(szSignature);
    FreeXmlSource(szDownloadStartTime);

    return UPNP_E_SUCCESS;
}

// Support the firmware update
int EnableSSLOptions(CURL *curl)
{
    //TODO : Check the below code is needed when releasing final f/w image
    char certfile[64] = "/sbin/BuiltinObjectToken-GoDaddyClass2CA.crt";
    char certloc[32] = "/sbin";

    // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    // curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    //char certfile[32] = "ca-certificates.crt";
    //char certloc[32] = "../sbin";

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CERTINFO, 0L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, certfile);
    curl_easy_setopt(curl, CURLOPT_CAPATH, certloc);

    return 0;
}
/* Check if URL is Valid for core/smart product or bulb */
bool isValidDownloadURL(char *pDownloadURL)
{
    CURL *curl;
    bool bValidURL = true;
    int resp = 0;
    long resp_code = 0;
    char *https = NULL;

    curl = curl_easy_init();
    if( !curl ) {
        APP_LOG("UPNP: device", LOG_DEBUG, "isValidDownloadURL: curl initialize error");
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, pDownloadURL);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    https = strstr(pDownloadURL, "https://");

    if( https ) {
        EnableSSLOptions(curl);
    }

    if( CURLE_OK == (resp = curl_easy_perform(curl)) ) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp_code);
        APP_LOG("UPNP: device", LOG_DEBUG, "isValidDownloadURL: resp_code = %ld", resp_code);

        if( resp_code < 500 && resp_code >= 400 ) {
            bValidURL = false;
        }
    } else {
        APP_LOG("UPNP: device", LOG_DEBUG, "isValidDownloadURL: curl_easy_perform error = %d", resp);

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp_code);
        APP_LOG("UPNP: device", LOG_DEBUG, "isValidDownloadURL: resp_code = %ld", resp_code);
    }

    curl_easy_cleanup(curl);

    return bValidURL;
}

int DownLoadFirmware(const char *FirmwareURL, int deferWdLogging, int withUnsigned)
{
    int state=0;
    char firmwareURL[MAX_FW_URL_LEN];
    bool validURL=false;
    int retVal = FAILURE;
#if defined(PRODUCT_WeMo_LightV2)
    SetWiFiLED(RGB_FIRMWARE_UPDATE);
#elif defined(PRODUCT_WeMo_SNSV2)
    SetWiFiLED(0x00);
#endif
    gFwDownloadTimeStamp = GetUTCTime();
    FirmwareUpdateStatusNotify(FM_STATUS_DOWNLOADING);
    setCurrFWUpdateState(FM_STATUS_DOWNLOADING);
    RemoteFirmwareUpdateStatusNotify();
    state = getCurrFWUpdateState();
    APP_LOG("UPNP: Device", LOG_DEBUG,"******** current firmware update state is:%d type:%d", state, withUnsigned);

    memset(firmwareURL, 0x0, sizeof(firmwareURL));
    strncpy(firmwareURL, FirmwareURL, sizeof(firmwareURL)-1);
    strncat(firmwareURL, "?mac=", sizeof(firmwareURL)-strlen(firmwareURL)-1);
    strncat(firmwareURL, g_szWiFiMacAddress, sizeof(firmwareURL)-strlen(firmwareURL)-1);

    validURL= isValidDownloadURL(firmwareURL);
    if(validURL == true) {
        APP_LOG("Firmware", LOG_DEBUG,"Validation of firmware download URL passed.");
        if(!deferWdLogging) {//Log the event in the WDLogFile only once
            APP_LOG("UPNP: Device", LOG_CRIT, "Starting firmware download ......[%s]", firmwareURL);
        } else {
            APP_LOG("UPNP: Device", LOG_DEBUG, "Starting firmware download ......[%s]", firmwareURL);
        }
        retVal = webAppFileDownload(firmwareURL, "/tmp/firmware.bin.gpg");
    } else {
        APP_LOG("Firmware", LOG_ERR,"Validation of firmware download URL failed.");
        UnSetBelkinParameter("FirmwareUpURL");
    }
    if (retVal != SUCCESS) {
        APP_LOG("Firmware", LOG_ERR,"Download firmware image failed");
    } else {
        APP_LOG("Firmware", LOG_CRIT,"Downloaded firmware image from location %s successfully\n", firmwareURL);
    }

    return retVal;
}

/* GetFirmwareVersion:
 * 	Firmware version request from APP
 *
 ********************************************************************************/
int GetFirmwareVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szSkuNumber[SIZE_32B];
    char szResponse[SIZE_128B];
    memset(szSkuNumber, 0x00, sizeof(szSkuNumber));
    memset(szResponse, 0x00, sizeof(szResponse));

    char* szPreviousSkuNo   = GetBelkinParameter("SkuNo");

    if (0x00 == szPreviousSkuNo || 0x00 == strlen(szPreviousSkuNo)) {
        snprintf(szSkuNumber, sizeof(szSkuNumber), "%s", DEFAULT_SKU_NO);
    } else {
        snprintf(szSkuNumber, sizeof(szSkuNumber), "%s", szPreviousSkuNo);
    }

    snprintf(szResponse, sizeof(szResponse), "FirmwareVersion:%s|SkuNo:%s", g_szBuiltFirmwareVersion, szSkuNumber);

    APP_LOG("UPNP: Device", LOG_DEBUG, "Firmware:%s", szResponse);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);


    UpnpAddToActionResponse(out, "GetFirmwareVersion", CtrleeDeviceServiceType[PLUGIN_E_FIRMWARE_SERVICE],
                            "FirmwareVersion", szResponse);



    return UPNP_E_SUCCESS;
}
//-----------------TODO: tmp here ----------------------------------

void FirmwareUpdate_AsyncUpdateNotify()
{
    pMessage msg = createMessage(META_FIRMWARE_UPDATE, 0x00, 0x00);
    SendMessage2App(msg);
}

void *updateMonitorCheckTh (void *args)
{
    APP_LOG("UPnPApp",LOG_ERR, "************Firmware Update Check Monitor thread Created");
    pluginUsleep(180000000);	// 3 mins
    APP_LOG("UPnPApp",LOG_ALERT, "************Firmware Update Check Monitor thread rebooting system...");
    system("reboot");
    return NULL;
}

void *firmwareUpdateTh(void *args)
{
    int status = FAILURE, state = 0, rect = 0;
    char *fwUpStr = NULL;
    FirmwareUpdateInfo fwUpdInf;
    int deferWdLogging = 0;
    FILE *CheckMemInfo = NULL;
    char * memThrshold = NULL;
    tu_set_my_thread_name( __FUNCTION__ );

    if(args) {
        memcpy(&fwUpdInf, args, sizeof(FirmwareUpdateInfo));
        free(args);
        args = NULL;
    }

    APP_LOG("UPnPApp",LOG_ERR, "**** Firmware Update thread Created with URL: %s ****", fwUpdInf.firmwareURL);

    gStopDownloadFW = 0;	//reset stop FW download flag used by httpwrapper curl
    memThrshold = GetBelkinParameter("FW_UPGRD_MemThr");
    if(memThrshold && (strlen(memThrshold) > 0)) {
        bool checkMemory = true;
        int count = 0;
        char * upgdCount = GetBelkinParameter("FW_UPGRD_COUNTER");
        if(NULL == upgdCount || 0 == strlen(upgdCount)) {
            SetBelkinParameter("FW_UPGRD_COUNTER", "0");
        } else {
            count = atoi(upgdCount);
            /* if the FW_UPGRD_COUNTER reaches to MAX_FW_UPGRD_RETRY_COUNT,
               avoid memory check before update */
            if(MAX_FW_UPGRD_RETRY_COUNT <= count) {
                UnSetBelkinParameter("FW_UPGRD_COUNTER");
                checkMemory = false;
                APP_LOG("UPNP: Device", LOG_DEBUG, "Skipping memory check");
            }
        }
        if(checkMemory) {
            CheckMemInfo = popen("cat /proc/meminfo | grep MemFree","r");
            if (CheckMemInfo != NULL) {
                char Label[16];
                int memoryFree = 0;
                int skip=0;

                if(fscanf(CheckMemInfo,"%16s %d %*s",Label,&memoryFree) == 2) {
                    APP_LOG("UPnPApp",LOG_DEBUG, "%s %d",Label,memoryFree);
                } else {
                    /* skip the memory check */
                    skip=1;
                }
                APP_LOG("UPnPApp",LOG_ERR, "**** Firmware Update thread found Free Memory %d needed by system is %s****", memoryFree,memThrshold);
                if(!skip &&  (memoryFree < atoi(memThrshold))) {
                    char buf[SIZE_8B];
                    memset(buf, 0x0, SIZE_8B);
                    snprintf(buf, SIZE_8B, "%d", count+1);
                    SetBelkinParameter("FW_UPGRD_COUNTER", buf);
                    resetSystem();
                }
                pclose(CheckMemInfo);
            }
        }
    }

    status  = DownLoadFirmware(fwUpdInf.firmwareURL, deferWdLogging, fwUpdInf.withUnsignedImage);

    if (status == SUCCESS) {
        setCurrFWUpdateState(FM_STATUS_DOWNLOAD_SUCCESS);
        state = getCurrFWUpdateState();
        APP_LOG("UPNP: Device", LOG_DEBUG,"******** current firmware update state is:%d", state);
        FirmwareUpdate_AsyncUpdateNotify();
        RemoteFirmwareUpdateStatusNotify();
        setCurrFWUpdateState(FM_STATUS_UPDATE_STARTING);

        APP_LOG("UPNP: Device", LOG_DEBUG,"Unsetting the firmwareUpdate flag");
        RemoteFirmwareUpdateStatusNotify();
        pluginUsleep(1000000);
        AsyncSaveData();
        /* wait for remote notification thread to send out the notifications for download success & upgrade start */
        pluginUsleep(6*1000000);
        APP_LOG("UPNP: Device", LOG_DEBUG,"firmwareUpdate continuing after sleep");

#if defined(PRODUCT_WeMo_Insight)
        Update30MinDataOnFlash();
#endif

        state = getCurrFWUpdateState();
        APP_LOG("UPNP: Device", LOG_DEBUG,"******** current firmware update state is:%d", state);
        UnSetBelkinParameter("FirmwareUpURL");
        UnSetBelkinParameter("FirmwareUpUnsigned");
#ifdef __OLDFWAPI__
        rect = Firmware_Update("/tmp/firmware.bin.gpg");
#else
        APP_LOG("UPNP: Device", LOG_DEBUG,"******** current firmware update state is:%d-%d", state, fwUpdInf.withUnsignedImage);
        if (fwUpdInf.withUnsignedImage) {
            rect = New_Firmware_Update("/tmp/firmware.bin.gpg", 0);
        } else {
            rect = New_Firmware_Update("/tmp/firmware.bin.gpg", 1);
        }
#endif

        if (0x00 != rect) {
            system("rm -f /tmp/firmware.bin.gpg");
            system("rm -f /tmp/firmware.img");
            APP_LOG("UPNP: Device", LOG_ALERT, "Gemtek API Firmware_Update called failure");

            firmwareUpThread = -1;
            system("reboot");
        }
    } else {
        deferWdLogging++;
        setCurrFWUpdateState(FM_STATUS_DOWNLOAD_UNSUCCESS);
        state = getCurrFWUpdateState();
        APP_LOG("UPNP: Device", LOG_DEBUG,"Current firmware update state is:%d", state);
        APP_LOG("UPNP: Device", LOG_ERR, "Firmware download failure");
#if defined(PRODUCT_WeMo_LightV2)
        SetWiFiLED(RGB_SWITCH_OFF);
#elif defined(PRODUCT_WeMo_SNSV2)
        SetWiFiLED(0x04);
#endif
        FirmwareUpdateStatusNotify(FM_STATUS_DOWNLOAD_UNSUCCESS);
        RemoteFirmwareUpdateStatusNotify();
        gFwDownloadTimeStamp = 0;
    }

    fwUpStr = GetBelkinParameter("FirmwareUpURL");
    firmwareUpThread=-1;

    if(fwUpStr && strlen(fwUpStr) != 0) {
        /* Wait for 1 Sec so that Thread FirmwareUpdateStart is finished */
        pluginUsleep(1000000);
        /* no staggered download required due to restart of download */
        APP_LOG("UPNP", LOG_DEBUG,"Setting the staggered time to 0 before restarting the update.");
        fwUpdInf.dwldStartTime = 0;
        StartFirmwareUpdate(fwUpdInf);
    }

    return NULL;
}

static SStateQueue *gStateQHead = NULL, *gStateQTail = NULL;
static pthread_attr_t sNotifyThread_attr;
static pthread_t sNotifyThread = -1;
static pthread_mutex_t gStateNotifyLock;

void initSensorStateQueueLock(void)
{
    osUtilsCreateLock(&gStateNotifyLock);
}

void LockStateQueue(void)
{
    osUtilsGetLock(&gStateNotifyLock);
}

void UnLockStateQueue(void)
{
    osUtilsReleaseLock(&gStateNotifyLock);
}

SStateQueue* dequeueState (void)
{
    SStateQueue* qNode = NULL;

    LockStateQueue();
    qNode = gStateQHead;;
    if(NULL != gStateQHead) {
        if(gStateQHead == gStateQTail) {
            gStateQHead = NULL;
            gStateQTail = NULL;
        } else {
            gStateQHead = gStateQHead->next;
        }
        APP_LOG("UPNP:state",LOG_DEBUG,"dequeued sensor state is: %d", qNode->state);
    }
    UnLockStateQueue();

    return qNode;
}

void* sensorNotifyThread(void* arg)
{
    SStateQueue *pStateQ=NULL;
    int state = -1;

    while(1) {
        pStateQ = dequeueState();
        if(!pStateQ) {
            pluginUsleep(1000000);
            continue;
        }

        state = pStateQ->state;
        if(state) {
            executeSensorRule();
            executeNotifyRule();
            LocalBinaryStateNotify(SENSORING_ON);
        } else {
            LocalBinaryStateNotify(SENSORING_OFF);
        }

        if(pStateQ) {
            free(pStateQ);
            pStateQ = NULL;
        }
    }

    sNotifyThread = -1;
    pthread_exit(0);
    return NULL;
}

void enqueueStateQueue(SStateQueue *qNode)
{
    if(!qNode) {
        APP_LOG("DeviceControl", LOG_DEBUG,"sensor notify thread: enqueue node null");
        return;
    }

    LockStateQueue();
    if(gStateQHead == NULL) {
        gStateQHead = qNode;
        gStateQTail = gStateQHead;
    } else {
        gStateQTail->next = qNode;
        gStateQTail = qNode;
    }
    UnLockStateQueue();
    APP_LOG("DeviceControl", LOG_DEBUG,"sensor notify thread: node enqueued");
}

void enqueueState (int state)
{
    SStateQueue* qNode = NULL;

    qNode = (SStateQueue*)CALLOC(1, sizeof(SStateQueue));
    qNode->state = state;
    /* TS value is required to keep motion sense time synced in remote mode, which may be required to sync in future
    Curretly, keeping TS value set to zero, as we are not using it now, however when it'll be in use then we need UTC seconds */
    //qNode->ts = GetUTCTime();
    qNode->ts = 0;

    APP_LOG("DeviceControl", LOG_DEBUG,"sensor notify thread: enqueue node: %d|%lu", qNode->state, qNode->ts);

    enqueueStateQueue(qNode);
}

void createSensorNotifyThread(int state)
{
    enqueueState(state);
    if(sNotifyThread != -1) {
        APP_LOG("DeviceControl", LOG_DEBUG,"sensor notify thread already running");
        return;
    }

    APP_LOG("DeviceControl", LOG_DEBUG,"Start sensor notify thread");

    pthread_attr_init(&sNotifyThread_attr);
    pthread_attr_setdetachstate(&sNotifyThread_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&sNotifyThread, &sNotifyThread_attr, sensorNotifyThread, NULL);

}

void NoMotionSensorInd()
{
    if (!IsUPnPNetworkMode())
        return;
    createSensorNotifyThread(SENSORING_OFF);
}



void MotionSensorInd()
{
    if (!IsUPnPNetworkMode())
        return;

    createSensorNotifyThread(SENSORING_ON);
}

void OverTempStateNotify(int state)
{
    if (device_handle == -1) {
        return;
    }

    if (!IsUPnPNetworkMode()) {
        //- Not report since not on router or internet
        return;
    }

    char* szState[1]= {0};
    szState[0] = (char *) MALLOC(SIZE_2B+1);
    snprintf(szState[0x00], SIZE_2B, "%d", state);

    char* paramters[] = {"OverTemp"} ;

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)szState, 0x01);

    APP_LOG("UPNP: Device", LOG_DEBUG, "Notification: OverTemp: state: %d %s", state,szState[0]);
    free(szState[0x00]);
}

void NameChangeNotify(char *name)
{
    char* parameters[] = {"FriendlyName"} ;
    char *changed_name[1] = {0};

    if (name == NULL) {
        return;
    }

    if (device_handle == -1) {
        return;
    }

    changed_name[0] = (char *) MALLOC(64);
    strcpy(changed_name[0], name);

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)parameters, (const char **)changed_name, 1);

    free(changed_name[0]);
}

#ifdef LONG_PRESS_SUPPORTED
void LongPressRuleNotify()
{
    char *parameters[] = {"longPressRuleDeviceCnt", "longPressRuleAction",
                          "longPressRuleState", "longPressRuleDeviceUdn"};
    char *values[4] = {0};
    char count[4] = {0};
    char action[4] = {0};
    char state[4] = {0};
    char *udns = NULL;

    if (gpsLongPressRule == NULL) {
        return;
    }

    snprintf(count, 4, "%d", gpsLongPressRule->count);
    snprintf(action, 4, "%d", gpsLongPressRule->action);
    snprintf(state, 4, "%d", gpsLongPressRule->state);
    if (gpsLongPressRule->pszUDNList) {
        udns = strdup(gpsLongPressRule->pszUDNList);
    }

    values[0] = count;
    values[1] = action;
    values[2] = state;
    values[3] = udns;

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].ServiceId,
               (const char **)parameters,
               (const char **)values, 4);

    APP_LOG("UPNP: LongPressRuleNotify", LOG_DEBUG, "Notification count: %s action: %s state :%s UDNs: %s",
            count, action, state, udns);
    return;
}
#endif

//---------- Button Status change notify -------
void LocalUserActionNotify(int curState)
{
    return;
    if (device_handle == -1) {
        return;
    }

    if (!IsUPnPNetworkMode()) {
        //- Not report since not on router or internet
        return;
    }

    char* szCurState[1];
    szCurState[0x00] = (char*)MALLOC(SIZE_2B+1);
    snprintf(szCurState[0x00], SIZE_2B, "%d", curState);

    char* paramters[] = {"UserAction"} ;

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)szCurState, 0x01);

    APP_LOG("UPNP: Device", LOG_DEBUG, "Notification: UserAction: state: %d", curState);

    free(szCurState[0x00]);

}


//---------- Button Status change notify -------
void LocalBinaryStateNotify(int curState)
{
    if (device_handle == -1) {
        return;
    }

    if (!IsUPnPNetworkMode()) {
        //- Not report since not on router or internet
        APP_LOG("UPNP", LOG_DEBUG, "Notification:BinaryState: Not in home network, ignored");
        return;
    }

    char* szCurState[1];
#ifdef PRODUCT_WeMo_Insight
    if ((0x00 == curState) || (0x01 == curState) || (0x08 == curState)) {
        StateChangeTimeStamp(curState);
    }
    szCurState[0x00] = (char*)ZALLOC(SIZE_100B);
    if (g_InitialMonthlyEstKWH) {
        snprintf(szCurState[0x00], SIZE_100B, "%d|%u|%u|%u|%u|%u|%u|%u|%u|%d", curState, g_StateChangeTS, g_ONFor,
                 g_TodayONTimeTS, g_TotalONTime14Days, g_HrsConnected, g_AvgPowerON, g_PowerNow, g_AccumulatedWattMinute, g_InitialMonthlyEstKWH);
    } else {
        snprintf(szCurState[0x00], SIZE_100B, "%d|%u|%u|%u|%u|%u|%u|%u|%u|%0.f", curState, g_StateChangeTS, g_ONFor,
                 g_TodayONTimeTS, g_TotalONTime14Days, g_HrsConnected, g_AvgPowerON, g_PowerNow, g_AccumulatedWattMinute, g_KWH14Days);
    }
    APP_LOG("UPNP", LOG_DEBUG, "Local Binary State Insight Parameters: %s", szCurState[0x00]);
#else
    szCurState[0x00] = (char*)ZALLOC(SIZE_32B+1);
    if (NULL == szCurState[0x00]) {
        APP_LOG ("UPNP", LOG_DEBUG, "Malloc Error");
        return;
    }
    snprintf (szCurState[0x00], SIZE_32B, "%d", curState);
    APP_LOG ("UPNP", LOG_DEBUG, "Local Binary State Parameters: %s", szCurState[0x00]);
#endif
    char* paramters[] = {"BinaryState"};
    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)szCurState, 0x01);
    APP_LOG("UPNP", LOG_DEBUG, "Notification:BinaryState:state: %d", curState);

    free(szCurState[0x00]);
}

void LocalCountdownTimerNotify()
{
    if (!IsUPnPNetworkMode() || (device_handle == -1)) {
        return;
    }

    char value[SIZE_64B] = {0,};
    char curtime[SIZE_64B] = {0,};
    char state[SIZE_4B] = {0,};
    unsigned long currentTime=0;
    char* valueSet[3];
    char* paramters[] = {"BinaryState", "CountdownEndTime", "deviceCurrentTime"};

    int status = GetCurBinaryState();
    /* BinaryState is needed alongwith CountdownEndTime for the app to
       reflect proper status. When timer is not in last minute, the
       End time zero always represents a binary state 0. So POWER_OFF
       for that case and current binary state for other cases. */
    if(0 == gCountdownEndTime && 0 == gCountdownRuleInLastMinute)
        snprintf(state, sizeof(state), "%d", POWER_OFF);
    else
        snprintf(state, sizeof(state), "%d", status);
    currentTime = GetUTCTime();
    snprintf(value, SIZE_64B, "%lu", gCountdownEndTime);
    snprintf(curtime, SIZE_64B, "%lu", currentTime);

    valueSet[0] = state;
    valueSet[1] = value;
    valueSet[2] = curtime;

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)valueSet, 0x03);

    APP_LOG("UPNP: Device", LOG_DEBUG, "Notification: binary state: %d countdown End Time: %lu, device current time: %lu ", status, gCountdownEndTime, currentTime);
}

//-------------------------------------------------------- Rule --------------------------------------------
//----------------------------------------- Rules related -------------------------------------------------
/*
 *
 *
 *
 *
 *
 *
 *
 **********************************************************/
void RuleOverrideNotify(int state)
{
    if (device_handle == -1) {
        return;
    }

    if (!IsUPnPNetworkMode()) {
        //- Not report since not on router or internet
        return;
    }

    char* szState[1]= {0};
    szState[0] = (char *) MALLOC(SIZE_2B+1);
    snprintf(szState[0x00], SIZE_2B, "%d", state);

    char* paramters[] = {"RuleOverrideStatus"} ;

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)szState, 0x01);

    APP_LOG("UPNP: Device", LOG_DEBUG, "Notification: RuleOverride: state: %d %s", state,szState[0]);
    free(szState[0x00]);
}

int AddRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s called", __FUNCTION__);

    char* szName 		= Util_GetFirstDocumentItem(request, "Name");
    char* szType		= Util_GetFirstDocumentItem(request, "Type");
    char* szIsEnable 	= Util_GetFirstDocumentItem(request, "IsEnable");

    char* szMon 		= Util_GetFirstDocumentItem(request, "Mon");
    char* szTues 		= Util_GetFirstDocumentItem(request, "Tues");
    char* szWed 		= Util_GetFirstDocumentItem(request, "Wed");
    char* szThurs 	= Util_GetFirstDocumentItem(request, "Thurs");
    char* szFri 		= Util_GetFirstDocumentItem(request, "Fri");
    char* szSat 		= Util_GetFirstDocumentItem(request, "Sat");
    char* szSun 		= Util_GetFirstDocumentItem(request, "Sun");

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "AddRule", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE], "status", "success");

    FreeXmlSource(szName);
    FreeXmlSource(szType);
    FreeXmlSource(szIsEnable);
    FreeXmlSource(szMon);
    FreeXmlSource(szTues);
    FreeXmlSource(szWed);
    FreeXmlSource(szThurs);
    FreeXmlSource(szFri);
    FreeXmlSource(szSat);
    FreeXmlSource(szSun);

    return UPNP_E_SUCCESS;
}

int EditRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s called", __FUNCTION__);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "EditRule", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE], "status", "success");
    return UPNP_E_SUCCESS;
}

int RemoveRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s called", __FUNCTION__);
    return UPNP_E_SUCCESS;
}

int EnableRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s called", __FUNCTION__);
    return UPNP_E_SUCCESS;
}
int DisableRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    return UPNP_E_SUCCESS;
}
int GetRules(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s called", __FUNCTION__);
    UpnpAddToActionResponse(out, "EditRule", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE], "GetRules", "success");
    return UPNP_E_SUCCESS;
}

#if defined(PRODUCT_WeMo_Insight) || defined(PRODUCT_WeMo_SNS)

int SetRuleID(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "Do nothing and return");

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SetRuleID",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"RuleID" , "success");
    return UPNP_E_SUCCESS;
}

int DeleteRuleID(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNP: Device", LOG_DEBUG, "Do nothing and return");
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "DeleteRuleID",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"RuleID" , "success");
    return UPNP_E_SUCCESS;
}

#endif

#if defined(PRODUCT_WeMo_Light) && !defined(PRODUCT_WeMo_Dimmer)

int changeNightLightStatus(char *DimValue)
{
    int IntDimVal=0, retVal=SUCCESS;

    SetBelkinParameter(DIMVALUE, DimValue);
    AsyncSaveData();
    IntDimVal = atoi(DimValue);
    APP_LOG("UPNPDevice", LOG_DEBUG, "Changing Night Light With DimValue: %d",IntDimVal);
    retVal = ChangeNightLight(IntDimVal);

    if(!retVal && IntDimVal==1) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "DimNightLight: ...........Rebooting the System for Diming the Night Light");
        AsyncRebootSystem();
    }

    return retVal;
}

void AsyncRebootSystem(void)
{
    pMessage msg = createMessage(NIGHTLIGHT_DIMMING_MESSAGE_REBOOT, 0x00, 0x00);
    SendMessage2App(msg);
}

int SetNightLightStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int retVal=UPNP_E_SUCCESS;
    APP_LOG("UPNP: DimNightLight", LOG_DEBUG, "%s called", __FUNCTION__);
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "DimNightLight: command paramter invalid");
        return UPNP_E_INVALID_ARGUMENT;
    }
    char* DimValue = Util_GetFirstDocumentItem(request, "DimValue");
    if (0x00 == DimValue || 0x00 == strlen(DimValue)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);
        APP_LOG("UPNPDevice", LOG_DEBUG, "DimNightLight: No DimValue");
        UpnpAddToActionResponse(out, "DimNightLight",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "DimValue", "Error");

        retVal=UPNP_E_INVALID_ARGUMENT;
    }
    if(UPNP_E_SUCCESS == retVal) {
        retVal = changeNightLightStatus(DimValue);
        if(!retVal) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            UpnpAddToActionResponse(out, "DimNightLight",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],"DimValue" , "success");
        } else {
            UpnpActionRequest_set_ErrCode(pActionRequest, 1);
            APP_LOG("UPNPDevice", LOG_DEBUG, "DimNightLight: ChangeNightLight Failed");
            UpnpAddToActionResponse(out, "DimNightLight",
                                    CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "DimValue", "Error");
        }
    }
    FreeXmlSource(DimValue);
    return retVal;
}

int GetNightLightStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szResponse[SIZE_256B];
    memset(szResponse, 0x00, sizeof(szResponse));
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s", __FUNCTION__);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    APP_LOG("UPNP: GetDimNightLight", LOG_DEBUG, "%s called", __FUNCTION__);
    char *dimVal = GetBelkinParameter (DIMVALUE);
    APP_LOG("UPNP: GetDimNightLight", LOG_DEBUG, "DimValue in Flash: %s ",dimVal);

    snprintf(szResponse, sizeof(szResponse), "%s", dimVal);
    UpnpAddToActionResponse(out, "GetDimNightLight", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                            "DimValue", szResponse);

    return UPNP_E_SUCCESS;
}
#endif

void PopulatePluginParams(int DeviceType)
{
    int hr=0, min=0, sec=0;
    memset(gPluginParms.UpTime, 0x0, SIZE_64B);
    memset(gPluginParms.DeviceInfo, 0x0, SIZE_128B);

    detectUptime(&hr, &min, &sec);
    snprintf(gPluginParms.UpTime, sizeof(gPluginParms.UpTime), "%d:%d:%d", hr, min, sec);
#ifdef PRODUCT_WeMo_Insight
    char *paramVersion = NULL,*paramPerUnitcost = NULL,*paramCurrency = NULL;
#endif
    gPluginParms.Internet = getCurrentClientState();
    gPluginParms.LastAuthVal=gLastAuthVal;//setting in API webAppErrorHandling
    gPluginParms.LastFWUpdateStatus=getCurrFWUpdateState();
    gPluginParms.NowTime=(int) GetUTCTime();
    gPluginParms.HomeID=g_szHomeId;
    gPluginParms.DeviceID=g_szWiFiMacAddress;

    switch(DeviceType) {
    case DEVICE_SOCKET:
        snprintf(gPluginParms.DeviceInfo, sizeof(gPluginParms.DeviceInfo), "%s","Socket");
        break;
    case DEVICE_SENSOR:
        snprintf(gPluginParms.DeviceInfo, sizeof(gPluginParms.DeviceInfo), "%s","Sensor");
        break;
    case DEVICE_INSIGHT:
#ifdef PRODUCT_WeMo_Insight
        paramVersion = GetBelkinParameter(ENERGYPERUNITCOSTVERSION);
        paramPerUnitcost = GetBelkinParameter(ENERGYPERUNITCOST);
        paramCurrency   = GetBelkinParameter(CURRENCY);
        snprintf(gPluginParms.DeviceInfo, sizeof(gPluginParms.DeviceInfo), "%s|%s|%s|%s|%d|%d|%d","Insight",
                 paramVersion,paramPerUnitcost,paramCurrency,g_PowerThreshold,g_EventEnable,g_s32DataExportType);
#endif
        break;
    case DEVICE_LIGHTSWITCH:
        snprintf(gPluginParms.DeviceInfo, sizeof(gPluginParms.DeviceInfo), "%s","Lightswitch");
        break;
    case DEVICE_LIGHTSWITCHV2:
        snprintf(gPluginParms.DeviceInfo, sizeof(gPluginParms.DeviceInfo), "%s","LightswitchV2");
        break;
    case DEVICE_LIGHTSWITCH3WAY:
        snprintf(gPluginParms.DeviceInfo, sizeof(gPluginParms.DeviceInfo), "%s","Lightswitch3Way");
        break;
    case DEVICE_DIMMER:
        snprintf(gPluginParms.DeviceInfo, sizeof(gPluginParms.DeviceInfo), "%s","Dimmer");
        break;
    default:
        break;
    }

}
//------------------------------------------ Extended Meta info -----------------
int GetExtMetaInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szResponse[SIZE_256B];
    memset(szResponse, 0x00, sizeof(szResponse));
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s", __FUNCTION__);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);


    {
#ifdef PRODUCT_WeMo_Light
                if ((DEVICE_LIGHTSWITCH == g_eDeviceTypeTemp) ||
                    (DEVICE_LIGHTSWITCHV2 == g_eDeviceTypeTemp) ||
                    (DEVICE_LIGHTSWITCH3WAY == g_eDeviceTypeTemp)) {

                    PopulatePluginParams(g_eDeviceTypeTemp);
                } else
#endif
#ifdef PRODUCT_WeMo_Insight
                    if(DEVICE_INSIGHT == g_eDeviceTypeTemp) {

                        PopulatePluginParams(g_eDeviceTypeTemp);

                    } else
#endif
#ifdef PRODUCT_WeMo_Dimmer
                        if (DEVICE_DIMMER == g_eDeviceTypeTemp) {

                            PopulatePluginParams(g_eDeviceTypeTemp);
                        } else
#endif

                        {
                            PopulatePluginParams(g_eDeviceType);
                        }
    }


    snprintf(szResponse, sizeof(szResponse), "%d|%d|%d|%d|%s|%d|%d|%s|%d|%s",
             gPluginParms.Internet,gPluginParms.CloudVia,
             gPluginParms.CloudConnectivity,gPluginParms.LastAuthVal,gPluginParms.UpTime,
             gPluginParms.LastFWUpdateStatus,gPluginParms.NowTime,gPluginParms.HomeID,
             gPluginParms.RemoteAccess,gPluginParms.DeviceInfo);
    APP_LOG("UPNP: Device", LOG_DEBUG, "ExtMetaInfo:%s", szResponse);
    UpnpAddToActionResponse(out, "GetExtMetaInfo", CtrleeDeviceServiceType[PLUGIN_E_METAINFO_SERVICE],
                            "ExtMetaInfo", szResponse);

    return UPNP_E_SUCCESS;
}

//------------------------------------------ Meta info -----------------
int GetMetaInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char szResponse[SIZE_256B];
    memset(szResponse, 0x00, sizeof(szResponse));
    APP_LOG("UPNP: Device", LOG_DEBUG, "%s", __FUNCTION__);
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    switch(g_eDeviceTypeTemp) {

#ifdef PRODUCT_WeMo_Light
    case DEVICE_LIGHTSWITCH:
        snprintf(szResponse, sizeof(szResponse), "%s|%s|%s|%s|%s|%s",
                 g_szWiFiMacAddress, g_szSerialNo, g_szSkuNo, g_szFirmwareVersion,
                 g_szApSSID, "Lightswitch");
        break;
    case DEVICE_LIGHTSWITCHV2:
        snprintf(szResponse, sizeof(szResponse), "%s|%s|%s|%s|%s|%s",
                 g_szWiFiMacAddress, g_szSerialNo, g_szSkuNo, g_szFirmwareVersion,
                 g_szApSSID, "LightswitchV2");
        break;
    case DEVICE_LIGHTSWITCH3WAY:
        snprintf(szResponse, sizeof(szResponse), "%s|%s|%s|%s|%s|%s",
                 g_szWiFiMacAddress, g_szSerialNo, g_szSkuNo, g_szFirmwareVersion,
                 g_szApSSID, "Lightswitch3Way");
        break;
#endif
#ifdef PRODUCT_WeMo_Insight
    case DEVICE_INSIGHT:
        snprintf(szResponse, sizeof(szResponse), "%s|%s|%s|%s|%s|%s",
                 g_szWiFiMacAddress, g_szSerialNo, g_szSkuNo, g_szFirmwareVersion,
                 g_szApSSID, "Insight");
        break;
#endif

#ifdef PRODUCT_WeMo_Dimmer
    case DEVICE_DIMMER:
        snprintf(szResponse, sizeof(szResponse), "%s|%s|%s|%s|%s|%s",
                 g_szWiFiMacAddress, g_szSerialNo, g_szSkuNo, g_szFirmwareVersion,
                 g_szApSSID, "Dimmer");
        break;
#endif
    default:
        snprintf(szResponse, sizeof(szResponse), "%s|%s|%s|%s|%s|%s",
                 g_szWiFiMacAddress, g_szSerialNo, g_szSkuNo, g_szFirmwareVersion,
                 g_szApSSID, (g_eDeviceType == DEVICE_SENSOR)? "Sensor":"Socket");
    }

    APP_LOG("UPNP: Device", LOG_DEBUG, "%s", szResponse);

    UpnpAddToActionResponse(out, "GetMetaInfo", CtrleeDeviceServiceType[PLUGIN_E_METAINFO_SERVICE],
                            "MetaInfo", szResponse);

    return UPNP_E_SUCCESS;
}

char* CreateManufactureData()
{
    mxml_node_t *pRespXml = NULL;
    mxml_node_t *pNodeRoot = NULL;
    mxml_node_t *pNode = NULL;
#if defined(PRODUCT_WeMo_LightV2) || defined(PRODUCT_WeMo_Insight)
    mxml_node_t *pSubNode = NULL;
    char text[32] = {0};
#endif
    char *pszResp = NULL;
    char *pSerialNumber = NULL;
    FILE *fp = NULL;

#ifdef PRODUCT_WeMo_Insight
    DataValues Values = {0,0,0,0,0};
    int Ret = 0;
#endif

    char *pCountryCode   = GetBelkinParameter ("country_code");
    //char *pFirmwareVer   = GetBelkinParameter ("WeMo_version");
    char *pFirmwareVer   = g_szFirmwareVersion;
    char *pAPMacAddress  = GetBelkinParameter ("wl0_macaddr");
    char *pTargetCountry = GetBelkinParameter ("target_country");
    char ucaMacAddress[SIZE_20B] = {0};
    struct ifreq  s;
    char *pSTAMacAddress = ucaMacAddress;
    int fd = socket (PF_INET, SOCK_DGRAM, IPPROTO_IP);

    pRespXml = mxmlNewXML("1.0");
    pNodeRoot = mxmlNewElement(pRespXml, "ManufactureData");

#ifdef __MIPSEL__
    strcpy (s.ifr_name, "apcli0");  // <-----<<< DANGER
#else
    strcpy (s.ifr_name, "br-lan");
#endif
    if (0 == ioctl (fd, SIOCGIFHWADDR, &s)) {
        const unsigned char* mac = (unsigned char *) s.ifr_hwaddr.sa_data;
        snprintf (ucaMacAddress, sizeof (ucaMacAddress),
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strncpy (ucaMacAddress, "00:00:00:00:00:00", sizeof (ucaMacAddress));
    }
#ifdef __MIPSEL__
    APP_LOG ("UPNP: Device", LOG_DEBUG, "&&**&& apcli0 MacAddress = %s &&**&&", \
             pSTAMacAddress);
#else
    APP_LOG ("UPNP: Device", LOG_DEBUG, "&&**&& br-lan MacAddress = %s &&**&&", \
             pSTAMacAddress);
#endif

#ifdef PRODUCT_WeMo_Insight
    /*Reading Instantaneous Power from daemon*/
    if((Ret = HAL_GetCurrentReadings(&Values)) != 0) {
        APP_LOG("UPNP: Device", LOG_DEBUG, "\nMetering Daemon Not Responding!!!\n");
    } else {
        APP_LOG("UPNP: Device", LOG_DEBUG, "\nRead Instantaneous Power values from daemon %d && %f\n",Values.vRMS,(float)(Values.vRMS/1000));

    }
#endif
    pSerialNumber = GetBelkinParameter("SerialNumber");

    char *pSSID = g_szApSSID;

    pNode = mxmlNewElement(pNodeRoot, "CountryCode");
    if( pCountryCode && pCountryCode[0] )
        mxmlNewText(pNode, 0, pCountryCode);
    else
        mxmlNewText(pNode, 0, "");

    pNode = mxmlNewElement(pNodeRoot, "FirmwareVesrion");
    if( pFirmwareVer && pFirmwareVer[0] )
        mxmlNewText(pNode, 0, pFirmwareVer);
    else
        mxmlNewText(pNode, 0, "");

    pNode = mxmlNewElement(pNodeRoot, "APMacAddress");
    if( pAPMacAddress && pAPMacAddress[0] )
        mxmlNewText(pNode, 0, pAPMacAddress);
    else
        mxmlNewText(pNode, 0, "");

    pNode = mxmlNewElement (pNodeRoot, "STAMacAddress");
    if (pSTAMacAddress && pSTAMacAddress[0])
        mxmlNewText (pNode, 0, pSTAMacAddress);
    else
        mxmlNewText (pNode, 0, "");

    pNode = mxmlNewElement(pNodeRoot, "SSID");
    if( pSSID && pSSID[0] )
        mxmlNewText(pNode, 0, pSSID);
    else
        mxmlNewText(pNode, 0, "");

    pNode = mxmlNewElement (pNodeRoot, "TargetCountry");
    if (pTargetCountry && pTargetCountry[0])
        mxmlNewText (pNode, 0, pTargetCountry);
    else
        mxmlNewText (pNode, 0, "");

    pNode = mxmlNewElement (pNodeRoot, "SerialNumber");
    if (pSerialNumber && pSerialNumber[0])
        mxmlNewText (pNode, 0, pSerialNumber);
    else
        mxmlNewText (pNode, 0, "");
#ifdef PRODUCT_WeMo_LightV2
    pNode = mxmlNewElement(pNodeRoot, "PowerMeter");
    pSubNode = mxmlNewElement(pNode, "Voltage");
    sprintf(text, "%.3f", get_line_voltage());
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "Current");
    sprintf(text, "%.3f", get_line_current());
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "AveragePower");
    sprintf(text, "%d", get_average_power());
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "ActivePower");
    sprintf(text, "%.3f", get_active_power());
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "Powerfactor");
    sprintf(text, "%.3f", get_power_factor());
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "Load");
    sprintf(text, "%.3f", get_bulb_load());
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "LineFreq");
    sprintf(text, "%d", get_line_frequency());
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "ApiVersion");
    mxmlNewText (pSubNode, 0, get_api_version());
    pNode = mxmlNewElement(pNodeRoot, "USB");
#endif
#ifdef PRODUCT_WeMo_Insight
    pNode = mxmlNewElement(pNodeRoot, "PowerMeter");
    pSubNode = mxmlNewElement(pNode, "vRMS");
    sprintf(text, "%d.%03d", (Values.vRMS/1000), (Values.vRMS%1000));
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "iRMS");
    sprintf(text, "%d.%03d", (Values.iRMS/1000), (Values.iRMS%1000));
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "Watts");
    sprintf(text, "%d.%03d", (Values.Watts/1000), (Values.Watts%1000));
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "PF");
    sprintf(text, "%d.%03d", (Values.PF/1000), (Values.PF%1000));
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "Freq");
    sprintf(text, "%d.%03d", (Values.Freq/100), (Values.Freq%100));
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "IntTemp");
    sprintf(text, "%d.%03d", (Values.InternalTemp/1000), (Values.InternalTemp%1000));
    mxmlNewText (pSubNode, 0, text);
    pSubNode = mxmlNewElement(pNode, "ExtTemp");
    sprintf(text, "%d.%03d", (Values.ExternalTemp/1000), (Values.ExternalTemp%1000));
    mxmlNewText (pSubNode, 0, text);
#endif

    /* write to the file */
    fp = fopen("/tmp/Belkin_settings/ManufactureData.xml", "w");

    if( pRespXml ) {
        if(!fp) {
            APP_LOG("UPNP: Device", LOG_ERR, "Could not open file for writing err: %s",strerror(errno));
        } else {
            mxmlSaveFile(pRespXml, fp, MXML_NO_CALLBACK);
            fclose(fp);
        }
        pszResp = mxmlSaveAllocString(pRespXml, MXML_NO_CALLBACK);
    }

    if( pRespXml )
        mxmlDelete(pRespXml);

    return pszResp;
}


//------------------------------------------ Manufacture info -----------------
int GetManufactureData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    ( *out ) = NULL;
    ( *errorString ) = NULL;

    char *pszRespXML = NULL;
    int retVal = UPNP_E_SUCCESS;

    pszRespXML = CreateManufactureData();

    // create a response
    if( UpnpAddToActionResponse( out, "GetManufactureData",
                                 CtrleeDeviceServiceType[PLUGIN_E_MANUFACTURE_SERVICE],
                                 "ManufactureData", pszRespXML) != UPNP_E_SUCCESS ) {
        ( *out ) = NULL;
        ( *errorString ) = "Internal Error";
        APP_LOG("UPNPDevice", LOG_DEBUG, "GetManufactureData: Internal Error");
        retVal = UPNP_E_INTERNAL_ERROR;
    }

    if(pszRespXML)
        free (pszRespXML);

    return retVal;
}


#define MAX_RULE_ENTRY_SIZE 1024

//-- NEW RULE implementation ---------------------

int UpdateWeeklyCalendar(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{

    APP_LOG("Rule", LOG_CRIT,"Restart rule engine, UpdateWeeklyCalendar");

    gRestartRuleEngine = RULE_ENGINE_SCHEDULED;
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "UpdateWeeklyCalendar", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                            "status", "success");

#ifdef SIMULATED_OCCUPANCY
    unsetSimulatedData();
#endif
    return UPNP_E_SUCCESS;
}

void	SaveDbVersion(char* szVersion)
{
    if (!szVersion)
        return;

    SaveDeviceConfig(RULE_DB_VERSION_KEY, szVersion);
}

/**
 *
 *
 *
 *
 *
 *
 *********************************************************************/
int SetRulesDBVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{

    char* szVersion = Util_GetFirstDocumentItem(request, "RulesDBVersion");

    if (szVersion && (strlen(szVersion) > 0 && strlen(szVersion) < SIZE_16B)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);
        APP_LOG("UPNP: Rule", LOG_DEBUG, "New database version %s", szVersion);

        UpnpAddToActionResponse(out, "SetRulesDBVersion", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "RulesDBVersion", szVersion);

        SetBelkinParameter(RULE_DB_VERSION_KEY, szVersion);
        RuleDBVersionNotify();
    } else {
        UpnpActionRequest_set_ErrCode(pActionRequest, 1);
        APP_LOG("UPNP: Rule", LOG_ERR, "parameters error: database version empty or larger than 15 bytes");
        UpnpAddToActionResponse(out, "SetRulesDBVersion", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "status", "unsuccess");
    }

    return UPNP_E_SUCCESS;
}

int GetRulesDBVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char *szVersion = GetDeviceConfig(RULE_DB_VERSION_KEY);
    if (szVersion && strlen(szVersion)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);
        APP_LOG("UPNP: Rule", LOG_DEBUG, "Database version:%s", szVersion);

        UpnpAddToActionResponse(out, "GetRulesDBVersion", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "RulesDBVersion", szVersion);
    } else {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);
        APP_LOG("UPNP: Rule", LOG_ERR, "database version not available");

        UpnpAddToActionResponse(out, "GetRulesDBVersion", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "RulesDBVersion", "0");

    }

    return UPNP_E_SUCCESS;

}

int EditWeeklycalendar(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{

    APP_LOG("Rule", LOG_CRIT,"Stop rule engine on EditWeeklycalendar");

    char* szAction = Util_GetFirstDocumentItem(request, "action");

    if ((0x00 == szAction) || (0x00 == strlen(szAction))) {

        UpnpActionRequest_set_ErrCode(pActionRequest, 1);

        UpnpAddToActionResponse(out, "EditWeeklycalendar", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "status", "unsuccess");

        APP_LOG("UPNP: Rule", LOG_ERR, "%s: paramters error", __FUNCTION__);

        return 0x00;
    }

    int action = atoi(szAction);

    APP_LOG("UPNP: Rule", LOG_DEBUG, "Rule stop command request:%d", action);

    if (RULE_ACTION_REMOVE == action) {
        gRestartRuleEngine = RULE_ENGINE_DEFAULT;

        UpnpActionRequest_set_ErrCode(pActionRequest, 0);

        UpnpAddToActionResponse(out, "EditWeeklycalendar", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "status", "success");

        StopRuleEngine();
        /*stop Countdown Timer*/
        stopCountdownTimer();

#ifdef SIMULATED_OCCUPANCY
        unsetSimulatedData();
        system("rm /tmp/Belkin_settings/simulatedRule.txt");
        UnSetBelkinParameter(SIM_DEVICE_COUNT);
        UnSetBelkinParameter(SIM_MANUAL_TRIGGER_DATE);
        AsyncSaveData();
        if(!gProcessData)
            StopPluginCtrlPoint();
#endif

        //- Reset to default sensor
        ResetSensor2Default();

        APP_LOG("UPNP: Rule", LOG_DEBUG, "Rule stop command executed");
    } else {
        APP_LOG("UPNP: Rule", LOG_DEBUG, "Rule stop command not executed: %d", action);
    }


    return UPNP_E_SUCCESS;
}


int GetRulesDBPath(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    char szDBURL[MAX_FW_URL_LEN];
    memset(szDBURL, 0x00, sizeof(szDBURL));

    //-Return the icon path of the device
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    snprintf(szDBURL, sizeof(szDBURL), "http://%s:%d/rules.db", g_server_ip, g_server_port);
    APP_LOG("UPNP: Rule", LOG_DEBUG, "DBRule:%s", szDBURL);

    UpnpAddToActionResponse(out, "GetRulesDBPath", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                            "RulesDBPath", szDBURL);

    return UPNP_E_SUCCESS;
}

#ifdef SIMULATED_OCCUPANCY

/************************************************************************
 * Function: SetAwayRuleTask
 *    UPnP action to start/end the away mode
 * Parameters:
 *    EnableAwayTask- 1 to start , 0 to end
 * Return:
 *    Returns 0 for success and 1 for failure.
************************************************************************/
int SetAwayRuleTask(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int enable = 0;
    int ruleID = 0;

    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_ERR, "SetAwayRuleTask: command paramter invalid");
        return 0x01;
    }

    char *enableAwayTask = Util_GetFirstDocumentItem(request, "EnableAwayTask");
    char *ruleId = Util_GetFirstDocumentItem(request, "RuleID");
    if (0x00 == enableAwayTask || 0x00 == strlen(enableAwayTask) ||
        0x00 == ruleId || 0x00 == strlen(ruleId)) {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
        UpnpAddToActionResponse(out, "SetAwayRuleTask",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "actionStatus", "Error");
        FreeXmlSource(enableAwayTask);
        FreeXmlSource(ruleId);
        return 0x01;
    } else {
        enable = atoi(enableAwayTask);
        ruleID = atoi(ruleId);
    }

    osUtilsGetLock(&longPressAwayLock);
    if(0 != enable) {
#ifdef PRODUCT_WeMo_Dimmer
        /* stop the night mode if active */
        stopNightMode();
        if(checkIfFaderRunning()) {
            /* call cancelFaderAndNotify which cancels the fader if running */
            cancelFaderAndNotify();
            /* wait until the brightness get updated after fader stops before starting
               the Away Task */
            sleep(5);
        }
#endif
#if 0 /* have to check if this is needed in future */
        /* there is another long press away mode rule active for which
           the device is a source. First disable the rule. */
        if(g_longPressOccurred) {
            APP_LOG("UPNPDevice", LOG_DEBUG, "SetAwayRuleTask: Another Long Press Away Rule is already active.Stopping it!!");
            //DisableLongPressAwayIfRunning();
        }
#endif
        /* Check if Away Mode Rule is already active.
           Stop the executor thread. */
        if(LONG_PRESS_AWAY_ACTIVE || gRuleHandle[e_AWAY_RULE].ruleCnt) {
            stopExecutorThread(e_AWAY_RULE);
        }
        /* re-initialize the ruleDB before going to
           parse TARGETDEVICES table */
        GetRuleDBHandle();

        APP_LOG("UPNPDevice", LOG_DEBUG, "SetAwayRuleTask: Long Press Away Mode ruleID:%d", ruleID);
        g_LongPressAwayRuleID = ruleID;

        char longPressAwayRuleId[SIZE_4B]= {0};
        /* save g_LongPressAwayRuleID to Nvram to fetch the ID
           if the wemoApp restarts due to some reason */
        snprintf(longPressAwayRuleId, sizeof(longPressAwayRuleId), "%d", g_LongPressAwayRuleID);
        SetBelkinParameter(LONG_PRESS_AWAY_RULE_ID, longPressAwayRuleId);

        g_longPressAwayRunning = 1;
        startExecutorThread(e_AWAY_RULE);
        /* save g_longPressAwayRunning to Nvram to fetch the state
           if the wemoApp restarts due to some reason */
        char longPressAwayModeState[SIZE_4B];
        snprintf(longPressAwayModeState, sizeof(longPressAwayModeState), "%d", g_longPressAwayRunning);
        SetBelkinParameter(LONG_PRESS_AWAY_MODE_STATE, longPressAwayModeState);
    } else {
        /* stop the away task */
        stopExecutorThread(e_AWAY_RULE);
    }
    osUtilsReleaseLock(&longPressAwayLock);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SetAwayRuleTask",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "actionStatus", "success");

    FreeXmlSource(enableAwayTask);
    FreeXmlSource(ruleId);
    return UPNP_E_SUCCESS;
}

int NotifyManualToggle(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    APP_LOG("UPNPDevice", LOG_DEBUG, "Notify Manual Toggle");

    /* manual toggle date isn't needed for long press
       away mode rule. */
#ifdef SIMULATED_OCCUPANCY
    if(!LONG_PRESS_AWAY_ACTIVE)
#endif
        saveManualTriggerData();

    /*stop away task for the day*/
    stopExecutorThread(e_AWAY_RULE);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "NotifyManualToggle",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "ManualToggle", "success");

    return UPNP_E_SUCCESS;
}

int GetSimulatedRuleData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char RuleData[SIZE_256B];
    int selfindex = -1;
    int remtimetotoggle = 0;

    APP_LOG("UPNPDevice", LOG_DEBUG, "GetSimulatedRuleData");
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "GetSimulatedRuleData: paramters error");
        return 0x01;
    }

    memset(RuleData, 0x00, sizeof(RuleData));

    LockLED();
    int curState = GetCurBinaryState();
    UnlockLED();

    int nowTime = daySeconds();
    LockSimulatedOccupancy();
    if(gpSimulatedDevice) {
        selfindex = gpSimulatedDevice->selfIndex;
        if(gpSimulatedDevice->randomTimeToToggle)
            remtimetotoggle = gpSimulatedDevice->randomTimeToToggle - nowTime;
    }
    UnlockSimulatedOccupancy();

    snprintf(RuleData, sizeof(RuleData), "%d|%d|%d|%s", selfindex, curState, remtimetotoggle, g_szUDN_1);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "GetSimulatedRuleData",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "RuleData", RuleData);

    APP_LOG("UPNPDevice", LOG_DEBUG, "GetSimulatedRuleData: %s", RuleData);
    return UPNP_E_SUCCESS;
}

int SimulatedRuleData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int retVal=SUCCESS;
    char* DeviceList = NULL;
    char* DeviceCount = NULL;
    char devCount[SIZE_8B];
    int totalCount = 0;

    APP_LOG("UPNP: Device", LOG_DEBUG, "%s called", __FUNCTION__);
    if (pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_ERR,"INVALID PARAMETERS");
        retVal = FAILURE;
        goto on_return;
    }

    DeviceList = Util_GetFirstDocumentItem(request, "DeviceList");
    DeviceCount = Util_GetFirstDocumentItem(request, "DeviceCount");
    if (0x00 == DeviceList || 0x00 == strlen(DeviceList)) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "Simulated Rule Data: No DeviceList");
        retVal = FAILURE;
        goto on_return;
    }
    if (0x00 == DeviceCount|| 0x00 == strlen(DeviceCount)) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "Simulated Rule Data: No DeviceCount");
        retVal = FAILURE;
        goto on_return;
    }

    totalCount = atoi(DeviceCount);
    if (totalCount <= 0) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "Simulated Rule Data: DeviceCount mal-formed");
        retVal = FAILURE;
        goto on_return;
    }
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SimulatedRuleData", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE], "status", "success");
    setSimulatedRuleFile(DeviceList);
    memset(devCount, 0, sizeof(devCount));
    snprintf(devCount, sizeof(devCount), "%d", totalCount);
    SetBelkinParameter(SIM_DEVICE_COUNT, devCount);
    AsyncSaveData();
    APP_LOG("UPNPDevice", LOG_DEBUG, "Simulated Rule Data: SimulatedDeviceCount: %d", totalCount);

on_return:
    FreeXmlSource(DeviceList);
    FreeXmlSource(DeviceCount);
    if(retVal != SUCCESS) {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_E_INVALID_ARGUMENT);
        UpnpAddToActionResponse(out, "SimulatedRuleData", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE], "status", "failure");
    }
    return retVal;
}
#endif
/**
 *  To get ssid prefix so that can form device AP ssid.
 *  Please note that, prefix table not pre-built since the list will not be long and "hard-coding"
 *
 *  @ szKey
 *  @ szBuffer char* INPUT, the buffer to save and return the results, please allocate 16 bytes for it
 */
char* GetSsidPrefix(char* szSerialNo, char* szBuffer)
{
    if ((0x00 == szBuffer) ||
        (0x00 == szSerialNo)
       ) {
        //- Error
        return 0x00;
    }

    if (MAX_SERIAL_LEN != strlen(szSerialNo)) {
        strncpy(szBuffer, SSID_PREFIX_ERROR, MAX_APSSID_LEN-1);
        return 0x00;
    }

    APP_LOG("Init", LOG_DEBUG, "Product type(K/L/B/M/V): %C", szSerialNo[SERIAL_TYPE_INDEX]);

    if ('K' == szSerialNo[SERIAL_TYPE_INDEX]) {
        /* Relay based products */
        if ('1' == szSerialNo[SERIAL_TYPE_INDEX+2]) {
#ifdef PRODUCT_WeMo_SNSV2
            strncpy(szBuffer, SSID_PREFIX_SWITCHV2, MAX_APSSID_LEN-1);
#else
            strncpy(szBuffer, SSID_PREFIX_SWITCH, MAX_APSSID_LEN-1);
#endif
        } else if ('2' == szSerialNo[SERIAL_TYPE_INDEX+2]) {
            strncpy(szBuffer, SSID_PREFIX_INSIGHT, MAX_APSSID_LEN-1);
        } else if ('3' == szSerialNo[SERIAL_TYPE_INDEX+2]) {
            strncpy(szBuffer, SSID_PREFIX_LIGHT, MAX_APSSID_LEN-1);
        } else if ('5' == szSerialNo[SERIAL_TYPE_INDEX+2]) {
            strncpy(szBuffer, SSID_PREFIX_DIMMER, MAX_APSSID_LEN-1);
        }
        else {
            strncpy(szBuffer, SSID_PREFIX_ERROR, MAX_APSSID_LEN-1);
        }
    } else if ('L' == szSerialNo[SERIAL_TYPE_INDEX]) {
        //- Sensor
        strncpy(szBuffer, SSID_PREFIX_MOTION, MAX_APSSID_LEN-1);
    } else if ('B' == szSerialNo[SERIAL_TYPE_INDEX]) {
        //- Bridge
        strncpy(szBuffer, SSID_PREFIX_BRIDGE, MAX_APSSID_LEN-1);
    }
    else {
        if (('2' == szSerialNo[SERIAL_TYPE_INDEX]) && ('9' == szSerialNo[SERIAL_TYPE_INDEX + 1])) {
            if ('N' == szSerialNo[SERIAL_TYPE_INDEX + 2]) {
                strncpy(szBuffer, SSID_PREFIX_LIGHTV2, MAX_APSSID_LEN-1);
            }
            else if ('P' == szSerialNo[SERIAL_TYPE_INDEX + 2]) {
                strncpy(szBuffer, SSID_PREFIX_LIGHT3WAY, MAX_APSSID_LEN-1);
            }
            else {
                //- Error SSID
                strncpy(szBuffer, SSID_PREFIX_ERROR, MAX_APSSID_LEN-1);
            }
        }
        else {
            //- Error SSID
            strncpy(szBuffer, SSID_PREFIX_ERROR, MAX_APSSID_LEN-1);
        }
    }


    return szBuffer;

}

void SetAppSSID()
{
    char szBuff[MAX_APSSID_LEN];

    memset(szBuff, 0x00, sizeof(szBuff));

    char* szSerialNo = GetSerialNumber();
    if ((0x00 == szSerialNo) || (0x00 == strlen(szSerialNo))) {
        APP_LOG("UPNP: Device", LOG_CRIT, "Serial Number is not there. This will ideally never happen!!");
        system("reboot");
    }

    //-Get prefix of the AP SSID
    GetSsidPrefix(szSerialNo, szBuff);

    strncpy(g_szSerialNo, szSerialNo, sizeof(g_szSerialNo)-1);

    strncat(szBuff, szSerialNo + strlen(szSerialNo) - DEFAULT_SERIAL_TAILER_SIZE, sizeof(szBuff)-strlen(szBuff)-1);

    APP_LOG("UPNP: Device", LOG_DEBUG, "APSSID: %s", szBuff);
    APP_LOG("STARTUP: Device", LOG_DEBUG, "serial: %s", g_szSerialNo);

#if !defined(PRODUCT_WeMo_Dimmer) && !defined(PRODUCT_WeMo_SNSV2) && !defined(PRODUCT_WeMo_LightV2)
    if(!g_bWiredEthernet) {
        wifisetSSIDOfAP(szBuff);
    }
#endif
    SAFE_STRCPY(g_szApSSID, szBuff);
}

//------------------------------ Sensor control --------------------
void *PowerSensorMonitorTask(void *args)
{
    tu_set_my_thread_name( __FUNCTION__ );
    APP_LOG("UPNP: sensor rule", LOG_DEBUG, "In Power Sensor Monitor Task");
    while(sPowerDuration > 0) {
        sleep(1);
        sPowerDuration--;
    }
#ifndef PRODUCT_WeMo_LEDLight
    /*stop Countdown Timer*/
    stopCountdownTimer();
#endif
    APP_LOG("UPNP: sensor rule", LOG_DEBUG, "Sensor event, monitoring thread stop: to change state to: %d", sPowerEndAction);

    setActuation(ACTUATION_SENSOR_RULE);
    ChangeBinaryState(sPowerEndAction);
    g_IsLastUserActionOn = sPowerEndAction;
    LocalBinaryStateNotify(sPowerEndAction);
    ithPowerSensorMonitorTask = -1;
    return NULL;
}

void FirmwareUpdateStatusNotify(int status)
{
    long unsigned int time;
    char* szCurState[3];
    szCurState[0x00] = (char*)ZALLOC(SIZE_2B);
    snprintf(szCurState[0x00], SIZE_2B, "%d", status);

    time = GetUTCTime();
    int n = snprintf(NULL, 0, "%lu", time);
    szCurState[0x01] = (char*)ZALLOC(n+1);
    snprintf(szCurState[0x01], n+1, "%lu", time);

    time = gFwDownloadTimeStamp;
    n = snprintf(NULL, 0, "%lu", time);
    szCurState[0x02] = (char*)ZALLOC(n+1);
    snprintf(szCurState[0x02], n+1, "%lu", time);

    char* paramters[] = {"FirmwareUpdateStatus", "CurrTimeStamp", "FWDownloadTimeStamp"} ;
    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_FIRMWARE_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_FIRMWARE_SERVICE].ServiceId, (const char **)paramters, (const char **)szCurState, 0x03);

    free(szCurState[0x00]);
    free(szCurState[0x01]);
    free(szCurState[0x02]);
    APP_LOG("UPNP: firmware update", LOG_DEBUG, "current status: %d", status);
}


void RemoteFirmwareUpdateStatusNotify(void)
{
    APP_LOG("UPNP",LOG_DEBUG, "REMOTE Firmware update status NOTIFICATION");
}

int GetTimeZoneIndex(float iTimeZoneValue)
{

    APP_LOG("UPNP: time sync", LOG_DEBUG, "to lookup time zone: %f", iTimeZoneValue);

    int counter = sizeof(g_tTimeZoneList)/sizeof(tTimeZone);
    int loop = 0x00;
    int index = 0x00;

    for (; loop < counter; loop++) {
        float delta = iTimeZoneValue - g_tTimeZoneList[loop].iTimeZone;

        if ((delta <= 0.001) && (delta >= -0.001)) {
            APP_LOG("UPNP: time sync", LOG_DEBUG, "time zone index found: %f, %f, %s", delta, iTimeZoneValue, g_tTimeZoneList[loop].szDescription);
            index = g_tTimeZoneList[loop].index;
            break;
        }
    }

    if (0x00 == index)
        APP_LOG("UPNP: time sync", LOG_ERR, "time zone index not found: %f", iTimeZoneValue);

    //- Reset NTP server
    if (index <= TIME_ZONE_NORTH_AMERICA_INDEX)
        s_szNtpServer = TIME_ZONE_NORTH_AMERICA_NTP_SERVER;
    else if(index == TIME_ZONE_UK_INDEX || index == TIME_ZONE_FRANCE_INDEX)
        s_szNtpServer = TIME_ZONE_EUROPE_NTP_SERVER;
    else if ((index > TIME_ZONE_NORTH_AMERICA_INDEX) && (index <= TIME_ZONE_ASIA_INDEX))
        s_szNtpServer = TIME_ZONE_ASIA_NTP_SERVER;		//- Use asia for Europ
    else
        s_szNtpServer = TIME_ZONE_ASIA_NTP_SERVER;

    return index;

}


int IsUPnPNetworkMode()
{
    if ((0x00 == strcmp(g_server_ip, "0.0.0.0")) ||
        0x00 == strcmp(g_server_ip, AP_LOCAL_IP)) {
        return 0x00;
    }

    return 0x01;
}

void UpdateUPnPNetworkMode(int networkMode)
{
    if (UPNP_LOCAL_MODE == networkMode) {
        APP_LOG("UPNP: network", LOG_DEBUG, "network mode changed to: UPNP_LOCAL_MODE");
        g_IsUPnPOnInternet	= FALSE;
        g_IsDeviceInSetupMode	= TRUE;
    } else if (UPNP_INTERNET_MODE == networkMode) {
        APP_LOG("UPNP: network", LOG_DEBUG, "network mode changed to: UPNP_INTERNET_MODE");
        g_IsUPnPOnInternet 	= TRUE;
        g_IsDeviceInSetupMode	= FALSE;
    } else {
        APP_LOG("UPNP: network", LOG_ERR, "wrong network mode");
    }
}


void		NotifyInternetConnected()
{
    pMessage msg = createMessage(NETWORK_INTERNET_CONNECTED, 0x00, 0x00);
    SendMessage2App(msg);
}


void detectIPChange()
{
    int ret = 0;
    char *currentIP = GetWanIPAddress();
    char *upnp_ip = UpnpGetServerIpAddress();

    if(!strcmp(currentIP,DEFAULT_INVALID_IP_ADDRESS)) {
        APP_LOG("WiFiApp", LOG_DEBUG, "****** invalid ip: %s ******", currentIP);
        return;
    }

    if(!strlen(gPrevIP)) {
        strncpy(gPrevIP, currentIP, SIZE_20B-1);
        APP_LOG("WiFiApp", LOG_DEBUG, "****** prevIP: %s ******", gPrevIP);
        return;
    }

    /* CASE-1: IP change case */
    if(strcmp(currentIP,gPrevIP)) {
        APP_LOG("WiFiApp", LOG_CRIT,"IP change detected !!, currentIP|prevIP: %s|%s, device_handle: %d", currentIP, gPrevIP, device_handle);
        ret = 1;
        /* WEMO-46727: sometimes device changes IP without going in reconnection state, and /etc/resolve.conf doesn't get update, so updating it here to make sure it gets update everytime */
        update_resolv_config();
    }
    /* CASE-2: IP same case, but upnp not running */
    else if(!strcmp(currentIP,gPrevIP) && device_handle==-1) {
        APP_LOG("WiFiApp", LOG_CRIT,"IP same, upnp not running!!, currentIP|prevIP: %s|%s, device_handle: %d", currentIP, gPrevIP, device_handle);
        ret = 1;
    }
    /* CASE-3: IP same but UPnP running on default IP address */
    else if(upnp_ip && strcmp(gPrevIP,upnp_ip)) {
        APP_LOG("WiFiApp", LOG_CRIT,"IP same but Upnp running on default IP address!!, currentIP|prevIP|upnp_ip: %s|%s|%s, device_handle: %d", currentIP, gPrevIP, upnp_ip, device_handle);
        ret = 1;
    } else {
        /* empty case */
    }

    if(ret) {
        NotifyInternetConnected();
    }

    return;
}

void UpdateUPnPNetwork(int status)
{
    //- If IP address change and etc
    char* ip_address = wifiGetIP(INTERFACE_CLIENT);
    char *upnpIP = UpnpGetServerIpAddress();
    char serverPort[SIZE_8B]= {'\0',};

    if(upnpIP && !strcmp(ip_address,upnpIP)) {
        APP_LOG("WiFiApp", LOG_CRIT,"currentIP and upnpIP are same..so returning without restarting upnp....");
        return;
    }

    if (status == NETWORK_INTERNET_CONNECTED) {
        APP_LOG("ITC: network", LOG_DEBUG, "router connection done, UPnP re-runs again");
        ControlleeDeviceStop();
        StopPluginCtrlPoint();
        pluginUsleep(2000000);

        if (ip_address && (0x00 != strcmp(ip_address, DEFAULT_INVALID_IP_ADDRESS))) {
            //-Start new UPnP in client AP new address
            APP_LOG("ITC: network", LOG_CRIT,"start new UPnP session: %s", ip_address);
            //gautam: update the Insight and LS Makefile to copy Insightsetup.xml and Lightsetup.xml in /sbin/web/ as setup.xml
            int ret=ControlleeDeviceStart(GetLanDeviceName(), 0x00, "setup.xml", DEFAULT_WEB_DIR);
            if(( ret != UPNP_E_SUCCESS ) && ( ret != UPNP_E_INIT ) ) {
                APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
                APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
                resetSystem();
            }

            // - WEMO-46017: Cloud cache for local discovery
            APP_LOG("UPNP", LOG_DEBUG,"prev ip & port: %s|%s, upnp ip & port : %s|%u",
                    gPrevIP,gPrevPort,g_server_ip,g_server_port);

            snprintf(serverPort, sizeof(serverPort), "%u", g_server_port);

            /* update prev Port, for first time and on change */
            if(!strlen(gPrevPort) || strcmp(gPrevPort, serverPort)) {
                memset(gPrevPort, 0, sizeof(gPrevPort));
                strncpy(gPrevPort, serverPort, sizeof(gPrevPort)-1);
                APP_LOG("WiFiApp", LOG_DEBUG, "****** update prevPort: %s ******", gPrevPort);
            }

            ret=StartPluginCtrlPoint(GetLanDeviceName(), 0x00);
            if(UPNP_E_INIT_FAILED==ret) {
                APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
                APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
                resetSystem();
            }
            EnableContrlPointRediscover(TRUE);
        }

        UpdateUPnPNetworkMode(UPNP_INTERNET_MODE);
    } else if (status == NETWORK_AP_OPEN_UPNP) {
        //- Get gateway address, check current running status
        char* szAPIp = GetLanIPAddress();
        if ((0x00 == szAPIp) || 0x00 == strlen(szAPIp)) {
            APP_LOG("ITC: network", LOG_ERR,"AP IP address unknown");
            return;
        }

        if (0x00 == strcmp(szAPIp, g_server_ip)) {
            APP_LOG("ITC: network", LOG_ERR,"UPnP already running on AP network");
            return;
        } else {
            APP_LOG("ITC: network", LOG_ERR,"UPnP is switching to AP network:%s", szAPIp);
            ControlleeDeviceStop();
            StopPluginCtrlPoint();
            pluginUsleep(2000000);
            //gautam: update the Insight and LS Makefile to copy Insightsetup.xml and Lightsetup.xml in /sbin/web/ as setup.xml
            int ret=ControlleeDeviceStart(INTERFACE_AP, 0x00, "setup.xml", DEFAULT_WEB_DIR);
            if(( ret != UPNP_E_SUCCESS ) && ( ret != UPNP_E_INIT ) ) {
                APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
                APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
                resetSystem();
            }
            UpdateUPnPNetworkMode(UPNP_LOCAL_MODE);
        }
    }
}


int ProcessItcEvent(pNode node)
{
    if (0x00 == node)
        return 0x01;

    if (0x00 == node->message) {
        free(node);
        return 0x01;
    }
    switch(node->message->ID) {
    case NETWORK_INTERNET_CONNECTED:
        APP_LOG("ITC: network", LOG_DEBUG, "NETWORK_INTERNET_CONNECTED");
        UpdateUPnPNetwork(NETWORK_INTERNET_CONNECTED);
        break;

    case META_SAVE_DATA:
        APP_LOG("ITC: meta", LOG_DEBUG, "META_SAVE_DATA");
        SaveSetting();
        break;

    case META_SOFT_RESET:
        APP_LOG("ITC: meta", LOG_CRIT, "META_SOFT_RESET");
        ClearRuleFromFlash();

        break;
    case META_FULL_RESET:
        APP_LOG("ITC: meta", LOG_CRIT, "META_FULL_RESET");
        pluginUsleep(1000000);
        APP_LOG("ITC: meta", LOG_DEBUG, "CALL DEREGISTER!!!");        
        ResetToFactoryDefault(0);
        break;
    case META_REMOTE_RESET:
        APP_LOG("ITC: meta", LOG_DEBUG, "META_REMOTE_RESET");
        UnSetBelkinParameter (DEFAULT_HOME_ID);
        memset(g_szHomeId, 0x00, sizeof(g_szHomeId));
        UnSetBelkinParameter (DEFAULT_PLUGIN_PRIVATE_KEY);
        memset(g_szPluginPrivatekey, 0x00, sizeof(g_szPluginPrivatekey));
        UnSetBelkinParameter (RESTORE_PARAM);
        memset(g_szRestoreState, 0x0, sizeof(g_szRestoreState));
        /* server environment settings cleanup and nat client destroy */
        serverEnvIPaddrInit();
        break;
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    case META_OVERTEMP_STATE:
        if(node->message->message) {
            APP_LOG("ITC: meta", LOG_DEBUG, "META_OVERTEMP_STATE %d",*((int *)(node->message->message)));
            OverTempStateNotify(*((int *)(node->message->message)));
        } else {
            APP_LOG("ITC: meta", LOG_ERR, "META_OVERTEMP_STATE No Message");
        }
        break;
#endif
    case NETWORK_AP_OPEN_UPNP:
        APP_LOG("ITC: network", LOG_DEBUG, "NETWORK_AP_OPEN_UPNP");
        UpdateUPnPNetwork(NETWORK_AP_OPEN_UPNP);
        break;

    case BTN_MESSAGE_ON_IND:
        APP_LOG("ITC: button", LOG_DEBUG, "BTN_MESSAGE_ON_IND");
        ToggleUpdate(0x01);
        break;
    case BTN_MESSAGE_OFF_IND:
        APP_LOG("ITC: button", LOG_DEBUG, "BTN_MESSAGE_OFF_IND");
        ToggleUpdate(0x00);
        break;
    case UPNP_MESSAGE_ON_IND:
        APP_LOG("ITC: button", LOG_DEBUG, "UPNP_MESSAGE_ON_IND");
        LocalBinaryStateNotify(0x01);
#ifdef PRODUCT_WeMo_Insight
        processInsightNotification(E_STATE, 0x01);
#endif
        break;
    case UPNP_MESSAGE_OFF_IND:
        APP_LOG("ITC: button", LOG_DEBUG, "UPNP_MESSAGE_OFF_IND");
        LocalBinaryStateNotify(0x00);
#ifdef PRODUCT_WeMo_Insight
        if(POWER_SBY != g_APNSLastState)
            processInsightNotification(E_STATE, 0x00);
#endif
        break;
#ifdef PRODUCT_WeMo_Insight
    case UPNP_MESSAGE_SBY_IND:
        APP_LOG("ITC: button", LOG_DEBUG, "UPNP_MESSAGE_SBY_IND");
        pluginUsleep(500000);
        LocalBinaryStateNotify(POWER_SBY);
        if(POWER_OFF != g_APNSLastState)
            processInsightNotification(E_STATE, 0x00);
        break;
    case UPNP_MESSAGE_PWR_IND:
        //APP_LOG("ITC: Power", LOG_DEBUG, "UPNP_MESSAGE_PWR_IND");
        pluginUsleep(500000);
        if(!g_NoNotificationFlag) {
            LocalInsightParamsNotify();
        } else {
            g_NoNotificationFlag =0;
        }
        break;
    case UPNP_MESSAGE_PWRTHRES_IND:
        APP_LOG("ITC: Power", LOG_DEBUG, "UPNP_MESSAGE_PWRTHRES_IND");
        pluginUsleep(500000);
        LocalPowerThresholdNotify(g_PowerThreshold);
        //Send this response towards cloud synchronously using same data socket
        break;
    case UPNP_MESSAGE_ENERGY_COST_CHANGE_IND:
        APP_LOG("ITC: Power", LOG_DEBUG, "UPNP_MESSAGE_ENERGY_COST_CHANGE_IND");
        pluginUsleep(500000);
        LocalInsightHomeSettingNotify();
        break;
    case UPNP_MESSAGE_DATA_EXPORT:
        APP_LOG("ITC: Power", LOG_DEBUG, "UPNP_MESSAGE_DATA_EXPORT");
        pluginUsleep(500000);
        StartDataExportSendThread((void*)(node->message->message));
        break;
#endif
    case UPNP_ACTION_MESSAGE_ON_IND:
        APP_LOG("ITC: button", LOG_DEBUG, "UPNP_ACTION_MESSAGE_ON_IND");
        pluginUsleep(500000);
        LocalUserActionNotify(0x03);
        break;
    case UPNP_ACTION_MESSAGE_OFF_IND:
        APP_LOG("ITC: button", LOG_DEBUG, "UPNP_ACTION_MESSAGE_OFF_IND");
        pluginUsleep(500000);
        LocalUserActionNotify(0x02);
        break;

    case META_FIRMWARE_UPDATE:
        APP_LOG("ITC: FIRMWARE_UPDATE", LOG_DEBUG, "META_FIRMWARE_UPDATE");
        FirmwareUpdateStatusNotify(FM_STATUS_DOWNLOAD_SUCCESS);
        FirmwareUpdateStatusNotify(FM_STATUS_UPDATE_STARTING);
        break;
#ifdef PRODUCT_WeMo_LEDLight
    case RULE_MESSAGE_RESTART_REQ:
        APP_LOG("ITC: RULE_MESSAGE_RESTART_REQ", LOG_DEBUG, "RULE_MESSAGE_RESTART_REQ");
        //- DST change to adv again, I am now living in new century
        Advertisement4TimeUpdate();
        //- DST time change, need to reload again
//            BR_ResetRule();
        break;
#endif
#ifdef PRODUCT_WeMo_Light
    case NIGHTLIGHT_DIMMING_MESSAGE_REBOOT:
        APP_LOG("ITC: NIGHTLIGHT_DIMMING_MESSAGE_REBOOT", LOG_DEBUG, "NIGHTLIGHT_DIMMING_MESSAGE_REBOOT");
        pluginUsleep(2000000);
        system("reboot");
#endif
    case META_CONTROLLEE_DEVICE_STOP:
        APP_LOG("ITC: CONTROLLEE_DEVICE_STOP", LOG_DEBUG, "META_CONTROLLEE_DEVICE_STOP");
        APP_LOG("UPNP: Device", LOG_DEBUG, "reset plugin, stop controllee device");
        ControlleeDeviceStop();
        break;
#ifdef PRODUCT_WeMo_Dimmer
    case NIGHTMODE_CONFIGURATION_NOTIFY:
        APP_LOG("ITC: NIGHTMODE_CONFIGURATION_NOTIFY", LOG_DEBUG, "NIGHTMODE_CONFIGURATION_NOTIFY");
        nightModeConfigurationNotify();
        break;
#endif
    default:
        break;
    }

    if (0x00 != node->message->message)
        free(node->message->message);
    free(node->message);
    free(node);
    return 0;
}


void	AsyncSaveData()
{
    pMessage msg = createMessage(META_SAVE_DATA, 0x00, 0x00);
    SendMessage2App(msg);
}


void RestoreIcon()
{
    APP_LOG("UPNP", LOG_DEBUG, "To use default icon");

    if (DEVICE_SOCKET == g_eDeviceType) {
        system("cp -f /etc/icon.jpg /tmp/Belkin_settings");
    } else {
        system("cp -f /etc/sensor.jpg /tmp/Belkin_settings/icon.jpg");
    }
}



int GetDeviceStateIF()
{
    int curState = 0x00;

    if (DEVICE_SOCKET == g_eDeviceType) {
        LockLED();
        curState = GetCurBinaryState();
        UnlockLED();
    } else if (DEVICE_SENSOR == g_eDeviceType) {
        LockSensor();
        curState = GetSensorState();
        UnlockSensor();
    }

    return curState;
}

void* FirmwareUpdateStart(void *args)
{
    int retVal = SUCCESS;
    int count=0, state=0, dwldStTime = 0, withUnsignedImage = 0;
    FirmwareUpdateInfo *pfwUpdInf = NULL;
    char FirmwareURL[MAX_FW_URL_LEN];

    tu_set_my_thread_name( __FUNCTION__ );
    memset(FirmwareURL, 0, sizeof(FirmwareURL));

    if(args) {
        pfwUpdInf = (FirmwareUpdateInfo *)args;
        dwldStTime = pfwUpdInf->dwldStartTime;
        withUnsignedImage = pfwUpdInf->withUnsignedImage;
        strncpy(FirmwareURL, pfwUpdInf->firmwareURL, sizeof(FirmwareURL)-1);
    }

    APP_LOG("UPnPApp",LOG_DEBUG, "**** [FIRMWARE UPDATE] Saving firmware update URL: %s ****", FirmwareURL);
    SetBelkinParameter("FirmwareUpURL", FirmwareURL);
    APP_LOG("UPnPApp",LOG_DEBUG, "**** [FIRMWARE UPDATE] Saving firmware update unsigned: %d ****", withUnsignedImage);
    if (withUnsignedImage) {
        SetBelkinParameter("FirmwareUpUnsigned", "1");
    } else {
        SetBelkinParameter("FirmwareUpUnsigned", "0");
    }

    AsyncSaveData();

    if(dwldStTime) {
        APP_LOG("UPnPApp", LOG_DEBUG,"******** [FIRMWARE UPDATE] Sleeping for %d mins zzzzzz", dwldStTime/60);
        pluginUsleep(dwldStTime * 1000000); //Staggering the downloading of firmware
    }

    pthread_attr_init(&firmwareUp_attr);
    pthread_attr_setdetachstate(&firmwareUp_attr, PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&firmwareUpThread, &firmwareUp_attr, (void*)&firmwareUpdateTh, (void *)args);
    if(retVal < SUCCESS) {
        APP_LOG("UPnPApp",LOG_ERR, "**** [FIRMWARE UPDATE] Firmware Update thread not Created****");
        goto on_return;
    }

    pluginUsleep (500000); //500 ms
    APP_LOG("UPnPApp",LOG_ERR, "************ [FIRMWARE UPDATE] Firmware Update Monitor thread Created");
    while(1) {
        count = 0;
        while (count < MAX_FW_DL_TIME_OUT) {
            pluginUsleep (10000000);
            if( (getCurrFWUpdateState() == FM_STATUS_DOWNLOAD_SUCCESS) ||
                (getCurrFWUpdateState() == FM_STATUS_UPDATE_STARTING) ) {
                int wait_for_flash = 0;
                APP_LOG("UPNP: Device", LOG_DEBUG,"******** [FIRMWARE UPDATE] Download is Complete");
                while(wait_for_flash < 36) { // 3 mins
                    if (firmwareUpThread == -1)
                        break;
                    wait_for_flash ++;
                    APP_LOG("UPNP: Device", LOG_DEBUG,"******** [FIRMWARE UPDATE] waiting for REBOOTING %d sec", wait_for_flash * 5);
                    pluginUsleep(5000000);
                }
                APP_LOG("UPnPApp",LOG_ALERT, "************ [FIRMWARE UPDATE] Firmware Update Monitor thread rebooting system...");
                firmwareUpThread = -1;

                system("jffs2reset -y");
                system("sync");
                pluginUsleep (5000000);

                int rc = system("reboot");
                if (rc != -1)
                    return NULL;
            } else if( getCurrFWUpdateState() == FM_STATUS_DOWNLOAD_UNSUCCESS )
                break;
            else
                count +=10;
        }

        state = getCurrFWUpdateState();
        APP_LOG("UPNP: Device", LOG_DEBUG,"[FIRMWARE UPDATE] Going to stop downloading...:%d", state);

        //- time out here, thread itself quit and the thread ID set to 0x00 for further probable re-visit
        //firmwareUpThread = -1;

        //Stop download firmware request
        StopDownloadRequest();

        //- Remove the file already created to save more space
        // Do not remove the downloaded file, so that curl can resume downloading file
        // when the download fails due to the slow network connection.
        /*remove partially downloaded file*/
        system("rm /tmp/firmware.bin.gpg");
        //- Reset the LED state back so that not confuse the user
#if defined(PRODUCT_WeMo_LightV2)
        SetWiFiLED(RGB_SWITCH_OFF);
#elif defined(PRODUCT_WeMo_SNSV2)
        SetWiFiLED(0x04);
#endif
        break;
    }
on_return:
    fwUpMonitorthread = -1;
    return NULL;
}

#if defined(AUTO_FW_UPDATE)

/**
 * Get AutoFwUpdateVar:
 *      Callback to Get "g_AutoFwUpdateVar"
 *
 *
 * *****************************************************************************************************************/
int GetAutoFwUpdateVar(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char *AutoFwStatus = NULL;
    AutoFwStatus = GetBelkinParameter(AUTOFWUPDATEVAR);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "GetAutoFwUpdateVar", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE], "AutoFwUpdateVar", AutoFwStatus);
    APP_LOG("UPNP: Device", LOG_DEBUG, "GetAutoFwUpdateVar response sent");

    return UPNP_E_SUCCESS;

}
/**
 * Set AutoFwUpdateVar:
 *      Callback to Set "g_AutoFwUpdateVar"
 *      To kill AutoFwUpdate Thread if g_AutoFwUpdateVar is set to '0'
 *
 * *****************************************************************************************************************/
int SetAutoFwUpdateVar(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{

    int errnum;
    int AutoFwStatus=0x00;
    if (pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"Set AutoFwUpdateVar: paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    char* szAutoFwUpdate = Util_GetFirstDocumentItem(request, "AutoFwUpdateVar");

    if( (szAutoFwUpdate == NULL) && (0x00 == strlen(szAutoFwUpdate))) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetAutoFwUpdateVar", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],"status", "Parameters Error");
        APP_LOG("UPNP: Device", LOG_ERR,"Set AutoFwUpdateVar: failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }


    AutoFwStatus = strtoul(szAutoFwUpdate, NULL, 0);
    if(g_AutoFwUpdateVar != AutoFwStatus) {
        g_AutoFwUpdateVar = AutoFwStatus;
        APP_LOG("UPNP: Device", LOG_DEBUG,"Set g_AutoFwUpdateVar to value : %u", g_AutoFwUpdateVar);

        SetBelkinParameter(AUTOFWUPDATEVAR, szAutoFwUpdate);
        AsyncSaveData();
        APP_LOG("UPNP: Device", LOG_DEBUG, "AutoFwUpdateVar state change to %s", szAutoFwUpdate);


        if (g_AutoFwUpdateVar == 0 ) {
            if ((-1 != AutofwUpgradethread)) {
                APP_LOG("UPNP: Device", LOG_DEBUG, "====Auto Firmware Update thread was created====");
                errnum = pthread_cancel(AutofwUpgradethread);
                if(errnum) {
                    APP_LOG("UPNP: Device",LOG_ERR, "pthread_cancel ret: %d...",errnum);
                    exit(0);
                }
                AutofwUpgradethread = -1;
                if(-1 != firmwareUpThread) {
                    APP_LOG("UPNP: Device",LOG_DEBUG, "====Firmware download thread was created====");
                    errnum = pthread_cancel(firmwareUpThread);
                    if(errnum) {
                        APP_LOG("UPNP: Device",LOG_ERR, "pthread_cancel ret: %d...",errnum);
                        exit(0);
                    }
                    firmwareUpThread = -1;
                }
                if(-1 != fwUpMonitorthread) {
                    errnum = pthread_cancel(fwUpMonitorthread);
                    if(errnum) {
                        APP_LOG("UPNP: Device",LOG_ERR, "pthread_cancel ret: %d...",errnum);
                        exit(0);
                    }
                    fwUpMonitorthread = -1;
                }
                setCurrFWUpdateState(FM_STATUS_DEFAULT);
            }
        } else if (g_AutoFwUpdateVar == 1 ) {
            if ((-1 == AutofwUpgradethread)) {
                if(AutoFWUpdate() < SUCCESS) {
                    APP_LOG("UPNP: Device",LOG_CRIT, "======AutoFirmware Upgrade thread not Created On Setting g_AutoFwUpdateVar to 1======");
                    exit(0);
                }
            }
        }
    }
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SetAutoFwUpdateVar", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE], "AutoFwUpdateVar", szAutoFwUpdate);
    APP_LOG("UPNP: Device", LOG_DEBUG, "set SetAutoFwUpdateVar response sent");
    FreeXmlSource(szAutoFwUpdate);

    return UPNP_E_SUCCESS;

}

void remove_slash(char *string)
{
    char *read, *write;

    for(read = write = string; *read != '\0'; ++read) {
        if(*read != '\\') {
            *(write++) = *read;
        }
    }
    *write = '\0';
}


#define MAX_LINK_SIZE 256
//API to parse Eco response coming from cloud to check If Firmware.txt Download link is available for Custom Download
void* parseEchoPostResp(void *RespXML, char *link)
{

    char *snRespXml = NULL;
    char *ptr       = NULL;
    int escapeVal   = 0;
    char *result    = 0x00;

    snRespXml = (char*)RespXML;

    APP_LOG("AutoFwUpdate", LOG_DEBUG, "XML String Loaded is %s\n", snRespXml);
    ptr = strstr(snRespXml,"fwUpgradeURL" );
    if(*ptr == NULL)
        return NULL;
    APP_LOG("AutoFwUpdate", LOG_DEBUG, "XML Parsed is %s\n", ptr);
    result = strtok(ptr, "\"");
    while(result != NULL) {
        APP_LOG("AutoFwUpdate", LOG_DEBUG, "%d . Found: %s\n",escapeVal,result);
        if(escapeVal == 2) {
            remove_slash(result);
            APP_LOG("AutoFwUpdate", LOG_DEBUG, "%d . After Removing Slash : %s\n",escapeVal,result);
            snprintf(link,MAX_LINK_SIZE,"%s",result);
            break;
        }
        result = strtok(NULL, "\"");
        escapeVal++;
    }

    return NULL;
}



#define FW_FILE_PATH	"/tmp/FwFile.txt"
#define MAX_READ_SIZE	1024
#define	DEVICE		"WeMoSignedEcho"
#define DEVICE_LEN      14
#define FW_VER_LEN      22

void ParseFirmwareTxtResp(void *sendNfResp)
{

    char *fwTxtResp = NULL;
    int counter=0;
    struct Command* temp=NULL;
    FILE *FwFile;
    int FileLen=0;
    char ReadBuffer[MAX_READ_SIZE] = {0,};
    //char FwDownloadUrl[MAX_READ_SIZE] = {0,};
    FirmwareUpdateInfo fwUpdInf;

    fwTxtResp = (char*)sendNfResp;
    FileLen = strlen(fwTxtResp) + 1;
    APP_LOG("AutoFwUpdate", LOG_DEBUG, "Length: %d Response received: %s",FileLen,fwTxtResp);
    FwFile = fopen(FW_FILE_PATH,"w");
    if(FwFile) {
        counter = fwrite(fwTxtResp,1,FileLen,FwFile);
        APP_LOG("AutoFwUpdate", LOG_DEBUG, "Total Bytes Written to File: %s is %d",FW_FILE_PATH,counter)
    } else {
        APP_LOG("AutoFwUpdate", LOG_ERR, "Cannot Open File: %s",FW_FILE_PATH);
    }
    fclose(FwFile);
    FwFile = fopen(FW_FILE_PATH,"r");
    if(FwFile) {
        while(1) {
            memset(ReadBuffer,0,MAX_READ_SIZE);
            if(fgets(ReadBuffer,MAX_READ_SIZE, FwFile) != NULL) {
                if(strncmp(ReadBuffer,DEVICE,DEVICE_LEN) == 0) {
                    APP_LOG("AutoFwUpdate", LOG_DEBUG, "Device Found: %s",DEVICE);
                    memset(ReadBuffer,0,MAX_READ_SIZE);
                    if(fgets(ReadBuffer,MAX_READ_SIZE, FwFile) != NULL) {
                        if(strncmp(ReadBuffer,g_szFirmwareVersion,FW_VER_LEN) != 0) {
                            APP_LOG("AutoFwUpdate", LOG_DEBUG, "Version Different: %s",g_szFirmwareVersion);
                            if(fgets(ReadBuffer,MAX_READ_SIZE, FwFile) != NULL) {
                                memset(ReadBuffer,0,MAX_READ_SIZE);
                                if(fgets(ReadBuffer,MAX_READ_SIZE, FwFile) != NULL) {
                                    SAFE_STRCPY(fwUpdInf.firmwareURL,ReadBuffer);
                                    APP_LOG("AutoFwUpdate", LOG_DEBUG, "Download URL: %s",fwUpdInf.firmwareURL);
                                    fwUpdInf.withUnsignedImage = 0;
                                    fwUpdInf.dwldStartTime = rand() % 3600;
                                    StartFirmwareUpdate(fwUpdInf);
                                    break;
                                }
                            } else
                                break;
                        } else
                            break;
                    } else
                        break;
                }
            } else
                break;
        }
        fclose(FwFile);
        system("rm /tmp/FwFile.txt");
    }
}

void* UpdateFw(void *args)
{

    char *szHomeId = NULL;
    unsigned int randomtime;
    char *downloadLink = NULL;

    int retVal = PLUGIN_SUCCESS;
    int ret = PLUGIN_SUCCESS;
    int count=0;
    int randSleep=0;
    int errnum;

    authSign *sign = NULL;
    int year = 0x00, monthIndex = 0x00, seconds = 0x00, dayIndex = 0x00;

    pthread_attr_t fwTxt_attr;
    pthread_t fwTxtthread=-1;


    // Now to perform a randomly deffered Firmware Upgrade
    // if Remote Enabled Do a Custom Download of Firmware.txt with http 'post'
    while(1) {
        GetCalendarDayInfo(&dayIndex, &monthIndex, &year, &seconds);
        if(year != 2000) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "*************start Firmware UPdate TASK NOW YEAR IS NOT 2000");
            break;
        }
        pluginUsleep(10*1000000);  //wait for 10 sec and check again

    }

    while(1) {
        /*calculate sleep interval*/
        GetCalendarDayInfo(&dayIndex, &monthIndex, &year, &seconds);
        randomtime = rand() % 3600;
        APP_LOG("AutoFwUpdate", LOG_DEBUG, "CurTime: %d , randomtime: %d",seconds,randomtime);
        randSleep = (86400 - seconds) + randomtime;


        /* loop until we find Internet connected */
        APP_LOG("AutoFwUpdate", LOG_ERR, "\n Firmware Update Thread Sleeping for %d Seconds\n",randSleep);
        sleep(randSleep);
        /* if Remote access enabled */
        if ((0x00 != strlen(g_szHomeId) ) && (0x00 != strlen(g_szPluginPrivatekey))\
            && (atoi(g_szRestoreState) == 0x0)) {
            /* if Internet available */
            while(1) {
                if (getCurrentClientState() == STATE_CONNECTED) {
                    break;
                }
                pluginUsleep(REMOTE_STATUS_SEND_RETRY_TIMEOUT);  //30 sec
            }
            if ((-1 != firmwareUpThread) || (-1 != fwUpMonitorthread)) {
                APP_LOG("AutoFwUpdate",LOG_DEBUG, "====Firmware Update thread already created====");
                if(-1 != firmwareUpThread) {
                    errnum = pthread_cancel(firmwareUpThread);
                    if(errnum) {
                        APP_LOG("AutoFwUpdate",LOG_ERR, "pthread_cancel ret: %d...",errnum);
                        exit(0);
                    }

                    firmwareUpThread = -1;
                }
                if(-1 != fwUpMonitorthread) {
                    errnum = pthread_cancel(fwUpMonitorthread);
                    if(errnum) {
                        APP_LOG("AutoFwUpdate",LOG_ERR, "pthread_cancel ret: %d...",errnum);
                        exit(0);
                    }

                    fwUpMonitorthread = -1;
                }
                //Do we need to Set the "currFWUpdateState" also to FM_STATUS_DEFAULT or FM_STATUS_DOWNLOAD_UNSUCCESS
                setCurrFWUpdateState(FM_STATUS_DEFAULT);
            }

            if(downloadLink) {
                free(downloadLink);
                downloadLink = NULL;
            }

            downloadLink  = (char *)CALLOC(1, SIZE_1024B);
            if(!downloadLink) {
                APP_LOG("AutoFwUpdate",LOG_ERR, "Memory allocation failed!!!");
                exit(0);
            }

            /* fetch FW upgrade URL */

            if(PLUGIN_SUCCESS == fetchFwUrl(downloadLink)) {
                /* initiate download and upgrade */
                // launch the thread do download Firmware.txt with "downloadLink"
                APP_LOG("AutoFwUpdate",LOG_DEBUG, "====== Download thread Create======");
                pthread_attr_init(&fwTxt_attr);
                pthread_attr_setdetachstate(&fwTxt_attr,PTHREAD_CREATE_DETACHED);
                ret = pthread_create(&fwTxtthread,&fwTxt_attr,(void*)&Downloadtxt, (void*)downloadLink);

                if(ret < SUCCESS) {
                    APP_LOG("AutoFwUpdate",LOG_DEBUG, "======Firmware.txt Download thread not Created======");
                    retVal = FAILURE;
                }
            } else {
                APP_LOG("AutoFwUpdate",LOG_DEBUG, "====== Fetch firmware URL failed");
            }

        }

    }


}
#endif

int StartFirmwareUpdate(FirmwareUpdateInfo fwUpdInf)
{
    int retVal = SUCCESS;
    FirmwareUpdateInfo *pfwUpdInf = NULL;

    if (-1 != fwUpMonitorthread) {
        APP_LOG("UPNP: Device", LOG_ERR, "############Firmware Update thread already created################");
        retVal = FAILURE;
        return retVal;
    }

    pfwUpdInf = (FirmwareUpdateInfo *)CALLOC(1, sizeof(FirmwareUpdateInfo));
    if(!pfwUpdInf) {
        APP_LOG("UPNPApp",LOG_ERR,"pfwUpdInf CALLOC failed...");
        return FAILURE;
    }

    pfwUpdInf->dwldStartTime = fwUpdInf.dwldStartTime;
    pfwUpdInf->withUnsignedImage = fwUpdInf.withUnsignedImage;
    strncpy(pfwUpdInf->firmwareURL, fwUpdInf.firmwareURL, sizeof(pfwUpdInf->firmwareURL)-1);

    pthread_attr_init(&updateFw_attr);
    pthread_attr_setdetachstate(&updateFw_attr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&fwUpMonitorthread,&updateFw_attr,(void*)&FirmwareUpdateStart, (void*)pfwUpdInf);
    if(retVal < SUCCESS) {
        APP_LOG("UPnPApp",LOG_ERR, "************Firmware Update Start thread not Created");
        retVal = FAILURE;
    }

    return retVal;
}

static int IsRuleDbCreated()
{
    int ret = 0x00;
    FILE* pfDb = fopen(RULE_DB_PATH , "r");
    if (pfDb) {
        ret = 0x01;
    }

    fclose(pfDb);

    return ret;
}

char*	GetRuleDBVersionIF(char* buf)
{
    char *szVersion = GetDeviceConfig(RULE_DB_VERSION_KEY);
    if (szVersion && strlen(szVersion)) {
        strncpy(buf, szVersion, SIZE_16B-1);
    } else {
        strncpy(buf, "0", SIZE_16B-1);
    }

    return buf;
}
void	SetRuleDBVersionIF(char* buf)
{
    if (0x00 == buf)
        return;

    SaveDbVersion(buf);
}


char* GetRuleDBPathIF(char* buf)
{

    if (0x00 == buf)
        return 0x00;

    if (IsRuleDbCreated()) {
        strncpy(buf, RULE_DB_PATH, strlen(RULE_DB_PATH));
    }


    return buf;
}
/*
 *
 * Make sure device is not in debounce test
 */

static pthread_t sensorDebounceHandle = -1;
int IsSensorDebounceTime()
{
    int ret = 0x00;

    LockLED();

    if (-1 != sensorDebounceHandle) {
        ret = 0x01;
    }

    UnlockLED();

    return ret;
}


void *RemoveSensorDebounce(void *args)
{
    tu_set_my_thread_name( __FUNCTION__ );
    pluginUsleep(20000000);
    APP_LOG("timer: network", LOG_DEBUG, "sensor debounce monitor expires");
    LockLED();
    sensorDebounceHandle = -1;
    UnlockLED();
    int status;
    ithread_exit(&status);
}


int CreateSensorDebounceMonitor()
{
    LockLED();
    if (-1 != sensorDebounceHandle) {
        ithread_cancel(sensorDebounceHandle);
        sensorDebounceHandle = -1;
        APP_LOG("timer: network", LOG_DEBUG, "sensor debounce monitor removed");
    }

    pthread_create(&sensorDebounceHandle, NULL, RemoveSensorDebounce, NULL);
    pthread_detach(sensorDebounceHandle);

    UnlockLED();

    APP_LOG("timer: network", LOG_DEBUG, "new sensor debounce monitor created");
    return 0;
}


void RestartRule4DST()
{

    //- Calculate now time to deal with edge case of 1:00 AM (DST OFF) and 3:00 AM
    time_t rawTime;
    struct tm * timeInfo;
    time(&rawTime);
    timeInfo = localtime (&rawTime);
    APP_LOG("timer: DST", LOG_DEBUG, "Local time hour:%d", timeInfo->tm_hour);


    if ((DST_TIME_NOW_OFF == timeInfo->tm_hour) ||
        (DST_TIME_NOW_ON == timeInfo->tm_hour) ||
        (DST_TIME_NOW_ON_2 == timeInfo->tm_hour)    //TODO: need to handle similar case of chatham isl. NZ, if gemtek will add support for it
       ) {
        //g_iDstNowTimeStatus = timeInfo->tm_hour;
        g_iDstNowTimeStatus = 0x01;
        APP_LOG("timer: DST", LOG_DEBUG, "g_iDstNowTimeStatus:%d", g_iDstNowTimeStatus);
    } else {
        g_iDstNowTimeStatus = 0x00;
    }


    APP_LOG("Rule", LOG_DEBUG, "DST changed, rule to restart to get executed again");

    gRestartRuleEngine = RULE_ENGINE_RELOAD;
}


char* GetOverridenStatusIF(char* szOutBuff)
{
    if (0x00 == szOutBuff)
        return 0x00;

    lockRule();
    strncpy(szOutBuff, szOverridenStatus, (SIZE_256B*2)-1);
    unlockRule();

    return szOutBuff;
}

/*  @ Function:
 *    ResetOverrideStatus

 *  @ Description:
 *    clean up the storage of override rule, this will be called upon rule get executed and notified
    since it means the executing rule not overridden
 *  @ Parameters:
 *
 *
 *
 *
 */
void ResetOverrideStatus()
{
    memset(szOverridenStatus, 0x00, MAX_OVERRIDEN_STATUS_LEN);
}

/*  IsOverriddenStatus
 *
 *    Check device overridden status in case pushing to device for update
 *
 *
 *
 *
 *
 *
 ********************************************************************************************/
int  IsOverriddenStatus()
{
    int isOverridden = 0x00;
    lockRule();
    if (strlen(szOverridenStatus) > 0x00)

        isOverridden = 0x01;
    unlockRule();

    if (isOverridden)
        PushStoredOverriddenStatus();
    return 0;
}


void PushStoredOverriddenStatus()
{
    char* szCurState[1];

    szCurState[0x00] = (char*)ZALLOC(MAX_RESP_LEN);

    lockRule();
    strncpy(szCurState[0x00], szOverridenStatus, MAX_RESP_LEN-1);
    unlockRule();

    int size = strlen((const char *)szCurState);
    if (size > 0x00) {
        //- Reset the last one
        if ('#' == (int)szCurState[size - 1])
            szCurState[0x00][size - 1] = 0x00;
    }

    char* paramters[] = {"RuleOverrideStatus"} ;

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)szCurState, 0x01);

    APP_LOG("Rule", LOG_DEBUG, "historical overridden status pushed:%s", szCurState[0x00]);

    free(szCurState[0x00]);
}
void Advertisement4TimeUpdate()
{
    if (!IsUPnPNetworkMode()) {
        APP_LOG("UPnP", LOG_DEBUG, "Not home network mode, %s not executed", __FUNCTION__);
        return;
    }

    //- Send per service, 7 services, so no repeating required here
    UpnpSendAdvertisement(device_handle, default_advr_expire);

    APP_LOG("UPnP", LOG_DEBUG, "Not home network mode, %s executed", __FUNCTION__);
}

/*  Function:
 *    isTimeEventInOVerrideTable
 *  Description:
 *    Check the timer event in historical table or not
 *
 **/
int isTimeEventInOVerrideTable(int time, int action)
{
    int isInTable = 0x00;
    char szMatch[SIZE_64B];
    memset(szMatch, 0x00, sizeof(szMatch));
    snprintf(szMatch, sizeof(szMatch), "%d|%d", time, action);

    APP_LOG("Rule", LOG_DEBUG, "rule string to match: %s", szMatch);

    if ( (0x00 != strlen(szOverridenStatus)) && (0x00 != strstr(szOverridenStatus, szMatch))) {
        APP_LOG("Rule", LOG_DEBUG, "string to match found in historical overridden information: %s", szOverridenStatus);
        isInTable = 0x01;
    } else {
        if (0x00 != strlen(szOverridenStatus)) {
            APP_LOG("Rule", LOG_DEBUG, "string to match NOT found in historical overridden information: %s", szOverridenStatus);
        } else {
            APP_LOG("Rule", LOG_DEBUG, "No historical overridden information found");
        }
    }

    return isInTable;
}


/**
 * @brief initSerialRequest: to initial serial number request
 *        Note: in SNS, this request was included in SetApSSID,
 *              it was NOT generic and NOT good for integration
 *
 * @return void
 */
void initSerialRequest()
{
    char szBuff[SIZE_64B];
    memset(szBuff, 0x00, sizeof(szBuff));

    char* szSerialNo = GetSerialNumber();
    if ((0x00 == szSerialNo) || (0x00 == strlen(szSerialNo))) {
        //-User default one
        szSerialNo = DEFAULT_SERIAL_NO;
    }

    strncpy(g_szSerialNo, szSerialNo, sizeof(g_szSerialNo)-1);

    APP_LOG("STARTUP: Device", LOG_DEBUG, "serial: %s", g_szSerialNo);
}

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
int GetGPIO(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest || 0 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    } else {
        char* szAction = Util_GetFirstDocumentItem(request, "GPIO");

        if ((0x00 == szAction) || (0x00 == strlen(szAction))) {
            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS);
            UpnpAddToActionResponse(out, "GetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                    "status", "unsuccess");

            APP_LOG("UPNP: Rule", LOG_ERR, "%s: paramters error", __FUNCTION__);

            return UPNP_E_SUCCESS;
        } else {
#ifdef DEBUG_ENABLE
            char value[2] = {0,};
            char gpiopath[128]= {0,};
            /* write to gpio/export to enable the reset button press/release */
            int direction_fd,value_fd;
            snprintf(gpiopath,sizeof(gpiopath),"/sys/class/gpio/gpio%s/direction",szAction);
            direction_fd = open (gpiopath, O_WRONLY);
            if(direction_fd > 0) {
                write (direction_fd, "in", 3);
                close(direction_fd);
                snprintf(gpiopath,sizeof(gpiopath),"/sys/class/gpio/gpio%s/value",szAction);
                value_fd = open (gpiopath, O_RDONLY);
                if(value_fd > 0) {
                    read (value_fd, value, 1);
                    close(value_fd);
                    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

                    UpnpAddToActionResponse(out, "GetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "GPIO", szAction);
                    UpnpAddToActionResponse(out, "GetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Value", value);
                    FreeXmlSource (szAction);
                    return UPNP_E_SUCCESS;
                }
            }
#endif
            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS);
            UpnpAddToActionResponse(out, "GetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                    "status", "unsuccess");

            APP_LOG("UPNP: Rule", LOG_ERR, "%s: paramters error", __FUNCTION__);
            FreeXmlSource (szAction);
            return UPNP_E_SUCCESS;

        }
    }
}

int SetGPIO(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    } else {
        char* szAction = Util_GetFirstDocumentItem(request, "GPIO");
        char* szValue = Util_GetFirstDocumentItem(request, "Value");

        if ((0x00 == szAction) || (0x00 == strlen(szAction))|| (0x00 == szValue) || (0x00 == strlen(szValue))) {

            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS);
            UpnpAddToActionResponse(out, "SetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                    "status", "unsuccess");

            FreeXmlSource (szAction);
            FreeXmlSource (szValue);
            APP_LOG("UPNP: Rule", LOG_ERR, "%s: paramters error", __FUNCTION__);

            return 0x00;
        } else {
#ifdef DEBUG_ENABLE
            char gpiopath[128]= {0,};
            /* write to gpio/export to enable the reset button press/release */
            int direction_fd,value_fd;
            snprintf(gpiopath,sizeof(gpiopath),"/sys/class/gpio/gpio%s/direction",szAction);
            direction_fd = open (gpiopath, O_WRONLY);
            if (direction_fd > 0) {
                write (direction_fd, "out", 4);
                close(direction_fd);
                snprintf(gpiopath,sizeof(gpiopath),"/sys/class/gpio/gpio%s/value",szAction);
                value_fd = open (gpiopath, O_WRONLY);
                if(value_fd > 0) {
                    write (value_fd, szValue, 2);
                    close(value_fd);
                    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
                    UpnpAddToActionResponse(out, "SetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "GPIO", szAction);
                    UpnpAddToActionResponse(out, "SetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Value", szValue);
                    FreeXmlSource (szAction);
                    FreeXmlSource (szValue);
                    return UPNP_E_SUCCESS;
                }
            }

#endif
            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS);
            UpnpAddToActionResponse(out, "SetGPIO", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                    "status", "unsuccess");

            APP_LOG("UPNP: Rule", LOG_ERR, "%s: paramters error", __FUNCTION__);

            FreeXmlSource (szAction);
            FreeXmlSource (szValue);
            return 0x00;
        }
    }
}
#endif /* PRODUCT_WeMo_SNSV2 */

int GetHKSetupInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int state = false;
    char *payload = NULL;
    char *setupCode = NULL;

    do {
        char setup_key[16];
        char *setupId = NULL;

        setupCode = HomekitstoreGet("SETUP_CODE");

        if (setupCode == NULL) {
            state = false;
            APP_LOG("UPNP: HOMEKIT", LOG_ERR, "Failed to retrieve SETUP_CODE from hkstore");
            break;
        }

        memset(setup_key, 0, sizeof(setup_key));
        if(sscanf(setupCode,"%3s-%2s-%3s", setup_key, &setup_key[3], &setup_key[5]) != 3) {
            state = false;
            APP_LOG("UPNP: HOMEKIT", LOG_ERR, "Wrong SETUP_CODE format: %s", setupCode);
            break;
        }

        setupId = HomekitstoreGet("SETUP_ID");
        if ((setupId == NULL) || (strlen(setupId) != 4)) {
            state = false;
            APP_LOG("UPNP: HOMEKIT", LOG_INFO, "Failed to retrieve SETUP_ID from hkstore");
            break;
        }

#if defined(PRODUCT_WeMo_SNSV2)
        payload = get_setup_payload(0, atoi(setup_key), setupId);
#endif
#if defined(PRODUCT_WeMo_Dimmer)
        payload = get_setup_payload(1, atoi(setup_key), setupId);
#endif
#if defined(PRODUCT_WeMo_LightV2)
        payload = get_setup_payload(2, atoi(setup_key), setupId);
#endif
        state = true;
    } while(false);

    if (state) {
        UpnpActionRequest_set_ErrCode(pActionRequest, 0);

        UpnpAddToActionResponse(out,
                                "GetHKSetupInfo",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "HKSetupKey",
                                payload);
        UpnpAddToActionResponse(out,
                                "GetHKSetupInfo",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "HKSetupCode",
                                setupCode);
    }
    else {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS);
        UpnpAddToActionResponse(out,
                                "GetHKSetupInfo",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");

        APP_LOG("UPNP: HOMEKIT", LOG_ERR, "%s: paramters error", __FUNCTION__);
    }
    if (payload) {
        free(payload);
    }
    return UPNP_E_SUCCESS;
}

int GetSetupDoneStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char* setup_done = NULL;
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    } else {
        setup_done = GetBelkinParameter("SetupDone");
        if ((setup_done == NULL) || (strlen(setup_done) == 0)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            UpnpAddToActionResponse(out, "GetSetupDoneStatus", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "SetupDone", "0");
            return UPNP_E_SUCCESS;
        } else {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            UpnpAddToActionResponse(out, "GetSetupDoneStatus", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "SetupDone", setup_done);
            return UPNP_E_SUCCESS;
        }
    }
}

int SetSetupDoneStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        UpnpAddToActionResponse(out, "SetSetupDoneStatus", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    } else {
        SetBelkinParameter("SetupDone", "1");
        UpnpAddToActionResponse(out, "SetSetupDoneStatus", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "success");
        return UPNP_E_SUCCESS;
    }
}

int getHKSetupState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char* setup_done = NULL;
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    } else {
        setup_done = GetBelkinParameter("HKSetupState");
        if ((setup_done == NULL) || (strlen(setup_done) == 0)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            UpnpAddToActionResponse(out, "getHKSetupState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "HKSetupDone", "0");
            return UPNP_E_SUCCESS;
        } else {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            UpnpAddToActionResponse(out, "getHKSetupState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "HKSetupDone", setup_done);
            return UPNP_E_SUCCESS;
        }
    }
}

int setHKSetupState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int hkSetupDone;
    char* paramters[] = {"HKSetupDone"} ;
    char* paramValue;

    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "setHKSetupState: command paramter invalid");
        UpnpAddToActionResponse(out, "setHKSetupState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    }

    paramValue = Util_GetFirstDocumentItem(request, "HKSetupDone");

    if(paramValue && strlen(paramValue)) {
        hkSetupDone  = atoi(paramValue);
        APP_LOG("UPNPDevice", LOG_DEBUG, "SetHKSetupState -->  %d", hkSetupDone);
        SetBelkinParameter("HKSetupState", paramValue);

        UpnpAddToActionResponse(out, "setHKSetupState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "success");

        UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
                   SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)&paramValue, 1);

        APP_LOG("UPNP: Device", LOG_DEBUG, "Notification: HKSetupDone: %s", paramValue);

        FreeXmlSource(paramValue);
        return UPNP_E_SUCCESS;
    }
    else {
        UpnpAddToActionResponse(out, "setHKSetupState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    }
}

int resetHKConfig(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "resetHKConfig: command paramter invalid");
        UpnpAddToActionResponse(out, "resetHKConfig", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    } else {
        SetBelkinParameter("HKSetupState", "0");
        system("rm -rf /tmp/Belkin_settings/.HomeKitStore");
        system("killall -9 wemohap");
        UpnpAddToActionResponse(out, "resetHKConfig", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "success");
        return UPNP_E_SUCCESS;
    }
}

int setAutoFWUpdate(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int enable;
    //    char* parameters[] = {"enableAutoUpdate"} ;
    char* paramValue;

    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "setAutoFWUpdate: command parameter invalid");
        UpnpAddToActionResponse(out, "setAutoFWUpdate", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    }

    paramValue = Util_GetFirstDocumentItem(request, "enableAutoUpdate");

    if(paramValue && strlen(paramValue)) {
        enable  = atoi(paramValue);
        APP_LOG("UPNPDevice", LOG_DEBUG, "enableAutoUpdate -->  %d", enable);
        if (enable) {
            SetBelkinParameter("fwup_disabled", "0");
        }
        else {
            SetBelkinParameter("fwup_disabled", "1");
        }
        system("sysevent set fwup_config  1");
        UpnpAddToActionResponse(out, "setAutoFWUpdate", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "success");

        FreeXmlSource(paramValue);
        return UPNP_E_SUCCESS;
    }
    else {
        UpnpAddToActionResponse(out, "setAutoFWUpdate", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    }
}

int SetEnforceSecurity(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char* enforce_string = NULL;

    if (pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"%s: paramter failure", __FUNCTION__);
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    enforce_string = Util_GetFirstDocumentItem(request, "SecurityEnabled");

    if( (enforce_string == NULL) || (strlen(enforce_string)) == 0) {
        UpnpActionRequest_set_ErrCode(pActionRequest, PLUGIN_ERROR_E_BASIC_EVENT);
        UpnpAddToActionResponse(out, "SetEnforceSecurity", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE],"status", "Parameters Error");
        APP_LOG("UPNP: Device", LOG_ERR,"SetEnforceSecurity: failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    SetBelkinParameter("Enforce_Security", enforce_string);
    APP_LOG("UPNP: Device", LOG_DEBUG, "Enforce_Security changed to %s", enforce_string);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse(out, "SetEnforceSecurity", CtrleeDeviceServiceType[PLUGIN_E_DEVICEINFO_SERVICE], "SecurityEnabled", enforce_string);
    APP_LOG("UPNP: Device", LOG_DEBUG, "SetEnforceSecurity response sent");
    FreeXmlSource(enforce_string);

    return UPNP_E_SUCCESS;
}

int GetEnforceSecurity(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char* security_enforced = NULL;
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    } else {
        security_enforced = GetBelkinParameter("Enforce_Security");
        if ((security_enforced == NULL) || (strlen(security_enforced) == 0)) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            UpnpAddToActionResponse(out, "GetEnforceSecurity", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "Securityenabled", "0");
            return UPNP_E_SUCCESS;
        } else {
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            UpnpAddToActionResponse(out, "GetEnforceSecurity", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "SecurityEnabled", security_enforced);
            return UPNP_E_SUCCESS;
        }
    }
}

int removeHomekitData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (pActionRequest == NULL || request == NULL) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "removeHomekitData", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "removeHomekitData", "SUCCESS");

    UnSetBelkinParameter("HomeKitSetup");
    UnSetBelkinParameter("HKSetupState");

    system("rm -rf /tmp/Belkin_settings/.HomeKitStore &");
    system("killall -9 wemohap &");
    return UpnpActionRequest_get_ErrCode(pActionRequest);
}

int StartIperf(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        UpnpAddToActionResponse(out, "StartIperf", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    } else {
                /* Make sure we only run one copy of "iperf" */
                system("killall iperf");
        system("iperf -s &");
                UpnpAddToActionResponse(out, "StartIperf", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                        "status", "success");
                return UPNP_E_SUCCESS;
    }
}

int StopIperf(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        UpnpAddToActionResponse(out, "StopIperf", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    } else {
        system("killall iperf");
        UpnpAddToActionResponse(out, "StopIperf", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                "status", "success");
        return UPNP_E_SUCCESS;
    }
}

/**
 * As part of WEMO-39685, it was thought to unify the XML input
 * for StoreRules in both Local and Remote mode by adding a new
 * tag "ruleDbData" in UPnP API. However, this would require the
 * App to treat rest of the XML as data to this new tag.
 * Other approach considered here was to have different parsing
 * in local and remote modes and have an API which takes as
 * parameters ruleDbVersion, processDb and ruleDbBody.
 *
 * But the effort in making these changes in both App and FW
 * seem to outweight the benefits of this change for both DEV & QA.
 * So, not going ahead with changes thought to be done in WEMO-39685.
 */
int StoreRules (pUPnPActionRequest   pActionRequest,
                IXML_Document       *request,
                IXML_Document      **out,
                const char         **errorString)
{
    int              retVal                = UPNP_E_SUCCESS;
    unsigned int     len                   = 0;

    char             aUpnpResp[SIZE_128B]  = {0};

    char            *sRuleDbVersion        = NULL;
    char            *sRuleProcessDb        = NULL;
    char            *sRuleDbBody           = NULL;
    char            *pRuleDbData           = NULL;

    /* Input parameter validation */
    if ((NULL == pActionRequest) || (NULL == request)) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: Invalid arguments", __LINE__);
        retVal = UPNP_E_INVALID_PARAM;
        snprintf (aUpnpResp, sizeof (aUpnpResp), "Invalid Parameters!");
        goto CLEAN_RETURN;
    }

    /* Extract the rule DB version */
    sRuleDbVersion = Util_GetFirstDocumentItem (
                         request,
                         "ruleDbVersion");
    /* Extract the process DB value */
    sRuleProcessDb = Util_GetFirstDocumentItem (
                         request,
                         "processDb");
    /* Extract the rule DB body */
    sRuleDbBody = Util_GetFirstDocumentItem (
                      request,
                      "ruleDbBody");
    if ((NULL == sRuleDbVersion) || (0 == strlen (sRuleDbVersion)) ||
        (NULL == sRuleProcessDb) || (0 == strlen (sRuleProcessDb)) ||
        (NULL == sRuleDbBody)    || (0 == strlen (sRuleDbBody))) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: Error in payload received",
                 __LINE__);
        retVal = UPNP_E_INVALID_ARGUMENT;
        snprintf (aUpnpResp, sizeof (aUpnpResp), "Invalid Payload received!");
        goto CLEAN_RETURN;
    }

    len = strlen (sRuleDbVersion) + strlen (sRuleProcessDb) +
          strlen (sRuleDbBody);

    pRuleDbData = (char *) ZALLOC (len * sizeof (char) + SIZE_128B);
    if (NULL == pRuleDbData) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: Malloc Error", __LINE__);
        retVal = UPNP_E_INVALID_ARGUMENT;
        snprintf (aUpnpResp, sizeof (aUpnpResp), "Invalid rule DB body!");
        goto CLEAN_RETURN;
    }

    snprintf (pRuleDbData, len * sizeof (char) + SIZE_128B,
              "<ruleDbData>"
              "<ruleDbVersion>%s</ruleDbVersion>"
              "<processDb>%s</processDb>"
              "<ruleDbBody>%s</ruleDbBody>"
              "</ruleDbData>",
              sRuleDbVersion, sRuleProcessDb, sRuleDbBody);


    retVal = DecodeRuleDbData (pRuleDbData);
    if (0 == retVal) {
        snprintf (aUpnpResp, sizeof (aUpnpResp),
                  "Storing of rules DB Successful!\n");
    } else {
        snprintf (aUpnpResp, sizeof (aUpnpResp),
                  "Storing of rules DB failed!!\n");
    }

CLEAN_RETURN:
    if (NULL != sRuleDbVersion)
        FreeXmlSource (sRuleDbVersion);

    if (NULL != sRuleProcessDb)
        FreeXmlSource (sRuleProcessDb);

    if (NULL != sRuleDbBody)
        FreeXmlSource (sRuleDbBody);

    if (NULL != pRuleDbData) {
        free (pRuleDbData);
        pRuleDbData = NULL;
    }

    APP_LOG ("UPNPDevice", LOG_DEBUG, "errorInfo: %s", aUpnpResp);

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
    UpnpAddToActionResponse (out,
                             "StoreRules",
                             CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE], "errorInfo",
                             aUpnpResp);

    return retVal;
}

int FetchRules (pUPnPActionRequest   pActionRequest,
                IXML_Document       *request,
                IXML_Document      **out,
                const char         **errorString)
{
    int                      retVal                       = UPNP_E_SUCCESS;

    char                     aUpnpResp[SIZE_50B]          = {0};
    char                     aRuleDBPath[SIZE_256B]       = {0};

    char                    *pRuleDbVersion               = "";

    APP_LOG ("UPNP: Device", LOG_DEBUG, "***** Entered *****");

    /* Input parameter validation */
    if ((NULL == pActionRequest) || (NULL == request)) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: Invalid arguments", __LINE__);
        retVal = UPNP_E_INVALID_PARAM;
        snprintf (aUpnpResp, sizeof (aUpnpResp), "Invalid Parameters!");
        goto CLEAN_RETURN;
    } else
        snprintf (aUpnpResp, sizeof (aUpnpResp), "SUCCESS");

    /* Fetch the rule Db version */
    pRuleDbVersion = GetDeviceConfig (RULE_DB_VERSION_KEY);
    if ((NULL == pRuleDbVersion) || (0 == strlen (pRuleDbVersion))) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: RuleDbVersion Error", __LINE__);
        pRuleDbVersion = "0";
    }

    APP_LOG ("UPNPDevice", LOG_DEBUG, "!!! pRuleDbVersion = %s !!!", pRuleDbVersion);

    /* Fetch the rule Db path */
    snprintf (aRuleDBPath, sizeof (aRuleDBPath), "http://%s:%d/rules.db",
              g_server_ip, g_server_port);
    APP_LOG ("UPNPDevice", LOG_DEBUG, "!!! rule Db path = %s !!!", aRuleDBPath);



CLEAN_RETURN:

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse (out,
                             "FetchRules",
                             CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                             "ruleDbPath", aRuleDBPath);

    UpnpAddToActionResponse (out,
                             "FetchRules",
                             CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                             "ruleDbVersion", pRuleDbVersion);

    UpnpAddToActionResponse (out,
                             "FetchRules",
                             CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                             "errorInfo", aUpnpResp);

    return retVal;
}

void RuleDBVersionNotify()
{
    if (device_handle == -1) {
        return;
    }

    if (!IsUPnPNetworkMode()) {
        //- Not report since not on router or internet
        return;
    }

    char* paramters[] = {"RulesDBVersion"} ;
    char* rule_db_version = GetBelkinParameter(RULE_DB_VERSION_KEY);

    if (strlen(rule_db_version)) {
        UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].UDN,
                   SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].ServiceId,
                   (const char **)paramters,
                   (const char **)&rule_db_version,
                   0x01);
    }
    else {
        char *version[1];
        version[0] = strdup("0");
        if (version[0]) {
            UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].UDN,
                       SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].ServiceId,
                       (const char **)paramters,
                       (const char **)version,
                       0x01);
            free(version[0]);
        }
    }

    APP_LOG("UPNP: Device", LOG_DEBUG, "Notification: RuleDbVersion %s", rule_db_version);
}

int RestartRuleEngine (pUPnPActionRequest   pActionRequest,
                       IXML_Document       *request,
                       IXML_Document      **out,
                       const char         **errorString)
{
    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNP", LOG_ERR, "Parameters error");
        UpnpAddToActionResponse(out, "RestartRuleEngine", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "status", "unsuccess");
        return UPNP_E_INVALID_PARAM;
    } else {
        gRestartRuleEngine = RULE_ENGINE_RELOAD;

        StopRuleEngine();
        /*stop Countdown Timer*/
        stopCountdownTimer();

        UpnpAddToActionResponse(out, "RestartRuleEngine", CtrleeDeviceServiceType[PLUGIN_E_RULES_SERVICE],
                                "status", "success");
        return UPNP_E_SUCCESS;
    }
}

// WEMO-46751 - The current IOS app sends the offset as 12.8/13.8 for
// Chatham Island instead of the correct value of 12.75/13.75.
// Perhaps this was to work around historical bugs in the Gemtek timezone table?
// Correct the value for backwards compatibility.
static char *Wemo46751(char *szTimeZone)
{
    char *NewValue = NULL;
    char *Ret = szTimeZone;

    if(strcmp(szTimeZone,"12.8") == 0) {
        NewValue = "12.75";
    } else if(strcmp(szTimeZone,"13.8") == 0) {
        NewValue = "13.75";
    }

    if(NewValue != NULL) {
        APP_LOG("UPNP: Device",LOG_DEBUG,"WEMO-46751 workaround applied, %s->%s\n",
                szTimeZone,NewValue);
        FreeXmlSource(Ret);
        Ret = STRDUP(NewValue);
    }

    return Ret;
}

#ifdef LONG_PRESS_SUPPORTED
extern const char* szButtonPath;
int SimulateLongPress(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    if (pActionRequest == 0x00 || pActionRequest == 0x00) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"SimulateLongPress: paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);
#ifdef DEBUG_ENABLE
    /* gSimulatedLongPress is not used for dimmer but retained for lightswitch */
    gSimulatedLongPress=1;
#ifdef PRODUCT_WeMo_Dimmer
    unsigned char longPress = LPR_BUTTON_LONG_PRESS;
    UpnpAddToActionResponse(out, "SimulateLongPress", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "LongPress", "1");
    if(SUCCESS != setWaspVariable(WASP_VAR_LONG_PRESS, WASP_VARTYPE_UINT8, (void*)&longPress)) {
        APP_LOG("waspPollTask", LOG_DEBUG, "Setting the Long Press WASP variable failed!!");
    }
#endif // PRODUCT_WeMo_Dimmer
#else
    UpnpAddToActionResponse(out, "SimulateLongPress", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "LongPress", "0");
#endif

    APP_LOG("UPNP: Device", LOG_DEBUG,"Simulated long press");

    return UPNP_E_SUCCESS;

}
#endif


#ifdef PRODUCT_WeMo_Dimmer

int SetBulbType(pUPnPActionRequest   pActionRequest,
                IXML_Document       *request,
                IXML_Document      **out,
                const char         **errorString)
{
    int     retVal                = UPNP_E_SUCCESS;
    char    szUpnpResp[SIZE_32B]  = {0};
    char    *pszBulbType          = NULL;
    int     i                     = 0;
    bool    found                 = false;

    /* Input parameter validation */
    if ((NULL == pActionRequest) || (NULL == request)) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: Invalid arguments", __LINE__);
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Parameters");
        goto CLEAN_RETURN;
    }

    /* Extract the BulbType */
    pszBulbType = Util_GetFirstDocumentItem (
                      request,
                      "bulbType");
    if((NULL == pszBulbType) || (0 == strlen (pszBulbType))) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "Error in payload received");
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Payload");
        goto CLEAN_RETURN;
    }
    /* compare the bulbType and extract the PreSet for this particular bulb type */
    for(i=0; i<BULB_TYPE_COUNT; i++) {
        if(0 == strcasecmp(DimmerBulbPreset[i][0], pszBulbType)) {
            /* bulb type matches to one in the preset,
               copy into the global and store it in Nvram. */
            strncpy(gBulbType, pszBulbType, sizeof(gBulbType)-1);
            gBulbType[sizeof(gBulbType)-1] = '\0';
            SetBelkinParameter(DIMMER_BULB_TYPE, pszBulbType);
            found = true;
            break;
        }
    }
    if(found) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "Bulb type: %s, preset: %s, min: %s, max: %s, turnon: %s", pszBulbType, DimmerBulbPreset[i][0], DimmerBulbPreset[i][1], DimmerBulbPreset[i][2], DimmerBulbPreset[i][3]);
        unsigned char  minLevel = (unsigned char)atoi(DimmerBulbPreset[i][1]);
        unsigned char  maxLevel = (unsigned char)atoi(DimmerBulbPreset[i][2]);
        unsigned char  minOnLevel = (unsigned char)atoi(DimmerBulbPreset[i][3]);

        retVal = setWaspVariable(WASP_VAR_MIN_LEVEL, WASP_VARTYPE_UINT8, (void*)&minLevel);
        if(!retVal && (retVal = setWaspVariable(WASP_VAR_MAX_LEVEL, WASP_VARTYPE_UINT8, (void*)&maxLevel)));
        if(!retVal && (retVal = setWaspVariable(WASP_VAR_MIN_ON_LEVEL, WASP_VARTYPE_UINT8, (void*)&minOnLevel)));
        if(0 != retVal) {
            APP_LOG ("UPNPDevice", LOG_ERR, "WASP Error: %d", retVal);
            retVal = UPNP_E_INTERNAL_ERROR;
            snprintf (szUpnpResp, sizeof (szUpnpResp), "FAILURE");
            goto CLEAN_RETURN;
        }

        UpnpAddToActionResponse (out,
                                 "SetBulbType", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                 "minLevel", DimmerBulbPreset[i][1]);
        UpnpAddToActionResponse (out,
                                 "SetBulbType", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                 "maxLevel", DimmerBulbPreset[i][2]);
        UpnpAddToActionResponse (out,
                                 "SetBulbType", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                                 "turnOnLevel", DimmerBulbPreset[i][3]);
        FreeXmlSource (pszBulbType);
        return retVal;
    } else {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "Error in payload received. Bulb Type does not match.");
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Payload");
    }

CLEAN_RETURN:

    FreeXmlSource (pszBulbType);

    APP_LOG ("UPNPDevice", LOG_DEBUG, "status: %s", szUpnpResp);
    UpnpActionRequest_set_ErrCode(pActionRequest, retVal);
    UpnpAddToActionResponse (out,
                             "SetBulbType", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                             "actionStatus", szUpnpResp);
    return retVal;
}

int Calibrate(pUPnPActionRequest   pActionRequest,
              IXML_Document       *request,
              IXML_Document      **out,
              const char         **errorString)
{
    int           retVal               = UPNP_E_SUCCESS;
    char          szUpnpResp[SIZE_32B] = {0};
    char          *pszState            = NULL;
    char          *pszLevel            = NULL;
    char          *pszFader            = NULL;
    unsigned char state                = 0;
    unsigned char level                = 0;
    char          fader[SIZE_8B]       = {0};
    unsigned char attrSet              = 0;

    /* Input parameter validation */
    if ((NULL == pActionRequest) || (NULL == request)) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: Invalid arguments", __LINE__);
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Parameters");
        goto CLEAN_RETURN;
    }

    /* Extract the parameters */
    pszState = Util_GetFirstDocumentItem (
                   request,
                   "binaryState");
    pszLevel = Util_GetFirstDocumentItem (
                   request,
                   "level");
    pszFader = Util_GetFirstDocumentItem (
                   request,
                   "fader");

    if(pszState && strlen(pszState)) {
        state = atoi(pszState);
        attrSet |= ATTR_STATE;
        APP_LOG("WiFiApp", LOG_DEBUG, "Request to change the state to: %u ", state);
    }
    if(pszLevel && strlen(pszLevel)) {
        level = atoi(pszLevel);
        attrSet |= ATTR_BRIGHTNESS;
        APP_LOG("WiFiApp", LOG_DEBUG, "Request to change the current level to: %u ", level);
    }
    if(pszFader && strlen(pszFader)) {
        strncpy(fader, pszFader, sizeof(fader)-1);
        attrSet |= ATTR_FADER;
    }

    if(!attrSet) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "Error in payload received");
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Payload");
        goto CLEAN_RETURN;
    }
    if(ATTR_STATE & attrSet) {
        /* set variable WASP_VAR_ON_OFF to change the
           state of the dimmer device. */
        retVal = setWaspVariable(WASP_VAR_ON_OFF, WASP_VARTYPE_BOOL, (void*)&state);
    }
    if(ATTR_BRIGHTNESS & attrSet) {
        if(!retVal && (retVal = setWaspVariable(WASP_VAR_CURRENT_LEVEL, WASP_VARTYPE_UINT8, (void*)&level)));
    }
    if(ATTR_FADER & attrSet) {
        unsigned int faderTimeSeconds = 0;
        unsigned int toLevel = 0;
        sscanf(fader, "%u:%u", &faderTimeSeconds, &toLevel);
        /* if fader is in the request, level represents the minLevel
           toLevel represents the maxLevel */
        if((level && level <=255) && (toLevel && toLevel<=255)) {
            gMinLevel = level;
            gMaxLevel = toLevel;
            if(!retVal && (retVal = setWaspVariable(WASP_VAR_MIN_LEVEL, WASP_VARTYPE_UINT8, (void*)&gMinLevel)));
            if(!retVal && (retVal = setWaspVariable(WASP_VAR_MAX_LEVEL, WASP_VARTYPE_UINT8, (void*)&gMaxLevel)));
        }

        unsigned short faderTimeWasp = (unsigned short)(faderTimeSeconds*20); /* The faderTimeWasp is in .05 second steps. */
        if(!retVal && (retVal = setWaspVariable(WASP_VAR_FADE_TIME, WASP_VARTYPE_UINT16, (void*)&faderTimeWasp)));

        unsigned char toLevelWasp = (unsigned char)100; /* ((toLevel-gMinLevel)*100/(gMaxLevel - gMinLevel)); conversion formula to WASP level */
        APP_LOG("WiFiApp", LOG_DEBUG, "Request to fade the plugin to level: %u (target brightness: %u) in %u seconds", toLevel, toLevelWasp, faderTimeSeconds);
        if(!retVal && (retVal = setWaspVariable(WASP_VAR_TARGET_BRIGHTNESS, WASP_VARTYPE_UINT8, (void*)&toLevelWasp)));
    }

    if(0 == retVal) {
        snprintf (szUpnpResp, sizeof (szUpnpResp), "SUCCESS");
    } else {
        retVal = UPNP_E_INTERNAL_ERROR;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "FAILURE");
    }

CLEAN_RETURN:

    FreeXmlSource (pszState);
    FreeXmlSource (pszLevel);
    FreeXmlSource (pszFader);

    APP_LOG ("UPNPDevice", LOG_DEBUG, "status: %s", szUpnpResp);
    UpnpActionRequest_set_ErrCode(pActionRequest, retVal);
    UpnpAddToActionResponse (out,
                             "Calibrate",
                             CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "actionStatus",
                             szUpnpResp);
    return retVal;
}

int ConfigureDimmingRange(pUPnPActionRequest   pActionRequest,
                          IXML_Document       *request,
                          IXML_Document      **out,
                          const char         **errorString)
{
    int              retVal                  = UPNP_E_SUCCESS;
    char             szUpnpResp[SIZE_32B]    = {0};

    char            *pszMinBrightness        = NULL;
    char            *pszMaxBrightness        = NULL;
    char            *pszTurnOnBrightness     = NULL;
    unsigned char   minBrightnessWasp        = 0;
    unsigned char   maxBrightnessWasp        = 0;
    unsigned char   turnOnBrightnessWasp     = 0;

    /* Input parameter validation */
    if ((NULL == pActionRequest) || (NULL == request)) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: Invalid arguments", __LINE__);
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Parameters");
        goto CLEAN_RETURN;
    }

    /* Extract the parameters */
    pszMinBrightness = Util_GetFirstDocumentItem (
                           request,
                           "minLevel");
    pszMaxBrightness = Util_GetFirstDocumentItem (
                           request,
                           "maxLevel");
    pszTurnOnBrightness = Util_GetFirstDocumentItem (
                              request,
                              "turnOnLevel");

    if ((NULL == pszMinBrightness) || (0 == strlen (pszMinBrightness)) ||
        (NULL == pszMaxBrightness) || (0 == strlen (pszMaxBrightness)) ||
        (NULL == pszTurnOnBrightness)    || (0 == strlen (pszTurnOnBrightness))) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "Error in payload received");
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Payload");
        goto CLEAN_RETURN;
    }


    /* WEMO-52475: Being redundant to facilitate revert, if need be */
    //gMinLevel = atoi(pszMinBrightness);
    gMinLevel = atoi(pszTurnOnBrightness);
    gMaxLevel = atoi(pszMaxBrightness);

    minBrightnessWasp = gMinLevel;
    maxBrightnessWasp = gMaxLevel;
    turnOnBrightnessWasp = atoi(pszTurnOnBrightness);

    if((minBrightnessWasp && minBrightnessWasp <= 255) && (maxBrightnessWasp && maxBrightnessWasp <= 255) &&
       (maxBrightnessWasp && maxBrightnessWasp <= 255)) {
        /* Set WASP variables */
        retVal = setWaspVariable(WASP_VAR_MIN_LEVEL, WASP_VARTYPE_UINT8, (void*)&minBrightnessWasp);
        if(!retVal && (retVal |= setWaspVariable(WASP_VAR_MAX_LEVEL, WASP_VARTYPE_UINT8, (void*)&maxBrightnessWasp)));
        if(!retVal && (retVal |= setWaspVariable(WASP_VAR_MIN_ON_LEVEL, WASP_VARTYPE_UINT8, (void*)&turnOnBrightnessWasp)));
    } else {
        retVal = UPNP_SOAP_E_INVALID_ARGS;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "Invalid Value(s)");
        goto CLEAN_RETURN;
    }

    if (0 == retVal) {
        snprintf (szUpnpResp, sizeof (szUpnpResp), "SUCCESS");
    } else {
        retVal = UPNP_E_INTERNAL_ERROR;
        snprintf (szUpnpResp, sizeof (szUpnpResp), "FAILURE");
    }

CLEAN_RETURN:

    FreeXmlSource (pszMinBrightness);
    FreeXmlSource (pszMaxBrightness);
    FreeXmlSource (pszTurnOnBrightness);

    APP_LOG ("UPNPDevice", LOG_DEBUG, "status: %s", szUpnpResp);

    UpnpActionRequest_set_ErrCode(pActionRequest, retVal);
    UpnpAddToActionResponse (out,
                             "ConfigureDimmingRange", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                             "dimmingRangeStatus", szUpnpResp);
    return retVal;
}
#endif

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
int SimulateOverTemp(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    unsigned char overTemp=0;
    char *pszOverTemp=NULL;

    if (pActionRequest == NULL || request == NULL) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

#ifdef DEBUG_ENABLE
    pszOverTemp = Util_GetFirstDocumentItem (request, "overTemp");

    if(!pszOverTemp || !strlen(pszOverTemp) || (((overTemp = atoi(pszOverTemp)) != 0) && (overTemp != 1))) {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
        UpnpAddToActionResponse(out, "SimulateOverTemp", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "overTempStatus", "Error");
    }
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    else if (overTemp == 1) {
        system("echo \"out\" > /sys/class/gpio/gpio18/direction ;echo \"1\" > /sys/class/gpio/gpio18/value");
        UpnpAddToActionResponse(out, "SimulateOverTemp", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "overTempStatus", pszOverTemp);
    } else {
        system("echo \"out\" > /sys/class/gpio/gpio18/direction ;echo \"0\" > /sys/class/gpio/gpio18/value");
        UpnpAddToActionResponse(out, "SimulateOverTemp", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "overTempStatus", pszOverTemp);
    }
#else //PRODUCT_WeMo_SNSV2
    else if (overTemp == 1) {
        //temperature sensor input is active low
        system("echo \"out\" > /sys/class/gpio/gpio18/direction ;echo \"0\" > /sys/class/gpio/gpio18/value");
        UpnpAddToActionResponse(out, "SimulateOverTemp", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "overTempStatus", pszOverTemp);
    } else {
        //temperature sensor input is active low
        system("echo \"out\" > /sys/class/gpio/gpio18/direction ;echo \"1\" > /sys/class/gpio/gpio18/value");
        UpnpAddToActionResponse(out, "SimulateOverTemp", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "overTempStatus", pszOverTemp);
    }
#endif //PRODUCT_WeMo_SNSV2
#else  //DEBUG_ENABLE
    UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS);
    UpnpAddToActionResponse(out, "SimulateOverTemp", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "overTempStatus", "Error");
    APP_LOG("UPNP: OverTemp", LOG_ERR, "%s: paramters error", __FUNCTION__);
#endif //DEBUG_ENABLE

    APP_LOG("UPNP: Device", LOG_DEBUG,"Simulated OverTemp: %d", overTemp);

    FreeXmlSource(pszOverTemp);
    return UpnpActionRequest_get_ErrCode(pActionRequest);
}
#endif

#if defined(PRODUCT_WeMo_Dimmer)
#define MAX_COMMAND_LEN 20
int setRGBLeds(char option,int ledRed)
{
    int ledGreen = ledRed+1;
    int ledRedVal,ledGreenVal,ledBlueVal;
    int ledBlue = ledRed+2;
    char command[MAX_COMMAND_LEN];
    switch(option) {
    case 'r':
        ledRedVal = 255;
        ledGreenVal = 0;
        ledBlueVal = 0;
        break;
    case 'g':
        ledRedVal = 0;
        ledGreenVal = 255;
        ledBlueVal = 0;
        break;

    case 'b':
        ledRedVal = 0;
        ledGreenVal = 0;
        ledBlueVal = 255;
        break;
    case '0':
        ledRedVal = 255;
        ledGreenVal = 0;
        ledBlueVal = 0;
        break;
    default :
        APP_LOG("UPNP: Device", LOG_DEBUG,"paramter failure");
        return FAILURE;
    }
    snprintf(command,MAX_COMMAND_LEN,"waspd -s130:12:%d",ledRed);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s131:12:%d",ledRedVal);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s130:12:%d",ledGreen);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s131:12:%d",ledGreenVal);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s130:12:%d",ledBlue);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s131:12:%d",ledBlueVal);
    system(command);
    return SUCCESS;
}

int setLevelLeds(char option,int level)
{
    int ledA = 31+level;
    int ledB = 15+level;
    int ledAVal,ledBVal;
    char command[MAX_COMMAND_LEN];
    switch(option) {
    case '0':
        ledAVal = 0;
        ledBVal = 0;
        break;
    case '1':
        ledAVal = 0;
        ledBVal = 255;
        break;

    case '2':
        ledAVal = 255;
        ledBVal = 0;
        break;
    default :
        APP_LOG("UPNP: Device", LOG_DEBUG,"paramter failure");
        return FAILURE;
    }
    snprintf(command,MAX_COMMAND_LEN,"waspd -s130:12:%d",ledA);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s131:12:%d",ledAVal);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s130:12:%d",ledB);
    system(command);
    snprintf(command,MAX_COMMAND_LEN,"waspd -s131:12:%d",ledBVal);
    system(command);
    return SUCCESS;
}
int
TestLEDs(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    unsigned char testEnable=0;
    int retVal = SUCCESS;
    char *ptestEnable=NULL;

    if (pActionRequest == NULL || request == NULL) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    ptestEnable = Util_GetFirstDocumentItem (request, "TestEnable");

    if(!ptestEnable || !strlen(ptestEnable) || (((testEnable = atoi(ptestEnable)) != 0) && (testEnable != 1))) {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
        UpnpAddToActionResponse(out, "TestLEDs", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "actionStatus", "Error");
        FreeXmlSource(ptestEnable);
        return UpnpActionRequest_get_ErrCode(pActionRequest);
    } else if (testEnable == 1) {
        char *pTestOption = Util_GetFirstDocumentItem (request, "Option");
        if(!pTestOption || !strlen(pTestOption) ) {
            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
            UpnpAddToActionResponse(out, "TestLEDs", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "actionStatus", "Error");
            FreeXmlSource(ptestEnable);
            return UpnpActionRequest_get_ErrCode(pActionRequest);
        }
        switch(pTestOption[0]) {
        case 'a':
            system("waspd -s128:12:2");
            break;
        case 'l':
            retVal = setRGBLeds(pTestOption[1],12);
            break;
        case 'm':
            retVal = setRGBLeds(pTestOption[1],8);
            break;
        case 'r':
            retVal = setRGBLeds(pTestOption[1],4);
            break;
        case 't':
            retVal = setRGBLeds(pTestOption[1],0);
            break;
        case '1' :
        case '2' :
        case '3' :
        case '4' :
        case '5' :
        case '6' :
        case '7' :
            retVal = setLevelLeds(pTestOption[1],pTestOption[0]-'0');
            break;
        case '8' :
            system("waspd -s128:12:7");
            break;
        case '9' :
            system("waspd -s128:12:3");
            break;
        default :
            retVal = FAILURE;

        }
        if(retVal == FAILURE) {
            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
            UpnpAddToActionResponse(out, "TestLEDs", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "actionStatus", "Error");
            FreeXmlSource(ptestEnable);
            return UpnpActionRequest_get_ErrCode(pActionRequest);
        }
    } else {
        /* stop the LED test and reset LPR_Mode to normal operation */
        system("waspd -s128:12:3; waspd  -s128:12:1");
    }

    APP_LOG("UPNP: Device", LOG_DEBUG,"TestLEDs Enable: %d", testEnable);
    UpnpAddToActionResponse(out, "TestLEDs", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "actionStatus", "Success");

    FreeXmlSource(ptestEnable);
    return UpnpActionRequest_get_ErrCode(pActionRequest);
}
#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
int
ConfigureHushMode(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    char *pHushParameter = NULL;
    if (pActionRequest == NULL || request == NULL) {
        APP_LOG("UPNP: Device", LOG_DEBUG,"paramter failure");
        return PLUGIN_ERROR_E_BASIC_EVENT;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    pHushParameter = Util_GetFirstDocumentItem (request, "hushMode");

    if(NULL == pHushParameter) {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
        UpnpAddToActionResponse(out, "ConfigureHushMode", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "hushStatus", "Error");
    } else {
        int mode = -1, selectedSuspendedOption = -1;
        unsigned long referenceUtc = 0;
        APP_LOG("UPNPDevice", LOG_DEBUG, "Set Hush Mode to %s", pHushParameter);
        int retVal = sscanf(pHushParameter, "%d:%lu:%d", &mode, &referenceUtc, &selectedSuspendedOption);
        if(retVal != 3)
            retVal = FAILURE;
        else
            retVal = startHushMode(mode, selectedSuspendedOption);

        if(retVal != SUCCESS) {
            UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_SOAP_E_INVALID_ARGS); /* Invalid Args */
            UpnpAddToActionResponse(out, "ConfigureHushMode", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "hushStatus", "Error");
        } else {
            UpnpAddToActionResponse(out, "ConfigureHushMode", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "hushStatus", "SUCCESS");
        }
    }
    FreeXmlSource(pHushParameter);
    return UpnpActionRequest_get_ErrCode(pActionRequest);

}

int identifyDevice(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "identifyDevice", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "DeviceIdentification", "SUCCESS");
    system("/sbin/identify_hw.sh");

    return UpnpActionRequest_get_ErrCode(pActionRequest);
}

int setDummyMode(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    UpnpAddToActionResponse(out, "setDummyMode", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "dummyMode", "SUCCESS");

    system("nvram set dummy_mode=1");
    system("/sbin/go_dummy.sh &");
    return UpnpActionRequest_get_ErrCode(pActionRequest);
}
#endif

int
CheckResetButtonState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    FILE * pButtonFile = 0x00;
    char szflag[SIZE_4B];
    char* pResult = 0x00;
    int command = BUTTON_RELEASED;
    extern char *szResetBottonPath;

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    /* open gpio38 in read mode */
    pButtonFile = fopen(szResetBottonPath, "r");
    if (NULL == pButtonFile) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "Open Reset Button file : %s failed.", szResetBottonPath);

        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_E_INTERNAL_ERROR;
        UpnpAddToActionResponse(out, "CheckResetButtonState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "resetButtonStatus", "Error");
        return UpnpActionRequest_get_ErrCode(pActionRequest);
    }

    pResult = fgets(szflag, sizeof(szflag), pButtonFile);
    if (pResult != 0x00) {
        if (0x0 != strlen(szflag))
            command = atoi(szflag);
        if (GPIO_BUTTON_PRESSED == command) {
            APP_LOG("UPNPDevice", LOG_DEBUG, "Reset Button is in pressed state.");
        } else {
            APP_LOG("UPNPDevice", LOG_DEBUG, "Reset Button is in released state.");
        }
        /* respond 1 for the button press state and 0 for release state. */
        snprintf(szflag, sizeof(szflag), "%d", !command);
        UpnpAddToActionResponse(out, "CheckResetButtonState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "resetButtonStatus", szflag);
    } else {
        UpnpActionRequest_set_ErrCode(pActionRequest, UPNP_E_INTERNAL_ERROR;
        UpnpAddToActionResponse(out, "CheckResetButtonState", CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "resetButtonStatus", "Error");
    }
    fclose(pButtonFile);

    return UpnpActionRequest_get_ErrCode(pActionRequest);
}
#endif


/************************/
/* emacs settings.       */
/* Please do not remove  */
/* Local Variables:      */
/* indent-tabs-mode: nil */
/* tab-width: 4          */
/* c-basic-offset: 4     */
/* End:                  */
/************************/
