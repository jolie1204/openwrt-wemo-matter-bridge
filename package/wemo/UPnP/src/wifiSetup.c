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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/types.h>                /* for "caddr_t" et al          */
#include <linux/socket.h>               /* for "struct sockaddr" et al  */
#include <linux/if.h>                   /* for IFNAMSIZ and co... */

#ifdef __MIPSEL__
#include <linux/wireless.h>
#endif
#include <math.h>
#include "global.h"
#include "osUtils.h"
#include "utils.h"
#include "logger.h"
#include "wifiHndlr.h"
#include "wifiSetup.h"
#include "watchDog.h"

#include "belkin_api.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

#define RT_PRIV_IOCTL	(SIOCIWFIRSTPRIV + 0x01)

char *pCommandList[] = {
    "ApCliAuthMode",
    "ApCliEncrypType",
    "ApCliKey1",
    "ApCliSsid",
    "ApCliWPAPSK",
};

char 	g_szSerialNo[SIZE_64B];
char g_szProdVarType[SIZE_16B];
int gRa0DownFlag=0;
int gDhcpcStarted=0;
extern int gReconnectFlag;
WIFI_PAIR_PARAMS gWiFiParams;
WIFI_PAIR_PARAMS gWifiSettings;
//globalLock
pthread_mutex_t gWifiSettingsLock;
pthread_mutex_t   s_client_state_mutex;
int gWiFiClientCurrState=0;
SERVERENV g_ServerEnvType = E_SERVERENV_PROD;
int gSignalStrength = 0;
extern int gWatchDogStatus;
#define _CHECK_IP_FOR_CONNECTION_ 1
#define MAX_UDHCPC_TIMEOUT 35

extern int g_bWiredEthernet;

/************************************************************************
 * Function: WifiInit
 *     Initialize settings configuration source.
 *  Parameters:
 *    None.
 *  Return:
 *     SUCCESS/FAILURE
 ************************************************************************/
int WifiInit()
{
    int retVal = SUCCESS;
    char command[SIZE_64B];

    retVal = osUtilsCreateLock(&gWifiSettingsLock);

    memset(command, '\0', SIZE_64B);
    strncpy(command, "SiteSurvey=1", sizeof(command)-1);
#ifdef __MIPSEL__
    retVal = wifiSetCommand (command,"apcli0");
#else
    retVal = wifiSetCommand (command,"br-lan");
#endif

    return retVal;
}

/************************************************************************
 * Function: wifiSetCommand
 *     send command to device.
 *  Parameters:
 *    pCommand - command to be sent to device.
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiSetCommand (char *pCommand, char *pInterface)
{
#ifdef __MIPSEL__
    struct iwreq wrq;
    char data[IW_SCAN_MAX_DATA];
    int ret = 0, sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "Socket Error %s", strerror(errno));
        return FAILURE;
    }

    if(!pCommand || !pInterface) {
        APP_LOG("NetworkControl", LOG_ERR, "Parameter Error");
        close (sock);
        return FAILURE;
    }

    memset(data, 0x00, IW_SCAN_MAX_DATA);

    strncpy(wrq.ifr_ifrn.ifrn_name, pInterface, IFNAMSIZ);
    strncpy(data, pCommand, sizeof(data)-1);
    wrq.u.data.length = strlen(data)+1;
    APP_LOG("NetworkControl", LOG_HIDE, "[Command] %s[len = %d]",data,wrq.u.data.length);
    wrq.u.data.pointer = data;
    wrq.u.data.flags = 0;

    ret = ioctl(sock, RTPRIV_IOCTL_SET, &wrq);

    if(ret < 0) {
        close(sock);
        APP_LOG("NetworkControl", LOG_ERR, "IOCTL Error %s", strerror(errno));
        return FAILURE;
    }
    APP_LOG("NetworkControl", LOG_HIDE, "RTPRIV_IOCTL_SET %s passed\n",data);

    close(sock);
#endif
    return 0;
}

/************************************************************************
 * Function: wifiGetStatus
 *     send command to get paired essid.
 *  Parameters:
 *    essid - name of connected device.
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiGetStatus (char *pEssid, char *pApmac, char *pInterface)
{
#ifdef __MIPSEL__
    if (g_bWiredEthernet) {
        char *routerSSID = NULL;
        char *routerMac = NULL;

        /* For X86, let's give fake essid & router mac address
           if they are not defined. */
        routerSSID = GetBelkinParameter(WIFI_ROUTER_SSID);
        if (routerSSID && routerSSID[0]) {
            strcpy(pEssid, routerSSID);
        } else {
            strcpy(pEssid, "EthernetConnected");
            SetBelkinParameter(WIFI_ROUTER_SSID, pEssid);
        }

        routerMac = GetBelkinParameter(WIFI_ROUTER_MAC);
        if (routerMac && routerMac[0]) {
            strcpy(pApmac, routerMac);
        } else {
            strcpy(pApmac, "8686ABCDEF02");
            SetBelkinParameter(WIFI_ROUTER_MAC, pApmac);
        }
        return SUCCESS;
    }

    struct iwreq wrq;
    char data[IW_ESSID_MAX_SIZE];
    int ret = 0, sock = socket(AF_INET, SOCK_DGRAM, 0);
    char macp[MAX_MAC_LEN];

    if (sock < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "Socket Error %s\n", strerror(errno));
        return FAILURE;
    }

    if(!pEssid) {
        APP_LOG("NetworkControl", LOG_ERR, "Param Error %s\n", strerror(errno));
        close (sock);
        return FAILURE;
    }
    if(!pApmac) {
        APP_LOG("NetworkControl", LOG_ERR, "Param Error %s\n", strerror(errno));
        close (sock);
        return FAILURE;
    }
    memset(data, 0x00, IW_ESSID_MAX_SIZE);

    strncpy(wrq.ifr_ifrn.ifrn_name, pInterface, IFNAMSIZ);
    wrq.u.data.length = IW_ESSID_MAX_SIZE;
    wrq.u.data.pointer = data;
    wrq.u.data.flags = 0;

    ret = ioctl(sock, SIOCGIWESSID, &wrq);
    if(ret < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "IOCTL Error %s", strerror(errno));
        close (sock);
        return FAILURE;
    }
    if (wrq.u.data.length < 1) {
        close (sock);
        return FAILURE;
    }
    APP_LOG("NetworkControl", LOG_DEBUG, "SIOCGIWESSID = %s length %d\n",data, wrq.u.essid.length);

    memcpy(pEssid, wrq.u.essid.pointer, wrq.u.essid.length);
    pEssid[wrq.u.essid.length] = '\0';

    // AP Mac address
    if ((ioctl(sock, SIOCGIWAP, &wrq))>=0) {
        memset(macp, '\0', MAX_MAC_LEN);
        snprintf(macp, sizeof(macp), "%02x%02x%02x%02x%02x%02x", (unsigned char)wrq.u.ap_addr.sa_data[0], (unsigned char)wrq.u.ap_addr.sa_data[1], (unsigned char)wrq.u.ap_addr.sa_data[2], (unsigned char)wrq.u.ap_addr.sa_data[3], (unsigned char)wrq.u.ap_addr.sa_data[4], (unsigned char)wrq.u.ap_addr.sa_data[5]);
        memcpy(pApmac, macp, strlen(macp));
    } else {
        APP_LOG("NetworkControl", LOG_ERR, "IOCTL Error %s", strerror(errno));
        close (sock);
        return FAILURE;
    }

    close (sock);
