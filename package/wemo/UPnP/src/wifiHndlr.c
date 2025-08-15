/***************************************************************************
*
*
* wifiSetup.c
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
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "global.h"
#include "wemodefs.h"
#include "wifiSetup.h"
#include "logger.h"
#include "wifiHndlr.h"
#include "aes_inc.h"
#include "itc.h"
#include <ithread.h>
#include "watchDog.h"
#include "osUtils.h"
#include "utils.h"
#include "controlledevice.h"
#include "utlist.h"
#if defined(PRODUCT_WeMo_LightV2)
#include "led_control.h"
#endif
#ifdef PRODUCT_WeMo_Insight
#include "insightUPnPHandler.h"
#endif
#include "smartSetupUPnPHandler.h"
#include "thready_utils.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

#define INVALID_THREAD_ID -1

extern int gRa0DownFlag;

int gDoDhcp=1;
int gBootReconnect=0;
char gPasswordKeyData[SIZE_64B];
int gWiFiConfigured;
/*
 * gWiFiConfigured : This variable is used to determine do we have AP info for connection or not
 *    We need to test Wifi Connection only when AP is connected
 *    In case, router is rebooted, we will loose the AP association as well
 */
extern int gDhcpcStarted;
extern int gSetupRequested;
extern char gUserKey[];
extern int gAppCalledCloseAp;
extern char 	g_szWiFiMacAddress[];
extern char 	g_szSerialNo[];
extern pthread_t CloseApWaiting_thread;
extern int gDstSupported;
extern int gDstEnable;
extern float g_lastTimeZone;
extern int g_ra0DownFlag;
extern WIFI_PAIR_PARAMS gWiFiParams;
void reconnectHome (void) ;
pthread_attr_t reconn_attr,con_attr,hid_attr,closeap_attr,mon_attr,ntp_attr;
pthread_t inetThd=0,reconnthread=0,closeApThId=0,ntpthread;
#ifdef _AP_SITE_SURVEY_
pthread_attr_t siteSur_attr;
#endif //_AP_SITE_SURVEY_

int connthread=-1;
int ledstatus=-1;
int gNTPTimeSet=0;
int gistimerinit = 0;
int gReconnectFlag=0;
int gInetHealthPunch=0;
int gExitReconnectThread=0;
extern int gSignalStrength;

#ifdef _OPENWRT_
int gNTPThreadRunning = 0;
#endif

extern int g_bWiredEthernet;

pthread_t WiFi_Connect_thread = -1, Hidden_Connect_thread=-1,WiFi_Monitor_thread=-1,Inet_Monitor_thread=0;
;
#ifdef USE_PING_DEFAULT
char bufAdr[SIZE_256B];
#endif

#define ISITE "4.2.2.2"
#define GOOGLE_DNS_1 "8.8.8.8"
#define GOOGLE_DNS_2 "8.8.4.4"
#define OPENDNS_HOME "208.67.222.222"
#define BELKIN_DOMAIN_NAME "www.belkin.com"
#define ROOTSERVER_DOMAIN_NAME "A.root-servers.net"
#define LED_TIME_OFF_INTERVAL 30
#define CLOSE_AP_TIMEOUT  600
#define SMART_AP_TIMEOUT  300
#define PASSWORD_SALT_LEN	(8+1)
#define PASSWORD_IV_LEN		(16+1)
#define PASSWORD_KEYDATA_LEN	SIZE_256B
#define CHECK_INET_TIMEVAL	60
#define SUBNET_MASK_24		"255.255.255.0"
#define SUBNET_MASK_8		"255.0.0.0"

#define CLOUD_SERVICE_URL "http://heartbeat.xwemo.com/check.txt"
#define CLOUD_PING_DOMAIN "heartbeat.lswf.net"

#define DRACONIAN_WIFI_SCAN

int gInetSleepInterval = CHECK_INET_TIMEVAL; //default value 1 minutes
volatile int gInternetCount = 5; // default internet check is 5 minutes
static pthread_mutex_t   s_setup_req_mutex;
extern void *CloseApWaitingThread(void *args);
void *InetMonitorTask(void *arg);
static int createLedTimerTaskThread(int time);

void StopInetTask(void)
{
    if(Inet_Monitor_thread) {
        APP_LOG("WiFiApp", LOG_CRIT,"Cancelling Inet_Monitor_Thread");
        pthread_cancel(Inet_Monitor_thread);
        Inet_Monitor_thread = 0;
    }
    pluginUsleep(500000);
    if(inetThd) {
        APP_LOG("WiFiApp", LOG_CRIT,"cancelling checkInetConnect thread");
        pthread_cancel(inetThd);
        inetThd = 0;
    }
    return;
}


#define CHECK_AND_INVOKE_NAT_REINIT(initType)	{\
	if(gpluginRemAccessEnable && UDS_pluginNatInitialized())\
	{\
		if(initType){\
			UDS_invokeNatReInit(NAT_REINIT);}\
		else{\
			UDS_invokeNatDestroy();}\
	}\
}

int gPassPlainTextLen =0;

int SetCurrentClientState(int curState)
{
    char *parameter[1] = {"NetworkStatus"};
    char *value[1];

    osUtilsGetLock(&s_client_state_mutex);
    gWiFiClientCurrState = curState;
    value[0] = (char *)MALLOC(sizeof(int));
    if (value[0]) {
        snprintf(value[0], sizeof(int), "%d", gWiFiClientCurrState);
        APP_LOG("DEVICE:WIFI", LOG_DEBUG, "Notify: NetworkStatus: %s", value[0]);
        UpnpNotify(device_handle,
               SocketDevice.service_table[PLUGIN_E_SETUP_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_SETUP_SERVICE].ServiceId,
               (const char **)parameter,
               (const char **)value,
               1);
        free(value[0]);
    }
    else {
        APP_LOG("DEVICE:rule", LOG_ERR, "MALLOC failed!!");
    }
    osUtilsReleaseLock(&s_client_state_mutex);

    return 0x00;
}

void checkScanFailCount (int count, int *scanFailCnt, int maxFailCnt)
{
    if(count == 0) {
        (*scanFailCnt)++;
        APP_LOG("checkScanFailCount", LOG_CRIT,"No networks found in SiteScan: %d...", *scanFailCnt);
#ifdef __MIPSEL__
        system("ifconfig apcli0 up");
#else
        system("ifconfig br-lan up");
#endif
    } else
        *scanFailCnt=0;

#ifdef DRACONIAN_WIFI_SCAN
    if(*scanFailCnt == maxFailCnt) {
        APP_LOG("checkScanFailCount", LOG_ALERT,"No networks found in SiteScan for consecutive %d times..., restarting radio!!!", *scanFailCnt);
        system("iwpriv ra0 set RadioOn=0;sleep 1;iwpriv ra0 set RadioOn=1");
        *scanFailCnt=0;
    }
#endif
}

void initClientStatus()
{
    osUtilsCreateLock(&s_client_state_mutex);
}


void initSetupMutex()
{
    osUtilsCreateLock(&s_setup_req_mutex);
}

int setSetupRequested(int state)
{
    osUtilsGetLock(&s_setup_req_mutex);
    gSetupRequested=state;
    osUtilsReleaseLock(&s_setup_req_mutex);

    return 0x00;
}

int isSetupRequested(void)
{
    int state;
    osUtilsGetLock(&s_setup_req_mutex);
    state = gSetupRequested;
    osUtilsReleaseLock(&s_setup_req_mutex);

    return state;
}

/************************************************************************
 * Function: initWiFiHandler
 *     Initialize settings configuration source.
 *  Parameters:
 *    None.
 *  Return:
 *     SUCCESS/FAILURE
 ************************************************************************/
int initWiFiHandler ()
{
    int retVal = FAILURE;

    memset (&gWiFiParams,0,sizeof(WIFI_PAIR_PARAMS));
#ifdef USE_PING_DEFAULT
    memset(bufAdr,0,SIZE_256B);
#endif

    initClientStatus();
    initSetupMutex();
    initSiteSurveyStateLock();

    retVal = wifiCheckConfigAvl();

#ifdef NFC_CONFIG
    if(FAILURE == retVal) {
        // We didn't find WiFi parameters in EEPROM, try to read
        // them from the NFC chip.
        void StartNfcThread(void);
        StartNfcThread();
    }
#endif

    if(FAILURE == retVal) {
        APP_LOG("WiFiApp", LOG_ERR, "No Data in Flash, State 5: Setup mode");
#if defined(PRODUCT_WeMo_Dimmer)
        /* set animation to signify that the device is in AP Mode(2) */
        setAnimation(LED_STATE_AP_MODE);
#elif defined(PRODUCT_WeMo_LightV2)
        system("touch /tmp/upnp.init");
        /* give a couple of sec for wemohap to start */
        sleep(2);
        SetWiFiLED(RGB_READY_TO_CONNECT);
#elif defined(PRODUCT_WeMo_SNSV2)
        SetWiFiLED(0x05);
#endif
        gBootReconnect=0;
        APP_LOG("WiFiApp",LOG_ERR, "gBootReconnect=0");

        //Create a timer task to monitor if Installation begins-TODO
        //Create a thread to look for connection happens.-TODO.
#ifdef _AP_SITE_SURVEY_
        pthread_t siteSur_thread;
#endif //_AP_SITE_SURVEY_
        g_ra0DownFlag = 0; //RA0 interface is UP
#ifdef _AP_SITE_SURVEY_
        APP_LOG("UPNP",LOG_DEBUG, "***************SiteSurvey Thread created***************\n");
        pthread_attr_init(&siteSur_attr);
        pthread_attr_setdetachstate(&siteSur_attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&siteSur_thread, &siteSur_attr, siteSurveyPeriodic, NULL);
#endif
    } else {
        /* For Switch V2 & dimmer, this will be happen after reconnect to AP */
        /* In rare condition, if "ifconfig ra0 down" happens in the middle of wifi connection */
        /* attempt, it will disrupt wifi connectivity. */
#if !defined(PRODUCT_WeMo_Dimmer) && !defined(PRODUCT_WeMo_SNSV2)
        setAPIfState("OFF");
        g_ra0DownFlag = 1; //RA0 interface is Down
        //Get Parameters from Flash and switch to Client Mode.-TODO
#endif
    }

    return retVal;
}

/**
 * @brief  base64Decode: Decodes the string that is base64 encoded
 * @param  sEnc   [IN]  - Encoded string.
 * @param  sDec   [OUT] - Decoded string.
 * @param  decLen [OUT] - Decoded string length.
 *
 * @return retVal: 0: On Success.
 *                -1: On Failure
 *
 * @author Christopher A F
 */
char* base64Decode(char *sEnc, unsigned int *decLen)
{
    int              retVal    = SUCCESS;
    char *sDec = NULL;
    unsigned int     len       = 0;
    unsigned int     decBufLen = 0;

    BIO             *bio       = NULL;
    BIO             *b64       = NULL;

    APP_LOG("AES", LOG_DEBUG, "***** Entered *****");

    /* Input parameter validation */
    if ((NULL == sEnc) || (NULL == decLen)) {
        APP_LOG("AES", LOG_DEBUG, "%d: Invalid arguments", __LINE__);
        retVal = INVALID_PARAMS;
        goto CLEAN_RETURN;
    }

    decBufLen = *decLen;

    sDec = (char *)MALLOC(decBufLen+1);

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf(sEnc, strlen (sEnc));

    if(!b64 || !bio) {
        APP_LOG("AES", LOG_DEBUG, "Either b64 or bio is null");
        if(bio)
            BIO_free_all(bio);
        retVal = FAILURE;
        goto CLEAN_RETURN;
    }

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);
    len = BIO_read(bio, sDec, decBufLen);

    APP_LOG("AES", LOG_DEBUG, "Base64 Decoded Message len = %d", len);

    if (len > decBufLen) {
        BIO_free_all(bio);
        retVal = FAILURE;
        goto CLEAN_RETURN;
    }

    *decLen = len;

    BIO_free_all(bio);

CLEAN_RETURN:

    if(retVal) {
        if(sDec) {
            free(sDec);
            sDec = NULL;
        }
    }

    return sDec;
}

