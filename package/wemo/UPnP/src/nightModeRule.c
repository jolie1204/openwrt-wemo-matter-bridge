/***************************************************************************
 *
 *
 * nightModeRule.c
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
#include <upnp.h>
#include "global.h"
#include "wemodefs.h"
#include "logger.h"
#include "controlledevice.h"
#ifdef _OPENWRT_
#include "belkin_api.h"
#else
#include "gemtek_api.h"
#endif
#include "rule.h"
#include "gpio.h"
#include "utils.h"
#include "mxml.h"
#include "osUtils.h"

#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

#ifdef PRODUCT_WeMo_Dimmer

extern UpnpDevice_Handle device_handle;
extern int gNTPTimeSet;
pthread_t gNightModeThread = INVALID_THREAD_HANDLE;
pthread_t gNightModeTimerThread = INVALID_THREAD_HANDLE;

/*
** This function notifies the night mode configuration changed
** to all the basic event subscribers
*/
void nightModeConfigurationNotify()
{
    if(gpsNightMode == NULL)
        return;

    char* paramters[] = {"nightMode", "startTime", "endTime", "nightModeBrightness"};
    char *nightConf[4] = {0};
    char nightMode[SIZE_2B] = {0};
    char startTime[SIZE_16B] = {0};
    char endTime[SIZE_16B] = {0};
    char brightness[SIZE_4B] = {0};

    snprintf(nightMode, sizeof(nightMode), "%u", gpsNightMode->nightMode);
    snprintf(startTime, sizeof(startTime), "%u", gpsNightMode->startTime);
    snprintf(endTime, sizeof(endTime), "%u", gpsNightMode->endTime);
    snprintf(brightness, sizeof(brightness), "%u", gpsNightMode->brightness);

    nightConf[0] = nightMode;
    nightConf[1] = startTime;
    nightConf[2] = endTime;
    nightConf[3] = brightness;

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)paramters, (const char **)nightConf, 4);

    APP_LOG("UPNP: NightModeConfiguration", LOG_DEBUG, "Notification NightMode:%u StartTime:%u EndTime:%u Brightness:%u",
            gpsNightMode->nightMode, gpsNightMode->startTime,
            gpsNightMode->endTime, gpsNightMode->brightness);
    return;
}

/*
** This thread will run when the night mode is enabled and
** will calculate if night mode is active and also compute the
** time to next transition (either start or end of night mode)
** It will sleep till next transition and will update the flag after waking up.
*/