#else
    char *routerSSID = NULL;
    char *routerMac = NULL;

    /* For X86, let's give fake essid & router mac address
     if they are not defined. */
    routerSSID = GetBelkinParameter(WIFI_ROUTER_SSID);
    if (routerSSID && routerSSID[0]) {
        strcpy(pEssid, routerSSID);
    } else {
        strcpy(pEssid, "X86ESSID");
        SetBelkinParameter(WIFI_ROUTER_SSID, pEssid);
    }

    routerMac = GetBelkinParameter(WIFI_ROUTER_MAC);
    if (routerMac && routerMac[0]) {
        strcpy(pApmac, routerMac);
    } else {
        strcpy(pApmac, "8686ABCDEF02");
        SetBelkinParameter(WIFI_ROUTER_MAC, pApmac);
    }
#endif
    return SUCCESS;
}

void update_resolv_config()
{
    struct stat sb;
    struct stat sb1;

    APP_LOG("WeMoApp",LOG_DEBUG, "update_resolv_config()...");

    if( (0 == stat("/tmp/resolv.conf", &sb)) && (0 == stat("/tmp/resolv.conf.auto", &sb1)) ) {
        APP_LOG("WeMoApp",LOG_DEBUG, "resolve.conf and resolv.conf.auto is existed...");
        system("cp /tmp/resolv.conf.auto /tmp/resolv.conf");
    }
}

void resetWifiDriver()
{
    system("ifconfig ra0 down");
    system("ifconfig apcli0 down");
    system("rmmod mt7628");
    system("modprobe mt7628");
}

void StopDhcpRequest()
{
    char cmdBuf[SIZE_256B];
    gDhcpcStarted = 0x00;
    if(gRa0DownFlag) {

#ifdef MT7628_AIRPLAY_SUPPORT
            system("iwpriv ra0 set airplayEnable=0");
#endif
        system("ifconfig ra0 down");//Story: 1727 "ra0 UP called in RUNdhcp to assign Valid IP"
        APP_LOG("NetworkControl", LOG_DEBUG, "RA0 DOWN");
        gRa0DownFlag =0;
    }

// Keep udhcpc demon to send DHCPREQUEST when half time of DHCP lease is coming
    memset (cmdBuf,0,SIZE_256B);
    /* do not actually kill udhcpc, but send "USR2" signal to release the ip */
    strncpy(cmdBuf, "killall -SIGUSR2 udhcpc", sizeof(cmdBuf)-1);
    system (cmdBuf);
#if defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_SNSV2)
    /*    resetWifiDriver(); */
#endif
    update_resolv_config();
}

extern void NotifyInternetConnected();
/*
 * RunDhcpRequest:
 *  1. Stop dhcp client,if any
 *  2. Start new dhcp client
 */
int RunDhcpRequest(int restart_upnp_server)
{
    char cmdBuf[SIZE_256B];
    char pid_path[SIZE_64B];

    gDhcpcStarted = 1;
    /* setting wan_netmask & wan_ipaddr is required to get a new IP */
    SetBelkinParameter(WAN_NETMASK, "0.0.0.0");
    SetBelkinParameter(WAN_IPADDR, "0.0.0.0");

    /* signal USR1 will release command to udhcpc */
    /* signal USR2 will renew command to udhcpc */
    memset(pid_path, 0, SIZE_64B);
    sprintf(pid_path, "/var/run/udhcpc-apcli0.pid");
    if (access (pid_path, F_OK) == 0) {
        /* release lease */
        memset(cmdBuf, 0, SIZE_256B);
        sprintf(cmdBuf, "kill -SIGUSR2 `cat %s`", pid_path);
        system((const char *)cmdBuf);
        sleep(1);
        /* renew lease */
        memset(cmdBuf, 0, SIZE_256B);
        sprintf(cmdBuf, "kill -SIGUSR1 `cat %s`", pid_path);
        system((const char *)cmdBuf);
    }
    else {
        memset(cmdBuf, 0, SIZE_256B);
        /* udhcpc -i apcli0 -b -p /var/run/udhcpc-apcli0.pid -R */
        sprintf(cmdBuf, "%s", UDHCPC_CMD);
        system ((const char *)cmdBuf);
    }

    if( restart_upnp_server ) {
        NotifyInternetConnected();
    }
    return 0;
}

int ReRunDhcpClient()
{
    char cmdBuf[SIZE_256B];

    gDhcpcStarted = 1;

    memset (cmdBuf,0,SIZE_256B);
    strncpy(cmdBuf, "killall -9 udhcpc", sizeof(cmdBuf)-1);
    system (cmdBuf);

    memset (cmdBuf, 0, SIZE_256B);
    strncpy (cmdBuf, UDHCPC_CMD, sizeof(cmdBuf)-1);
    APP_LOG("NetworkControl", LOG_DEBUG, "Restarted dhcp client");

    System (cmdBuf);
    return 0;
}

void disconnectFromRouter()
{
    char pCommand[SIZE_64B];

    memset(pCommand, '\0', SIZE_64B);
    strncpy(pCommand, "ApCliSsid= \"\"", sizeof(pCommand)-1);
#ifdef __MIPSEL__
    wifiSetCommand(pCommand,"apcli0");

    system("ifconfig apcli0 0.0.0.0");
#else
    wifiSetCommand(pCommand,"br-lan");

    system("ifconfig br-lan 0.0.0.0");
#endif
    APP_LOG("NetworkControl", LOG_DEBUG,"Disconnected from router");
}

