/***************************************************************************
*
*
* controlledevice.h
*
* Copyright (c) 2012-2014 Belkin International, Inc. and/or its affiliates.
* All rights reserved.
*
* Permission to use, copy, modify, and/or distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
*
*
* THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO ALL IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT,
* INCIDENTAL, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
* RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
* NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH
* THE USE OR PERFORMANCE OF THIS SOFTWARE.
*
*
***************************************************************************/

#ifndef CONTROLLEDEVICE_H_
#define CONTROLLEDEVICE_H_

#include <upnp.h>
#include "upnpCommon.h"
#include "itc.h"
#include "global.h"
#include "wemodefs.h"
#if defined(PRODUCT_WeMo_Insight) || defined(PRODUCT_WeMo_SNS)
#include "WemoDB.h"
#endif
#ifdef PRODUCT_WeMo_Insight
#include "insightUPnPHandler.h"
#endif
#include <stdbool.h>

#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
#include "dimmer_attr.h"
#endif

#define ACTUATION_TIME_RULE	"Automatic - TimeRule"
#define ACTUATION_COUNTDOWN_TIMER_RULE	"Automatic - CountDownTimerRule"

#define IS_FIRMWARE_FLASHING (getCurrFWUpdateState() == FM_STATUS_UPDATE_STARTING)
typedef enum {PLUGIN_E_SETUP_SERVICE = 0x00,
              PLUGIN_E_TIME_SYNC_SERVICE,
              PLUGIN_E_EVENT_SERVICE,
              PLUGIN_E_FIRMWARE_SERVICE,
              PLUGIN_E_RULES_SERVICE,
              PLUGIN_E_METAINFO_SERVICE,
#ifdef PRODUCT_WeMo_Insight
              PLUGIN_E_INSIGHT_SERVICE,
#endif
              PLUGIN_E_BRIDGE_SERVICE,
              PLUGIN_E_DEVICEINFO_SERVICE,
              PLUGIN_E_SMART_SETUP_SERVICE,
              PLUGIN_E_MANUFACTURE_SERVICE,
              PLUGIN_MAX_SERVICES
             } PLUGIN_SERVICE_TYPE;

#define DEFAULT_INVALID_IP_ADDRESS	"0.0.0.0"

#define PLUGIN_MAX_ACTION_NAME SIZE_32B

#define ERROR_MESSAGE_BASE 100

#define PLUGIN_ERROR_E_TIME_SYNC ERROR_MESSAGE_BASE + 1
#define PLUGIN_ERROR_E_BASIC_EVENT ERROR_MESSAGE_BASE + 2
#define PLUGIN_ERROR_E_NETWORK_ERROR ERROR_MESSAGE_BASE + 3
#define PLUGIN_ERROR_E_REMOTE_ACCESS ERROR_MESSAGE_BASE + 4
#define PLUGIN_ERROR_E_RULE ERROR_MESSAGE_BASE + 5



#define PLUGIN_UNSUCCESS 0x01

#define RULE_DB_PATH		"/tmp/Belkin_settings/rules.db"
#define DEFAULT_WEB_DIR		"/tmp/Belkin_settings"
#define ICON_FILE_URL           "/tmp/Belkin_settings/icon.jpg"

#define DEFAULT_DEVICE_TYPE_KEY "DeviceType"

#define DEFAULT_FIRMWARE_VERSION "1.0.0.0"

#define DEFAULT_SKU_NO "Plugin Device"

//#define DEFAULT_HOME_ID "HomeId"
#define DEFAULT_HOME_ID "home_id"

#define DEFAULT_SMART_DEVICE_ID "SmartDeviceId"

#define DEFAULT_SMART_PRIVATE_KEY "SmartPrivatekey"

#define DEFAULT_SMART_DEV_ID "smtDeviceID"
#define DEFAULT_SMART_DEV_NAME "smtDeviceNAME"

#define SLOW_COOKER_START_TIME "SlowCookerStartTime"

#define CLOUD_AUTH_FAIL_ERR_CODE "ERR_002"
#define CLOUD_AUTH_EXPIRE_ERR_CODE "ERR_006"
#define CLOUD_HOME_EXIST_ERR_CODE "PLGN_AUTH_401"

//#define DEFAULT_PLUGIN_PRIVATE_KEY "PluginPrivatekey"
#define DEFAULT_PLUGIN_PRIVATE_KEY "plugin_key"