void createPasswordKeyDataV3()
{
    /* bVduaWFyZkllaWVAb3RjbHAkcm9uT2Jh */
    const char string[] = "bVdu";
    const char string1[] = "aWFy";
    const char string2[] = "Zkll";
    const char string3[] = "aWVA";
    const char string4[] = "b3Rj";
    const char string5[] = "bHAk";
    const char string6[] = "cm9u";
    const char string7[] = "T2Jh";

    memset(gPasswordKeyData,0,sizeof(gPasswordKeyData));
    /* copy 3 MSB of the MAC address */
    memcpy(gPasswordKeyData, g_szWiFiMacAddress, 3);
    /* 9-11 */
    strncat(gPasswordKeyData, g_szWiFiMacAddress + 9, 3);

    /* Append the  serial number */
    strncat(gPasswordKeyData, g_szSerialNo, sizeof(gPasswordKeyData) - strlen(gPasswordKeyData) - 1);
    strncat(gPasswordKeyData, string, 4);
    strncat(gPasswordKeyData, string1, 4);
    strncat(gPasswordKeyData, string2, 4);
    strncat(gPasswordKeyData, string3, 4);
    strncat(gPasswordKeyData, string4, 4);
    strncat(gPasswordKeyData, string5, 4);
    strncat(gPasswordKeyData, string6, 4);
    strncat(gPasswordKeyData, string7, 4);

    /* 6 - 8 */
    strncat(gPasswordKeyData, g_szWiFiMacAddress + 6, 3);
    /* 3 - 5 */
    strncat(gPasswordKeyData, g_szWiFiMacAddress + 3, 3);

}

void createPasswordKeyDataV2()
{
    const char string[] = "b3{8";
    const char string1[] = "t;80";
    const char string2[] = "dIN{";
    const char string3[] = "ra83";
    const char string4[] = "eC1s";
    const char string5[] = "?M70";
    const char string6[] = "?683";
    const char string7[] = "@2Yf";

    memset(gPasswordKeyData,0,sizeof(gPasswordKeyData));
    /* copy 3 MSB of the MAC address */
    memcpy(gPasswordKeyData, g_szWiFiMacAddress, 6);
    /* Append the  serial number */
    strncat(gPasswordKeyData,g_szSerialNo, sizeof(gPasswordKeyData)-strlen(gPasswordKeyData)-1);
    /* Now copy 3 LSB of the MAC address */
    strncat(gPasswordKeyData,g_szWiFiMacAddress+6, sizeof(gPasswordKeyData)-strlen(gPasswordKeyData)-1);
    strncat(gPasswordKeyData, string, 4);
    strncat(gPasswordKeyData, string1, 4);
    strncat(gPasswordKeyData, string2, 4);
    strncat(gPasswordKeyData, string3, 4);
    strncat(gPasswordKeyData, string4, 4);
    strncat(gPasswordKeyData, string5, 4);
    strncat(gPasswordKeyData, string6, 4);
    strncat(gPasswordKeyData, string7, 4);

    APP_LOG("WiFiApp", LOG_HIDE, "Password key data: %s",gPasswordKeyData);
}

void createPasswordKeyDataV1()
{
    memset(gPasswordKeyData,0,sizeof(gPasswordKeyData));
    /* copy 3 MSB of the MAC address */
    memcpy(gPasswordKeyData, g_szWiFiMacAddress, 6);
    /* Append the  serial number */
    strncat(gPasswordKeyData,g_szSerialNo, sizeof(gPasswordKeyData)-strlen(gPasswordKeyData)-1);
    /* Now copy 3 LSB of the MAC address */
    strncat(gPasswordKeyData,g_szWiFiMacAddress+6, sizeof(gPasswordKeyData)-strlen(gPasswordKeyData)-1);

    APP_LOG("WiFiApp", LOG_HIDE, "Password key data: %s",gPasswordKeyData);
}

void encryptPassword(char *input, char *finalstr)
{
    unsigned char key_data[PASSWORD_KEYDATA_LEN];
    unsigned char salt[PASSWORD_SALT_LEN];
    unsigned char iv[PASSWORD_IV_LEN];
    int key_data_len, salt_len, iv_len;
    unsigned char *ciphertext = (unsigned char *)input;
    int len;
    char basePassword[SIZE_256B];
    char* encStr;
    char lenstr[SIZE_4B];
    int cipher_len=0;

    memset(key_data, 0, sizeof(key_data));
    memset(salt, 0, sizeof(salt));
    memset(iv, 0, sizeof(iv));
    memset(finalstr, 0, PASSWORD_MAX_LEN);
    memset(basePassword, 0, sizeof(basePassword));
    memset(lenstr, 0, sizeof(lenstr));

    len = strlen(input);
    strncpy((char *)key_data, gPasswordKeyData, sizeof(key_data)-1);
    key_data_len = strlen((char *)key_data);
    memcpy(salt, gPasswordKeyData, PASSWORD_SALT_LEN-1);
    memcpy(iv, gPasswordKeyData, PASSWORD_IV_LEN-1);
    salt_len = strlen((char *)salt);
    iv_len = strlen((char *)iv);

    APP_LOG("WiFiApp", LOG_HIDE,"input: %s, input len: %d KeyData: %s KeyData len: %d salt: %s salt len: %d iv: %s iv len: %d", input, len, key_data, key_data_len, salt, salt_len, iv, iv_len);

    ciphertext = pluginAES128Encrypt(key_data, key_data_len, salt, salt_len, iv, iv_len, input, &len);
    if(!ciphertext) {
        APP_LOG("WiFiApp",LOG_ERR, "pluginAESEncrypt failed!!!");
        return;
    }
    ciphertext[len] = '\0';

    APP_LOG("WiFiApp", LOG_HIDE,"Encrypted ciphertext password: %s, ciphertext password len: %d", ciphertext, strlen((char *)ciphertext));
    encStr = base64Encode(ciphertext, len);
    len = strlen(encStr);
    APP_LOG("WiFiApp", LOG_HIDE,"Base 64 encoded ciphertext len: %d",len);
    cipher_len = len;

    strncpy(finalstr,encStr,PASSWORD_MAX_LEN);

    snprintf(lenstr, sizeof(lenstr), "%02X", cipher_len);
    strcat(finalstr,lenstr);

    memset(lenstr, 0, sizeof(lenstr));
    snprintf(lenstr, sizeof(lenstr), "%02X", strlen(input));
    strcat(finalstr,lenstr);

    APP_LOG("WiFiApp", LOG_HIDE,"Final String to be sent: %s",finalstr);

    free(ciphertext);
    free(encStr);

}

int decryptPassword(char *input, char *output)
{
    unsigned char salt[PASSWORD_SALT_LEN];
    unsigned char iv[PASSWORD_IV_LEN];
    char ciphertext[PASSWORD_MAX_LEN];
    char* decodedString;
    unsigned char *password;
    size_t ciphertext_len = 0;
    size_t decrypt_length = 0;

    memset(salt, 0, sizeof(salt));
    memset(iv, 0, sizeof(iv));

    APP_LOG("WiFiApp", LOG_HIDE,"input: %s", input);

    /* update the password string */
    strncpy(ciphertext, input, sizeof(ciphertext)-1);
    ciphertext_len = strlen(ciphertext);

    if(ciphertext[ciphertext_len - 1] != '\n')
        ciphertext[ciphertext_len -4] = '\0';
    else
        ciphertext[ciphertext_len] = '\0';

    /* Removing '\n' characters from the ciphertext before decoding
       as the BIO_FLAGS_BASE64_NO_NL flag is set in decoding API which
       will return decoded length zero if '\n' found in ciphertext     */

    char LocalCip[PASSWORD_MAX_LEN]=" ";
    int i=0,j=0;
    while( (ciphertext[i]) && (i< (PASSWORD_MAX_LEN-1)) ) {
        if(ciphertext[i] != '\n') {
            LocalCip[j++]=ciphertext[i];
        }
        i++;
    }
    LocalCip[j]='\0';
    memset(ciphertext, 0, sizeof(ciphertext));
    strncpy(ciphertext,LocalCip, sizeof(ciphertext)-1);

    ciphertext_len = strlen(ciphertext);
    APP_LOG("WiFiApp", LOG_HIDE,"Updated ciphertext: %s, len: %d, KeyData: %s", ciphertext, ciphertext_len,gPasswordKeyData);

    if(NULL == (decodedString = base64Decode(ciphertext, &ciphertext_len))) {
        APP_LOG("WiFiApp", LOG_ERR,"Base decoding failed...");
        return FAILURE;
    } else {
        APP_LOG("WiFiApp", LOG_DEBUG,"decoded passwd success");
    }

    memcpy(salt, gPasswordKeyData, PASSWORD_SALT_LEN-1);
    memcpy(iv, gPasswordKeyData, PASSWORD_IV_LEN-1);

	decrypt_length = ciphertext_len;
    password = (unsigned char *)pluginPasswordDecrypt((unsigned char *)gPasswordKeyData, strlen(gPasswordKeyData),
                                                      salt, PASSWORD_SALT_LEN-1,
                                                      iv, PASSWORD_IV_LEN-1,
                                                      (unsigned char *)decodedString, (int *) &decrypt_length);
    if(!password) {
        APP_LOG("WiFiApp",LOG_ERR, "passphrase decrypt failed[to 1]...");
        /* use old method for gPasswordKeyData */
        createPasswordKeyDataV1();
        memcpy(salt, gPasswordKeyData, PASSWORD_SALT_LEN-1);
        memcpy(iv, gPasswordKeyData, PASSWORD_IV_LEN-1);
        memcpy(salt, gPasswordKeyData, PASSWORD_SALT_LEN-1);
        memcpy(iv, gPasswordKeyData, PASSWORD_IV_LEN-1);

        decrypt_length = ciphertext_len;
        password = (unsigned char *)pluginPasswordDecrypt((unsigned char *)gPasswordKeyData, strlen(gPasswordKeyData),
                                                          salt, PASSWORD_SALT_LEN-1,
                                                          iv, PASSWORD_IV_LEN-1,
                                                          (unsigned char *)decodedString, (int *) &decrypt_length);
    }
    if(!password) {
        APP_LOG("WiFiApp",LOG_ERR, "passphrase decrypt failed[to 2]...");
        /* use old method for gPasswordKeyData */
        createPasswordKeyDataV2();
        memcpy(salt, gPasswordKeyData, PASSWORD_SALT_LEN-1);
        memcpy(iv, gPasswordKeyData, PASSWORD_IV_LEN-1);
        memcpy(salt, gPasswordKeyData, PASSWORD_SALT_LEN-1);
        memcpy(iv, gPasswordKeyData, PASSWORD_IV_LEN-1);

        decrypt_length = ciphertext_len;
        password = (unsigned char *)pluginPasswordDecrypt((unsigned char *)gPasswordKeyData, strlen(gPasswordKeyData),
                                                          salt, PASSWORD_SALT_LEN-1,
                                                          iv, PASSWORD_IV_LEN-1,
                                                          (unsigned char *)decodedString, (int *) &decrypt_length);
    }
    if(!password) {
        APP_LOG("WiFiApp",LOG_ERR, "passphrase decrypt failed...");
        return FAILURE;
    }

    password[decrypt_length] = '\0';

    APP_LOG("WiFiApp", LOG_HIDE,"Decrypted password: %s, password len - %d", password, strlen((char *)password));

    /* store password plain text len */
    gPassPlainTextLen = decrypt_length;

    if(output)
        strncpy(output, (char *)password, PASSWORD_MAX_LEN);

    if(decodedString)
        free(decodedString);

    free(password);
    return SUCCESS;
}

int findChannelForSSID(char *ssid)
{
    PMY_SITE_SURVEY pAvlNetwork=NULL;
    int chan=-1;
    int found=0;
    int i=0;
    int count=0;
    int retries=5;

    pAvlNetwork = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
    while(retries > 0) {
        EnableSiteSurvey(ssid);
        getCurrentAPList (pAvlNetwork,&count);
        APP_LOG("WiFiApp", LOG_DEBUG,"Avl network list cnt: %d", count);
        for (i=0; i<count; i++) {

            if (!strcmp (pAvlNetwork[i].ssid,ssid)) {

                APP_LOG("WiFiApp", LOG_DEBUG,
                        "pAvlNetwork[%d].channel: %s,pAvlNetwork[%d].ssid: %s",
                        i, pAvlNetwork[i].channel, i, pAvlNetwork[i].ssid);
                chan = atoi((const char *)pAvlNetwork[i].channel);
                found = 1;
                APP_LOG("WiFiApp", LOG_DEBUG,
                        "Channel determined from SITE SURVEY: <%d>",chan);
                break;
            }
        }
        if(found) {
            APP_LOG("WiFiApp", LOG_DEBUG,
                    "Channel determined from SITE SURVEY: <%d> on retry#%d",chan,4-retries);
            break;
        } else {
            retries --;
            pluginUsleep(1000000);
        }
    }

    free(pAvlNetwork);

    if(!found) {
        APP_LOG("WiFiApp", LOG_ERR,
                "SSID: %s not found in the available network list",ssid);
        return -1;
    } else {
        return chan;
    }


}

/************************************************************************
 * Function: wifiCheckConfigAvl
 *     Initialize settings configuration source. Read the Flash area
 *     if network configuration is available. Copy it if avl.
 *  Parameters:
 *    None.
 *  Return:
 *     SUCCESS/FAILURE
 ************************************************************************/