/************************************************************************
 * Function: wifiPair
 *     get settings information and pair with AP.
 *  Parameters:
 *    Pairing parameters, interface.
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
#define PCMD_LEN SIZE_128B
int wifiPair (PWIFI_PAIR_PARAMS pWifiParams,char *pInterface)
{
    char buffer[SIZE_256B];
    int ret = 0;
    char pCommand[PCMD_LEN],chBuf[SIZE_16B];
    char ssid[WIFI_MAXSTR_LEN+1];
    int flag_retried=FAILURE;
    if(!pWifiParams) {
        APP_LOG("NetworkControl", LOG_ERR,"Error Params");
        return FAILURE;
    }
    APP_LOG("NetworkControl", LOG_DEBUG, "WiFi Pairing Commands start...");

pair_again:
    {
        FILE *pipe;

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
        if (fgets(buffer, SIZE_256B, pipe) == NULL) {
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

    memset(pCommand, '\0', PCMD_LEN);
    strncpy(pCommand, "ApCliEnable=0", sizeof(pCommand)-1);

    ret = wifiSetCommand (pCommand,pInterface);

    if(ret < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
        return FAILURE;
    }

    memset(pCommand, '\0', PCMD_LEN);
    strncpy(pCommand, "Channel=", sizeof(pCommand)-1);
    snprintf(chBuf, sizeof(chBuf), "%d",pWifiParams->channel);
    strncat(pCommand,chBuf, sizeof(pCommand)-strlen(pCommand)-1);
    ret = wifiSetCommand (pCommand,INTERFACE_AP);
    if(ret < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
        return FAILURE;
    }

    /************************************************************************
     *
     *  Story: 2571
     *  Driver returns the SSID with having non printable characters in hex string
     *  format. Chinese SSID is also returned in Hex format.
     *  This "convertSSID" will convert the hex string in raw bytes format,
     *  which is used in pairing with the router
     *
     ************************************************************************/

    strncpy(buffer,pWifiParams->AuthMode, sizeof(buffer)-1);
    strncpy(ssid, pWifiParams->SSID, sizeof(ssid)-1);
    strncpy(ssid, convertSSID(ssid), sizeof(ssid)-1);

    memset(pCommand, '\0', PCMD_LEN);
    strncpy(pCommand, "ApCliAuthMode=", sizeof(pCommand)-1);
    if(!strcmp (buffer,"WPA1PSKWPA2PSK") || (!strcmp(buffer,"WPAPSKWPA2PSK"))) {
        strncat(pCommand, "WPAPSKWPA2PSK", sizeof(pCommand)-strlen(pCommand)-1);
    } else if (!strcmp(buffer,"WEP")) {
        strncat(pCommand, "WEPAUTO", sizeof(pCommand)-strlen(pCommand)-1);
        strncpy(pWifiParams->EncrypType,"WEP", sizeof(pWifiParams->EncrypType)-1);
    } else {
        strncat(pCommand, buffer, sizeof(pCommand)-strlen(pCommand)-1);
    }
    ret = wifiSetCommand(pCommand,pInterface);
    if(ret < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
        return FAILURE;
    }
    memset(pCommand, '\0', PCMD_LEN);
    strncpy(pCommand, "ApCliEncrypType=", sizeof(pCommand)-1);
    strncat(pCommand, pWifiParams->EncrypType, sizeof(pCommand)-strlen(pCommand)-1);

    ret = wifiSetCommand(pCommand,pInterface);
    if(ret < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
        return FAILURE;
    }

    if ((strcmp(pWifiParams->EncrypType, "WEP")) == 0) {
        // set default key id
        memset(pCommand, '\0', PCMD_LEN);
        strncpy(pCommand, "ApCliDefaultKeyID=1", sizeof(pCommand)-1);
        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
        memset(pCommand, '\0', PCMD_LEN);
        strncpy(pCommand, "ApCliKey1=", sizeof(pCommand)-1);
        strncat(pCommand, pWifiParams->Key, sizeof(pCommand)-strlen(pCommand)-1);
        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
        memset(pCommand, '\0', PCMD_LEN);
        if (pWifiParams->unicode) {
            strncpy(pCommand, "ApCliUniCodeSsid=", sizeof(pCommand)-1);
        }
        else {
            strncpy(pCommand, "ApCliSsid=", sizeof(pCommand)-1);
        }
        strncat(pCommand, ssid, sizeof(pCommand)-strlen(pCommand)-1);
        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
    } else if ((strcmp(pWifiParams->EncrypType, "TKIP")) == 0)	{
        memset(pCommand, '\0', PCMD_LEN);
        strncpy(pCommand, "ApCliWPAPSK=", sizeof(pCommand)-1);
        strncat(pCommand, pWifiParams->Key, sizeof(pCommand)-strlen(pCommand)-1);

        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
        memset(pCommand, '\0', PCMD_LEN);
        if (pWifiParams->unicode) {
            strncpy(pCommand, "ApCliUniCodeSsid=", sizeof(pCommand)-1);
        }
        else {
            strncpy(pCommand, "ApCliSsid=", sizeof(pCommand)-1);
        }
        strncat(pCommand, ssid, sizeof(pCommand)-strlen(pCommand)-1);

        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
    } else if ((strcmp(pWifiParams->EncrypType, "AES")) == 0) {
        memset(pCommand, '\0', PCMD_LEN);
        strncpy(pCommand, "ApCliWPAPSK=", sizeof(pCommand)-1);
        strncat(pCommand, pWifiParams->Key, sizeof(pCommand)-strlen(pCommand)-1);
        ret = wifiSetCommand(pCommand, pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
        memset(pCommand, '\0', PCMD_LEN);
        if (pWifiParams->unicode) {
            strncpy(pCommand, "ApCliUniCodeSsid=", sizeof(pCommand)-1);
        }
        else {
            strncpy(pCommand, "ApCliSsid=", sizeof(pCommand)-1);
        }
        strncat(pCommand, ssid, sizeof(pCommand)-strlen(pCommand)-1);
        ret = wifiSetCommand(pCommand, pInterface);

        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
            return FAILURE;
        }
    } else if ((strcmp(pWifiParams->EncrypType, "TKIPAES")) == 0)	{
        memset(pCommand, '\0', PCMD_LEN);
        strncpy(pCommand, "ApCliWPAPSK=", sizeof(pCommand)-1);
        strncat(pCommand, pWifiParams->Key, sizeof(pCommand)-strlen(pCommand)-1);
        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
            return FAILURE;
        }
        memset(pCommand, '\0', PCMD_LEN);
        strncpy(pCommand, "ApCliSsid=", sizeof(pCommand)-1);
        strncat(pCommand, ssid, sizeof(pCommand)-strlen(pCommand)-1);
        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
    } else if ((strcmp(pWifiParams->EncrypType, "NONE")) == 0)	{
        memset(pCommand, '\0', PCMD_LEN);
        if (pWifiParams->unicode) {
            strncpy(pCommand, "ApCliUniCodeSsid=", sizeof(pCommand)-1);
        }
        else {
            strncpy(pCommand, "ApCliSsid=", sizeof(pCommand)-1);
        }
        strncat(pCommand, ssid, sizeof(pCommand)-strlen(pCommand)-1);
        ret = wifiSetCommand(pCommand,pInterface);
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR,"%s - failed", pCommand);
            return FAILURE;
        }
    }

    memset(pCommand, '\0', PCMD_LEN);
    strncpy(pCommand, "ApCliEnable=1", sizeof(pCommand)-1);

    ret = wifiSetCommand (pCommand,pInterface);

    if(ret < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
        return FAILURE;
    }

    ret = isAPConnected(ssid, pWifiParams->EncrypType);
    APP_LOG("NetworkControl", LOG_DEBUG, "Pairing IOCTL's:%d", ret);
    if(flag_retried==SUCCESS && ret==SUCCESS) {
        APP_LOG("NetworkControl", LOG_DEBUG, "flag_retried and ret is SUCCESS...");
        char *pTemp = GetBelkinParameter (WIFI_CLIENT_ENCRYP);
        if(pTemp!= NULL && (strlen(pTemp) > 0)) {
            APP_LOG("NetworkControl", LOG_DEBUG, "encryption Type is: %s", pTemp);
            if(memcmp(pWifiParams->EncrypType,pTemp,strlen(pWifiParams->EncrypType))) {
                SetBelkinParameter (WIFI_CLIENT_ENCRYP,pWifiParams->EncrypType);
            }
        }
        pTemp = GetBelkinParameter (WIFI_CLIENT_AUTH);
        if(pTemp!= NULL && (strlen(pTemp) > 0)) {
            APP_LOG("NetworkControl", LOG_DEBUG, "AuthCode is: %s", pTemp);
            if(memcmp(pWifiParams->AuthMode,pTemp,strlen(pWifiParams->AuthMode))) {
                SetBelkinParameter (WIFI_CLIENT_AUTH,pWifiParams->AuthMode);
            }
        }
    }

    if(ret < SUCCESS) {
        FILE *pipe;

        APP_LOG("WiFiApp", LOG_DEBUG, "trying to connect Router Again ");
#ifdef __MIPSEL__
        strncpy(pCommand,"ifconfig | grep apcli0", sizeof(pCommand)-1);
#else
        strncpy(pCommand,"ifconfig | grep br-lan", sizeof(pCommand)-1);
#endif
        pipe = popen(pCommand,"r");
        if (pipe == NULL) {
            APP_LOG("NetworkControl", LOG_ERR, "Popen Error %s", strerror(errno));
            resetSystem();
        }
        if (fgets(buffer, SIZE_256B, pipe) == NULL) {
#ifdef __MIPSEL__
            APP_LOG("NetworkControl", LOG_DEBUG, "NOT FOUND apcli0: again");
            system("ifconfig apcli0 up");
#else
            APP_LOG("NetworkControl", LOG_DEBUG, "NOT FOUND br-lan: again");
            system("ifconfig br-lan up");
#endif
        }
        pclose(pipe);

        flag_retried = findEncryptionForSsid(pWifiParams->SSID, pWifiParams->EncrypType, pWifiParams->AuthMode);

        if( flag_retried == SUCCESS ) {
            goto pair_again;
        }
    }

    return ret;
}