#define DEFAULT_PLUGIN_CLOUD_ID "PluginCloudId"

#define CLOUD_SERVER_ENVIRONMENT "ServerEnvironment"
#define CLOUD_SERVER_ENVIRONMENT_TYPE "ServerEnvironmentType"
#define CLOUD_TURNSERVER_ENVIRONMENT "TurnServerEnvironment"

#ifdef PRODUCT_WeMo_Light
#define DIMVALUE			"DimValue"
#define	DEFAULT_DIMVALUE		"0"	// No Dimming
#endif
#define MAX_DBQUERY_LEN								256
#define MAX_RULEMSG_LEN								256
#define MAX_RULEID_LEN								15
#define MAX_RULEFREQ_LEN							15

//#define DEFAULT_SOCKET_FRIENDLY_NAME "Belkin Socket Device"
//#define DEFAULT_SENSOR_FRIENDLY_NAME "Belkin Sensor Device"

#if defined(PRODUCT_WeMo_SNSV2)
#define DEFAULT_SOCKETV2_FRIENDLY_NAME "Wemo Mini"
#else
#define DEFAULT_SOCKET_FRIENDLY_NAME "WeMo Switch"
#endif
#define DEFAULT_INSIGHT_FRIENDLY_NAME "WeMo Insight"
#define DEFAULT_SENSOR_FRIENDLY_NAME "WeMo Motion"
#define DEFAULT_BABY_FRIENDLY_NAME "WeMo Baby"
#define DEFAULT_LIGHTSWITCH_FRIENDLY_NAME "WeMo Light Switch"
#define DEFAULT_LIGHTSWITCHV2_FRIENDLY_NAME "Wemo Light Switch"
#define DEFAULT_LIGHTSWITCH3WAY_FRIENDLY_NAME "Wemo Light Switch 3-Way"
#define DEFAULT_STREAMING_FRIENDLY_NAME "WeMo Streaming"
#define DEFAULT_BRIDGE_FRIENDLY_NAME "WeMo Link"
#define DEFAULT_NETCAM_FRIENDLY_NAME "NetCam Motion"
#define DEFAULT_LINKSYSWNC_FRIENDLY_NAME "Linksys WNC Motion"
#define DEFAULT_SBIRON_FRIENDLY_NAME    "Sunbeam® Iron"
#define DEFAULT_MRCOFFEE_FRIENDLY_NAME  "MR. COFFEE®"
#define DEFAULT_PETFEEDER_FRIENDLY_NAME "Oster® Pet Feeder"
#define DEFAULT_CROCKPOT_FRIENDLY_NAME  "Crock-Pot® Slow Cooker"
#define DEFAULT_SMART_FRIENDLY_NAME     "WeMo Smart Module"
#define DEFAULT_MAKER_FRIENDLY_NAME     "WeMo Maker"
#define DEFAULT_ECHO_FRIENDLY_NAME              "EchoWater"
#define DEFAULT_DIMMER_FRIENDLY_NAME "Wemo Dimmer"

#undef  ERROR_BASE		/* Conflict w/ definition in wemodefs.h */
#define STATUS_BASE                      0x400
#define ERROR_BASE                      -(STATUS_BASE)

#define SET_CPTIME_FAILURE                      (ERROR_BASE|0x1)
#define SET_CPMODE_FAILURE                      (ERROR_BASE|0x2)
#define SET_PARAMS_NULL                         (ERROR_BASE|0x3)
#define SET_PARAM_ERROR                         (ERROR_BASE|0x4)
#define GET_PARAMS_NULL                         (ERROR_BASE|0x5)
#define GET_PARAM_ERROR                         (ERROR_BASE|0x6)
#define GET_CPTIME_FAILURE                      (ERROR_BASE|0x7)
#define GET_CPMODE_FAILURE                      (ERROR_BASE|0x8)

#define RULE_DB_VERSION_KEY					"RuleDbVersion"

#define	RULE_ACTION_REMOVE					2

#define ICON_VERSION_KEY					"IconVersion"

//#define RESTORE_PARAM "RestoreParam"
#define RESTORE_PARAM "restore_state"

#define SETTIME_SEC "settime_sec"

