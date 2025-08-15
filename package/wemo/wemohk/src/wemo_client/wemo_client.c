#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/if.h>
#include <linux/wireless.h>

#include "aes.h"
#include "wemo_engine.h"

#define DEVICE_TABLE "wemo_device"
#define STATE_TABLE "state"

#define CONFIG_FILE_PATH "/etc/wemo_ctrl.conf"
#define DEFAULT_WEMO_DEVICE_DB "/tmp/wemo_device.db"
#define DEFAULT_WEMO_STATE_DB "/tmp/wemo_state.db"
#define DEFAULT_IFNAME "eth2"

char wemo_device_db[128];
char wemo_state_db[128];
char upnp_ifname[16];
enum cmdloop_cmds {
    HELP = 0,
    POWON,
    POWOFF,
    SETLEVEL,
    SETDIMMER,
    GETSTATE,
    PRTDEV,
    LSTDEV,
    DELDEV,
    SETUP,
    NETSTATE,
    CLOSESETUP,
    DISCOVER,
    FIRMUPDATE,
    SET_HKSETUP,
    CHANGE_NAME,
    RESET,
    RESTART_RULE,
    GET_INFORMATION,
    SCHEDULE_INSIGHT,
    GET_INSIGHT_EXPORT_INFO,
    SET_INSIGHT_THRESHOLD,
    GET_INSIGHT_THRESHOLD,
    SET_INSIGHT_HOME,
    GET_INSIGHT_HOME,
    EXITCMD
};

struct cmdloop_commands {
    char *str;
    int cmdnum;
    int numargs;
    char *args;
} cmdloop_commands;

static struct cmdloop_commands cmdloop_cmdlist[] = {
    {"help", HELP, 1, ""},
    {"listdev", LSTDEV, 1, ""},
    {"printdev", PRTDEV, 2, "<devnum>"},
    {"poweron", POWON, 2, "<devnum>"},
    {"poweroff", POWOFF, 2, "<devnum>"},
    {"setlevel", SETLEVEL, 3, "<devnum> <level>"},
    {"setdimmer", SETDIMMER, 4, "<devnum> <0/1> <level>"},
    {"getstate", GETSTATE, 2, "<devnum>"},
    {"deletedev", DELDEV, 2, "<devnum>"},
    {"setup", SETUP, 7, "<devnum> <ssid> <passphrase> <auth> <encrypt> <channel>"},
    {"getnetstate", NETSTATE, 2, "<devnum>"},
    {"closesetup", CLOSESETUP, 2, "<devnum>"},
    {"discover", DISCOVER, 1, ""},
    {"firmup", FIRMUPDATE, 5, "<devnum> <starttime> <withunsign> <url>"},
    {"set_hksetup", SET_HKSETUP, 3, "<devnum> <HK setup state>"},
    {"changename", CHANGE_NAME, 3, "<devnum> <new name>"},
    {"reset", RESET, 3, "<devnum> <reset type (1: soft, 2: full, 3:remote, 4: insight, 5: wifi)>"},
    {"restartrule", RESTART_RULE, 2, "<devnum>"},
    {"getinformation", GET_INFORMATION, 2, "<devnum>"},
    {"scheduledataexport", SCHEDULE_INSIGHT, 3, "<email> <export type> ONLY for Insight"},
    {"getexportinfo", GET_INSIGHT_EXPORT_INFO, 1, "ONLY for Insight"},
    {"setpowerthreshold", SET_INSIGHT_THRESHOLD, 2, "<threshold> ONLY for Insight"},
    {"getpowerthreshold", GET_INSIGHT_THRESHOLD, 1, "ONLY for Insight"},
    {"sethomesettings", SET_INSIGHT_HOME, 3, "<energyPerUnitCost> <currency> ONLY for Insight"},
    {"gethomesettings", GET_INSIGHT_HOME, 1, "ONLY for Insight"},
    {"exit", EXITCMD, 1, ""}
};

static int run_cmdloop = 1;