int get_conn_state()
{
    struct iwreq wrq;
    char data[IW_ESSID_MAX_SIZE];
    int sock = 0;
    char output[4];

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        return FAILURE;
    }

    memset (data, 0, sizeof(data));
    sprintf(wrq.ifr_ifrn.ifrn_name, "%s", "apcli0");
    sprintf(data, "%s", "conn_state");
    wrq.u.data.length = strlen(data) + 1;
    wrq.u.data.pointer = data;
    wrq.u.data.flags = 0;

    if (ioctl(sock, RTPRIV_IOCTL_SHOW, &wrq) < 0) {
        APP_LOG("NetworkControl", LOG_DEBUG, "ioctl - RTPRIV_IOCTL_SHOW : conn_state failed");
        close(sock);
        return FAILURE;
    }

    memset(output, 0, 4);
    memcpy(output, wrq.u.data.pointer, wrq.u.data.length);
    if (atoi(output) == 1) {
        close(sock);
        APP_LOG("NetworkControl", LOG_DEBUG, "apcli0 paired");
        return SUCCESS;
    }
    else {
        close(sock);
        APP_LOG("NetworkControl", LOG_DEBUG, "apcli0 NOT paired");
        return FAILURE;
    }
}

int isAPConnected(char *pSSID, char *encryption)
{
    int i;

    if (g_bWiredEthernet) {
        return SUCCESS;
    }

    if (!pSSID) {
        return FAILURE;
    }
    if (!encryption) {
        return FAILURE;
    }

    for (i = 0; i < 5 ; i++) {
        if (get_conn_state() == SUCCESS) {
            return SUCCESS;
        }
        /* be nice, check the driver every 5 sec instead of 1 sec */
        pluginUsleep(5000000);
    }

    APP_LOG("NetworkControl", LOG_DEBUG, "AP connection failed");

    return FAILURE;
}

/**
 *  Check the AP ssid is existed or NOT
 */
int IsValidSSID(char* szApSSID)
{
    int rect = 0x00;
    int cntApCount = 0x00;
    PMY_SITE_SURVEY pCurApList = 0x00;

    if ((0x00 == szApSSID) || (0x00 == strlen(szApSSID))) {
        APP_LOG("NetworkControl",LOG_INFO, "IsValidSSID : wrong parameter");
        return rect;
    }

    pCurApList = (PMY_SITE_SURVEY)MALLOC(sizeof(MY_SITE_SURVEY)*MAX_LEN_OF_BSS_TABLE);
    if(!pCurApList) {
        APP_LOG("NetworkControl",LOG_DEBUG, "Malloc Failed");
        return rect;
    }

    //- Get the list again
    APP_LOG("NetworkControl",LOG_DEBUG, "Reading cached AP list");
#ifdef __MIPSEL__
    wifiGetNetworkList(pCurApList, "apcli0", &cntApCount);
#else
    wifiGetNetworkList(pCurApList, "br-lan", &cntApCount);
#endif
    if ((cntApCount > 0x00) && (0x00 != pCurApList)) {
        //-Check the AP list and get the network type here
        int index = 0x00;
        APP_LOG("NetworkControl",LOG_DEBUG, "Cached AP list:%d", cntApCount);
        for (; index < cntApCount; index++) {
            //- compare the ssid
            if (0x00 == strlen(pCurApList[index].ssid))
                continue;

            if (0x00 == strcmp(pCurApList[index].ssid, szApSSID)) {
                // founded
                rect = 0x01;
                break;
            }
        }
    }

    free(pCurApList);

    APP_LOG("NetworkControl",LOG_INFO, "ssid:%s %s", szApSSID, rect ? "FOUND" : "NOT FOUND");
    return rect;
}

int isValidIp()
{
    char *pIp = NULL,*pGateway = NULL;
    char *pSavedSSID = NULL;
    int count = 0;
    int maxUdhcpTimeOut = 0;
    char ssid[WIFI_MAXSTR_LEN+1];
    char rSSID[WIFI_MAXSTR_LEN+1];
    char rMAC[MAX_DVAL_LEN];

    memset(rSSID, 0x0, WIFI_MAXSTR_LEN+1);
    memset(rMAC, 0x0, MAX_DVAL_LEN);

    maxUdhcpTimeOut = MAX_UDHCPC_TIMEOUT;

    while(count < maxUdhcpTimeOut) {
        count++;
        pIp = GetWanIPAddress ();
        APP_LOG("NetworkControl", LOG_DEBUG,"IP:%s",pIp);
        if( pIp && strlen(pIp) > 0 && (0 != strcmp(pIp, "0.0.0.0")) ) {
            /* we were anyways not checking what the gateway IP is */
            // pGateway = GetWanDefaultGateway();
            pGateway = UpdateWanDefaultGateway();
            while(0x00 == strcmp(pGateway, "0.0.0.0")) {
                sleep(1);
                count++;
                pGateway = UpdateWanDefaultGateway();
                if(count > maxUdhcpTimeOut) {
                    APP_LOG("NetworkControl", LOG_CRIT, "Bad password case?");
                    return STATE_DISCONNECTED;
                }
            }

            wifiGetStatus(rSSID, rMAC, INTERFACE_CLIENT);

            APP_LOG("NetworkControl", LOG_CRIT, "IP: %s, Gateway: %s, BSSID: %s", pIp, pGateway, rMAC);

            update_resolv_config();

            // We have a valid IP address, start ntpclient
            createNTPUpdateThread();
            return STATE_INTERNET_NOT_CONNECTED;
        } else {
            /* Give two tries: one for 30 seconds and other for 20 seconds in usual setup, and in every 30 sec in case of inta*/
            if(!(count%30)) {
                pSavedSSID = gWiFiParams.SSID;

                memset(ssid, '\0', WIFI_MAXSTR_LEN+1);
                /************************************************************************
                 *  Story: 2571
                 *  This "convertSSID" will convert the hex string in raw bytes format,
                 *  which is used in pairing with the router
                 ************************************************************************/
                strncpy(ssid, pSavedSSID, sizeof(ssid)-1);
                //strncpy(ssid, convertSSID(ssid), sizeof(ssid)-1);
                if(isAPConnected(ssid, gWiFiParams.EncrypType) == SUCCESS) {
                    APP_LOG("NetworkControl", LOG_DEBUG, "#To issue dhcpc again");
                    RunDhcpRequest(0);
                } else {
                    APP_LOG("NetworkControl", LOG_DEBUG, "AP association lost!!!!");
                    /* We don't need to stop the dhcp process if it is running */
                    /*                    StopDhcpRequest(); */
                    update_resolv_config();
                    return STATE_DISCONNECTED;
                }
            }
        }
        pluginUsleep(2000000);
    }

    /*    StopDhcpRequest(); */
    update_resolv_config();
    return STATE_DISCONNECTED;
}