#define		TIME_ZONE_NORTH_AMERICA_INDEX          22
#define		TIME_ZONE_ASIA_INDEX				   40
#define		TIME_ZONE_UK_INDEX				   26
#define		TIME_ZONE_FRANCE_INDEX				   31
#define		TIME_ZONE_NORTH_AMERICA_NTP_SERVER	   "192.43.244.18"
#define		TIME_ZONE_ASIA_NTP_SERVER	   		   "129.132.2.21"
#define		TIME_ZONE_EUROPE_NTP_SERVER	   	   "130.149.17.8"

#define		LOCAL_DST_SUPPORTED_OFF					0x00
#define  	LOCAL_DST_SUPPORTED_ON					0x01
#define		DST_ON									0x01
#define		DST_OFF									0x00


#define UK_TIMEZONE 0.00 //(GMT)
#define FRANCE_TIMEZONE 1.00 //(GMT+1:00)
#define CHINA_TIMEZONE 8.00 //(GMT+8:00)
#define JAPAN_TIMEZONE 9.00 //(GMT+9:00)
#define AUS_TIMEZONE_1 10.00 //(GMT+10:00)
#define AUS_TIMEZONE_2 9.50 //(GMT+9:30) Countries: Broken Hill and Adelaide
#define AUS_TIMEZONE_3 10.50 //(GMT+10:30) Country: Lord Howe Island
#define AUS_TIMEZONE_4 11.00 //(GMT+11:00) Country: Magada
#define NZ_TIMEZONE_1 12.00 //(GMT+12:00) Wellington, Auckland, Fiji, Kamchatka, Marshall Is.
#define NZ_TIMEZONE_2 12.75 //(GMT+12:45) Waitangi, Chatham Island

#if defined(PRODUCT_WeMo_SNSV2)
#define SSID_PREFIX_SWITCHV2  "Wemo.Mini."
#else
#define SSID_PREFIX_SWITCH  "WeMo.Switch."
#endif
#define SSID_PREFIX_INSIGHT "WeMo.Insight."
#define SSID_PREFIX_LIGHT   "Wemo.Light."
#define SSID_PREFIX_LIGHTV2   "Wemo.LightV2."
#define SSID_PREFIX_LIGHT3WAY   "Wemo.LS3W."
#define SSID_PREFIX_CROCK   "WeMo.SlowCooker."
#define SSID_PREFIX_MOTION  "WeMo.Motion."
#define SSID_PREFIX_SBIRON   "WeMo.Iron."
#define SSID_PREFIX_MRCOFFEE   "WeMo.CoffeeMaker."
#define SSID_PREFIX_PETFEEDER   "WeMo.PetFed."
#define SSID_PREFIX_SMART	   "WeMo.Smart."
#define SSID_PREFIX_BRIDGE  "WeMo.Link."
#define SSID_PREFIX_DIMMER  "Wemo.Dimmer."
#define SSID_PREFIX_ERROR   "WeMo.Error."

#define LOG_UPLOAD_ENABLE       "LOG_UPLOAD_ENABLE"
#define UPNP_E_NO_DEVICES             800
#define UPNP_E_OPEN_NETWORK           801
#define UPNP_E_NO_CAPABILITYID        802
#define UPNP_E_CAPABILITYPROFILELIST  803
#define UPNP_E_DEVICECAPABILITY       804
#define UPNP_E_GETGROUPS              805
#define UPNP_E_PARSING                806
#define UPNP_E_COMMAND                807
#define UPNP_E_SCANJOIN               808

#define WAIT_10MS                     10000   //0.01 sec
#define WAIT_20MS                     20000   //0.02 sec
#define WAIT_30MS                     30000   //0.03 sec
#define WAIT_50MS                     50000   //0.05 sec
#define WAIT_100MS                    100000  //0.1 sec
#define WAIT_200MS                    200000  //0.2 sec
#define WAIT_350MS                    350000  //0.35 sec
#define WAIT_500MS                    500000  //0.5 sec
#define WAIT_1SEC                    1000000  //1 sec
#define WAIT_2SEC                    2000000  //2 sec
#define WAIT_3SEC                    3000000  //3 sec
#define WAIT_5SEC                    5000000  //5 sec

//------------------------------

extern char *CtrleeDeviceServiceType[];

extern UpnpDevice_Handle device_handle;