int wifiCheckConfigAvl()
{
    char *pSSID=NULL;
    char *pEncPasswd=NULL;
    int chan=-1;
    PMY_SITE_SURVEY pAvlNetwork;
    int i=0, count=0, found=0;

    pSSID = GetBelkinParameter (WIFI_CLIENT_SSID);
    APP_LOG("WiFiApp", LOG_DEBUG, "SSID:%s",pSSID);
    if(pSSID && strlen (pSSID) > 0) {
        strncpy(gWiFiParams.SSID,pSSID, sizeof(gWiFiParams.SSID)-1);

        pEncPasswd = GetBelkinParameter (WIFI_CLIENT_PASS);
        APP_LOG("WiFiApp", LOG_HIDE, "Encrypted Pass:%s",pEncPasswd);

        if(pEncPasswd) {
            strncpy(gWiFiParams.Key,pEncPasswd, sizeof(gWiFiParams.Key)-1);
            memcpy(gUserKey,pEncPasswd,PASSWORD_MAX_LEN);

            gWiFiConfigured=1;

            /* We should discover the channel every-time we come up for the saved SSID, as it might change */
            /* also use auth and encryption found */
            pAvlNetwork = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
            if(!pAvlNetwork) {
                APP_LOG("WiFiApp", LOG_ERR, "Malloc Failed....\n");
                return FAILURE;
            }
#ifdef PRODUCT_WeMo_Dimmer
            /* set animation to signify that the connection information
               is available. */
            setAnimation(LED_STATE_KNOWN_CONNECTION);
#endif
            EnableSiteSurvey(gWiFiParams.SSID);

            getCompleteAPList (pAvlNetwork,&count);
            for (i=0; i<count; i++) {
                if (strncmp(pAvlNetwork[i].unicode, "Y", 1) == 0) {
                    convertSSID(pAvlNetwork[i].ssid);
                }
                if (!strcmp (pAvlNetwork[i].ssid,gWiFiParams.SSID)) {

                    APP_LOG("WiFiApp", LOG_DEBUG,
                            "pAvlNetwork[%d].channel: %s, gWiFiParams.channel: %d, \
									    pAvlNetwork[%d].ssid: %s, gWiFiParams.SSID: %s",
                            i, pAvlNetwork[i].channel, gWiFiParams.channel,
                            i, pAvlNetwork[i].ssid,gWiFiParams.SSID);
                    chan = atoi((const char *)pAvlNetwork[i].channel);
                    gWiFiParams.channel = chan;
                    APP_LOG("WiFiApp", LOG_DEBUG,
                            "Channel determined from SITE SURVEY: <%d>",chan);
                    sscanf(pAvlNetwork[i].security, "%[^'\\/']/%s",
                           gWiFiParams.AuthMode, gWiFiParams.EncrypType);
                    found = 1;
                    break;
                }
            }

            if(!found) {
                APP_LOG("WiFiApp", LOG_ERR,
                        "SSID: %s not found in the available network list",gWiFiParams.SSID);
                if(!inetThd) {
                    /* create the Inet connectivity monitor thread */
                    createDetachedThread(&inetThd,checkInetConnectivity, NULL);
                }
                if(!Inet_Monitor_thread) {
                    /* create the Inet connectivity monitor thread */
                    createDetachedThread(&Inet_Monitor_thread, InetMonitorTask, NULL);
                }

                APP_LOG("WiFiApp", LOG_DEBUG, "Created checkInetConnectivity Thread...");
                gBootReconnect=1;
                APP_LOG("WiFiApp",LOG_DEBUG, "gBootReconnect=1");

                free(pAvlNetwork);
                return SUCCESS;
            }

            free (pAvlNetwork);
        }
        else {
            return FAILURE;
        }
    }
    else {
        return FAILURE;
    }

    if(chan != -1) {
        APP_LOG("WiFiApp", LOG_DEBUG, "Connection Data available..Trying to connect : %s",pSSID);
        if(!inetThd)
            createDetachedThread(&inetThd,checkInetConnectivity, NULL);

        if(!Inet_Monitor_thread) {
            /* create the Inet connectivity monitor thread */
            createDetachedThread(&Inet_Monitor_thread, InetMonitorTask, NULL);
        }
        gBootReconnect=1;
        APP_LOG("WiFiApp",LOG_DEBUG, "gBootReconnect=1");

    }

    return SUCCESS;
}


int isPhysicalConnected()
{
    int ret = FAILURE;
    char *pSavedSSID = NULL;
    char *pSavedEncrypType = NULL;
    char ssid[SIZE_64B];
    /* if device is disconnected and no apcli0 interface please up it */
    if (getCurrentClientState()==STATE_DISCONNECTED) {
        FILE    *pipe;
        char    pCommand[WIFI_MAXSTR_LEN];
        char    buffer[WIFI_MAXSTR_LEN];
#ifdef __MIPSEL__
        strncpy(pCommand, "ifconfig | grep apcli0", sizeof(pCommand)-1);
#else
        strncpy(pCommand, "ifconfig | grep br-lan", sizeof(pCommand)-1);
#endif
        pipe = popen(pCommand, "r");
        if (pipe == NULL) {
            APP_LOG("NetworkControl", LOG_ERR, "Popen Error %s", strerror(errno));
            resetSystem();
        }
        if (fgets(buffer, WIFI_MAXSTR_LEN, pipe) == NULL) {
#ifdef __MIPSEL__
            APP_LOG("NetworkControl", LOG_DEBUG, "NOT FOUND apcli0: again");
            system("ifconfig apcli0 up");
#else
            APP_LOG("NetworkControl", LOG_DEBUG, "NOT FOUND br-lan: again");
            system("ifconfig br-lan up");
#endif
        }
        pclose(pipe);
    }

    APP_LOG("WiFiApp", LOG_DEBUG, "gWiFiConfigured: %d, gWiFiClientCurrState: %d",
            gWiFiConfigured, getCurrentClientState());

    gInetHealthPunch++;

    if (gWiFiConfigured) {
        /*
         * We need to test Wifi Connection only when AP is connected
         * In case, router is rebooted, we will loose the AP association as well
         */

        pSavedSSID = GetBelkinParameter (WIFI_CLIENT_SSID);
        pSavedEncrypType = GetBelkinParameter(WIFI_CLIENT_ENCRYP);
        if ((pSavedSSID && 0x00 != strlen(pSavedSSID)) &&
            (pSavedEncrypType && strlen(pSavedEncrypType) != 0)) {
            memset(ssid, '\0', WIFI_MAXSTR_LEN+1);
            /************************************************************************
             *  Story: 2571
             *  This "convertSSID" will convert the hex string in raw bytes format,
             *  which is used in pairing with the router
             ************************************************************************/
            strncpy(ssid, pSavedSSID, sizeof(ssid)-1);
            strncpy(ssid, convertSSID(ssid), sizeof(ssid)-1);

            APP_LOG("WiFiApp", LOG_DEBUG, "to checked connection of saved ssid: %s", pSavedSSID);
            ret = isAPConnected(ssid, pSavedEncrypType);
        } else {
            APP_LOG("WiFiApp", LOG_ERR, "#no saved ssid found, connection check stopped and executed later");
            return ret;
        }
    }
    return ret;
}

int getLookup (char *pDomain)
{
    FILE *pipe;
    char command1[SIZE_256B];
    char buf[SIZE_256B] = {0,};
    int retVal=SUCCESS;

    strncpy(command1,"nslookup ", sizeof(command1));

    strncat(command1, pDomain, sizeof(command1)-strlen(command1)-1);

    pipe = popen(command1,"r");
    if (pipe == NULL) {
        APP_LOG("NetworkControl", LOG_ERR, "Popen Error %s", strerror(errno));
        resetSystem();
    }

    while (fgets( buf, SIZE_256B, pipe) != NULL)

    {
        if (strstr(buf, "Resolver") != NULL) {
            APP_LOG("NetworkControl", LOG_DEBUG, "FAILURE----buf:%s", buf);
            retVal = FAILURE;
            break;
        } else if (strstr(buf, "Name")) {
            APP_LOG("NetworkControl", LOG_DEBUG, "buf:%s", buf);
            retVal = FAILURE;
            break;
        }
    }
    pclose(pipe);
    return retVal;
}

int ping_status(char *host, int count)
{
    char command[128];
    char buffer[128];
    FILE *pipe;
    int packet_loss = 0;

    if (strlen(host) == 0) {
        return 0;
    }

    APP_LOG("ping_status", LOG_DEBUG, "ping %d %s ", count, host);

    memset(command, 0, 128);
    sprintf(command, "ping -w 2 -c %d %s | grep \"packet loss\" | awk \'{ print $7 }\'", count, host);

    pipe = popen(command, "r");
    if (pipe == NULL) {
        APP_LOG("ping_status", LOG_DEBUG, "popen Error %s", strerror(errno));
        /* we should never come here, we'll simply return 0 */
        /* so we can keep checking */
        return 0;
    }

    memset(buffer, 0, 128);
    if (!fread(buffer, sizeof(char), 128, pipe)) {
        APP_LOG("ping_state", LOG_DEBUG, "ping failed");
        pclose(pipe);
        /* in case ping failure such os network unreachable case */
        /* we will return 0, and let caller check again */
        return 0;
    }
    pclose(pipe);

    packet_loss = atoi(buffer);
    APP_LOG("ping_status", LOG_DEBUG, "ping %s packet loss = %d", host, packet_loss);
    return packet_loss;
}

int checkRouterConnectivity(int count)
{
    char *pIp,*pGateway;
    char *SSID = NULL;
    char *EncrypType = NULL;

    pIp = GetWanIPAddress();
    pGateway = GetWanDefaultGateway();

    if( pIp && pGateway ) {
        if( strcmp(pIp, "0.0.0.0") && strcmp(pGateway, "0.0.0.0") ) {
            APP_LOG("checkRouterConnectivity", LOG_DEBUG, "pGateway is %s", pGateway);
            SSID = GetBelkinParameter (WIFI_CLIENT_SSID);
            EncrypType = GetBelkinParameter(WIFI_CLIENT_ENCRYP);
            if ((SSID && 0x00 != strlen(SSID)) &&
                (EncrypType && strlen(EncrypType) != 0)) {
                if(isAPConnected(SSID, EncrypType) == SUCCESS) {
                    APP_LOG("checkRouterConnectivity", LOG_DEBUG, "AP paired");
                    return SUCCESS;
                }
                else {
                    APP_LOG("checkRouterConnectivity", LOG_DEBUG, "AP connection lost");
                    return FAILURE;
                }
            }
            else {
                APP_LOG("checkRouterConnectivity", LOG_DEBUG, "AP not configured");
                return FAILURE;
            }

            if (!strcmp(EncrypType, "NONE") || !strcmp(EncrypType, "WEP")) {
                if( PACKET_LOSE_100_PERCENT != ping_status(pGateway, count) ) {
                    APP_LOG("checkRouterConnectivity", LOG_DEBUG, "ping_status(%s) is alive...", pGateway);
                    return SUCCESS;
                } else {
                    APP_LOG("checkRouterConnectivity", LOG_DEBUG, "ping_status(%s) is dead...", pGateway);
                    return FAILURE;
                }
            }
        }
    } else {
        APP_LOG("checkRouterConnectivity", LOG_DEBUG, "pIp and pGateway is not existed...")
    }
    return FAILURE;
}
/*
  EnableSiteSurvey:
  If argument is null, then it will just issue SiteSurvey.
  If argement is specific ssid, then it will do site survey for the ssid.
 */
void EnableSiteSurvey(char *ssid)
{
    char command[SIZE_64B];
    memset(command, '\0', SIZE_64B);
    if (ssid == NULL) {
        strncpy(command, "SiteSurvey=1", sizeof(command)-1);
    }
    else {
        sprintf(command, "SiteSurvey=%s", ssid);
    }
#ifdef __MIPSEL__
    wifiSetCommand (command,"apcli0");
#else
    wifiSetCommand (command,"br-lan");
#endif
    pluginUsleep(4000000);
}

int getCurrentAPList (PMY_SITE_SURVEY SiteSurvey,int *pListCount)
{
#ifdef __MIPSEL__
    return wifiGetNetworkList (SiteSurvey,"apcli0",pListCount);
#else
    return wifiGetNetworkList (SiteSurvey,"br-lan",pListCount);
#endif
}