int nvram_get(char *key, char *value)
{
    FILE *pipe;
    char cmd_buffer[128];
    int value_length = 0;
    char buffer[256];

    if (key == NULL || value == NULL) {
        printf("key or value is NULL");
        return -1;
    }

    memset(cmd_buffer, 0, 128);
    snprintf(cmd_buffer, sizeof(cmd_buffer), "nvram_get %s", key);

    pipe = popen(cmd_buffer, "r");
    if (pipe == NULL) {
        fprintf(stderr, "popen error %s\n", strerror(errno));
        return -1;
    }

    memset(buffer, 0, 256);
    if (!fread(buffer, sizeof(char), 256, pipe)) {
        fprintf(stderr, "key - %s not set\n", key);
        pclose(pipe);
        return -1;
    }

    strcpy(value, buffer);
    /* remove the new line character at the end of value */
    value_length = strlen(value);
    if (value[value_length - 1] == '\n') {
        value[value_length - 1] = 0;
        value_length--;
    }

    pclose(pipe);

    return value_length;
}

static int get_macaddr_no_colon(const char *interface, char *mac, int mac_len)
{
    int s;
    struct ifreq buffer;
    int i = 0;

    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        fprintf(stderr, "%s: Error in creating socket for getting mac address", __FUNCTION__);
        return s;
    }

    memset(&buffer, 0x00, sizeof(buffer));

    strcpy(buffer.ifr_name, interface);

    ioctl(s, SIOCGIFHWADDR, &buffer);

    close(s);

    snprintf(mac, mac_len, "%02x%02x%02x%02x%02x%02x",
            (unsigned char)buffer.ifr_hwaddr.sa_data[0],
            (unsigned char)buffer.ifr_hwaddr.sa_data[1],
            (unsigned char)buffer.ifr_hwaddr.sa_data[2],
            (unsigned char)buffer.ifr_hwaddr.sa_data[3],
            (unsigned char)buffer.ifr_hwaddr.sa_data[4],
            (unsigned char)buffer.ifr_hwaddr.sa_data[5]);

    for (i = 0; i < strlen(mac); i++) {
        mac[i] = toupper(mac[i]);
    }

    return 0;
}

static int get_password_key(char *key, int key_len)
{
    size_t i;

    char serial[32];
    char key_data[64];
    char ra0_mac_addr[13];
    /* bVduaWFyZkllaWVAb3RjbHAkcm9uT2Jh */
    const char string[] = "bVdu";
    const char string1[] = "aWFy";
    const char string2[] = "Zkll";
    const char string3[] = "aWVA";
    const char string4[] = "b3Rj";
    const char string5[] = "bHAk";
    const char string6[] = "cm9u";
    const char string7[] = "T2Jh";

    memset(key, 0, key_len);
    memset(key_data, 0, sizeof(key_data));
    memset(serial, 0, sizeof(serial));
    memset(ra0_mac_addr, 0, sizeof(ra0_mac_addr));

    get_macaddr_no_colon("ra0", ra0_mac_addr, sizeof(ra0_mac_addr));

    if (nvram_get("SerialNumber", serial) == 0) {
        fprintf(stderr, "%s : Error getting serial number", __FUNCTION__);
        return 0;
    }

    /* copy 3 MSB of the MAC address */
    memcpy(key_data, ra0_mac_addr, 3);
    /* 9-11 */
    strncat(key_data, ra0_mac_addr + 9, 3);

    /* Append the  serial number */
    strncat(key_data, serial, sizeof(key_data) - strlen(key_data) - 1);

    strncat(key_data, string, 4);
    strncat(key_data, string1, 4);
    strncat(key_data, string2, 4);
    strncat(key_data, string3, 4);
    strncat(key_data, string4, 4);
    strncat(key_data, string5, 4);
    strncat(key_data, string6, 4);
    strncat(key_data, string7, 4);

    /* 6 - 8 */
    strncat(key_data, ra0_mac_addr + 6, 3);
    /* 3 - 5 */
    strncat(key_data, ra0_mac_addr + 3, 3);

    for (i = 0; i < strlen(key_data); i++) {
        key[i] = key_data[i];
    }

    return 1;
}

#define PASSWORD_KEYDATA_LEN 256
#define PASSWORD_SALT_LEN   (8 + 1)
#define PASSWORD_IV_LEN     (16 + 1)