#define		SIZE_UDN		SIZE_64B
#define		MAX_RETRY_INTERVAL	300
#define		INIT_RETRY_INTERVAL	4
/* max count to check free memory before firmware update */
#define         MAX_FW_UPGRD_RETRY_COUNT 1

extern char* ip_address;
extern char* desc_doc_name;
extern char* web_dir_path;

extern char  g_server_ip[SIZE_32B];
extern unsigned short g_server_port;
extern unsigned int szDeviceID;

extern char g_szWiFiMacAddress[SIZE_64B];
extern char g_szSerialNo[SIZE_64B];

extern char g_szFirmwareVersion[SIZE_64B];
extern char g_szSkuNo[SIZE_64B];
extern char	g_szUDN[];
extern char	g_szUDN_1[];

extern char g_szClientType[SIZE_128B];

extern char g_szFriendlyName[SIZE_256B];

extern int gWebIconVersion;
extern char* s_szNtpServer;
extern float g_lastTimeZone;
extern int gDstEnable;
#if defined(PRODUCT_WeMo_Insight) || defined(PRODUCT_WeMo_SNS)

extern sqlite3 *g_APNSRuleDB;
extern char g_SendInsightParams;
extern int g_isExecuteTimerToo;
extern int g_isDSTApplied;
extern int g_overlappedInsightAction;
extern unsigned int g_StateLog;
#endif

extern char g_szActuation[SIZE_128B];
extern char g_szRemote[SIZE_8B];

int ControlleeDeviceStart(char *if_name,
                          unsigned short port,
                          char *desc_doc_name,
                          char *web_dir_path);

/****
 * Stop all AP related, release resource
 *
 * BE careful the sensor
 *
 *
 *
 *
 **********************************************************/
char *GetWemoMacAddress(void);
char *GetWemoFriendlyName(void);
char *GetWemoFirmwareVersion(void);
char *GetWemoDeviceUDN(void);
const char *getProductName(char *);
void getModelCode(char *);

int ControlleeDeviceStop();

int ControlleeDeviceStateTableInit(
    /*! [in] The description document URL. */
    char *DescDocURL);

int PluginDeviceHandleSubscriptionRequest(UpnpSubscriptionRequest *);

int PluginDeviceHandleGetVarRequest(UpnpStateVarRequest *);

int PluginDeviceHandleActionRequest(UpnpActionRequest *);

#define PLUGIN_MAX_VARS                               16
#define PLUGIN_MAX_ACTIONS                            16


/*! Setup service */
//#define PLUGIN_E_SETUP_SERVICE                      0

/*! Time sync service */
//#define PLUGIN_E_TIME_SYNC_SERVICE                  1

/*! Events including sensor and control service */
//#define PLUGIN_E_EVENT_SERVICE                      2

/*! RUELS service including rule add, remove, enable, disable and etc */
//#define PLUGIN_E_RULES_SERVICE                      3

/*! RUELS service including rule add, remove, enable, disable and etc */
//#define PLUGIN_E_FIRMWARE_SERVICE                   4


extern char* szServiceTypeInfo[];