void* nightModeThread(void *arg)
{

    unsigned int nowSeconds = 0;
    unsigned int secondsLeft=0;
    int start=1;

    /* Wait for Time Sync to happen */
    APP_LOG("UPNPDevice", LOG_DEBUG, "Waiting for time sync");

    while(!gNTPTimeSet)
        pluginUsleep(10*1000000);


    do {
        nowSeconds = daySeconds();
        APP_LOG("UPNPDevice", LOG_DEBUG, "nowSeconds %d, start: %d, end: %d, state: %d", nowSeconds, gpsNightMode->startTime, gpsNightMode->endTime, GetCurBinaryState());
        if(gpsNightMode->startTime < gpsNightMode->endTime) {
            /* same day schedule */
            /*
            			---------|---------------|------------
            			^	S.T.	^	E.T
            			|		|
            			Midnight	Now
            */

            gNightModeActive = ((nowSeconds >= gpsNightMode->startTime) && (nowSeconds < gpsNightMode->endTime));

            if(nowSeconds <= gpsNightMode->startTime)
                secondsLeft = gpsNightMode->startTime - nowSeconds;
            else if(nowSeconds <= gpsNightMode->endTime)
                secondsLeft = gpsNightMode->endTime - nowSeconds;
            else
                secondsLeft =  ONE_DAY_SECONDS - nowSeconds + gpsNightMode->startTime;

            APP_LOG("UPNPDevice", LOG_DEBUG, "gNightModeActive: %d, secondsLeft: %d", gNightModeActive, secondsLeft);
        } else {
            /* overnight schedule */
            /*
            			   X	      now>st		now<st		X
            			---------|--------------|----------------|------------
            				S.T.		^		E.T
            						|
            						Midnight
            */
            gNightModeActive = !((nowSeconds < gpsNightMode->startTime) && (nowSeconds > gpsNightMode->endTime));

            if(gNightModeActive && (nowSeconds >= gpsNightMode->startTime))
                secondsLeft = gpsNightMode->endTime + ONE_DAY_SECONDS - nowSeconds;
            else if(gNightModeActive && (nowSeconds < gpsNightMode->startTime))
                secondsLeft = gpsNightMode->endTime - nowSeconds;
            else if(!gNightModeActive && (nowSeconds < gpsNightMode->startTime))
                secondsLeft = gpsNightMode->startTime - nowSeconds;
            else if(!gNightModeActive && (nowSeconds > gpsNightMode->endTime))
                secondsLeft =  ONE_DAY_SECONDS - nowSeconds + gpsNightMode->startTime;

            APP_LOG("UPNPDevice", LOG_DEBUG, "gNightModeActive: %d, secondsLeft: %d", gNightModeActive, secondsLeft);
        }

        /* if we are entering night mode save last brightness unconditionally */

        if(gNightModeActive) {
            char *pBrightness = GetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);
            /* set only if BRIGHTNESS_BEFORE_NIGHT_MODE is not already set which
               might happen if the device gets rebooted during night mode */
            if(pBrightness && strlen(pBrightness)) {
                APP_LOG("UPNPDevice", LOG_DEBUG, "******Not Setting BRIGHTNESS_BEFORE_NIGHT_MODE******");
            } else {
            char szBrightness[SIZE_4B] = {'\0',};
            snprintf(szBrightness, sizeof(szBrightness), "%d", getBrightness());
            SetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE, szBrightness);
            }
            /*
               WEMO-48337: If a switch is off and transition in default brightness occurs as the day progresses,
               the brightness indicator on the physical switch changes to match the new default brightness
            */
            if(GetCurBinaryState() == POWER_OFF) {
                APP_LOG("UPNPDevice", LOG_DEBUG, "Applying night mode brightness to %d", gpsNightMode->brightness);
                setBrightness(gpsNightMode->brightness, false);
            }
        } else if(!start) {
            /*
            	restore the saved brightness value, if any
            	After device reboot, it will set whatever is in the variable
            	Need to ensure this is after transition from 1 to 0 and not after device bootup
            */
            char *pBrightness = GetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);
            if(pBrightness && strlen(pBrightness) && (GetCurBinaryState() == POWER_OFF)) {
                /* apply last stored brightness */
                APP_LOG("UPNPDevice", LOG_DEBUG, "Smart brightness change to %s", pBrightness);
                setBrightness(atoi(pBrightness), false);
                UnSetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);

            }
        }

        /* sleep an extra second to avoid looping few times at the same second value */
        APP_LOG("UPNPDevice", LOG_DEBUG, "sleeping for %d seconds", secondsLeft);
        sleep(secondsLeft+1);
        start=0;

    } while(!g_longPressAwayRunning);

    return NULL;
}

int scheduleNightMode(void)
{
    int retVal=0;
    if(gNightModeThread == INVALID_THREAD_HANDLE) {
        pthread_attr_t nightModeThread_attr;

        pthread_attr_init(&nightModeThread_attr);
        pthread_attr_setdetachstate(&nightModeThread_attr,PTHREAD_CREATE_DETACHED);
        retVal = pthread_create(&gNightModeThread,&nightModeThread_attr,
                                (void*)&nightModeThread, NULL);

        if(retVal < 0) {
            APP_LOG("WiFiApp",LOG_ERR, "Night mode thread not created");
            resetSystem();
        } else {
            APP_LOG("WiFiApp",LOG_DEBUG, "Night mode thread created successfully");
        }
        pthread_attr_destroy(&nightModeThread_attr);
    }
    return SUCCESS;
}

/************************************************************************
 * Function: processNightModeConfiguration
 *     Parse the night mode configuration XML, populate global data structure
 *     and schedule night mode thread, if required
 *  Parameters:
 *     formatXml - Input XML string to be parsed
 *	Sample night mode configuration
	<NightModeConfiguration>
      		<nightMode>1</nightMode>
      		<startTime>50100</startTime>
      		<endTime>50400</endTime>
      		<nightModeBrightness>20</nightModeBrightness>
	</NightModeConfiguration>
 *  Return:
 *     Returns SUCCESS or FAILURE
************************************************************************/