int encryptPassword(char *src, int src_len, char *dst, int dst_len)
{
    int len = 0;
    int cipher_len=0;
    int key_data_len, salt_len, iv_len;

    unsigned char key_data[PASSWORD_KEYDATA_LEN];
    unsigned char salt[PASSWORD_SALT_LEN];
    unsigned char iv[PASSWORD_IV_LEN];

    unsigned char *ciphertext = NULL;

    char *encStr = NULL;
    char lenstr[5];
    char basePassword[256];
    char password_key_data[64];

    memset(key_data, 0, sizeof(key_data));
    memset(salt, 0, sizeof(salt));
    memset(iv, 0, sizeof(iv));
    memset(basePassword, 0, sizeof(basePassword));
    memset(lenstr, 0, sizeof(lenstr));
    memset(password_key_data, 0, sizeof(password_key_data));
    memset(dst, 0, dst_len);

    if (!get_password_key(password_key_data, sizeof(password_key_data))) {
        fprintf(stderr, "%s : Failed to get password key", __FUNCTION__);
        return 0;
    }

    len = src_len;
    strncpy((char *)key_data, password_key_data, sizeof(key_data)-1);
    key_data_len = strlen((char *)key_data);
    memcpy(salt, password_key_data, PASSWORD_SALT_LEN-1);
    memcpy(iv, password_key_data, PASSWORD_IV_LEN-1);
    salt_len = strlen((char *)salt);
    iv_len = strlen((char *)iv);

    ciphertext = pluginAES128Encrypt(key_data, key_data_len, salt, salt_len, iv, iv_len, src, &len);
    if (!ciphertext) {
        fprintf(stderr, "%s : Failed to get cipher text", __FUNCTION__);
        return 0;
    }
    ciphertext[len] = '\0';

    encStr = base64Encode(ciphertext, len);
    cipher_len = strlen(encStr);

    if (cipher_len + 4 > dst_len) {
        fprintf(stderr, "%s : Error: Chiper length is bigger than dst buffer", __FUNCTION__);
        return 0;
    }

    snprintf(dst, dst_len, "%s%02X%02X", encStr, cipher_len, src_len);
    printf("%s: encrypted password = %s", __FUNCTION__, dst);

    free(ciphertext);
    free(encStr);

    return 1;
}

void event_callback(int wemo_id, struct we_state *data)
{
    printf("%s: wemo_id = %d, is_online = %d, state = %d, level = %d\n",
           __FUNCTION__, wemo_id, data->is_online, data->state, data->level);
}

void netstate_callback(int wemo_id, struct we_network_status *data)
{
    printf("%s: wemo_id = %d, netstate = %d\n",
           __FUNCTION__, wemo_id, data->connection_state);
}

void name_change_callback(int wemo_id, struct we_name_change *data)
{
    printf("%s: wemo_id = %d, name = %s\n",
           __FUNCTION__, wemo_id, data->name);
}

void name_value_callback(int wemo_id, struct we_name_value *data)
{
    printf("%s: wemo_id = %d, name = %s, value = %s\n",
           __FUNCTION__, wemo_id, data->name, data->value);
}

void dev_info_callback(int wemo_id, struct we_dev_information *data)
{
    printf("%s: wemo_id = %d\n", __FUNCTION__, wemo_id);
    printf("\tproductName: %s\n", data->productName);
    printf("\tbinaryState: %d\n", data->binaryState);
    printf("\tbrightness: %d\n", data->brightness);
    printf("\tfader: %s\n", data->fader);
    printf("\thushMode: %s\n", data->hushMode);
    printf("\tOverTemp: %d\n", data->OverTemp);
    printf("\tnightMode: %d\n", data->nightMode);
    printf("\t\tstartTime: %ld\n", data->startTime);
    printf("\t\tendTime; %ld\n", data->endTime);
    printf("\t\tnightModeBrightness: %d\n", data->nightModeBrightness);
    printf("\tCountdownEndTime: %ld\n", data->CountdownEndTime);
    printf("\tlongPressRuleDeviceCnt: %d\n", data->longPressRuleDeviceCnt);
    printf("\tlongPressRuleAction: %d\n", data->longPressRuleAction);
    printf("\tlongPressRuleState: %d\n", data->longPressRuleState);
    printf("\tlongPressRuleDeviceUdn: %s\n",
           data->longPressRuleDeviceUdn? data->longPressRuleDeviceUdn : "empty");
}