/************************************************************************
 * Function: wifiTestConnection
 *     test the internet connection
 *  Parameters:
 *    interface.
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiTestConnection (char *pInterface, int count,int dhcp)
{
    int i = 0;
    int  success = 0;
    char command1[SIZE_256B],command2[SIZE_256B],cmdBuf[SIZE_256B];
    char *pIp,*pGateway;

    /* let's give few seconds for dhcp before start testing. */
    APP_LOG("NetworkControl", LOG_DEBUG,"sleeping to give time for dhcp");
    sleep(5);

    memset(command1,0,sizeof(command1));
    strncpy (command1,"ifconfig ", sizeof(command1)-1);
    strncat (command1, pInterface, sizeof(command1)-strlen(command1)-1);

    memset(command2,0,sizeof(command2));
    strncpy (command2,"iwpriv ", sizeof(command2)-1);
    strncat (command2,pInterface, sizeof(command2)-strlen(command2)-1);
    strncat (command2," show connStatus", sizeof(command2)-strlen(command2)-1);
    if(0) {
        strncpy(cmdBuf, "killall -9 psmon wan_connect ledctrl udhcpc", sizeof(cmdBuf)-1);
        system (cmdBuf);
        memset (cmdBuf,0,SIZE_256B);
#ifdef __MIPSEL__
        strncpy (cmdBuf, "udhcpc -i apcli0 -s /bin/udhcpc.sh", sizeof(cmdBuf)-1);
#else
        strncpy (cmdBuf, "udhcpc -i br-lan -s /bin/udhcpc.sh", sizeof(cmdBuf)-1);
#endif
        System (cmdBuf);
    } else {
        pIp = GetWanIPAddress ();
        APP_LOG("NetworkControl", LOG_DEBUG,"#IP:%s", pIp);

        if(pIp && strcmp(pIp, "0.0.0.0")) {
            // pGateway = GetWanDefaultGateway();
            pGateway = UpdateWanDefaultGateway();
            if (pGateway && (strlen (pGateway) > 1) && (strcmp(pGateway, "0.0.0.0")) ) {
                APP_LOG("NetworkControl", LOG_DEBUG, "#IP:%s, Gateway:%s", pIp, pGateway);
                if( PACKET_LOSE_100_PERCENT != ping_status(pGateway, 5) ) {
                    return 1;
                } else {
                    APP_LOG("NetworkControl", LOG_CRIT, "#can not ping gateway: %s", pGateway);
                    APP_LOG("NetworkControl", LOG_DEBUG, "#Request ip address again");
                    RunDhcpRequest(0);
                    pluginUsleep(5000000);
                }
            } else {
                APP_LOG("NetworkControl", LOG_CRIT, "***NO GATEWAY NOT CONNECTED***\n");
            }
        }
    }


    for(i=0; i < count; i++) {
        //Start the DHCP client
#ifdef _CHECK_IP_FOR_CONNECTION_

        pIp = GetWanIPAddress ();
        APP_LOG("NetworkControl", LOG_DEBUG,"IP:%s", pIp);

        // pGateway = GetWanDefaultGateway();
        pGateway = UpdateWanDefaultGateway();
        APP_LOG("NetworkControl", LOG_DEBUG,"Gateway IP:%s", pGateway);

        if(pIp) {
            if(strcmp(pIp,"0.0.0.0") && strcmp(pGateway, "0.0.0.0")) {
                if( PACKET_LOSE_100_PERCENT != ping_status(pGateway, 5) )
                    success = 1;
            }
        }


        if (success == 1) {
            APP_LOG("NetworkControl", LOG_CRIT, "IP: %s, Gateway: %s connection tested", pIp, pGateway);
            break;
        }
#endif //_CHECK_IP_FOR_CONNECTION_
        pluginUsleep (2000000);
    }
    return success;
}

static int cmp_signal(const void *p1, const void *p2)
{
    return atoi(((PMY_SITE_SURVEY)p2)->signal) - atoi(((PMY_SITE_SURVEY)p1)->signal);
}

/************************************************************************
 * Function: wifiGetNetworkList
 *     get available wifi connections list.
 *  Parameters:
 *    getList - list coming from wifi.
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiGetNetworkList(PMY_SITE_SURVEY SiteSurvey,
                       char *pInterface, int *pListCount)
{
#ifdef __MIPSEL__
    struct iwreq wrq;
    int retVal = SUCCESS, apCount = 0, sock = socket(AF_INET, SOCK_DGRAM, 0);
    int i = 0;
    int TotalLen = 0;
    char *data = NULL;


    if (sock < 0) {
        perror("socket failed ");
        return -1;
    }

    /* buffer allocated is as per the WIFI driver. Refer: cmm_info.c*/
    /* Memory allocated for 128 entries */
    TotalLen = (MAX_LEN_OF_BSS_TABLE*sizeof(MY_SITE_SURVEY)) + SITE_SURVEY_HDR_LEN;
    data = (char *)ZALLOC(TotalLen);
    if(data == NULL) {
        perror("Memory allocation failed");
        return -1;
    }

    strncpy (data,"", TotalLen-1);
    strncpy(wrq.ifr_name, pInterface, sizeof(wrq.ifr_name)-1);
#ifdef MT7628_AIRPLAY_SUPPORT
    /* airplay supported mt7628 driver expects to have length field 0 */
    /* if the length field is none zero, then data should contain the target */
    /* ssid to search for */
    wrq.u.data.length = 0;
#else
    wrq.u.data.length = TotalLen;
