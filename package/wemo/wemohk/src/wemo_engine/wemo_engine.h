#ifndef WEMO_ENGINE_H_
#define WEMO_ENGINE_H_

#define SOCKET_NAME "/tmp/wemo_engine.socket"

#define IPC_DATA_MAX 2048

#define STATE_DISCONNECTED		            0
#define STATE_CONNECTED			            1
#define STATE_PAIRING_FAILURE_IND	        2
#define STATE_INTERNET_NOT_CONNECTED	    3
#define STATE_IPADDR_NEGOTIATION_FAILED	    4

typedef enum {
    CMD_SETUP = 1,
    CMD_CONNECTION_STATE,
    CMD_CLOSESETUP,
    CMD_SET,
    CMD_GET,
    CMD_DELETE,
    CMD_DISCOVER,
    CMD_FIRMWARE_UPDATE,
    CMD_SET_HKSETUP_STATE,
    CMD_CHANGE_NAME,
    CMD_NAME_VALUE,
    CMD_RESET,
    CMD_RESTART_RULE,
    CMD_GET_DEVINFO,
    CMD_GET_INSIGHTHOME_SETTINGS,
    CMD_SET_INSIGHTHOME_SETTINGS,
    CMD_GET_INSIGHT_PARAMS,
    CMD_SET_POWER_THRESHOLD,
    CMD_GET_POWER_THRESHOLD,
    CMD_GET_DATA_EXPORTINFO,
    CMD_SCHEDULE_DATA_EXPORT,
} ipc_cmd_t;

typedef enum {
    EVENT_SETUP = 1,
    EVENT_CONNECTION_STATE,
    EVENT_STATE,
    EVENT_NAME_CHANGE,
    EVENT_NAME_VALUE,
    EVENT_RESET,
    EVENT_DEVICE_INFO,
    EVENT_INSIGHT_HOME_SETTINGS,
} ipc_event_t;

/* wemo device types */
typedef enum {
    WEMO_NONE,
    WEMO_SWITCH,
    WEMO_LIGHT,
    WEMO_MINI,
    WEMO_DIMMER,
    WEMO_INSIGHT,
    WEMO_SENSOR,
    WEMO_UNKNOWN
} dev_id_t;

typedef enum {
    CAP_NONE,
    CAP_BINARY,
    CAP_LEVEL,
    CAP_FUTURE
} cap_t;

typedef enum {
    RESET_SOFT = 1,
    RESET_FULL,
    RESET_REMOTE,
    RESET_INSIGHT,
    RESET_WIFI
} resettype_t;

struct we_conn_data {
    char ssid[64];
    char passphrase[128];
    char auth[16];
    char encrypt[16];
    int channel;
};

struct we_network_status {
    int connection_state;
};

struct we_name_change {
    char name[64];
};

struct we_name_value {
    char name[64];
    char value[1280];
};

struct we_dev_information {
    int binaryState;
    int brightness;
    int OverTemp;
    int nightMode;
    long startTime;
    long endTime;
    int nightModeBrightness;
    long CountdownEndTime;
    int longPressRuleDeviceCnt;
    int longPressRuleAction;
    int longPressRuleState;
    char *productName;
    char *fader;
    char *hushMode;
    char *longPressRuleDeviceUdn;
};

/* data structure to be used to communicate to wemo engine */
struct we_state {
    /* indication whether the device is online or not */
    /* 0 : offline or dead, 1: online and controllable */
    int is_online;
    /* triggers on/off */
    /* 0 : off, 1 : on */
    int state;
    /* Dimming range (0 - 100) */
    /* only applicable to dimming capable devices */
    int level;
};

struct we_firmware_data {
    long start_time;
    int unsign_img;
    char url[128];
};

struct we_hksetup_state {
    int hksetup_state;
};

struct we_reset {
    resettype_t reset_type;
};

struct we_insight_home_settings {
    char HomeSettingsVersion[8];
    char energyPerUnitCost[8];
    char Currency[8];
};

struct we_insight_threshold {
    char threshold[8];
};

struct we_insight_export {
    int version;
    char export_type[8];
    char email[256 + 64];
};

struct we_ipc_hdr {
    /* Device ID found from wemo_device.db */
    int wemo_id;
    int cmd;
    int size;
};

typedef void (*event_callback_t)(int wemo_id, struct we_state *data);
typedef void (*netstate_callback_t)(int wemo_id, struct we_network_status *data);
typedef void (*name_change_callback_t)(int wemo_id, struct we_name_change *data);
typedef void (*name_value_callback_t)(int wemo_id, struct we_name_value *data);
typedef void (*dev_info_callback_t)(int wemo_id, struct we_dev_information *data);
typedef void (*insight_home_settings_callback_t)(int wemo_id, struct we_insight_home_settings *data);

struct wemo_engine_callback {
    event_callback_t event_callback;
    netstate_callback_t netstate_callback;
    name_change_callback_t name_change_callback;
    name_value_callback_t name_value_callback;
    dev_info_callback_t dev_info_callback;
    insight_home_settings_callback_t insight_home_settings_callback;
};

/* initialize the unix domain socket IPC client to wemo engine */
/* note: it will spawn a pthread to communicate to wemo engine */
int we_init();
/* This function will register a user defined callback.
   When the notification is arrived from the wemo engine,
   then callback will be called.*/
int we_register_event_callback(void (*callback)(int wemo_id, struct we_state *data));
int we_register_netstate_callback(void (*callback)(int wemo_id, struct we_network_status *data));
int we_register_name_change_callback(void (*callback)(int wemo_id, struct we_name_change *data));
int we_register_name_value_callback(void (*callback)(int wemo_id, struct we_name_value *data));
int we_register_dev_info_callback(void (*callback)(int wemo_id, struct we_dev_information *data));
int we_register_insight_home_settings_callback(void (*callback) (int wemo_id, struct we_insight_home_settings *data));

/* This function will pass the get action to to wemo engine */
int we_get_action(int wemo_id, struct we_state *we_state_data);
/* This function will pass the set action to to wemo engine */
int we_set_action(int wemo_id, struct we_state *we_state_data);
/* This function will delete the wemo device in DB when called */
/* The user should not delete the device currently online */
int we_del_action(int wemo_id, struct we_state *we_state_data);
/* retrieve network state */
int we_get_netstate(int wemo_id, struct we_network_status *network_status);
/* command wemoApp to connect to designated AP */
int we_connect(int wemo_id, struct we_conn_data *conn_data);
int we_closesetup(int wemo_id);
int we_discover(int wemo_id);
int we_firm_update(int wemo_id, struct we_firmware_data *firm_data);
int we_set_hksetup_state(int wemo_id, struct we_hksetup_state *setup_state);
int we_change_name(int wemo_id, struct we_name_change *name_data);
int we_set_name_value(int wemo_id, struct we_name_value *data);
int we_reset(int wemo_id, struct we_reset *reset_data);
int we_restart_rule(int wemo_id);
int we_get_devinfo(int wemo_id);
int we_get_insightHomeSettings(int wemo_id);
int we_set_insightHomeSettings(int wemo_id, struct we_insight_home_settings *home_settings);
int we_get_insightParams(int wemo_id);
int we_set_powerThreshold(int wemo_id, struct we_insight_threshold *threshold);
int we_get_powerThreshold(int wemo_id);
int we_get_dataExportInfo(int wemo_id);
int we_schedule_dataExport(int wemo_id, struct we_insight_export *export);

/* stop IPC and clean up */
int we_end();

#endif /* WEMO_CTRL_H_ */