void printhelp()
{
    printf("commands:\n");
    printf("\thelp\n");
    printf("\tlistdev\n");
    printf("\tprintdev <devnum>\n");
    printf("\tpoweron <devnum>\n");
    printf("\tpoweroff <devnum>\n");
    printf("\tsetlevel <devnum> <level>\n");
    printf("\tsetdimmer <devnum> <0/1> <level>\n");
    printf("\tgetstate <devnum>\n");
    printf("\tdeletedev <devnum>\n");
    printf("\tsetup <devnum> <ssid> <passphrase> <auth> <encrypt> <channel>\n");
    printf("\tgetnetstate <devnum>\n");
    printf("\tclosesetup <devnum>\n");
    printf("\tdiscover\n");
    printf("\tfirmup <devnum> <starttime> <withunsign> <url>\n");
    printf("\tset_hksetup <devnum> <HK setup state>\n");
    printf("\tchangename <devnum> '<new name>'\n");
    printf("\treset <devnum> <reset type (1: soft, 2: full, 3:remote, 4: insight, 5: wifi)>\n");
    printf("\trestartrule <devnum>\n");
    printf("\tgetinformation <devnum>\n");
    printf("\tscheduledataexport <email> <export type> ONLY for Insight\n");
    printf("\tgetexportinfo (ONLY for Insight)\n");
    printf("\tsetpowerthreshold <threshold> (ONLY for Insight)\n");
    printf("\tgetpowerthreshold (ONLY for Insight)\n");
    printf("\tsethomesettings <energyPerUnitCost> <currency> (ONLY for Insight)\n");
    printf("\tgethomesettings (ONLY for Insight)\n");

    printf("\texit\n");
}