typedef int (*upnp_action)(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

struct plugin_device_upnp_action {
    const char*         actionName;
    upnp_action         pUpnpAction;
    unsigned int        secure;
};
typedef struct plugin_device_upnp_action PluginDeviceUpnpAction, *pDeviceAction;

struct Device_Params {
    int Internet;
    int CloudVia;
    int CloudConnectivity;
    int LastAuthVal;
    char UpTime[SIZE_64B];
    int LastFWUpdateStatus;
    int NowTime;
    char *HomeID;
    char *DeviceID;
    int RemoteAccess;
    char DeviceInfo[SIZE_128B];
};
typedef struct Device_Params PluginParams;
PluginParams gPluginParms;


/** The data struct for device servuce */
struct plugin_service {
    char                           UDN[SIZE_UDN];
    char                           ServiceId[NAME_SIZE];
    char                           ServiceType[NAME_SIZE];
    const  char*                   VariableName[PLUGIN_MAX_VARS];
    char*                          VariableStrVal[PLUGIN_MAX_VARS];
    PluginDeviceUpnpAction*        ActionTable;
    int                            cntTableSize;
    int VariableCount;
};

typedef struct plugin_service PluginServiceT, *pPluginService;

/*! the service table  */
//PluginServiceT plugin_service_table[PLUGIN_SERVICE_COUNT];
/** Device profile */
typedef int UPnP_Device_handle;

struct plugin_device {
    /** Deviec handler*/
    UPnP_Device_handle   device_handle;
    /** The service number device has*/
    int                  service_number;
    /** service table entry*/
    PluginServiceT       service_table[PLUGIN_MAX_SERVICES];
};

typedef struct plugin_device PluginDevice;

struct firmware_update_info {
    int dwldStartTime;
    int withUnsignedImage;
    char firmwareURL[MAX_FW_URL_LEN];
};
typedef struct firmware_update_info FirmwareUpdateInfo;

/* remote register parameters from App */
struct remote_access_info {
    char homeId[MAX_RVAL_LEN];
    char smartDeviceId[MAX_PKEY_LEN];
    char smartDeviceName[MAX_DESC_LEN];
    char setupToken[MAX_PKEY_LEN];
    char groupId[MAX_PKEY_LEN];
};
typedef struct remote_access_info RemoteAccessInfo;
extern RemoteAccessInfo *pgAppRegInfo;

/* register parameters for proxy registration */
struct proxy_remote_access_info {
    char proxy_restoreState[MAX_RES_LEN];
    char proxy_homeId[MAX_RVAL_LEN];
    char proxy_macAddress[MAX_MAC_LEN];
    char proxy_serialNumber[MAX_MAC_LEN];
    char proxy_pluginUniqueId[MAX_PKEY_LEN];
    char proxy_privateKey[MAX_PKEY_LEN];
};
typedef struct proxy_remote_access_info ProxyRemoteAccessInfo;

typedef struct s_sensorStateQueue {
    int state;
    unsigned long ts;
    struct s_sensorStateQueue *next;
} SStateQueue;
void initSensorStateQueueLock(void);
/* notifications related structures */

struct s_attrDetails {
    char Name[20];
    char PrevValue[32];
    char CurValue[32];
};
typedef struct s_attrDetails attrDetails;

typedef struct productNameTbl {
    const char* modelCode;
    const char* productName;
} productNameTbl;

#ifdef PRODUCT_WeMo_Dimmer
typedef struct nightModeConfiguration {
    unsigned int nightMode;
    unsigned int startTime;
    unsigned int endTime;
    unsigned int brightness;
} SNightModeConfiguration;

extern SNightModeConfiguration *gpsNightMode;
extern int gNightModeActive;
extern pthread_t gNightModeTimerThread;

#define NIGHT_MODE_CONFIG_FILE_PATH "/tmp/Belkin_settings/nightModeConfig.txt"
#endif

/**
 * CtrleeDeviceSetActionTable:
 * 	Initialize action table for each service
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 ***********************************************************************/
int CtrleeDeviceSetActionTable(PLUGIN_SERVICE_TYPE serviceType, pPluginService pOut);

/**
 * CtrleeDeviceHandleActionRequest:
 * 	Callback/entry to process action request from remote control
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 ***********************************************************************/
int CtrleeDeviceHandleActionRequest(UpnpActionRequest *pActionRequest);
/**
 * CtrleeDeviceSetServiceTable:
 * 	Initialize service table
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 ***********************************************************************/
int CtrleeDeviceSetServiceTable(int serviceType,
                                const char *UDN,
                                const char *serviceId,
                                const char *serviceTypeS,
                                pPluginService pService);




void FreeActionRequestSource(UpnpActionRequest* pEvent);


extern PluginDevice SocketDevice;

//----------------------------------- Setup service --------------------------------------------
/**
 * GetApList:
 *	Callback to return AP List
 *
 *
 *
 ******************************************************************************************************************/
int GetApList(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);


/**
 * GetNetworkList:
 *	Callback to return AP List
 *
 *
 *
 ******************************************************************************************************************/
int GetNetworkList(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * ConnectHomeNetwork:
 *	Connect to home network (pairing)
 *
 *
 *
 **********************************************************************************************/
int ConnectHomeNetwork(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);


/**
 * GetNetworkStatus:
 *	Get home network status
 *
 *
 *
 **********************************************************************************************/
int GetNetworkStatus(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * Resetup:
 * 	Callback to reset plugin
 *
 *
 * *****************************************************************************************************************/
int ReSetup(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * SetBinaryState:
 * 	Callback to set binary state
 *
 *
 * *****************************************************************************************************************/
int SetBinaryState(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * SetLogLevelOption:
 * 	Callback to set log level and option
 *
 *
 * *****************************************************************************************************************/
int SetLogLevelOption (pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * GetBinaryState:
 * 	Callback to Get binary state
 *
 *
 * *****************************************************************************************************************/
int GetBinaryState(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * GetBinaryState:
 * 	Callback to set device icon
 * 	device icon is update by http POST in separate request
 *
 * *****************************************************************************************************************/
int SetIcon(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * SetIcon:
 * 	Callback to Get icon URL
 *
 *
 * *****************************************************************************************************************/
int GetIcon(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int GetLogFilePath(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int GetWatchdogFile(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int SignalStrengthGet(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int SetServerEnvironment(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetServerEnvironment(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * Get Friendly Name:
 * 	Callback to get device friendly name
 *
 *
 * *****************************************************************************************************************/
int GetFriendlyName(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetDeviceId(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetShareHWInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int SetDeviceId(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetMacAddr(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetSerialNo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetPluginUDN(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#ifdef PRODUCT_WeMo_Light
int SetNightLightStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetNightLightStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int changeNightLightStatus(char *DimValue);
#endif

/**
 * GetIconVersion:
 *      Callback to Get Icon Version
 * *****************************************************************************************************************/
int GetIconVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * SetIconVersion:
 *
 */
int SetIconVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * GetInformation:
 * 	Callback to Get the device information in XML format
 * *****************************************************************************************************************/
int GetInformation(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/**
 * GetDeviceInformation:
 * 	Callback to Get the device information
 * *****************************************************************************************************************/
int GetDeviceInformation(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
/**
 * SetIcon:
 * 	Callback to Get icon URL
 *
 *
 * *****************************************************************************************************************/
int SetFriendlyName(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
void ShareSmartDevXmlUrl(void);

/**
 * RemoteAccess:
 * 	Callback to Set Remote Access
 *
 *
 * *****************************************************************************************************************/
int RemoteAccess(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/************************************************
 * The sync only occurs when setup, so there is no judgement for the latest
 *
 * NTP change time?
 *
 *
 *
 * **********************************************/
int SyncTime(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/***********************************************************
 * Get device current system time including utc, time zone, dst enable or not
 *
 *
 *
 *
 *
 *
 * *******************************************************************************/
int GetDeviceTime(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);


/* CloseSetup:
 * 	Close the setup so AP will be dropp and UPnP FINISH and restart again
 *
 *
 *
 *
 *
 *
 *********************************************************************************************************************/
int CloseSetup(pUPnPActionRequest pRequest, IXML_Document *request, IXML_Document **out, const char **errorString);


/* UpdateFirmware:
 * 	update firmware notification from APP
 *
 *
 *
 *
 *
 *
 *
 *
 *
 ********************************************************************************/
int UpdateFirmware(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

/* GetFirmwareVersion:
 * 	Firmware version request from APP
 *
 *
 *
 *
 *
 *
 *
 *
 *
 ********************************************************************************/
int GetFirmwareVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

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
int AddRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int EditRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int RemoveRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int EnableRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int DisableRule(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetRules(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int UpdateWeeklyCalendar(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int EditWeeklycalendar(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int GetRulesDBPath(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int SetRulesDBVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetRulesDBVersion(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int StoreRules (pUPnPActionRequest   pActionRequest,
                IXML_Document       *request,
                IXML_Document      **out,
                const char         **errorString);

int FetchRules (pUPnPActionRequest   pActionRequest,
                IXML_Document       *request,
                IXML_Document      **out,
                const char         **errorString);

void RuleDBVersionNotify();

int RestartRuleEngine (pUPnPActionRequest   pActionRequest,
                       IXML_Document       *request,
                       IXML_Document      **out,
                       const char         **errorString);

int StopPair(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);


int StopRuleEngine();

//-
int GetMetaInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetManufactureData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetExtMetaInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#if defined(PRODUCT_WeMo_Insight) || defined(PRODUCT_WeMo_SNS)
int SetRuleID(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int DeleteRuleID(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int UpdateAPNSDB(char* RuleID,char* RuleMSG, char* RuleFreq,int Action);
#endif

/***************************
 *  DeviceActionResponse:
 *
 *
 *
 *
 * ***************************************************/
int DeviceActionResponse(pUPnPActionRequest pRequest,
                         const char* responseName,
                         const char* serviceType,
                         const char* variabName,
                         const char *variableValue
                        );

/**************
 *
 *
 *
 *
 * ************************************************/

int IsNtpUpdate();

int DownLoadFirmware(const char *FirmwareURL, int deferWdLogging, int withUnsigned);

int fmDownloadCallback(int);

void *firmwareUpdateTh(void *);

void *CloseApWaitingThread(void *args);


//--------------------
/*Initialize the UPNP paramters here
 *
 *
 *
 *
 *
 *****************************************/
void initDeviceUPnP();

void initFWUpdateStateLock();

void initSiteSurveyStateLock();

void initLongPressAwayLock();

void setCurrFWUpdateState(int state);

int getCurrFWUpdateState(void);

void LocalBinaryStateNotify(int curState);

void SetAppSSID();

int getNvramParameter(char paramVal[]);

void GetDeviceType();

int  UpdateXML2Factory();
void GetMacAddress();

void GetFirmware();
void GetSkuNo();

/**
 *	Motion detection indication
 *
 *
 *
 ***************************/
void MotionSensorInd();



/**
 *	Motion no detection indication
 *
 *
 *
 ***************************/
void NoMotionSensorInd();



extern int g_isTimeSyncByMobileApp;

extern int g_eDeviceType;
extern int g_eDeviceTypeTemp;

void *PowerSensorMonitorTask(void *args);

/**
 * UpdatePowerMonitorTimer
 *		Control the sensor duration
 *
 *
 ******************************************************/
void UpdatePowerMonitorTimer(int duration, int endAction);

void StopPowerMonitorTimer();



extern char* g_szBuiltFirmwareVersion;
extern char* g_szBuiltTime;

void GetDeviceFriendlyName();



void	SaveDbVersion(char* szVersion);


void FirmwareUpdateStatusNotify(int status);
void RemoteFirmwareUpdateStatusNotify(void);

#define FM_STATUS_DOWNLOADING  					  0
#define FM_STATUS_DOWNLOAD_SUCCESS				1
#define FM_STATUS_DOWNLOAD_UNSUCCESS	 		2
#define FM_STATUS_UPDATE_STARTING	 			  3
#define FM_STATUS_DEFAULT					        4



//- Time Zone Update from new SDK

int GetTimeZoneIndex(float iTimeZoneValue);

struct __TimeZone__ {
    float 		iTimeZone;
    int			index;
    char		szDescription[SIZE_128B];
};
typedef struct __TimeZone__ tTimeZone, *pTimeZone;



//- Global flag for UPnP running IP
extern int g_IsUPnPOnInternet;
extern int g_IsDeviceInSetupMode;

extern pthread_t g_ipPortThreadId;
void notifyIpPortThread(void);
void detectIPChange(void);
void UpdateUPnPNetworkMode(int networkMode);

void		NotifyInternetConnected();
int 		ProcessItcEvent(pNode node);


#define		UPNP_LOCAL_MODE		0x00
#define		UPNP_INTERNET_MODE	0x01

int IsUPnPNetworkMode();

void	AsyncSaveData();

void 	RestoreIcon();


int OverrideRule(int userActionIndex);

void RuleOverrideNotify(int state);

int GetDeviceStateIF();
char*	GetRuleDBVersionIF(char* buf);
void	SetRuleDBVersionIF(char* buf);
char*	GetRuleDBPathIF(char* buf);


int ExecuteReset(int resetIndex);

void NameChangeNotify(char *name);

#ifdef LONG_PRESS_SUPPORTED
void LongPressRuleNotify();
#endif

void LocalUserActionNotify(int curState);


void LocalCountdownTimerNotify();

void initUPnPThreadParam();

int	 IsTimeUpdateByMobileApp();

void RestartRule4DST();


char* GetOverridenStatusIF(char* szOutBuff);


void ResetOverrideStatus();

int  IsOverriddenStatus();

void PushStoredOverriddenStatus();

extern int g_iDstNowTimeStatus; //- 1:00 DST just OFF, 3:00 am, DST just ON

#define	DST_TIME_NOW_OFF	1	//1:00 am
#define DST_TIME_NOW_ON		3	//3:00 am
#define DST_TIME_NOW_ON_2		2	//2:00 am

#define		DST_TIME_ON_SECONDS		3 * 3600	//3:00 am
#define		DST_TIME_ON_SECONDS_2		2 * 3600	//2:00 am
#define		DST_TIME_OFF_SECONDS	1 * 3600	//1:00 am

#define		AP_LOCAL_IP				"10.22.22.1"


/* Actuation values */
#define ACTUATION_MANUAL_DEVICE	"Manual - WeMo Device"
#define	ACTUATION_MANUAL_APP	"Manual - mobile App"
#define ACTUATION_TIME_RULE	"Automatic - TimeRule"
#define ACTUATION_AWAY_RULE	"Automatic - AwayRule"
#define ACTUATION_SENSOR_RULE	"Automatic - Sensor Trigger"
#define ACTUATION_COUNTDOWN_TIMER_RULE	"Automatic - CountDownTimerRule"
#define ACTUATION_EVENT_RULE     "Automatic - EventRule"

void ClearRuleFromFlash(void);
void	Advertisement4TimeUpdate();

char* CreateManufactureData(void);
int isTimeEventInOVerrideTable(int time, int action);
void UPnPSetHomeDeviceIdNotify(void);
void* FirmwareUpdateStart(void *args);
int StartFirmwareUpdate(FirmwareUpdateInfo fwUpdInf);

void correctUbootParams();

int getTimeZoneIndexFromFlash(void);

void SetRouterInfo(const char* szMac, const char* szSSID);
void initSerialRequest();
void* siteSurveyPeriodic(void *args);
char* getDefaultFriendlyName(void);
void initRemoteAccessLock(void);
char* getDeviceUDNString(void);
void freeRemoteXMLRes(char* pDeviceId, char *pHomeId, char *pDevicePrivKey, char *pMacAddress, char *pDeviceName);
#ifdef SIMULATED_OCCUPANCY
int SetAwayRuleTask(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int SimulatedRuleData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetSimulatedRuleData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int NotifyManualToggle(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#endif
#ifdef PRODUCT_WeMo_Light
void AsyncRebootSystem(void);
#endif

#define ATTR_STATE 1<<0
#define CONTROL_LOCAL    1
#define CONTROL_REMOTE   2

#ifdef PRODUCT_WeMo_Dimmer
#define BRIGHTNESS_BEFORE_NIGHT_MODE "BrightnessBeforeNightMode"
#define BRIGHTNESS_OVERRIDE_TIMER_VALUE 4 /* seconds */

#define DEFAULT_NIGHT_MODE_STATUS 	0
#define DEFAULT_NIGHT_MODE_BRIGHTNESS 20
#define DEFAULT_NIGHT_MODE_START_TIME 0 /* 12 AM */
#define DEFAULT_NIGHT_MODE_END_TIME 23400 /* 6.30 AM */

#define DIMMER_BULB_TYPE "BulbType"
extern int gLocalAttrSet;
extern int gRemoteAttrSet;

extern char gBulbType[SIZE_16B];
extern int gMinLevel;
extern int gMaxLevel;
#endif

void setActuation(char *);
void setRemote(char *);
int getClientType(void);
int  findEncryptionForSsid(char *ssid,char* EncryptType,char *AuthMode);
void UPnPInternalToggleUpdate(int curState);
void enqueInAppNotification(int ruleId, char *ruleMsg, int ruleFreq);

#ifdef PRODUCT_WeMo_Dimmer
int ConfigureNightMode(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetNightModeConfiguration(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int ConfigureDimmingRange(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int SetBulbType(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int Calibrate(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int loadNightModeConfiguration(void);
int TestLEDs(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int CheckResetButtonState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#endif
#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
int ConfigureHushMode(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int identifyDevice(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int setDummyMode(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#endif
#ifdef LONG_PRESS_SUPPORTED
int SimulateLongPress(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#endif
#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
int SimulateOverTemp(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#endif

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
int SetGPIO(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetGPIO(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#endif
int StartIperf(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int StopIperf(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);

int GetHKSetupInfo(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetSetupDoneStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int SetSetupDoneStatus(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int setHKSetupState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int getHKSetupState(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int resetHKConfig(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int setAutoFWUpdate(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int SetEnforceSecurity(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int GetEnforceSecurity(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
int removeHomekitData(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString);
#endif /* CONTROLLEDEVICE_H_ */