int saveData (int chan,char *pSSID,char *pAuth,char *pEnc,char *pPass)
{
    char channel[SIZE_4B];
    char lenstr[SIZE_4B];
    memset(lenstr, 0, sizeof(lenstr));

    snprintf (channel, sizeof(channel), "%d",chan);
    SetBelkinParameter (WIFI_CLIENT_SSID,pSSID);

    /* copy cipher and plain pass len if not exists */
    if(pPass[strlen(pPass) - 1] == '\n') {
        snprintf(lenstr, sizeof(lenstr), "%02X", strlen(pPass));
        strcat(pPass,lenstr);

        memset(lenstr, 0, sizeof(lenstr));
        snprintf(lenstr, sizeof(lenstr), "%02X", gPassPlainTextLen);
        strcat(pPass,lenstr);
    }

    SetBelkinParameter (WIFI_CLIENT_PASS,pPass);
    SetBelkinParameter (WIFI_CLIENT_AUTH,pAuth);
    SetBelkinParameter (WIFI_CLIENT_ENCRYP,pEnc);
    SetBelkinParameter (WIFI_AP_CHAN,channel);
    UnSetBelkinParameter("wifi_reset_happened");
    gWiFiConfigured=1;
    AsyncSaveData();
    return 0;
}

#if defined(PRODUCT_WeMo_LightV2)
extern int g_PowerStatus;
#endif
int ledStatusOff (void)
{
    //APP_LOG("WiFiApp", LOG_DEBUG,"State 4: after 30 seconds OFF");
#if defined(PRODUCT_WeMo_LightV2)
    if (g_PowerStatus) {
        SetWiFiLED(RGB_SWITCH_ON);
    }
    else {
        SetWiFiLED(RGB_SWITCH_OFF);
    }
#elif defined(PRODUCT_WeMo_SNSV2)
    SetWiFiLED(0x04);
#endif
    return SUCCESS;
}

void* ledTimerTask(void *pTime)
{
    unsigned int time;
    tu_set_my_thread_name( __FUNCTION__ );

    if(pTime) {
        time = *((int *)pTime);
        /* changing time in microseconds to pass to pluginUsleep() */
        pluginUsleep(time * 1000000);
        free(pTime);
    }

    ledStatusOff();
    return NULL;
}

static int createLedTimerTaskThread(int time)
{
    pthread_t ledThread = INVALID_THREAD_ID;
    pthread_attr_t ledThreadAttr;
    int retVal;
    int* pTime = MALLOC(sizeof(int));
    *pTime=time;

    pthread_attr_init(&ledThreadAttr);
    pthread_attr_setdetachstate(&ledThreadAttr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&ledThread, &ledThreadAttr, ledTimerTask, (void *)pTime);
    if(retVal < SUCCESS) {
        free(pTime);
        APP_LOG("WiFiApp",LOG_ALERT, "Thread for status led change is not created, so reset app");
        resetSystem();
    }

    if(time) {
        APP_LOG("WiFiApp",LOG_DEBUG,"Started the timer for %d seconds...", time);
    } else {
        APP_LOG("WiFiApp",LOG_DEBUG,"Disarmed the timer...");
    }

    return 0;
}

void *InetMonitorTask(void *arg)
{

    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("WiFiApp",LOG_DEBUG,"InetMonitorTask running...");

    if(g_bWiredEthernet) {
        // Wait for dhcp to complete
        APP_LOG("WiFiApp",LOG_CRIT,"Waiting for IP address in wired mode");
        for( ; ; ) {
            if(isValidIp()) {
                APP_LOG("WiFiApp",LOG_CRIT,"Got IP address in wired mode");
                setAPIfState("OFF");
                g_ra0DownFlag = 1; //RA0 interface is Down
#if defined(PRODUCT_WeMo_LightV2)
            if(!isSetupRequested())
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_1);
            else
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_2);
#elif defined(PRODUCT_WeMo_SNSV2)
                SetWiFiLED(0x01);
#endif
#ifdef __MIPSEL__
                if(!gistimerinit) {
                    createLedTimerTaskThread(LED_TIME_OFF_INTERVAL);
                }
#endif
                SetCurrentClientState(STATE_CONNECTED);
                NotifyInternetConnected();
#ifdef PRODUCT_WeMo_Insight
                {
                    char SetUpCompleteTS[SIZE_32B];
                    memset(SetUpCompleteTS, 0, sizeof(SetUpCompleteTS));
                    if(!g_SetUpCompleteTS) {
                        g_SetUpCompleteTS = GetUTCTime();
                        sprintf(SetUpCompleteTS, "%lu", g_SetUpCompleteTS);
                        SetBelkinParameter(SETUP_COMPLETE_TS, SetUpCompleteTS);
                        AsyncSaveData();
                    }
                    APP_LOG("ITC: network",LOG_ERR,"UPnP updated on setup complete g_SetUpCompleteTS---%lu, SetUpCompleteTS--------%s:", g_SetUpCompleteTS, SetUpCompleteTS);
                }
#endif
                break;
            }
            sleep(1);
        }
    }

    while(1) {
        pluginUsleep(120000000);
        /* WEMO-47850:exit the thread to avoid reset during firmware update */
        if(IS_FIRMWARE_FLASHING) {
            APP_LOG("WiFiApp", LOG_DEBUG, "InetMon Thread exiting....");
            pthread_exit(NULL);
        }
        if(gInetHealthPunch == 0) {
            APP_LOG("WiFiApp",LOG_CRIT,"InetMonitorTask detected bad health so resetSystem...");
            resetSystem();
        } else {
            APP_LOG("WiFiApp",LOG_DEBUG,"Inet connectivity thread health OK [%d]...", gInetHealthPunch);
            gInetHealthPunch = 0;
        }
    }
    return NULL;
}

void checkApCloseStatus()
{
    APP_LOG("WiFiApp",LOG_DEBUG, "In check Close AP Status...");
    if(!gAppCalledCloseAp) {
        APP_LOG("WiFiApp",LOG_DEBUG, "App did not close AP...");
        if (-1 != CloseApWaiting_thread) {
            APP_LOG("WiFi", LOG_DEBUG, "close ap thread already created");
            return;
        }
        pthread_attr_t closeApWaiting_attr;
        pthread_attr_init(&closeApWaiting_attr);
        /* WEMO-46785:detach the thread to avoid any resource leak. */
        pthread_attr_setdetachstate(&closeApWaiting_attr, PTHREAD_CREATE_DETACHED);
        /* CloseApWaitingThread itself turns off AP */
        pthread_create(&CloseApWaiting_thread, &closeApWaiting_attr, CloseApWaitingThread, NULL);
        APP_LOG("UPNP: Device", LOG_DEBUG, "AP closing now .......");
    } else
        APP_LOG("WiFiApp",LOG_DEBUG, "App has already closed AP, nothing to do...");
}

void* closeApThread(void *arg)
{
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("WiFiApp",LOG_DEBUG, "In Close AP Thread...");
    int timeOut = *(int *)arg;
    free(arg);
    pluginUsleep(timeOut*1000000);
    checkApCloseStatus();
    APP_LOG("WiFiApp",LOG_DEBUG, "Close AP Thread exiting...");

    return 0;
}

void createCloseApThread(int timeout)
{
    int retVal;
    int *timeOut = (int*)CALLOC(1, sizeof(int));
    if(!timeOut) {
        APP_LOG("WiFi",LOG_DEBUG, "Memory could not be allocated for timeOut");
        resetSystem();
    }
    *timeOut = timeout;
    pthread_attr_init(&closeap_attr);
    pthread_attr_setdetachstate(&closeap_attr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&closeApThId,&closeap_attr,
                            (void*)&closeApThread, (void*)timeOut);
    if(retVal < SUCCESS) {
        APP_LOG("WiFiApp",LOG_CRIT,
                "Close AP Thread not created, errno: %d", errno);
    } else
        APP_LOG("WiFiApp",LOG_DEBUG,
                "Close AP Thread created successfully");
}

void *ConnectWiFiTask(void *args)
{
    int ret = 0x00,ret1=0x0;
    PWIFI_PAIR_PARAMS pWiFi = (PWIFI_PAIR_PARAMS)args;
    int oldstate,oldtype;
    char buffer[SIZE_64B];


    memset(buffer, 0,  sizeof(buffer));
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE , &oldstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);


    APP_LOG("WiFiApp", LOG_DEBUG, "############ WiFi pairing task is running ##############");
wifi_pair:
#ifdef __MIPSEL__
    ret = wifiPair(pWiFi, "apcli0");
#else
    ret = wifiPair(pWiFi, "br-lan");
#endif
    if (ret >= SUCCESS) {
        APP_LOG("WiFiApp", LOG_DEBUG, "WiFi physical connection established and dhcpc started");

        memcpy (&gWiFiParams,pWiFi,sizeof(WIFI_PAIR_PARAMS));

        /* over-write the password as encrypted password string */
        memset(gWiFiParams.Key,0,sizeof(gWiFiParams.Key));
        memcpy(gWiFiParams.Key,gUserKey,sizeof(gWiFiParams.Key));
        APP_LOG("WiFiApp", LOG_HIDE,"Updated Key: %s, gUserKey: %s", gWiFiParams.Key, gUserKey);

        if(gReconnectFlag) {
            pluginUsleep(500000);
            gReconnectFlag=0;
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
            setAPIfState("OFF");
            g_ra0DownFlag = 1; //RA0 interface is Down
#endif
            APP_LOG("WiFiApp", LOG_DEBUG,"Going to test connection now");
            ret1 = wifiTestConnection (INTERFACE_CLIENT, 10, 1);
        } else {
            //- Check IP address
            APP_LOG("WiFiApp", LOG_DEBUG, "################# Waiting IP address to be allocated now #################");
            ret1 = isValidIp();
            if (!ret1) {
                SetCurrentClientState(STATE_PAIRING_FAILURE_IND);
            }
        }
        //Just trying to avoid confusion with the pairing ret, changed Name to ret1 i.e. result of testConnection.
        //return value of wifiTestConnection is either 1 or 0. testing for 3 will make this loop never enter.
        //Also extending this loop to make sure all saving also happens once this connection is tested.
        if(STATE_CONNECTED == ret1 || STATE_INTERNET_NOT_CONNECTED == ret1) {
            APP_LOG("WiFiApp", LOG_DEBUG,"State: %d", getCurrentClientState());
            SetCurrentClientState(STATE_INTERNET_NOT_CONNECTED);
#ifdef PRODUCT_WeMo_Dimmer
            /* set animation to signify that the connection is established
               LED_STATE_CONNECTION_ESTABLISHED(3) */
            setAnimation(LED_STATE_CONNECTION_ESTABLISHED);
#elif defined(PRODUCT_WeMo_LightV2)
            if(!isSetupRequested())
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_1);
            else
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_2);
#elif defined(PRODUCT_WeMo_SNSV2)
            SetWiFiLED(0x01);
#endif

#ifdef __MIPSEL__
            if(!gistimerinit) {
                createLedTimerTaskThread(LED_TIME_OFF_INTERVAL);
            }
#endif
            gDoDhcp = 0;
            system("route del -net 239.0.0.0 netmask 255.0.0.0");

            saveData(gWiFiParams.channel,pWiFi->SSID,pWiFi->AuthMode,pWiFi->EncrypType,gWiFiParams.Key);
            APP_LOG("WiFiApp", LOG_HIDE,"Saved Key to: %s", gWiFiParams.Key);

            if(!inetThd) {
                //Create a thread to monitor the client connection.
                gInetSleepInterval = 60;	//run Internet status check every minute
                gInternetCount = 5;        //default value 5 minutes
                APP_LOG("WiFiApp", LOG_DEBUG,"Changed gInetSleepInterval to %d and gInternetCheckCount to %d", gInetSleepInterval, gInternetCount);
                createDetachedThread(&inetThd,checkInetConnectivity, NULL);
                if(!Inet_Monitor_thread) {
                    /* create the Inet connectivity monitor thread */
                    createDetachedThread(&Inet_Monitor_thread, InetMonitorTask, NULL);
                }

            }
        } else {
            //MARK this as Pairing failure???
            APP_LOG("NetworkControl", LOG_CRIT, "########## Pairing failure-Connect ***********************");
            //We should save the configuration even if we couldn't obtain an IP
            //saveData(gWiFiParams.channel,pWiFi->SSID,pWiFi->AuthMode,pWiFi->EncrypType,gWiFiParams.Key);

            SetCurrentClientState(STATE_IPADDR_NEGOTIATION_FAILED);
            /*            StopDhcpRequest(); */
            ret = INVALID_PARAMS;
            if(inetThd)
                gExitReconnectThread=1;
        }
    } else {
        //- Put it as pairing failure
        APP_LOG("NetworkControl", LOG_CRIT, "######################## Pairing failure ***********************");


        // - Jira story 2252 - Apple airport express firmware version 7.6.1 bug
        if( (!strcmp(pWiFi->AuthMode,"WPA2PSK")) ) {
            APP_LOG("NetworkControl", LOG_CRIT, "Pairing failure in WPA mode, re-trying once using WEP mode...");
            strncpy(buffer,pWiFi->AuthMode, sizeof(buffer)-1);
            memset(pWiFi->AuthMode, 0, sizeof(pWiFi->AuthMode));
            strncpy(pWiFi->AuthMode, "WEP", sizeof(pWiFi->AuthMode)-1);
            ret = 0x0;
            goto wifi_pair;
        }

        if(strlen(buffer) > 0x1) {
            memset(pWiFi->AuthMode, 0, sizeof(pWiFi->AuthMode));
            strncpy(pWiFi->AuthMode, buffer, sizeof(pWiFi->AuthMode)-1);
            memset(buffer, 0, sizeof(buffer));
        }

        SetCurrentClientState(STATE_PAIRING_FAILURE_IND);
        ret = INVALID_PARAMS;
    }

    /* kill the monitoring thread as we are done with the required processing */
    APP_LOG("NetworkControl", LOG_DEBUG, "Killing the monitoring thread...");
    if(-1 != WiFi_Monitor_thread)
        pthread_cancel(WiFi_Monitor_thread);

    free (pWiFi);
    setSetupRequested(0);
    WiFi_Connect_thread=-1;
    if (ret >= SUCCESS) {
            createCloseApThread(CLOSE_AP_TIMEOUT);
    }
    return (void *)ret;
}