#endif
    wrq.u.data.pointer = data;
    wrq.u.data.flags = 0;

    APP_LOG("NetworkControl", LOG_DEBUG, "interface = %s, wrq data length = [%d]\n", pInterface, wrq.u.data.length);

    retVal = ioctl(sock, RTPRIV_IOCTL_GSITESURVEY, &wrq);
    if(retVal < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "IOCTL Error %s", strerror(errno));
        free(data);
        close(sock);
        return FAILURE;
    }

    APP_LOG("NetworkControl", LOG_DEBUG, "RTPRIV_IOCTL_SET passed \n");

    if (wrq.u.data.length > 0) {
        int len = wrq.u.data.length,j=0;
        char *ptr = data;
        char *ptr1;

        APP_LOG("NetworkControl", LOG_DEBUG, "data length = [%d]\n", len);

        ptr = ptr + SITE_SURVEY_HDR_LEN - 1;
        while((apCount*(sizeof(MY_SITE_SURVEY))) < len) {
            if ( i >= MAX_LEN_OF_BSS_TABLE ) {
                break;
            }

            strncpy(SiteSurvey[i].Hidden, ptr+HIDDEN_LOC, HIDDEN_LEN);
            SiteSurvey[i].Hidden[HIDDEN_LEN-1] = '\0';

            if(atoi(SiteSurvey[i].Hidden)) {
                ptr = ptr + sizeof(MY_SITE_SURVEY);
                ++apCount;
                continue;
            }

            strncpy((char *)SiteSurvey[i].channel, ptr, CHANNEL_LEN);
            SiteSurvey[i].channel[CHANNEL_LEN-1] = '\0';

            int ssid_length;
            ptr1 = ptr+SSIDSIZE_LOC;
            strncpy((char *)SiteSurvey[i].ssid_size, ptr1, SSIDSIZE_LEN);
            SiteSurvey[i].ssid_size[SSIDSIZE_LEN - 1] = 0;
            ssid_length = atoi((char *)SiteSurvey[i].ssid_size);

            memset(SiteSurvey[i].unicode, 0, UNICODE_LEN);
            strncpy((char *)SiteSurvey[i].unicode, ptr + UNICODE_LOC, UNICODE_LEN);
            SiteSurvey[i].unicode[UNICODE_LEN - 1] = '\0';

            memset (SiteSurvey[i].ssid, 0, SSID_LEN);
            ptr1 = ptr+SSID_LOC;
            if (SiteSurvey[i].unicode[0] == 'N') {
                for (j=0; j<ssid_length; j++) {
                    SiteSurvey[i].ssid[j] = ptr1[j];
                }
            }
            else {
                for (j=0; j<SSID_LEN; j++) {
                    if(ptr1[j] <= ' ' && ptr1[j+1] <= ' ')
                        break;
                    SiteSurvey[i].ssid[j] = ptr1[j];
                }
            }

            APP_LOG("NetworkControl", LOG_DEBUG, "SiteSurvey SSID = [%s], unicode = [%s]", SiteSurvey[i].ssid, SiteSurvey[i].unicode);

            strncpy((char *)SiteSurvey[i].bssid, ptr+BSSID_LOC, BSSID_LEN);
            SiteSurvey[i].bssid[BSSID_LEN-1] = '\0';

            memset (SiteSurvey[i].security, 0, SECURITY_LEN);
            ptr1 = ptr+SECURITY_LOC;
            for (j=0; j<SECURITY_LEN; j++) {
                if(ptr1[j] <= ' ')
                    break;
                SiteSurvey[i].security[j] = ptr1[j];
            }
            SiteSurvey[i].security[SECURITY_LEN-1] = '\0';
            if(!strcmp(SiteSurvey[i].security,"NONE"))
                strncpy(SiteSurvey[i].security,"OPEN/NONE", strlen("OPEN/NONE"));

            strncpy(SiteSurvey[i].signal, ptr+SIGNAL_LOC, SIGNAL_LEN);
            SiteSurvey[i].signal[SIGNAL_LEN-1] = '\0';

            memset (SiteSurvey[i].WMode, 0, WMODE_LEN);
            ptr1 = ptr+WMODE_LOC;
            for (j=0; j<WMODE_LEN; j++) {
                if(ptr1[j] <= ' ')
                    break;
                SiteSurvey[i].WMode[j] = ptr1[j];
            }
            SiteSurvey[i].WMode[WMODE_LEN-1] = '\0';

            strncpy((char *)SiteSurvey[i].ExtCH, ptr+EXTCH_LOC, EXTCH_LEN);
            SiteSurvey[i].ExtCH[EXTCH_LEN-1] = '\0';

            strncpy(SiteSurvey[i].Hidden, ptr+HIDDEN_LOC, HIDDEN_LEN);
            SiteSurvey[i].Hidden[HIDDEN_LEN-1] = '\0';

            strncpy(SiteSurvey[i].VAR_IE, ptr+VAR_IE_LOC, VAR_IE_LEN);
            SiteSurvey[i].VAR_IE[VAR_IE_LEN-1] = '\0';

            ptr = ptr + sizeof(MY_SITE_SURVEY);
            i++, apCount++;
        }

        /* pListCount should be i - 1 */
        *pListCount = i - 1;
        APP_LOG("NetworkControl", LOG_INFO,"Count:<%d>:<%d>\n",*pListCount,apCount);
    }

    APP_LOG("NetworkControl", LOG_DEBUG,"Sorting output by signal strength...\n");
    qsort((void *)SiteSurvey, *pListCount, sizeof(MY_SITE_SURVEY), cmp_signal);

    APP_LOG("NetworkControl", LOG_INFO,"Exit wifiGetNetworkList...\n");

    free(data);
    close(sock);

#endif
    return SUCCESS;
}

int getCompleteAPList(PMY_SITE_SURVEY SiteSurvey,
                      int *pListCount)
{
#ifdef __MIPSEL__
    struct iwreq wrq;
    int retVal = SUCCESS, apCount = 0, sock = socket(AF_INET, SOCK_DGRAM, 0);
    int i = 0;
    int TotalLen = 0;
    char *data = NULL;

    if (sock < 0) {
        perror("socket failed ");
        return -1;
    }

    /* buffer allocated is as per the WIFI driver. Refer: cmm_info.c*/
    /* Memory allocated for 128 entries */
    TotalLen = (MAX_LEN_OF_BSS_TABLE*sizeof(MY_SITE_SURVEY)) + SITE_SURVEY_HDR_LEN;
    data = (char *)ZALLOC(TotalLen);
    if(data == NULL) {
        perror("Memory allocation failed");
        return -1;
    }

    strncpy (data,"", TotalLen-1);
    strncpy(wrq.ifr_name, "apcli0", sizeof(wrq.ifr_name)-1);
#ifdef MT7628_AIRPLAY_SUPPORT
    wrq.u.data.length = 0;
#else
    wrq.u.data.length = TotalLen;
#endif
    wrq.u.data.pointer = data;
    wrq.u.data.flags = 0;

    APP_LOG("NetworkControl", LOG_DEBUG, "wrq data length = [%d]\n", wrq.u.data.length);

    retVal = ioctl(sock, RTPRIV_IOCTL_GSITESURVEY, &wrq);
    if(retVal < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "IOCTL Error %s", strerror(errno));
        free(data);
        close(sock);
        return FAILURE;
    }

    APP_LOG("NetworkControl", LOG_DEBUG, "RTPRIV_IOCTL_SET passed \n");

    if (wrq.u.data.length > 0) {
        int len = wrq.u.data.length,j=0;
        char *ptr = data;
        char *ptr1;

        APP_LOG("NetworkControl", LOG_DEBUG, "data length = [%d]\n", len);

        ptr = ptr + SITE_SURVEY_HDR_LEN - 1;
        while((i*(sizeof(MY_SITE_SURVEY))) < len) {
            if ( i >= MAX_LEN_OF_BSS_TABLE ) {
                break;
            }
            strncpy((char *)SiteSurvey[i].channel, ptr, CHANNEL_LEN);
            SiteSurvey[i].channel[CHANNEL_LEN-1] = '\0';

            int ssid_length;
            ptr1 = ptr+SSIDSIZE_LOC;
            strncpy((char *)SiteSurvey[i].ssid_size, ptr1, SSIDSIZE_LEN);
            SiteSurvey[i].ssid_size[SSIDSIZE_LEN - 1] = 0;
            ssid_length = atoi((char *)SiteSurvey[i].ssid_size);

            memset(SiteSurvey[i].unicode, 0, UNICODE_LEN);
            strncpy((char *)SiteSurvey[i].unicode, ptr + UNICODE_LOC, UNICODE_LEN);
            SiteSurvey[i].unicode[UNICODE_LEN - 1] = '\0';

            memset (SiteSurvey[i].ssid, 0, SSID_LEN);
            ptr1 = ptr+SSID_LOC;
            if (SiteSurvey[i].unicode[0] == 'N') {
                for (j=0; j<ssid_length; j++) {
                    SiteSurvey[i].ssid[j] = ptr1[j];
                }
            }
            else {
                for (j=0; j<SSID_LEN; j++) {
                    if(ptr1[j] <= ' ' && ptr1[j+1] <= ' ')
                        break;
                    SiteSurvey[i].ssid[j] = ptr1[j];
                }
            }
            //            APP_LOG("NetworkControl", LOG_DEBUG, "SiteSurvey SSID = [%s], unicode = [%s]", SiteSurvey[i].ssid, SiteSurvey[i].unicode);

            strncpy((char *)SiteSurvey[i].bssid, ptr+BSSID_LOC, BSSID_LEN);
            SiteSurvey[i].bssid[BSSID_LEN-1] = '\0';

            memset (SiteSurvey[i].security, 0, SECURITY_LEN);
            ptr1 = ptr+SECURITY_LOC;
            for (j=0; j<SECURITY_LEN; j++) {
                if(ptr1[j] <= ' ')
                    break;
                SiteSurvey[i].security[j] = ptr1[j];
            }
            SiteSurvey[i].security[SECURITY_LEN-1] = '\0';
            if(!strcmp(SiteSurvey[i].security,"NONE"))
                strncpy(SiteSurvey[i].security,"OPEN/NONE", sizeof(SiteSurvey[i].security)-1);

            strncpy(SiteSurvey[i].signal, ptr+SIGNAL_LOC, SIGNAL_LEN);
            SiteSurvey[i].signal[SIGNAL_LEN-1] = '\0';

            strncpy(SiteSurvey[i].WMode, ptr+WMODE_LOC, WMODE_LEN);
            SiteSurvey[i].WMode[WMODE_LEN-1] = '\0';

            strncpy((char *)SiteSurvey[i].ExtCH, ptr+EXTCH_LOC, EXTCH_LEN);
            SiteSurvey[i].ExtCH[EXTCH_LEN-1] = '\0';

            strncpy(SiteSurvey[i].Hidden, ptr+HIDDEN_LOC, HIDDEN_LEN);
            SiteSurvey[i].Hidden[HIDDEN_LEN-1] = '\0';

            strncpy(SiteSurvey[i].VAR_IE, ptr+VAR_IE_LOC, VAR_IE_LEN);
            SiteSurvey[i].VAR_IE[VAR_IE_LEN-1] = '\0';

            ptr = ptr + sizeof(MY_SITE_SURVEY);
            i++, apCount++;
        }
        //*pListCount = i-2;
        //if ( *pListCount  )
        *pListCount = i - 1;
        APP_LOG("NetworkControl", LOG_INFO,"Count:<%d>:<%d>\n",*pListCount,apCount);
    }

    APP_LOG("NetworkControl", LOG_DEBUG,"Sorting output by signal strength...\n");
    qsort((void *)SiteSurvey, *pListCount, sizeof(MY_SITE_SURVEY), cmp_signal);
    APP_LOG("NetworkControl", LOG_INFO, "Exit getCompleteAPList");

    free(data);
    close(sock);