/* This function should be passed a XML formatted string */
int processNightModeConfiguration(char *formatXml)
{
    SXmlAttributes sXmlAttr[4];
    int ret = FAILURE;

    /* cancel current scheduler thread, if running */
    if(gNightModeThread != INVALID_THREAD_HANDLE) {
        pthread_cancel(gNightModeThread);
        gNightModeThread = INVALID_THREAD_HANDLE;

        /*
          To take care of night mode interval shift,
          if device is OFF, restore previous saved brightness
        */
        if(GetCurBinaryState() == POWER_OFF) {
            char *pBrightness = GetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);
            if(pBrightness && strlen(pBrightness)) {
                /* apply last stored brightness */
                APP_LOG("UPNPDevice", LOG_DEBUG, "Smart brightness change to %s", pBrightness);
                setBrightness(atoi(pBrightness), false);
                UnSetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);

            }
        }
    }

    memset(sXmlAttr, 0, 4*sizeof(SXmlAttributes));

    strncpy(sXmlAttr[0].name, "nightMode", sizeof(sXmlAttr[0].name));
    strncpy(sXmlAttr[1].name, "nightModeBrightness", sizeof(sXmlAttr[1].name));
    strncpy(sXmlAttr[2].name, "startTime", sizeof(sXmlAttr[2].name));
    strncpy(sXmlAttr[3].name, "endTime", sizeof(sXmlAttr[3].name));

    ret = parseXmlTags(4, formatXml, (SXmlAttributes *)&sXmlAttr);
    if(SUCCESS == ret) {
        if(!gpsNightMode)
            gpsNightMode = (SNightModeConfiguration *) ZALLOC(sizeof(SNightModeConfiguration));

        gpsNightMode->nightMode = atoi(sXmlAttr[0].value);
        gpsNightMode->brightness = atoi(sXmlAttr[1].value);
        gpsNightMode->startTime = atoi(sXmlAttr[2].value);
        gpsNightMode->endTime = atoi(sXmlAttr[3].value);

        APP_LOG("Upnp", LOG_DEBUG,"Configuration loaded successfully");
        if(gpsNightMode->nightMode) {
            scheduleNightMode();
        } else {
            /* If night mode is disabled while active, restore saved brightness if device is OFF */
            if(gNightModeActive && (POWER_OFF == GetCurBinaryState())) {
                char *pBrightness = GetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);
                if(pBrightness && strlen(pBrightness)) {
                    /* apply last stored brightness */
                    APP_LOG("UPNPDevice", LOG_DEBUG, "Smart brightness change to %s", pBrightness);
                    setBrightness(atoi(pBrightness), false);
                    UnSetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);
                }
            }

            /* As night mode is not in enabled state */
            gNightModeActive=0;
        }
        /* notify to the subscibers in the local nenetwork */
        pMessage msg = createMessage(NIGHTMODE_CONFIGURATION_NOTIFY, 0x00, 0x00);
        SendMessage2App(msg);
    }

    return ret;
}

int loadNightModeConfiguration()
{
    char buffer[SIZE_512B];
    int len = SIZE_512B;
    memset(buffer, 0, sizeof(buffer));


    int ret = readFileToBuffer(buffer, NIGHT_MODE_CONFIG_FILE_PATH, &len);

    if(ret) {
        APP_LOG("Upnp", LOG_ERR,"Loading configuration failed");
        return FAILURE;
    } else
        ret = processNightModeConfiguration(buffer);

    return ret;
}

/*
 	Sample night mode configuration
	<NightModeConfiguration>
      		<nightMode>1</nightMode>
      		<startTime>50100</startTime>
      		<endTime>50400</endTime>
      		<nightModeBrightness>20</nightModeBrightness>
	</NightModeConfiguration>
*/
int ConfigureNightMode(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int ret=0;
    char *status = NULL;

    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "ConfigureNightMode: command paramter invalid");
        return 0x01;
    }

    char* paramValue = Util_GetFirstDocumentItem(request, "NightModeConfiguration");

    UpnpActionRequest_set_ErrCode(pActionRequest, 402);
    status = "Invalid Args";

    if(!paramValue || !strlen(paramValue)) {
        UpnpAddToActionResponse(out, "ConfigureNightMode",
                                CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "status", status);

        FreeXmlSource(paramValue);
        return 0x00;
    }
    char* formatXml = (char*)CALLOC(1, (strlen(paramValue)+SIZE_128B));

    snprintf(formatXml, (strlen(paramValue)+SIZE_128B), "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><NightModeConfiguration>%s</NightModeConfiguration>", paramValue);

    ret = processNightModeConfiguration(formatXml);
    if(!ret) {
        /*Save configuration for later use*/
        ret = writeBufferToFile(formatXml, NIGHT_MODE_CONFIG_FILE_PATH, NULL);
        if(ret) {
            UpnpActionRequest_set_ErrCode(pActionRequest, 604);
            status = "Internal error";
        } else {
            APP_LOG("Upnp", LOG_DEBUG,"Configuration saved successfully");
            UpnpActionRequest_set_ErrCode(pActionRequest, 0);
            status = "SUCCESS";
        }
    }


    UpnpAddToActionResponse (out,
                             "ConfigureNightMode",
                             CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE],
                             "Status", status);

    if (formatXml)
        free(formatXml);
    FreeXmlSource(paramValue);

    return UPNP_E_SUCCESS;
}