int connectHomeNetwork(int chan, char *pSSID, int unicode, char *pAuth, char *pEnc, char *pPass)
{
    PWIFI_PAIR_PARAMS pWiFi;
    int ret = 0;
    char password[SIZE_256B]; //the decrypted password string

    APP_LOG("WiFiApp", LOG_HIDE,"%d-%s-%s-%s", chan, pSSID, pAuth, pEnc);
    APP_LOG("WiFiApp", LOG_HIDE,"Password: %s",pPass);

    int networkState = getCurrentClientState();

    if((networkState > STATE_DISCONNECTED) && (networkState != STATE_PAIRING_FAILURE_IND)  &&
       (networkState != STATE_IPADDR_NEGOTIATION_FAILED)) {
        APP_LOG("WiFiApp", LOG_DEBUG,"#### Network already connected to %s #####", gWiFiParams.SSID);
#ifdef PRODUCT_WeMo_Dimmer
        setAnimation(LED_STATE_CONNECTION_RESTABLISHED);
#endif
        return networkState;
    }

    pWiFi = (PWIFI_PAIR_PARAMS) ZALLOC(sizeof(WIFI_PAIR_PARAMS));

    if(!pWiFi) {
        APP_LOG("WiFiApp", LOG_ERR,"Malloc FAILED");
        return FAILURE;
    }

    /* Using base64 encoding/decoding scheme for password encryption */

    memset(password, 0, sizeof(password));
    if(strcmp(pAuth,"OPEN")) {
        {
            if(SUCCESS != decryptPassword(pPass, password)) {
                APP_LOG("WiFiApp", LOG_DEBUG,"Base decoding failed...");
                free(pWiFi);
                return FAILURE;
            } else {
                APP_LOG("WiFiApp", LOG_HIDE,"decoded passwd: %s...", password);
            }
        }
    } else
        strncpy(password,"NOTHING", sizeof(password)-1);

    strncpy(pWiFi->SSID,pSSID, sizeof(pWiFi->SSID)-1);
    pWiFi->unicode = unicode;
    strncpy(pWiFi->AuthMode,pAuth, sizeof(pWiFi->AuthMode)-1);
    strncpy(pWiFi->EncrypType,pEnc, sizeof(pWiFi->EncrypType)-1);
    strncpy(pWiFi->Key,password, sizeof(pWiFi->Key)-1);
    pWiFi->channel = chan;

    /* JIRA WEMO-435: ra0 to be kept up while setup is in progress */
    if(!isSetupRequested())
        gRa0DownFlag = 1;

#ifdef __MIPSEL__
    ret = wifiPair(pWiFi, "apcli0");
#else
    ret = wifiPair(pWiFi, "br-lan");
#endif
    if (ret >= SUCCESS) {
        APP_LOG("WiFiApp", LOG_DEBUG,"Pairing Successful");
        memcpy (&gWiFiParams,pWiFi,sizeof(WIFI_PAIR_PARAMS));

//Save the encrypted password
        /* over-write the password as encrypted password string */
        memset(gWiFiParams.Key,0,sizeof(gWiFiParams.Key));
        memcpy(gWiFiParams.Key,gUserKey,sizeof(gWiFiParams.Key));
        APP_LOG("WiFiApp", LOG_HIDE,"Updated Key: %s, gUserKey: %s", gWiFiParams.Key, gUserKey);
        /* udhcpc has been just started in wifiPair, wait for some time before test connection */
        if(gReconnectFlag) {
            gReconnectFlag=0;
            APP_LOG("WiFiApp", LOG_DEBUG,"Going to test connection now....");

            ret = isValidIp();
            if(ret == STATE_INTERNET_NOT_CONNECTED)
                ret = wifiTestConnection (INTERFACE_CLIENT,10,1);
        } else {
            if(STATE_INTERNET_NOT_CONNECTED == (ret = isValidIp()))
                ret = STATE_CONNECTED;

        }

        if(STATE_CONNECTED == ret) {
            APP_LOG("WiFiApp", LOG_DEBUG,"State: Connection OK");
#ifdef PRODUCT_WeMo_Dimmer
            /* set animation to signify that the connection is re-established
               LED_STATE_CONNECTION_RESTABLISHED(3b) */
            setAnimation(LED_STATE_CONNECTION_RESTABLISHED);
#elif defined(PRODUCT_WeMo_LightV2)
            if(!isSetupRequested())
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_1);
            else
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_2);
#elif defined(PRODUCT_WeMo_SNSV2)
            SetWiFiLED(0x01);
#endif

#ifdef __MIPSEL__
            if(!gistimerinit) {
                createLedTimerTaskThread(LED_TIME_OFF_INTERVAL);
            }
#endif
            SetCurrentClientState(STATE_INTERNET_NOT_CONNECTED);
            gDoDhcp = 0;
            system("route del -net 239.0.0.0 netmask 255.0.0.0");

            if(!inetThd) {
                //Create a thread to monitor the client connection.
                createDetachedThread(&inetThd,checkInetConnectivity, NULL);

                if(!Inet_Monitor_thread) {
                    /* create the Inet connectivity monitor thread */
                    createDetachedThread(&Inet_Monitor_thread, InetMonitorTask, NULL);
                }

            }
        }

        gWiFiConfigured=1;

    } else {
        ret = INVALID_PARAMS;
    }

    free (pWiFi);

    return ret;
}


void StopWiFiPairingTask()
{
    int errnum;

    if (-1 != WiFi_Connect_thread) {
        APP_LOG("WiFiApp",LOG_DEBUG, "Sending kill to connectWiFitask...");
        errnum = pthread_cancel(WiFi_Connect_thread);
        if(errnum)
            APP_LOG("WiFiApp",LOG_DEBUG, "pthread_kill ret: %d...",errnum);

        WiFi_Connect_thread = -1;
        setSetupRequested(0);
    }
}

void *MonitorWiFiTask (void *args)
{
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("WiFiApp",LOG_DEBUG, "Running MonitorWiFiTask...");
    /* sleep for 70 seconds and kill the connectWiFiThread if it exists */
    pluginUsleep(70000000);

    APP_LOG("WiFiApp",LOG_DEBUG, "Going to kill connectWiFitask if it exists...");
    StopWiFiPairingTask();
    return NULL;
}

int threadConnectHomeNetwork(int chan, char *pSSID, char *pAuth, char *pEnc, char *pPass)
{
    PWIFI_PAIR_PARAMS pWiFi;
    int retVal=0;
    char password[PASSWORD_MAX_LEN]; //the decrypted password string

    APP_LOG("WiFiApp", LOG_CRIT, "%d-%s-%s-%s", chan, pSSID, pAuth, pEnc);
    APP_LOG("WiFiApp", LOG_HIDE, "Password: %s", pPass);

    int networkStatus = getCurrentClientState();

    if (networkStatus == STATE_PAIRING_FAILURE_IND || networkStatus == STATE_IPADDR_NEGOTIATION_FAILED) {
        //- New connection, reset if previous one is incorrect
        SetCurrentClientState(STATE_DISCONNECTED);
    }

    if((networkStatus > STATE_DISCONNECTED) && (networkStatus != STATE_PAIRING_FAILURE_IND)  &&
       (networkStatus != STATE_IPADDR_NEGOTIATION_FAILED)) {
        APP_LOG("WiFiApp", LOG_DEBUG,"#### already connected to %s ####", gWiFiParams.SSID);
        setSetupRequested(0);
        return FAILURE;
    }

    pWiFi = (PWIFI_PAIR_PARAMS) ZALLOC(sizeof(WIFI_PAIR_PARAMS));

    if(!pWiFi) {
        APP_LOG("WiFiApp", LOG_ERR,"Malloc FAILED");
        setSetupRequested(0);
        return FAILURE;
    }

    /* Using base64 encoding/decoding scheme for password encryption */

    memset(password, 0, sizeof(password));
    if(strcmp(pAuth,"OPEN")) {
        if( (!pPass) || (strlen(pPass) < 0x1) || (strcmp(pPass, "(null)") == 0x0) ) {
            APP_LOG("WiFiApp", LOG_CRIT,"Requesting pairing without password, while Auth set... failed...");
            setSetupRequested(0);
            return FAILURE;
        }
        {
            if(SUCCESS != decryptPassword(pPass, password)) {
                APP_LOG("WiFiApp", LOG_DEBUG,"Base decoding failed...");
                free(pWiFi);
                setSetupRequested(0);
                return FAILURE;
            } else {
                APP_LOG("WiFiApp", LOG_HIDE,"decoded passwd: %s...", password);
            }
        }
    } else
        strncpy(password,"NOTHING", sizeof(password)-1);

    strncpy(pWiFi->SSID, pSSID, sizeof(pWiFi->SSID)-1);
    if (isStrPrintAble(pWiFi->SSID, strlen(pWiFi->SSID))) {
        pWiFi->unicode = 0;
    }
    else {
        pWiFi->unicode = 1;
    }
    strncpy(pWiFi->AuthMode,pAuth, sizeof(pWiFi->AuthMode)-1);
    strncpy(pWiFi->EncrypType,pEnc, sizeof(pWiFi->EncrypType)-1);
    strncpy(pWiFi->Key,password, sizeof(pWiFi->Key)-1);
    pWiFi->channel = chan;

    StopWiFiPairingTask();

    pthread_attr_init(&con_attr);
    pthread_attr_setdetachstate(&con_attr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&WiFi_Connect_thread, &con_attr, ConnectWiFiTask, (void *)pWiFi);
    if(retVal != 0) {
        APP_LOG("WiFiApp",LOG_CRIT,
                "WifiCon Thread not created, errno: %d", errno);
    }

    pthread_attr_init(&mon_attr);
    pthread_attr_setdetachstate(&mon_attr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&WiFi_Monitor_thread, &mon_attr, MonitorWiFiTask, NULL);
    if(retVal != 0) {
        APP_LOG("WiFiApp",LOG_CRIT,
                "WifiMon Thread not created, errno: %d", errno);
    }

    return STATE_DISCONNECTED;
}