#endif
    return SUCCESS;
}

double wifi_iw_freq2float(struct	iw_freq*    in)
{
#ifdef __MIPSEL__
#ifdef WE_NOLIBM
    int           i;
    double        res = (double) in->m;
    for(i = 0; i < in->e; i++)
        res *= 10;
    return(res);
#else
    return ((double) in->m) * pow(10,in->e);
#endif
#else
    return 0;
#endif
}

void wifi_iw_print_freq_value(char *      buffer,
                              int         buflen,
                              double      freq)
{
    if(freq < KILO)
        snprintf(buffer, buflen, "%g", freq);
    else {
        char      scale;
        int       divisor;

        if(freq >= GIGA) {
            scale = 'G';
            divisor = GIGA;
        } else {
            if(freq >= MEGA) {
                scale = 'M';
                divisor = MEGA;
            } else {
                scale = 'k';
                divisor = KILO;
            }
        }
        snprintf(buffer, buflen, "%g %cHz", freq / divisor, scale);
    }
}

void wifi_iwPrintFreq(char *      buffer,
                      int       buflen,
                      double    freq,
                      int       channel,
                      int       freq_flags)
{
    char  vbuf[SIZE_16B];

    /* Print the frequency/channel value */
    wifi_iw_print_freq_value(vbuf, sizeof(vbuf), freq);

    /* Check if channel only */
    if(freq < KILO)
        snprintf(buffer, buflen, "%s", vbuf);
    else
        snprintf(buffer, buflen, "%s", vbuf);
}

/************************************************************************
 * Function: wifiGetStats
 *     get WiFi Statistics Counters from driver
 *  Parameters:
 *    pInterface - interface
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiGetStats (char *pInterface, PWIFI_STAT_COUNTERS pWiFiStats)
{
#ifdef __MIPSEL__
    struct iwreq wrq;
    char data[SIZE_2048B];
    int ret = 0, sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        APP_LOG("NetworkControl", LOG_ERR, "Socket Error %s", strerror(errno));
        return FAILURE;
    }

    if(!pInterface) {
        APP_LOG("NetworkControl", LOG_ERR, "Parameter Error");
        close (sock);
        return FAILURE;
    }

    memset(data, 0x00, SIZE_2048B);
    strncpy(data, "", sizeof(data)-1);

    strncpy(wrq.ifr_ifrn.ifrn_name, pInterface, IFNAMSIZ);

    wrq.u.data.length = 0;
    APP_LOG("NetworkControl", LOG_INFO, "[Command] %s[len = %d]",data,wrq.u.data.length);
    wrq.u.data.pointer = data;
    wrq.u.data.flags = 0;

    ret = ioctl(sock, RTPRIV_IOCTL_STATISTICS, &wrq);

    if(ret < 0) {
        close(sock);
        APP_LOG("NetworkControl", LOG_ERR, "IOCTL Error %s", strerror(errno));
        return -1;
    }
    APP_LOG("NetworkControl", LOG_DEBUG, "RTPRIV_IOCTL_STATISTICS, passed\n");
    {
        int i;
        char *pSP = wrq.u.data.pointer;
        unsigned long *pCounter = (unsigned long *) pWiFiStats;

        for (i=0; i<13; i++) {
            pSP = strstr (pSP,"=");
            pSP = pSP+2;
            sscanf(pSP, "%ul", (unsigned int *)&pCounter[i]);
        }

        //Print Some data Here
        APP_LOG("NetworkControl", LOG_INFO,"TxSuccess:<%lu>\n",pWiFiStats->TxSuccessTotal);
        APP_LOG("NetworkControl", LOG_INFO,"RSSIA:<%lu>\n",pWiFiStats->RssiA);
        APP_LOG("NetworkControl", LOG_INFO,"RSSIB:<%lu>\n",pWiFiStats->RssiB);


    }

    close(sock);
#endif
    return 0;
}

/************************************************************************
 * Function: wifiGetAPIfState
 *     get off/On AP mode
 *  Parameters:
 *     State- NONE
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiGetAPIfState ()
{
    return GetEnableAP();
}

/************************************************************************
 * Function: wifiChangeAPIfState
 *     to
 *     swith off/On AP mode
 *  Parameters:
 *     State- ON/OFF
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiChangeAPIfState (int state)
{
    char cmd[SIZE_256B];

    switch (state) {
    case TURN_ON:
        memset (cmd,0x0,SIZE_256B);
        sprintf(cmd, "ifconfig %s up", INTERFACE_AP);
        system (cmd);
#ifdef MT7628_AIRPLAY_SUPPORT
        memset (cmd,0x0,SIZE_256B);
        sprintf(cmd, "iwpriv %s set airplayEnable=1", INTERFACE_AP);
        system(cmd);
#endif
        break;
    case TURN_OFF:
        sprintf(cmd, "iwpriv %s set airplayEnable=0", INTERFACE_AP);
        system(cmd);
        sprintf(cmd, "ifconfig %s down", INTERFACE_AP);
        system (cmd);
        break;
    }

    return SUCCESS;
}

/************************************************************************
 * Function: wifiChangeClientIfState
 *     to
 *     Enable/Disable Client mode
 *  Parameters:
 *     State- ON/OFF
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifiChangeClientIfState (int state)
{
    char cmd[SIZE_256B];
    int retVal=SUCCESS;

    memset (cmd,0x0,SIZE_256B);
    strncpy(cmd,"ApCliEnable=",sizeof(cmd)-1);
    switch (state) {
    case TURN_ON: {
        strncat (cmd,"1",sizeof(cmd)-strlen(cmd)-1);
    }
    break;
    case TURN_OFF: {
        strncat (cmd,"0",sizeof(cmd)-strlen(cmd)-1);

    }
    break;
    }
    retVal = wifiSetCommand (cmd,INTERFACE_CLIENT);
    //Enable SiteSurvey Here itself.
    if(SUCCESS == retVal) {
        memset (cmd,0x0,SIZE_256B);
        strncpy(cmd,"SiteSurvey=1",sizeof(cmd)-1);
        retVal = wifiSetCommand (cmd,INTERFACE_CLIENT);
    }
    return retVal;
}

/************************************************************************
 * Function: wifisetSSIDOfAP ()
 *     to
 *     change the SSID of AP.
 *  Parameters:
 *     New Name
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifisetSSIDOfAP (char *pSSID)
{
    char cmd[SIZE_64B];

    if(!pSSID) {
        APP_LOG("NetworkControl", LOG_ERR, "Param ER \n");
        return FAILURE;
    } else {
        FILE *pipe;
        char buffer[SIZE_256B];

        strncpy(cmd, "ifconfig | grep ra0", sizeof(cmd)-1);
        pipe = popen( cmd, "r");
        if (pipe == NULL) {
            APP_LOG("NetworkControl", LOG_ERR, "Popen Error %s", strerror(errno));
            resetSystem();
        }
        if (fgets(buffer, SIZE_256B, pipe) == NULL) {
            APP_LOG("NetworkControl", LOG_DEBUG, "NOT FOUND ra0");
            system("ifconfig ra0 up");
        }
        pclose(pipe);
    }
    memset(cmd, '\0',sizeof(cmd));
    strncpy(cmd, "SSID=", sizeof(cmd)-1);
    strncat(cmd, pSSID,sizeof(cmd)-strlen(cmd)-1);
    return wifiSetCommand (cmd,INTERFACE_AP);
}

/************************************************************************
 * Function: wifisetChannelOfAP ()
 *     to
 *     change the channel of AP.
 *  Parameters:
 *     channel value
 *  Return:
 *     SUCCESS if success else < SUCCESS
 ************************************************************************/