int GetNightModeConfiguration(pUPnPActionRequest pActionRequest, IXML_Document *request, IXML_Document **out, const char **errorString)
{
    int nightMode=DEFAULT_NIGHT_MODE_STATUS;
    int brightness=DEFAULT_NIGHT_MODE_BRIGHTNESS;
    int startTime=DEFAULT_NIGHT_MODE_START_TIME;
    int endTime=DEFAULT_NIGHT_MODE_END_TIME;
    char temp[SIZE_64B];

    if (0x00 == pActionRequest || 0x00 == request) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "ConfigureNightMode: command paramter invalid");
        return 0x01;
    }

    if(gpsNightMode) {
        nightMode = gpsNightMode->nightMode;
        startTime = gpsNightMode->startTime;
        endTime = gpsNightMode->endTime;
        brightness = gpsNightMode->brightness;
    }

    UpnpActionRequest_set_ErrCode(pActionRequest, 0);

    memset(temp, 0, sizeof(temp));

    snprintf(temp, sizeof(temp), "%d", nightMode);
    UpnpAddToActionResponse(out, "GetNightModeConfiguration",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "nightMode", temp);

    snprintf(temp, sizeof(temp), "%d", startTime);
    UpnpAddToActionResponse(out, "GetNightModeConfiguration",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "startTime", temp);

    snprintf(temp, sizeof(temp), "%d", endTime);
    UpnpAddToActionResponse(out, "GetNightModeConfiguration",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "endTime", temp);

    snprintf(temp, sizeof(temp), "%d", brightness);
    UpnpAddToActionResponse(out, "GetNightModeConfiguration",
                            CtrleeDeviceServiceType[PLUGIN_E_EVENT_SERVICE], "nightModeBrightness", temp);


    return UPNP_E_SUCCESS;
}

void* nightModeTimerThread(void *arg)
{
    int curState=0;

    tu_set_my_thread_name( __FUNCTION__ );
    APP_LOG("WiFiApp",LOG_DEBUG, "Night mode timer thread running");
    pluginUsleep(BRIGHTNESS_OVERRIDE_TIMER_VALUE*1000000);

    /*
    	If we reach here then brightness needs to be restored to night mode
    	default brightness but ensure night mode is still active
    */

    if(gNightModeActive && ((curState = GetCurBinaryState()) == POWER_OFF)) {
        APP_LOG("WiFiApp",LOG_DEBUG, "Setting night mode default brightness");
        setBrightness(gpsNightMode->brightness, false);
        UPnPInternalToggleUpdate(curState);
    }

    gNightModeTimerThread = INVALID_THREAD_HANDLE;
    APP_LOG("WiFiApp",LOG_DEBUG, "Night mode timer thread exiting");

    return NULL;
}

void start_night_mode_timer()
{
    int retVal=0;
    pthread_attr_t nightModeThread_attr;

    pthread_attr_init(&nightModeThread_attr);
    pthread_attr_setdetachstate(&nightModeThread_attr,PTHREAD_CREATE_DETACHED);
    retVal = pthread_create(&gNightModeTimerThread,&nightModeThread_attr,
                            (void*)&nightModeTimerThread, NULL);

    if(retVal < 0) {
        APP_LOG("WiFiApp",LOG_ERR, "Night mode timer thread not created");
        resetSystem();
    } else {
        APP_LOG("WiFiApp",LOG_DEBUG, "Night mode timer thread created successfully");
    }

    pthread_attr_destroy(&nightModeThread_attr);

}

void stop_night_mode_timer()
{
    if(gNightModeTimerThread != INVALID_THREAD_HANDLE) {
        pthread_cancel(gNightModeTimerThread);
        gNightModeTimerThread = INVALID_THREAD_HANDLE;
        APP_LOG("WiFiApp",LOG_DEBUG, "Night mode timer thread stopped");
    }
}

void stopNightMode(void)
{
    stop_night_mode_timer();

    if(gNightModeThread != INVALID_THREAD_HANDLE) {
        pthread_cancel(gNightModeThread);
        gNightModeThread = INVALID_THREAD_HANDLE;
        APP_LOG("WiFiApp",LOG_DEBUG, "Night mode thread stopped");
    }
}

#endif