/* return 0 to indicate failure */
int AttemptPairing(PWIFI_PAIR_PARAMS pWiFi)
{
    int ret = 0, ret1 = 0;
    char buffer[SIZE_64B] = {0};
    PMY_SITE_SURVEY pAvlNetwork;
    int count=0;
    char auth[SIZE_20B];
    int i=0;
    int retries = 0;
    int found = 0;

    while (retries < 35) {
        pAvlNetwork = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
        if(!pAvlNetwork) {
            APP_LOG("WiFiApp", LOG_ERR,"Malloc Failed..exiting...");
            return 0;
        }

        EnableSiteSurvey(pWiFi->SSID);

        getCompleteAPList (pAvlNetwork,&count);
        for(i=0; i<count; i++) {
#ifdef MT7628_AIRPLAY_SUPPORT
            if (strncmp(pAvlNetwork[i].unicode, "Y", 1) == 0) {
                pWiFi->unicode = 1;
                convertSSID(pAvlNetwork[i].ssid);
            }
            else {
                pWiFi->unicode = 0;
            }
#endif
            if (!strcmp (pAvlNetwork[i].ssid, pWiFi->SSID)) {
                memset(auth, 0, sizeof(auth));
                sscanf(pAvlNetwork[i].security, "%[^'\\/']/", auth);
                APP_LOG("WiFiApp", LOG_DEBUG,"Network mode: mixed mode, network mode adjusted");
                wifiSetForMixedMode(pWiFi->SSID, auth, pAvlNetwork[i].WMode);
                pWiFi->channel = atoi((char *) pAvlNetwork[i].channel);
                APP_LOG("WiFiApp", LOG_DEBUG,"Channel found for ssid: %s, channel: %d", pWiFi->SSID, pWiFi->channel);
                found = 1;
                break;
            }
        }
        retries++;
        free(pAvlNetwork);
        if (found) {
            break;
        }
        /* if the sitesurvey finds less than 32 AP's around, then try for 16 times */
        if ((count < 32) && (retries >= 16)) {
            break;
        }
        pluginUsleep(1000000);
    }

    if (!found) {
        setSetupRequested(0);
        return 0;
    }

wifi_pair:
#ifdef __MIPSEL__
    ret = wifiPair(pWiFi, "apcli0");
#else
    ret = wifiPair(pWiFi, "br-lan");
#endif
    if (ret >= SUCCESS) {
        APP_LOG("WiFiApp", LOG_DEBUG, "WiFi physical connection established and dhcpc started");

        memcpy (&gWiFiParams,pWiFi,sizeof(WIFI_PAIR_PARAMS));

        /* over-write the password as encrypted password string */
        memset(gWiFiParams.Key,0,sizeof(gWiFiParams.Key));
        memcpy(gWiFiParams.Key,gUserKey,sizeof(gWiFiParams.Key));
        APP_LOG("WiFiApp", LOG_HIDE,"Updated Key: %s, gUserKey: %s", gWiFiParams.Key, gUserKey);

        if(gReconnectFlag) {
            pluginUsleep(500000);
            gReconnectFlag=0;
            APP_LOG("WiFiApp", LOG_DEBUG,"Going to test connection now");
            ret1 = wifiTestConnection (INTERFACE_CLIENT, 10, 1);
        } else {
            //- Check IP address
            APP_LOG("WiFiApp", LOG_DEBUG, "################# Waiting IP address to be allocated now #################");
            ret1 = isValidIp();
            if (!ret1) {
                SetCurrentClientState(STATE_PAIRING_FAILURE_IND);
            }
        }
        //Just trying to avoid confusion with the pairing ret, changed Name to ret1 i.e. result of testConnection.
        //return value of wifiTestConnection is either 1 or 0. testing for 3 will make this loop never enter.
        //Also extending this loop to make sure all saving also happens once this connection is tested.
        if(STATE_CONNECTED == ret1 || STATE_INTERNET_NOT_CONNECTED == ret1) {
            APP_LOG("WiFiApp", LOG_DEBUG,"State: %d", getCurrentClientState());
            SetCurrentClientState(STATE_INTERNET_NOT_CONNECTED);
#ifdef PRODUCT_WeMo_Dimmer
            /* set animation to signify that the connection is established
               LED_STATE_CONNECTION_ESTABLISHED(3) */
            setAnimation(LED_STATE_CONNECTION_ESTABLISHED);
#elif defined(PRODUCT_WeMo_LightV2)
            if(!isSetupRequested())
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_1);
            else
                SetWiFiLED(RGB_CONNECTION_ESTABLISHED_2);
#elif defined(PRODUCT_WeMo_SNSV2)
            SetWiFiLED(0x01);
#endif

#ifdef __MIPSEL__
            if(!gistimerinit) {
                createLedTimerTaskThread(LED_TIME_OFF_INTERVAL);
            }
#endif
            gDoDhcp = 0;
            system("route del -net 239.0.0.0 netmask 255.0.0.0");

#ifdef PRODUCT_WeMo_Insight
            char SetUpCompleteTS[SIZE_32B];
            memset(SetUpCompleteTS, 0, sizeof(SetUpCompleteTS));
            if(!g_SetUpCompleteTS) {
                g_SetUpCompleteTS = GetUTCTime();
                sprintf(SetUpCompleteTS, "%lu", g_SetUpCompleteTS);
                SetBelkinParameter(SETUP_COMPLETE_TS, SetUpCompleteTS);
                APP_LOG("ITC: network", LOG_ERR,"UPnP  updated on setup complete g_SetUpCompleteTS---%lu, SetUpCompleteTS--------%s:", g_SetUpCompleteTS, SetUpCompleteTS);
            }
#endif

            saveData(gWiFiParams.channel,pWiFi->SSID,pWiFi->AuthMode,pWiFi->EncrypType,gWiFiParams.Key);
            APP_LOG("WiFiApp", LOG_HIDE,"Saved Key to: %s", gWiFiParams.Key);

            if(!inetThd) {
                //Create a thread to monitor the client connection.
                gInetSleepInterval = 60;	//run Internet status check every minute
                gInternetCount = 5;        //default value 5 minutes
                APP_LOG("WiFiApp", LOG_DEBUG,"Changed gInetSleepInterval to %d and gInternetCount to %d", gInetSleepInterval,gInternetCount);
                createDetachedThread(&inetThd,checkInetConnectivity, NULL);

                if(!Inet_Monitor_thread) {
                    /* create the Inet connectivity monitor thread */
                    createDetachedThread(&Inet_Monitor_thread, InetMonitorTask, NULL);
                }

            }
        } else {
            //MARK this as Pairing failure???
            APP_LOG("NetworkControl", LOG_ERR, "########## Pairing failure-Connect ***********************");
            //We should save the configuration even if we couldn't obtain an IP
            //saveData(gWiFiParams.channel,pWiFi->SSID,pWiFi->AuthMode,pWiFi->EncrypType,gWiFiParams.Key);

            SetCurrentClientState(STATE_IPADDR_NEGOTIATION_FAILED);
            /*            StopDhcpRequest(); */
            ret = INVALID_PARAMS;
            if(inetThd)
                gExitReconnectThread=1;
        }
    } else {
        //- Put it as pairing failure
        APP_LOG("NetworkControl", LOG_ERR, "######################## Pairing failure ***********************");


        // - Jira story 2252 - Apple airport express firmware version 7.6.1 bug
        if( (!strcmp(pWiFi->AuthMode,"WPA2PSK")) ) {
            APP_LOG("NetworkControl", LOG_ERR, "Pairing failure in WPA mode, re-trying once using WEP mode...");
            strncpy(buffer,pWiFi->AuthMode, sizeof(buffer)-1);
            memset(pWiFi->AuthMode, 0, sizeof(pWiFi->AuthMode));
            strncpy(pWiFi->AuthMode, "WEP", sizeof(pWiFi->AuthMode)-1);
            ret = 0x0;
            goto wifi_pair;
        }

        if(strlen(buffer) > 0x1) {
            memset(pWiFi->AuthMode, 0, sizeof(pWiFi->AuthMode));
            strncpy(pWiFi->AuthMode, buffer, sizeof(pWiFi->AuthMode)-1);
            memset(buffer, 0, sizeof(buffer));
        }

        SetCurrentClientState(STATE_PAIRING_FAILURE_IND);
        ret = INVALID_PARAMS;
    }
    setSetupRequested(0);
    return ret1;
}

int syncConnectHomeNetwork(PWIFI_PAIR_PARAMS pWiFi)
{
    char password[PASSWORD_MAX_LEN]; //the decrypted password string

    APP_LOG("WiFiApp", LOG_CRIT,"%d-%s-%s-%s", pWiFi->channel, pWiFi->SSID, pWiFi->AuthMode, pWiFi->EncrypType);
    APP_LOG("WiFiApp", LOG_HIDE,"Password: %s", pWiFi->Key);

    int networkStatus = getCurrentClientState();

    if (networkStatus == STATE_PAIRING_FAILURE_IND || networkStatus == STATE_IPADDR_NEGOTIATION_FAILED) {
        //- New connection, reset if previous one is incorrect
        SetCurrentClientState(STATE_DISCONNECTED);
    }

    if((networkStatus > STATE_DISCONNECTED) && (networkStatus != STATE_PAIRING_FAILURE_IND)  &&
       (networkStatus != STATE_IPADDR_NEGOTIATION_FAILED)) {
        APP_LOG("WiFiApp", LOG_DEBUG,"#### already connected to %s ####", gWiFiParams.SSID);
        setSetupRequested(0);
        return FAILURE;
    }

    /* Using base64 encoding/decoding scheme for password encryption */

    memset(password, 0, sizeof(password));
    if(strcmp(pWiFi->AuthMode,"OPEN")) {
        if( (!pWiFi->Key) || (strlen(pWiFi->Key) < 0x1) || (strcmp(pWiFi->Key, "(null)") == 0x0) ) {
            APP_LOG("WiFiApp", LOG_ERR,"Requesting pairing without password, while Auth set... failed...");
            setSetupRequested(0);
            return FAILURE;
        }
        if(SUCCESS != decryptPassword(pWiFi->Key, password)) {
            APP_LOG("WiFiApp", LOG_DEBUG,"Base decoding failed...");
            setSetupRequested(0);
            return FAILURE;
        } else
            APP_LOG("WiFiApp", LOG_DEBUG,"decoded passwd: %s...", password);
    } else
        strncpy(password,"NOTHING", sizeof(password)-1);

    /* over-write key with decrypted password */
    memset(pWiFi->Key, 0, sizeof(pWiFi->Key));
    strncpy(pWiFi->Key,password, sizeof(pWiFi->Key)-1);
    APP_LOG("UPNPDevice",LOG_HIDE,"Pairing with %s",pWiFi->Key);

    if(AttemptPairing(pWiFi)) {
        /*success*/
        APP_LOG("UPNPDevice",LOG_DEBUG,"Pairing attempt successful");
        /* store back encrypted key in pWiFiParams */
        strncpy(pWiFi->Key, gUserKey, sizeof(pWiFi->Key)-1);
        return SUCCESS;
    } else {
        /*success*/
        APP_LOG("UPNPDevice",LOG_DEBUG,"Pairing attempt failed");
#ifdef MT7628_AIRPLAY_SUPPORT
        system("iwpriv ra0 set airplayEnable=1");
#endif
        /* store back encrypted key in pWiFiParams */
        strncpy(pWiFi->Key, gUserKey, sizeof(pWiFi->Key)-1);
        return FAILURE;
    }
}

int pairToRouter(PWIFI_PAIR_PARAMS pWiFi)
{
    int ret = FAILURE;

    APP_LOG("UPNPDevice",LOG_DEBUG,"connect to selected network: %s", pWiFi->SSID);
    ret = syncConnectHomeNetwork(pWiFi);

    if(ret == SUCCESS) {
        {
            /* Connected to router, turn down AP and switch UPnP */
#ifdef MT7628_AIRPLAY_SUPPORT
            system("iwpriv ra0 set airplayEnable=0");
#endif
            system("ifconfig ra0 down");
            g_ra0DownFlag = 1; //RA0 interface is Down
            pluginUsleep(1000000);
            NotifyInternetConnected();
        }
    }
    return ret;
}

int setAPIfState(char *mode)
{
    int ret, state;

    APP_LOG("WiFiApp", LOG_DEBUG,"Setting AP mode %s",mode);
    if((strcmp(mode,"ON")==0) || (strcmp(mode,"on")==0)) {
        state = 1;
#ifdef PRODUCT_WeMo_Dimmer
        /* set animation to signify that the device is in AP Mode(2) */
        setAnimation(LED_STATE_AP_MODE);
#elif defined(PRODUCT_WeMo_LightV2)
        if (errorAnimRes)
            SetWiFiLED(RGB_READY_TO_CONNECT);
#elif defined(PRODUCT_WeMo_SNSV2)
        SetWiFiLED(0x05);
#endif
    } else {
        state = 0;
    }

    ret = wifiChangeAPIfState(state);
    pluginUsleep(1000000);
    if(SUCCESS == ret) {
        APP_LOG("WiFiApp", LOG_DEBUG,"Successfully set AP mode");
        ret = SUCCESS;
    } else {
        APP_LOG("WiFiApp", LOG_CRIT,"Failed in set AP mode");
    }


    return ret;
}

int setClientIfState(char *mode)
{
    int ret, state;

    APP_LOG("WiFiApp", LOG_DEBUG,"Setting Client mode %s",mode);
    if((strcmp(mode,"ON")==0) || (strcmp(mode,"on")==0)) {
        state = 1;
    } else {
        state = 0;
    }

    ret = wifiChangeClientIfState(state);
    if(SUCCESS == ret) {
        APP_LOG("WiFiApp", LOG_DEBUG,"Successfully set Client mode");
    } else {
        APP_LOG("WiFiApp", LOG_CRIT,"Failed in set Client mode");
    }

    return ret;
}

int getWiFiStatsCounters (PWIFI_STAT_COUNTERS wifistatcounters)
{
    if((wifiGetStats (INTERFACE_CLIENT, wifistatcounters)) < 0 ) {
        return FAILURE;
    }
    return SUCCESS;
}

#define SIGNALTIMEVAL 180
#define ROUTERTIMEVAL 60
#define SIGNALTHRESHOLD 25
#define IPCHANGEDETECTTIMEVAL 60
extern UpnpDevice_Handle device_handle;