static int list_device_callback(void *data, int argc, char **argv, char **colName)
{
    int i;
    printf("%s: ", (const char *)data);
    for (i = 0; i < argc; i++) {
        printf("%s = %s\n", colName[i], argv[i]? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

static int print_device_callback(void *data, int argc, char **argv, char **colName)
{
    int i;
    printf("%s: ", (const char *)data);
    for (i = 0; i < argc; i++) {
        printf("%s = %s\n", colName[i], argv[i]? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

void wemo_list_devices()
{
    sqlite3 *db;
    char *errmsg = NULL;
    int rc;
    char *sql;
    const char *data = "wemo devices in DB";

    rc = sqlite3_open(wemo_device_db, &db);
    if (rc) {
        fprintf(stderr, "can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }
    sql = "SELECT * from wemo_device";
    rc = sqlite3_exec(db, sql, list_device_callback, (void *)data, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "%s error: %s\n", __FUNCTION__, errmsg);
        sqlite3_free(errmsg);
    }
    
    sqlite3_close(db);
}

void wemo_print_device(int wemo_id)
{
    sqlite3 *db;
    char *errmsg = NULL;
    int rc;
    char sql[256];
    const char *data = "wemo device";

    rc = sqlite3_open(wemo_device_db, &db);
    if (rc) {
        fprintf(stderr, "can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    sprintf(sql, "SELECT * from wemo_device where wemo_id = %d", wemo_id);;
    rc = sqlite3_exec(db, sql, print_device_callback, (void *)data, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "%s error: %s\n", __FUNCTION__, errmsg);
        sqlite3_free(errmsg);
    }
    
    sqlite3_close(db);
}

void wemo_client_process_command(char *cmdline)
{
    char cmd[100];
    int arg_val_err = -99999;
    int arg1 = arg_val_err;
    int arg2 = arg_val_err;
    int arg3 = arg_val_err;
    int cmdnum = -1;
    int numofcmds = sizeof(cmdloop_cmdlist)/sizeof(cmdloop_commands);
    int cmdfound = 0;
    int i;
    int invalidargs = 0;
    int validargs;

    struct we_state state_buffer;
    struct we_network_status netstate;
    struct we_conn_data conn_data;
    struct we_firmware_data firm_data;
    struct we_hksetup_state hksetup_data;
    struct we_name_change name_data;
    struct we_reset reset_data;
    struct we_insight_export export_data;
    struct we_insight_threshold threshold;
    struct we_insight_home_settings settings;
    char passphrase[128];

    memset((void *)&state_buffer, 0, sizeof(struct we_state));
    memset((void *)&netstate, 0, sizeof(struct we_network_status));
    memset((void *)&conn_data, 0, sizeof(struct we_conn_data));
    memset((void *)&firm_data, 0, sizeof(struct we_firmware_data));
    memset((void *)&hksetup_data, 0, sizeof(struct we_hksetup_state));
    memset((void *)&name_data, 0, sizeof(struct we_name_change));
    memset((void *)&reset_data, 0, sizeof(struct we_reset));

    if (strncmp(cmdline, "setup", 5) == 0) {
        validargs = sscanf(cmdline, "%s %d %s %s %s %s %d", cmd,
                           &arg1,
                           conn_data.ssid,
                           passphrase,
                           conn_data.auth,
                           conn_data.encrypt,
                           &conn_data.channel);
        encryptPassword(passphrase, strlen(passphrase),
                        conn_data.passphrase, sizeof(conn_data.passphrase));
    } else if (strncmp(cmdline, "firmup", 6) == 0) {
        validargs = sscanf(cmdline, "%s %d %ld %d %s", cmd, &arg1,
                           &firm_data.start_time, &firm_data.unsign_img,
                           firm_data.url);
    } else if (strncmp(cmdline, "changename", 10) == 0) {
        validargs = sscanf(cmdline, "%s %d '%[^']'", cmd, &arg1,
                           name_data.name);
    } else if (strncmp(cmdline, "reset", 5) == 0) {
        validargs = sscanf(cmdline, "%s %d %d", cmd, &arg1,
                           (int *)&reset_data.reset_type);
    } else if (strncmp(cmdline, "scheduledataexport", strlen("scheduledataexport")) == 0) {
        memset(&export_data, 0, sizeof(struct we_insight_export));
        validargs = sscanf(cmdline, "%s %s %s",
                           cmd,
                           export_data.email,
                           export_data.export_type);
        printf("%s: %s, %s\n", cmd, export_data.email, export_data.export_type);
    } else if (strncmp(cmdline, "setpowerthreshold", strlen("setpowerthreshold")) == 0) {
        memset(&threshold, 0, sizeof(struct we_insight_threshold));
        validargs = sscanf(cmdline, "%s %s", cmd,
                           threshold.threshold);
    } else if (strncmp(cmdline, "sethomesettings", strlen("sethomesettings")) == 0) {
        memset(&settings, 0, sizeof(struct we_insight_home_settings));
        validargs = sscanf(cmdline, "%s %s %s", cmd,
                           settings.energyPerUnitCost,
                           settings.Currency);
    } else {
        validargs = sscanf(cmdline, "%s %d %d %d", cmd, &arg1, &arg2, &arg3);
    }

    for(i = 0; i < numofcmds; i++) {
        if (strcasecmp(cmd, cmdloop_cmdlist[i].str) == 0) {
            cmdnum = cmdloop_cmdlist[i].cmdnum;
            cmdfound++;
            if (validargs != cmdloop_cmdlist[i].numargs) {
                invalidargs++;
            }
            break;
        }
    }

    if (!cmdfound) {
        printf("Command not found: try 'help'\n");
        return;
    }

    if (invalidargs) {
        printf("invalid arguments: try 'help'\n");
        return;
    }

    switch (cmdnum) {
    case HELP:
        printhelp();
        break;
    case POWON:
        state_buffer.is_online = 0; // will be ignored
        state_buffer.state = 1;
        state_buffer.level = -1;
        we_set_action(arg1, &state_buffer);
        break;
    case POWOFF:
        state_buffer.is_online = 0; // will be ignored
        state_buffer.state = 0;
        state_buffer.level = -1;
        we_set_action(arg1, &state_buffer);
        break;
    case SETLEVEL:
        state_buffer.is_online = 0; // will be ignored
        state_buffer.state = -1; // -1 means ignore
        state_buffer.level = arg2;
        we_set_action(arg1, &state_buffer);
        break;
    case SETDIMMER:
        state_buffer.is_online = 0; //will be ignored
        state_buffer.state = arg2;
        state_buffer.level = arg3;
        we_set_action(arg1, &state_buffer);
        break;
    case GETSTATE:
        we_get_action(arg1, &state_buffer);
        break;
    case PRTDEV:
        wemo_print_device(arg1);
        break;
    case DELDEV:
        we_del_action(arg1, &state_buffer);
        break;
    case LSTDEV:
        wemo_list_devices();
        break;
    case SETUP:
        we_connect(arg1, &conn_data);
        break;
    case NETSTATE:
        we_get_netstate(arg1, &netstate);
        break;
    case CLOSESETUP:
        we_closesetup(arg1);
        break;
    case DISCOVER:
        we_discover(arg1);
        break;
    case FIRMUPDATE:
        we_firm_update(arg1, &firm_data);
        break;
    case SET_HKSETUP:
        hksetup_data.hksetup_state = arg2;
        we_set_hksetup_state(arg1, &hksetup_data);
        break;
    case CHANGE_NAME:
        we_change_name(arg1, &name_data);
        break;
    case RESET:
        we_reset(arg1, &reset_data);
        break;
    case RESTART_RULE:
        we_restart_rule(arg1);
        break;
    case GET_INFORMATION:
        we_get_devinfo(arg1);
        break;
    case SCHEDULE_INSIGHT:
        we_schedule_dataExport(1, &export_data);
        break;
    case GET_INSIGHT_EXPORT_INFO:
        we_get_dataExportInfo(1);
        break;
    case SET_INSIGHT_THRESHOLD:
        we_set_powerThreshold(1, &threshold);
        break;
    case GET_INSIGHT_THRESHOLD:
        we_get_dataExportInfo(1);
        break;
    case SET_INSIGHT_HOME:
        we_set_insightHomeSettings(1, &settings);
        break;
    case GET_INSIGHT_HOME:
        we_get_insightParams(1);
        break;
    case EXITCMD:
        run_cmdloop = 0;
        break;
    default:
        printf("command not implemented: 'help'\n");
        break;
    }
}

void wemo_client_cmdloop()
{
    char cmdline[100];

    while(run_cmdloop) {
        printf("\n>> ");
        fgets(cmdline, 100, stdin);
        wemo_client_process_command(cmdline);
    }
}

static void strip_string(char *str)
{
    int str_size = strlen(str);
    if (str_size > 0) {
        if (str[str_size - 1] == '\n') {
            str[str_size - 1] = 0;
            if (str[str_size - 2] == '\r') {
                str[str_size - 2] = 0;
            }
        }
    }
}

int process_config()
{
    FILE *conf = fopen (CONFIG_FILE_PATH, "r");

    if (conf != NULL) {
        char line[256];

        while(fgets(line, sizeof(line), conf) != NULL) {
            char *token;
            token = strtok(line, "= ");
            if (!strcasecmp("wemo_device_db", token)) {
                token = strtok(NULL, "= ");
                if (token != NULL) {
                    strcpy(wemo_device_db, token);
                    strip_string(wemo_device_db);
                    printf("wemo_device_db = %s\n", wemo_device_db);
                }
                else {
                    printf("error parsing wemo_device_db!\n");
                }
            }
            else if (!strcasecmp("wemo_state_db", token)) {
                token = strtok(NULL, "= ");
                if (token != NULL) {
                    strcpy(wemo_state_db, token);
                    strip_string(wemo_state_db);
                    printf("wemo_state_db = %s\n", wemo_state_db);
                }
                else {
                    printf("error parsing wemo_state_db!\n");
                }
            }
            else if (!strcasecmp("ifname", token)) {
                token = strtok(NULL, "= ");
                if (token != NULL) {
                    strcpy(upnp_ifname, token);
                    strip_string(upnp_ifname);
                    printf("upnp interface name= %s\n", upnp_ifname);
                }
                else {
                    printf("error parsing ifname!\n");
                }
            }
            else {
                printf("unknown item %s\n", token);
            }
        }
        fclose(conf);
    }
    else {
        printf("No configuration file (/etc/wemo_ctrl.conf)...\n");
        printf("using default DB path /tmp/wemo_device.db and /tmp/wemo_state.db\n");
        return 0;
    }

    return 1;
 }
int main()
{
    /* use default values if any one of config item is not found */
    strcpy(wemo_device_db, DEFAULT_WEMO_DEVICE_DB);
    strcpy(wemo_state_db, DEFAULT_WEMO_STATE_DB);
    strcpy(upnp_ifname, DEFAULT_IFNAME);

    if (process_config() == 0) {
        printf("/etc/wemo_ctrl.conf not found using default DB paths\n");
    }
    we_init();
    we_register_event_callback(&event_callback);
    we_register_netstate_callback(&netstate_callback);
    we_register_name_change_callback(&name_change_callback);
    we_register_name_value_callback(&name_value_callback);
    we_register_dev_info_callback(&dev_info_callback);
    wemo_client_cmdloop();
    we_end();
    return 0;
}