int wifisetChannelOfAP (char *nChannel)
{
    char cmd[SIZE_64B];

    if(!nChannel) {
        APP_LOG("NetworkControl", LOG_ERR, "Param ER \n");
        return FAILURE;
    }
    memset(cmd, '\0', SIZE_64B);
    strncpy(cmd, "Channel=", sizeof(cmd)-1);
    strncat(cmd, nChannel,sizeof(cmd)-strlen(cmd)-1);
    return wifiSetCommand (cmd,INTERFACE_AP);
}

/**
 *  Check the AP ssid is mixed mode or NOT
 */
/*
  Description: Wireless mode configuration value for mt7628
  0: legacy 11b/g mixed
  1: legacy 11B only
  2: legacy 11A only
  3: legacy 11a/b/g mixed
  4: legacy 11G only
  5: 11ABGN mixed
  6: 11N only in 2.4G
  7: 11GN mixed
  8: 11AN mixed
  9: 11BGN mixed
  10: 11AGN mixed
  11: 11N only in 5G
  14: 11A/AN/AC mixed 5G band only (only 11AC chipset support)
  15: 11 AN/AC mixed 5G band only (only 11AC chipset support)

  and
  AP_MIXED_MODE_STR defined "11b/g/n"
 */

int wifiSetForMixedMode(char* ssid, char *auth, char *wmode)
{
    int ret = 0;
    char pCommand[PCMD_LEN];
    int rect = 0;
    if ((0x00 == ssid) || (0x00 == strlen(ssid))) {
        return rect;
    }

    if(!strcmp (auth, "WPA1PSKWPA2PSK") || (!strcmp(auth, "WPAPSKWPA2PSK"))) {
        if(0x00 == strcmp(AP_MIXED_MODE_STR, wmode)) {
            memset(pCommand, '\0', PCMD_LEN);
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
            strncpy(pCommand, "WirelessMode=9", sizeof(pCommand)-1);
#else
            strncpy(pCommand, "WirelessMode=0", sizeof(pCommand)-1);
#endif
            ret = wifiSetCommand (pCommand, "apcli0");
            if(ret < 0) {
                APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
                return FAILURE;
            }
            rect = 1;
            APP_LOG("NetworkControl", LOG_DEBUG, "**********Command Set: %s",pCommand);
            APP_LOG("NetworkControl", LOG_DEBUG, "iwpriv apcli0 set %s command executed", pCommand);
        }
        memset(pCommand, '\0', PCMD_LEN);
        strncpy(pCommand, "HtDisallowTKIP=1", sizeof(pCommand)-1);
        ret = wifiSetCommand (pCommand,"apcli0");
        if(ret < 0) {
            APP_LOG("NetworkControl", LOG_ERR, "%s - failed", pCommand);
            return FAILURE;
        }
        APP_LOG("NetworkControl", LOG_DEBUG, "**********Command Set: %s\n",pCommand);
    }
    return rect;
}

int getCurrentClientState(void)
{
    int state;
    osUtilsGetLock(&s_client_state_mutex);
#ifdef __MIPSEL__
    state = gWiFiClientCurrState;
#else
    state = STATE_CONNECTED;
#endif
    osUtilsReleaseLock(&s_client_state_mutex);
    return state;
}

int wifiGetRSSI(char *pInterface)
{
#ifdef __MIPSEL__
    struct iwreq wrq;
    int rssiData = 0;
    unsigned int Rssi_Quality = 0;
    int ret = 0, sock = -1;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        APP_LOG("NetworkControl", LOG_DEBUG, "Socket Error");
        return Rssi_Quality;
    }

    if(!pInterface) {
        APP_LOG("NetworkControl", LOG_DEBUG, "Parameter Error");
        close (sock);
        return Rssi_Quality;
    }

    strncpy(wrq.ifr_name, pInterface, sizeof(wrq.ifr_name)-1);

    wrq.u.data.length = sizeof(rssiData);
    wrq.u.data.pointer = (void*)(&rssiData);
#if defined(PRODUCT_WeMo_Dimmer) || defined (PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    wrq.u.data.flags = OID_802_11_RSSI;
#else
    wrq.u.data.flags = RT_OID_APCLI_RSSI;
#endif
    ret = ioctl(sock,RT_PRIV_IOCTL,&wrq);

    if(ret < 0) {
        close(sock);
        APP_LOG("NetworkControl", LOG_DEBUG,"IOCTL Error %d",ret);
        return Rssi_Quality;
    }

    if (wrq.u.data.length > 0) {
        APP_LOG("NetworkControl", LOG_DEBUG,"rssiData:%d",rssiData);
        if(rssiData >= -50)
            Rssi_Quality = 100;
        else if(rssiData >= -80)    /* between -50 ~ -80dbm*/
            Rssi_Quality = (unsigned int)(24 + ((rssiData + 80) * 26)/10);

        else if(rssiData >= -90)   /* between -80 ~ -90dbm*/
            Rssi_Quality = (unsigned int)(((rssiData + 90) * 26)/10);
        else    /* < -84 dbm*/
            Rssi_Quality = 0;
        APP_LOG("NetworkControl", LOG_DEBUG,"Rssi quality in percent:%u%%", Rssi_Quality);
    }
    close(sock);

    return Rssi_Quality;
#else
    return 0;
#endif
}