#ifdef _OPENWRT_
void *ntpUpdateThread(void *arg)
{
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("WiFiApp", LOG_DEBUG, "NTP update thread Running..");
    while(1) {
        if(RunNTP() == BELKIN_SUCCESS) {
            // Time has been set, exit thread
            break;
        }
        // Time couuld not be set, wait for a while and then retry
        sleep(DELAY_60SEC);
    }
    gNTPTimeSet = 1;
    APP_LOG("WiFiApp", LOG_ALERT, "Date set, thread exiting..");
    gNTPThreadRunning = 0;

    return NULL;
}

int createNTPUpdateThread()
{
    int retVal;

    if(!gNTPThreadRunning) {
        gNTPThreadRunning = 1;
        pthread_attr_init(&ntp_attr);
        pthread_attr_setdetachstate(&ntp_attr,PTHREAD_CREATE_DETACHED);
        retVal = pthread_create(&ntpthread,&ntp_attr,ntpUpdateThread,NULL);
        if(retVal < SUCCESS) {
            APP_LOG("WiFiApp",LOG_CRIT,"NTP Thread not created");
            gNTPThreadRunning = 0;
        }
    }

    return 0;
}

#else
// GemTek version
void* ntpUpdateThread(void *arg)
{
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("WiFiApp", LOG_DEBUG, "NTP update thread Running..");
    while(1) {
        sleep(DELAY_60SEC);
        if (IsNtpUpdate()) {
            APP_LOG("WiFiApp", LOG_ALERT, "NTP updated, thread exiting..");
            return NULL;
        } else {
            RunNTP();
        }
    }

}

int createNTPUpdateThread()
{
    int retVal;

    pthread_attr_init(&ntp_attr);
    pthread_attr_setdetachstate(&ntp_attr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&ntpthread,&ntp_attr,
                            (void*)&ntpUpdateThread, NULL);
    if(retVal < SUCCESS) {
        APP_LOG("WiFiApp",LOG_CRIT,
                "NTP Thread not created");
    }

    return 0;
}
#endif

void* checkInetConnectivity( void * arg)
{
    int retVal;//let the initial log appear
    int checkNow=0;
    int curState=0,nextState=0;
    int internetChecking=0;
    int signalwait=0;
#ifndef DEBUG_ENABLE
    int logStatus = 0;
    struct stat buf;
    int ret;
#endif
#ifdef PRODUCT_WeMo_Dimmer
    bool errorSet = false;
#endif
    tu_set_my_thread_name( __FUNCTION__ );
    APP_LOG("WiFiApp", LOG_DEBUG,"*******Internet Connection Status thread started *****");

    while (1) {
        retVal = FAILURE;
#ifndef DEBUG_ENABLE
        /* check if /tmp/Belkin_settings/enableLog is there; enable logging */
        ret = stat(PVT_LOGS_ENABLER_FILE, &buf);
        if(ret == SUCCESS && logStatus == 0) {
            logStatus=1;
            APP_LOG("WiFiApp", LOG_DEBUG,"Enabling logging.");
        } else if (ret != SUCCESS && logStatus == 1) {
            APP_LOG("WiFiApp", LOG_DEBUG,"Disabling logging.");
            logStatus=0;
        }
#endif

        if(IS_FIRMWARE_FLASHING) {
            APP_LOG("WiFiApp", LOG_DEBUG, "*******Internet Connection Status thread terminated *****");
            inetThd = 0;
            gExitReconnectThread=1;
            pthread_exit(NULL);
        }
        if(gExitReconnectThread) {
            APP_LOG("WiFiApp", LOG_DEBUG,"*******Internet Connection Status thread exiting *****");
            inetThd = 0;
            gExitReconnectThread=0;
            pthread_exit(NULL);
        }

        gInetHealthPunch++;

        if(!isSetupRequested()) {
            curState = getCurrentClientState();
            if(curState == STATE_DISCONNECTED ) {
#if defined(PRODUCT_WeMo_LightV2)
                APP_LOG("WiFiApp", LOG_DEBUG,"errorAnimRes : %d", errorAnimRes);
                if (errorAnimRes)
                    SetWiFiLED(RGB_LOST_CONNECTION);
#elif defined(PRODUCT_WeMo_SNSV2)
                SetWiFiLED(0x03);
#endif
                reconnectHome();
                /* check everything internet and router connectivity */
                checkNow =1;
            }
            if((!(signalwait % gInetSleepInterval ))||checkNow) {
                internetChecking=((!(signalwait % (gInternetCount*gInetSleepInterval) ))||checkNow);
                APP_LOG("WiFiApp", LOG_DEBUG,"Checking Inet connection status %d",internetChecking);
                if(internetChecking) {
                    /* check for internet connectivity */
                    if(checkInternet(2)==SUCCESS) {
                        APP_LOG("WiFiApp", LOG_DEBUG,"cloud connection ok");
                        gInetHealthPunch++;
                        retVal = SUCCESS;
                    }
                    /* device is good and connected to internet */
                    if(retVal == SUCCESS) {
                        gInternetCount = 5;
                        nextState = STATE_CONNECTED;
                    }
                }
                if(retVal == FAILURE) {
                    /* check connectivity with router */
                    retVal = checkRouterConnectivity(3);
#if !defined(PRODUCT_WeMo_Dimmer) && !defined(PRODUCT_WeMo_SNSV2) && !defined(PRODUCT_WeMo_LightV2)
                    if(retVal== FAILURE) {
                        retVal = isPhysicalConnected();
                        if(retVal==SUCCESS) {
                            APP_LOG("WiFiApp", LOG_DEBUG, "Physically Connected");
                        }
                    } else {
                        APP_LOG("WiFiApp", LOG_DEBUG, "Router ping OK");
                    }
#endif
                    if(retVal == SUCCESS) {
                        /* set next state based on did we check internet */
                        if(internetChecking) {
                            nextState = STATE_INTERNET_NOT_CONNECTED;
                            gInternetCount = 1;
                        } else {
                            nextState = curState;
                        }
                    } else {
                        APP_LOG("WiFiApp", LOG_DEBUG, "NOT Connected");
                        nextState = STATE_DISCONNECTED;
                    }
                }

#if defined(PRODUCT_WeMo_Dimmer)
                if((!errorSet || errorAnimRes) && nextState == STATE_INTERNET_NOT_CONNECTED) {
                    /* set animation to signify there is issue with internet/cloud connectivity */
                    setAnimation(LED_STATE_ERR_2_DETECTED);
                    errorSet = true;
                    errorAnimRes = false;
                }
#endif

                checkNow=0;
                APP_LOG("WiFiApp", LOG_DEBUG,"InetThread curState %d - nextState %d - signalwait %d - gInternetCount %d", curState,nextState, signalwait,gInternetCount );
                if(curState != nextState) {
                    SetCurrentClientState(nextState);
                    if(nextState == STATE_CONNECTED) {
#ifdef PRODUCT_WeMo_Dimmer
                        if(errorSet) {
                            setAnimation(LED_STATE_CANCEL_ERR);
                            errorSet = false;
                        }
#endif
                    }
                }
            }
#ifdef __WIRED_ETH__
            if( !(signalwait % SIGNALTIMEVAL) && !g_bWiredEthernet  )
#else
            if(!(signalwait % SIGNALTIMEVAL) && ((nextState == STATE_CONNECTED)||(nextState == STATE_INTERNET_NOT_CONNECTED)))
#endif
            {
                if(chksignalstrength()) {
                    APP_LOG("WiFiApp", LOG_DEBUG,"State 2: Poor connection");
#if defined(PRODUCT_WeMo_LightV2)
                    if (errorAnimRes)
                        SetWiFiLED(RGB_LOW_WIFI);
#elif defined(PRODUCT_WeMo_SNSV2)
                    SetWiFiLED(0x02);
#endif
                }
            }

            if(!(signalwait % IPCHANGEDETECTTIMEVAL) && ((nextState == STATE_CONNECTED)||(nextState == STATE_INTERNET_NOT_CONNECTED))) {
#ifdef PRODUCT_WeMo_LEDLight
                if(g_ra0DownFlag)
#endif
                    detectIPChange();
            }
            pluginUsleep(1000000); //loop every 1 sec
            ++signalwait;
        } else {
            APP_LOG("WiFiApp", LOG_DEBUG,"Going to the next iteration...");
            pluginUsleep (5000000);
        }
    }
}
/************************************************************************
 * Function: checkInternet
 *    This function can be used to check internet connnectivity of router
 *  Parameter
 *    count - Number of ping packets to send
 *  Return:
 *    Returns SUCCESS or FAILURE based on internet connectivity of WeMo
************************************************************************/

int checkInternet(int count)
{
    APP_LOG("WiFiApp", LOG_DEBUG,"Entry");
    int retVal = SUCCESS;
    if( PACKET_LOSE_100_PERCENT != ping_status(CLOUD_PING_DOMAIN, count) ) {
        APP_LOG("WiFiApp", LOG_DEBUG,"ping to domain %s success",CLOUD_PING_DOMAIN);
        return retVal;
    }
    gInetHealthPunch++;
    if( PACKET_LOSE_100_PERCENT != ping_status(GOOGLE_DNS_1, count) ) {
        APP_LOG("WiFiApp", LOG_DEBUG,"ping to domain %s success",GOOGLE_DNS_1);
        return retVal;
    }
    gInetHealthPunch++;
    APP_LOG("WiFiApp", LOG_DEBUG,"Exit");
    return FAILURE;
}

extern void NotifyInternetConnected();


void reconnectHome()
{
    //Either the Home AP is down
    //or some security change has happened.
    //Do a Site Survey
    PMY_SITE_SURVEY pAvlNetwork;
    int retVal=FAILURE,chan=-1,retry_cnt=0;
    int unicode = 0;
    char *pSSID=NULL,*pAuth=NULL,*pEnc=NULL,*pPass=NULL;
    int count=0,i=0;
    int scanFailCnt=0;
#ifdef PRODUCT_WeMo_Dimmer
    bool errorSet = false;
#endif
    APP_LOG("Reconnect", LOG_DEBUG,"gWiFiConfigured:%d, gWiFiClientCurrState: %d",gWiFiConfigured,
            getCurrentClientState());

    /* To be allocated once */
    pAvlNetwork = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
    if(!pAvlNetwork) {
        APP_LOG("Reconnect", LOG_ERR,"Malloc Failed..exiting Reconnect...");
        return;
    }

    while (gWiFiConfigured && !isSetupRequested()) {
        int currentState;
        gReconnectFlag=1;

        gInetHealthPunch++;

        currentState = getCurrentClientState();
        if(STATE_CONNECTED == currentState || STATE_INTERNET_NOT_CONNECTED == currentState) {
            APP_LOG("Reconnect", LOG_DEBUG,"network connected");
            free (pAvlNetwork);
            EnableSiteSurvey(gWiFiParams.SSID);
            NotifyInternetConnected();
#ifdef PRODUCT_WeMo_Dimmer
            setAnimation(LED_STATE_CONNECTION_RESTABLISHED);
#endif
            return;
        }

        // unset ip, so that dhcp gets ip
        system("ifconfig apcli0 0.0.0.0");

        EnableSiteSurvey(gWiFiParams.SSID);
        getCompleteAPList (pAvlNetwork,&count);
        if(count == 0) {
            scanFailCnt++;
            APP_LOG("Reconnect", LOG_CRIT,"No networks found in SiteScan: %d...", scanFailCnt);
        } else
            scanFailCnt=0;

        //[WEMO-31632] - Force to up apcli0 interface again when no network found
        if ((scanFailCnt > 0 ) && (scanFailCnt % (MAX_SCAN_FAIL_CNT + 1)) == 0) {
#ifdef __MIPSEL__
            APP_LOG("Reconnect", LOG_CRIT,"Force to up 'apcli0' again. scanFailCnt=%d", scanFailCnt);
            system("ifconfig apcli0 up");
#else
            APP_LOG("Reconnect", LOG_CRIT,"Force to up 'br-lan' again, scanFailCnd=%d", scanFailCnt);
            system("ifconfig br-lan up");
#endif
        }

#ifdef DRACONIAN_WIFI_SCAN
        if(scanFailCnt == MAX_SCAN_FAIL_CNT) {
            APP_LOG("Reconnect", LOG_ALERT,"No networks found in SiteScan for consecutive %d times...,restarting radio !!!", scanFailCnt);
            system("iwpriv ra0 set RadioOn=0;sleep 1;iwpriv ra0 set RadioOn=1");
            scanFailCnt=0;
        }
#endif

        for (i=0; i<count; i++) {
#ifdef MT7628_AIRPLAY_SUPPORT
                if (strncmp(pAvlNetwork[i].unicode, "Y", 1) == 0) {
                    unicode = 1;
                    convertSSID(pAvlNetwork[i].ssid);
                }
                else {
                    unicode = 0;
                }
#endif
            if (!strcmp (pAvlNetwork[i].ssid,gWiFiParams.SSID)) {

                APP_LOG("Reconnect", LOG_DEBUG,
                        "pAvlNetwork[%d].channel: %s, gWiFiParams.channel: %d, \
					     pAvlNetwork[%d].ssid: %s, gWiFiParams.SSID: %s",
                        i, pAvlNetwork[i].channel, gWiFiParams.channel,
                        i, pAvlNetwork[i].ssid,gWiFiParams.SSID);

                pSSID=gWiFiParams.SSID;
                sscanf(pAvlNetwork[i].security, "%[^'\\/']/%s",
                       gWiFiParams.AuthMode, gWiFiParams.EncrypType);
                pAuth=gWiFiParams.AuthMode;
                pEnc=gWiFiParams.EncrypType;
                chan = atoi((const char *)pAvlNetwork[i].channel);
                pPass=gWiFiParams.Key;
                //Check for Network parameters before trying-TODO
                //Case of
                APP_LOG("Reconnect", LOG_DEBUG,
                        "Tryin to Reconnect to <%s> .\n",pAvlNetwork[i].ssid);

                APP_LOG("Reconnect", LOG_CRIT,"%d-%s-%s-%s", chan, pSSID, pAuth, pEnc);
                wifiSetForMixedMode(pSSID, pAuth, pAvlNetwork[i].WMode);
                retVal = connectHomeNetwork(chan, pSSID, unicode, pAuth, pEnc, pPass) ;

                /* In either case break from the loop */
                break;
            }
        }

        if(i>=count) {
#ifdef PRODUCT_WeMo_Dimmer
            /* Animation is set after a few iterations to avoid any momentary error */
            if((!errorSet || errorAnimRes) && retry_cnt>2) {
                /* set animation to signify there is issue with the router connectivity(local n/w) */
                setAnimation(LED_STATE_ERR_1_DETECTED);
                errorSet = true;
                errorAnimRes = false;
            }
#endif
#if defined(PRODUCT_WeMo_LightV2)
            APP_LOG("WiFiApp", LOG_DEBUG,"errorAnimRes : %d", errorAnimRes);
            if (errorAnimRes)
                SetWiFiLED(RGB_LOST_CONNECTION);
#endif
            APP_LOG("Reconnect", LOG_ERR,"Retry no. <%d> : Network SSID <%s> doesn't exist any more..Pls. switch on your Home Wireless Router...\n", retry_cnt, gWiFiParams.SSID);
        } else
        {
            if(retVal == STATE_CONNECTED) {
                /* connected to network successfully, exit the thread */
                APP_LOG("Reconnect", LOG_CRIT,"Re-Connected to Home Network SSID <%s> !!!", gWiFiParams.SSID);
                NotifyInternetConnected();

                /*
                We need to close AP only if this is invoked during bootup or
                App called closeAP some time
                     */
                if(gBootReconnect || gAppCalledCloseAp || gRa0DownFlag) {
                    APP_LOG("WiFiApp",LOG_DEBUG, "gBootReconnect=%d,gAppCalledCloseAp=%d,gRa0DownFlag=%d\n",
                            gBootReconnect,gAppCalledCloseAp,gRa0DownFlag);
                    /* switch off AP mode */
                    setAPIfState("OFF");
                    gRa0DownFlag=0;
                }

                break;
            }
        }
        retry_cnt++;
        pluginUsleep(5000000);
    }

    /* common exit point, free memory and exit */
    free (pAvlNetwork);

    if(isSetupRequested()) {
        APP_LOG("Reconnect", LOG_ERR,"Not trying to connect to Home Network SSID <%s> !!! \n", gWiFiParams.SSID);
        gReconnectFlag=0;
    }

    return;
}

char *wifiGetIP (char *pInterface)
{
    if(!strcmp (pInterface, INTERFACE_CLIENT)) {
        return GetWanIPAddress();
    } else if (!strcmp (pInterface, INTERFACE_AP)) {
        return GetLanIPAddress ();
    }
    //Invalid Interface
    return NULL;
}

int resetNetworkParams()
{
    setClientIfState("OFF");
    SetBelkinParameter (WIFI_CLIENT_SSID,"");
    //Doing SSID should be sufficient.
    gWiFiConfigured=0;
    SaveSetting();
    return 0;
}

int chksignalstrength (void)
{
    static int ledppoorconn;

    gSignalStrength = wifiGetRSSI(INTERFACE_CLIENT);
    if(gSignalStrength < SIGNALTHRESHOLD) {
        APP_LOG("WiFiApp", LOG_ALERT,"Poor Connection strength: %d", gSignalStrength);
        ledppoorconn = 0x01;
    } else if((gSignalStrength >= SIGNALTHRESHOLD) && ledppoorconn) {
        APP_LOG("WiFiApp", LOG_CRIT,"State 4: Connection strength regained: %d\n", gSignalStrength);
        ledppoorconn = 0x0;
#if defined(PRODUCT_WeMo_LightV2)
        ledStatusOff();
        //SetWiFiLED(RGB_SWITCH_OFF);
#elif defined(PRODUCT_WeMo_SNSV2)
        SetWiFiLED(0x04);
#endif
    }
    APP_LOG("WiFiApp", LOG_DEBUG,"Connection signal check: strength is:%d,ledppoorconn:%d",gSignalStrength,ledppoorconn);
    return ledppoorconn;
}

void getRouterEssidMac (const char *p_essid, const char *p_macaddr, const char *pInterface)
{
    wifiGetStatus ((char *)p_essid, (char *)p_macaddr, (char *)pInterface);
}

int  findEncryptionForSsid(char *ssid,char* EncryptType,char *AuthMode)
{
    PMY_SITE_SURVEY pAvlNetwork=NULL;
    int found=0;
    int i=0;
    int count=0;
    int retries=3;
    char auth[WIFI_MAXSTR_LEN];
    char encrypt[WIFI_MAXSTR_LEN];
    int ret=FAILURE;

    pAvlNetwork = (PMY_SITE_SURVEY) ZALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
    if(!pAvlNetwork) {
        APP_LOG("WiFiApp", LOG_ERR,"Malloc Failed....");
        return FAILURE;
    }

    EnableSiteSurvey(ssid);
    while(retries > 0) {
        memset(pAvlNetwork, 0, (sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE));
        getCurrentAPList (pAvlNetwork,&count);
        APP_LOG("WiFiApp", LOG_DEBUG,"Avl network list cnt: %d", count);
        for (i=0; i<count; i++) {

            if (!strcmp (pAvlNetwork[i].ssid,ssid)) {
                memset(auth, 0, sizeof(auth));
                memset(encrypt, 0, sizeof(encrypt));

                sscanf(pAvlNetwork[i].security, "%[^'\\/']/%s",auth,encrypt);


                APP_LOG("WiFiApp", LOG_DEBUG,
                        "Encryption Type  is %s  auth Mode is %s ",encrypt,auth);
                found = 1;
                break;
            }
        }
        if(found) {
            APP_LOG("WiFiApp", LOG_DEBUG,
                    "Encryption Type determined from SITE SURVEY: <%s> on retry#%d",encrypt,4-retries);
            break;
        } else {
            retries --;
            pluginUsleep(500000);
        }
    }
    if(pAvlNetwork)
        free(pAvlNetwork);

    if(!found) {
        APP_LOG("WiFiApp", LOG_ERR,
                "SSID: %s not found in the available network list",ssid);
        return FAILURE;
    } else {
        /*check if AuthMode changes from secure to OPEN, if so do not update security info*/
        if((0 != strcmp(AuthMode,"OPEN")) && (0 == strcmp(auth,"OPEN"))) {
            APP_LOG("WiFiApp", LOG_DEBUG,"Not updating router security info!Router auth mode changed from %s to %s", AuthMode,auth);
            return FAILURE;
        }

        if(memcmp(EncryptType,encrypt,strlen(encrypt))) {
            memset(EncryptType,0,WIFI_MAXSTR_LEN);
            memcpy(EncryptType,encrypt,strlen(encrypt));
            APP_LOG("WiFiApp", LOG_DEBUG,"Encryption Type copied ass %s ",EncryptType);
            ret=SUCCESS;
        }
        if(memcmp(AuthMode,auth,strlen(auth))) {
            memset(AuthMode,0,WIFI_MAXSTR_LEN);
            memcpy(AuthMode,auth,strlen(auth));
            APP_LOG("WiFiApp", LOG_DEBUG,"authMode  copied ass %s ",AuthMode);
            ret=SUCCESS;
        }
        return ret;
    }


}
void SetAppSSIDCommand(void)
{
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********setting wifi App ssid commands:");
    int ret = -1;
    char command[SIZE_64B];
    memset(command, '\0', SIZE_64B);
    strncpy(command, "RadioOn=1", sizeof(command)-1);
#ifdef __MIPSEL__
    ret = wifiSetCommand (command,"apcli0");
#else
    ret = wifiSetCommand (command,"br-lan");
#endif
    if(ret < 0) {
        APP_LOG("SetAppSSIDCommand", LOG_ERR, "%s - failed", command);
    }
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********Command Set: %s",command);

    memset(command, '\0', SIZE_64B);
#ifdef __MIPSEL__
    strncpy(command, "ifconfig apcli0 up", sizeof(command)-1);
#else
    strncpy(command, "ifconfig br-lan up", sizeof(command)-1);
#endif
    system(command);
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********Command Set: %s",command);

    ret = -1;
    memset(command, '\0', SIZE_64B);
    strncpy(command, "SiteSurvey=1", sizeof(command)-1);
#ifdef __MIPSEL__
    ret = wifiSetCommand (command,"apcli0");
#else
    ret = wifiSetCommand (command,"br-lan");
#endif
    if(ret < 0) {
        APP_LOG("SetAppSSIDCommand", LOG_ERR, "%s - failed", command);
    }
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********Command Set: %s",command);

    ret = -1;
    memset(command, '\0', SIZE_64B);
    strncpy(command, "RadioOn=1", sizeof(command)-1);
    ret = wifiSetCommand (command,"ra0");
    if(ret < 0) {
        APP_LOG("SetAppSSIDCommand", LOG_ERR, "%s - failed", command);
    }
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********Command Set: %s",command);
    memset(command, '\0', SIZE_64B);
    strncpy(command, "ifconfig ra0 up", sizeof(command)-1);
    system(command);
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********Command Set: %s",command);
#ifdef MT7628_AIRPLAY_SUPPORT
    memset(command, '\0', SIZE_64B);
    strncpy(command, "iwpriv ra0 set airplayEnable=1", sizeof(command)-1);
    system(command);
#endif

    ret = -1;
    memset(command, '\0', SIZE_64B);
    strncpy(command, "SiteSurvey=1", sizeof(command)-1);
    ret = wifiSetCommand (command,"ra0");
    if(ret < 0) {
        APP_LOG("SetAppSSIDCommand", LOG_ERR, "%s - failed", command);
    }
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********Command Set: %s",command);

    ret = -1;
    memset(command, '\0', SIZE_64B);
    strncpy(command, "HideSSID=0", sizeof(command)-1);
    ret = wifiSetCommand (command,"ra0");
    if(ret < 0) {
        APP_LOG("SetAppSSIDCommand", LOG_ERR, "%s - failed", command);
    }
    APP_LOG("SetAppSSIDCommand", LOG_DEBUG, "**********Command Set: %s",command);
}

void StartDhcpOnWiredIf()
{
    char CmdLine[80];
    char *cp = CmdLine;

    system("killall -KILL udhcpc");
    snprintf(CmdLine,sizeof(CmdLine),"udhcpc -i %s -x hostname:%s -b &",
             GetLanDeviceName(),g_szApSSID);
// '.' characters are not legal in hostnames, change them to '-'
    while((cp = strchr(cp,'.')) != NULL) {
        *cp++ = '-';
    }
// Start dhcpc on the wired interface
    APP_LOG("WiFiApp",LOG_CRIT,"running: %s",CmdLine);
    System(CmdLine);
}

// sh: Create threads to monitor the Internet connection in wired Ethernet mode
void StartInetMonitorThread()
{
    int retVal;

    StartDhcpOnWiredIf();

#ifdef __MIPSEL__
// Turn off WiFi radio
    if((retVal = wifiSetCommand("RadioOn=0","ra0")) < 0) {
        APP_LOG("SetAppSSIDCommand", LOG_ERR, "wifiSetCommand - failed");
    }

    if((retVal = wifiSetCommand("RadioOn=0","apcli0")) < 0) {
        APP_LOG("SetAppSSIDCommand", LOG_ERR, "wifiSetCommand - failed");
    }
#endif

    if(!inetThd) {
        retVal = createDetachedThread(&inetThd,checkInetConnectivity,NULL);
    }

    if(!Inet_Monitor_thread) {
        // create the Inet connectivity monitor thread
        createDetachedThread(&Inet_Monitor_thread,InetMonitorTask,NULL);
    }
}
