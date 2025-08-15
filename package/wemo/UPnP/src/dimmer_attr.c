/***************************************************************************
 *
 *
 * dimmer_attr.c
 *
 * Created by Belkin International, Software Engineering on MAY 05, 2016
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

#ifdef PRODUCT_WeMo_Dimmer

#include "utils.h"
#include "osUtils.h"
#include "dimmer_attr.h"
#include "gpio.h"
#include "rule.h"
#include "plugin_wasp.h"
#include "belkin_api.h"
#include "belkin_diag.h"

#ifndef __MIPSEL__   /* code for the simulation environment */
static pthread_t simulation_thread = INVALID_THREAD_HANDLE;
#endif
static pthread_t sleeptimer_thread = INVALID_THREAD_HANDLE;
static pthread_mutex_t gAttrNotifyLockDimmer;
int gLocalAttrSet = 0;
int gRemoteAttrSet = 0;
int g_brightness = 100;
char g_overTemp = 0;
/* factory default value of the fader as given in spec */
char g_fader[MAX_FADER_LENGTH];
bool g_faderRunning = false;
bool g_faderToTimer = false;

static pthread_t hushAnimation_thread = INVALID_THREAD_HANDLE;
bool g_bHushAnimation;

/* three colon separated values for hushParam, first is Enable/Disable for hush,
   second is the referenceUtc time and third is one of the 1H/1D/1W to hush the
   animation */
char g_hushAnimParam[SIZE_16B]="0:0:0";
#endif

#ifdef PRODUCT_WeMo_Dimmer
void initAttrNotifyLockDimmer(void)
{
    osUtilsCreateLock(&gAttrNotifyLockDimmer);
}

void lockAttrDimmer(void)
{
    osUtilsGetLock(&gAttrNotifyLockDimmer);
}

void unlockAttrDimmer(void)
{
    osUtilsReleaseLock(&gAttrNotifyLockDimmer);
}

/************************************************
Function: setAttFalgs
 ** flag =>  ATTR_STATE/ATTR_BRIGHTNESS/ATTR_FADER
 ** how =>   0 for setting local flag
             1 for setting remote flag
             2 for setting both local and remote
 ** reset => 0 to set the flag
             non zero to unset the flag
 ** returns FAILURE if invalid flag passed else
            SUCCESS
*************************************************/
int setAttrFlagDimmer(int flag, int how, int reset)
{
    if(flag != ATTR_STATE && flag != ATTR_BRIGHTNESS
       && flag != ATTR_FADER && flag != ATTR_OVERHEAT) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "Invalid attribute flag passed.");
        return FAILURE;
    }
    lockAttrDimmer();
    if(0 == how) {
        if(!reset)
            gLocalAttrSet|=flag;
        else
            gLocalAttrSet^=flag;
    } else if(1 == how) {
        if(!reset)
            gRemoteAttrSet|=flag;
        else
            gRemoteAttrSet^=flag;
    } else if(2 == how) {
        if(!reset) {
            gLocalAttrSet|=flag;
            gRemoteAttrSet|=flag;
        } else {
            gLocalAttrSet^=flag;
            gRemoteAttrSet^=flag;
        }
    } else {
        APP_LOG("UPNPDevice", LOG_DEBUG, "Invalid method to reset the attribute flags.");
        unlockAttrDimmer();
        return FAILURE;
    }
    unlockAttrDimmer();
    return SUCCESS;
}

#ifndef __MIPSEL__   /* code for the simulation environment */
void* SimulateTimer(void *arg)
{
    if(!arg)
        return NULL;
    unsigned int sleepTime = *(unsigned int*)arg;
    int ret;
    free(arg);
    APP_LOG("UPNPDevice", LOG_DEBUG, "Simulated time:%u", sleepTime);
    unsigned char curr=0;
    if(SUCCESS != getWaspVariable(WASP_VAR_CURRENT_LEVEL, WASP_VARTYPE_UINT8, &curr)) {
        APP_LOG("WiFiApp", LOG_ERR, "Fetching of WASP_VAR_CURRENT_LEVEL failed.");
        return NULL;
    }

    double tmp_1 = (double)(curr*100)/255;
    unsigned char curBrightness = ceil(tmp_1);
    float delta = (float)(curBrightness-1)/sleepTime;
    int i=0;
    float tmp = curBrightness;
    unsigned short faderRemainingTimeWasp = 0;

    /* iterate for one less than the sleepTime to set the
       target WASP_VAR_CURRENT_LEVEL and WASP_VAR_FADE_REMAINING
       in the end. */
    for(i=0; i<sleepTime-1; i++) {
        pluginUsleep(1000000);
        faderRemainingTimeWasp = sleepTime-i-1;
        setWaspVariable(WASP_VAR_FADE_REMAINING, WASP_VARTYPE_UINT16, (void*)&faderRemainingTimeWasp);

        tmp-=delta;
        curBrightness = ceil(tmp);
        brightnessWasp = (unsigned char)(curBrightness*255/100);
        setWaspVariable(WASP_VAR_CURRENT_LEVEL, WASP_VARTYPE_UINT8, (void*)&brightnessWasp);
    }
    pluginUsleep(1000000);

    /* set the WASP_VAR_FADE_REMAINING to 0 */
    faderRemainingTimeWasp = 0;
    setWaspVariable(WASP_VAR_FADE_REMAINING, WASP_VARTYPE_UINT16, (void*)&faderRemainingTimeWasp);
    /* set the WASP_VAR_CURRENT_BRIGHTNESS to the target level */
    curBrightness = 1;
    setWaspVariable(WASP_VAR_CURRENT_BRIGHTNESS, WASP_VARTYPE_UINT8, (void*)&curBrightness);

    g_faderRunning = false;
    simulation_thread = INVALID_THREAD_HANDLE;
    return NULL;
}
#endif

/**
 * upnpFaderNotify
 * - sends the fader change notification to local
     as well as remote apps.
 * - args:
 *    fadeTime:   fade/sleep time
 *    utcTime:    reference UTC time
 *    fadeEnable: fader enabled/disabled
 *    delta:      is the per second change in the brightness
 *                for the app to change the brightness slider accordingly
 *    brightness: is the starting brigntness from where the fade
                  out has started.
 ***************************************************/
void
upnpFaderNotify(unsigned int fadeTime, long int utcTime, bool fadeEnable,
                float delta, unsigned char brightness)
{
    char* parameters[] = {"Fader"};
    char* value[1];
    value[0] = (char*)MALLOC(SIZE_32B);
    /* The fade time in .05 second steps. */
    snprintf(value[0], SIZE_32B, "%u:%ld:%d:%f:%u", fadeTime, utcTime, fadeEnable, delta, brightness);
    APP_LOG("UPNP", LOG_DEBUG, "Notification:BinaryState:fader: %s", value[0]);
    lockAttrDimmer();
    memset(g_fader, 0, MAX_FADER_LENGTH);
    strncpy(g_fader, value[0], MAX_FADER_LENGTH-1);
    unlockAttrDimmer();

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)parameters, (const char **)value, 1);

    setAttrFlagDimmer(ATTR_FADER, 1, 0);
    free(value[0]);
}

/************************************************************************
 * Function: cancelSleepTimer
 *     Stop the sleep timer thread if running
 *  Parameters:
 *     None
 *  Return:
 *     Success if cancelled otherwise FAILURE
************************************************************************/

int cancelSleepTimer()
{
    if(INVALID_THREAD_HANDLE != sleeptimer_thread) {
        pthread_cancel(sleeptimer_thread);
        sleeptimer_thread = INVALID_THREAD_HANDLE;
        APP_LOG("WiFiApp", LOG_DEBUG, "Cancelling the sleep timer..");
        return SUCCESS;
    }
    return FAILURE;
}

/**
 * stopFader
 * - Function to stop the running fader/sleeptimer
 * - return:
 *      0: success
 *     -1: failure
 ***************************************************/
static int
stopFader(bool fadeEnable)
{
    WaspVariable fadeVar;
    memset(&fadeVar, 0, sizeof(fadeVar));
    int ret=0;

    if(fadeEnable) {
#ifndef __MIPSEL__   /* code for the simulation environment */
        if(INVALID_THREAD_HANDLE != simulation_thread) {
            pthread_cancel(simulation_thread);
            simulation_thread = INVALID_THREAD_HANDLE;
            APP_LOG("WiFiApp", LOG_DEBUG, "Cancelling the simulation timer..");

            unsigned short faderRemainingTimeWasp = (unsigned short)(0);
            setWaspVariable(WASP_VAR_FADE_REMAINING, WASP_VARTYPE_UINT16, (void*)&faderRemainingTimeWasp);
        }
#endif
        /* set WASP_VAR_STOP_FADE to notify the device to stop fading
           immediately */
        unsigned char fadeStop = (unsigned char)true;
        ret = setWaspVariable(WASP_VAR_STOP_FADE, WASP_VARTYPE_BOOL, (void*)&fadeStop);
        /* fader has stopped, set g_faderRunning to false */
        g_faderRunning = false;
        /* let new brightness be updated in g_brightness */
        sleep(1);
    } else {
        if(g_faderToTimer) {
            /* set the WASP_VAR_ON_OFF to true, to cancel the sleep timer thread running
               in DPR. As variable is already true, setting it to true wont have any other effect. */
            bool state = true;
            if(SUCCESS == (ret = setWaspVariable(WASP_VAR_ON_OFF, WASP_VARTYPE_BOOL, (void*)&state))) {
                APP_LOG("WiFiApp", LOG_DEBUG, "Cancelling the sleep timer thread in DPR..");
                g_faderToTimer = false;
            } else {
                APP_LOG("WiFiApp", LOG_DEBUG, "Failed to cancel the sleep timer thread in DPR!!");
            }
        } else {
            /* cancel the sleep timer thread if running. */
            cancelSleepTimer();
        }
    }
    return ret;
}

/**
 * checkIfFaderRunning
 * - Function to check if fader is running
 * - return:
 *      true: If running
 *      false: If not running
 ***************************************************/
bool
checkIfFaderRunning(void)
{
    bool ret = false;
    /* fader/sleeptimer is running */
    if(g_faderToTimer || g_faderRunning || sleeptimer_thread != INVALID_THREAD_HANDLE) {
        ret = true;
    }
    return ret;
}

/**
 * cancelFaderAndNotify
 *  Cancels the Fader and notifies to app.
 * - return:
 *      0: success
 *     -1: failure
 ***************************************************/
int
cancelFaderAndNotify(void)
{
    /* fader/sleeptimer is running, set fader to stop it */
    if(checkIfFaderRunning()) {
        unsigned int faderTimeSeconds;
        long int referenceTime;
        int enable;
        int toBrightness=0;
        float delta=0;
        char fader[MAX_FADER_LENGTH]= {0};
        getFader(fader);
        sscanf(fader, "%u:%ld:%d:%f:%d", &faderTimeSeconds, &referenceTime, &enable, &delta, &toBrightness);

        snprintf(fader, MAX_FADER_LENGTH, "%u:-1:%d:%f:%d", faderTimeSeconds, enable, delta, toBrightness);
        setFader(fader, true);
    }
    return SUCCESS;
}

/************************************************************************
 * Function: sendFaderStopNotification
 *    send notification to stop fader
 *  Parameters:
 *     stopTimer - if 1, notification to stop timer on app (OFF case)
 *                 if 0, notification to just cancel fade but
 *                 timer should keep running (brightness change case)
 *  Return:
 *     None
************************************************************************/


void sendFaderStopNotification(int stopTimer)
{
    unsigned int faderTimeSeconds=0;
    long int referenceTime=0;
    int enable=0;
    int toBrightness=0;
    float delta=0;
    char fader[MAX_FADER_LENGTH]= {0};

    getFader(fader);
    sscanf(fader, "%u:%ld:%d:%f:%d", &faderTimeSeconds, &referenceTime, &enable, &delta, &toBrightness);
    toBrightness = getBrightness();

    if(stopTimer) {
        /* check if the night mode is active, apply the night mode
           brightness */
        if(gNightModeActive && gpsNightMode) {
            toBrightness = gpsNightMode->brightness;
        }
        upnpFaderNotify(faderTimeSeconds, -1, enable, delta, toBrightness);
    } else
        upnpFaderNotify(faderTimeSeconds, referenceTime, 0, 0, toBrightness);
}

/**
 * setBrightness
 * - sets the dimmer brightness.
 * - args:
 *   brightness: value to set the brightness to
 *   fadeEnable: if the device has to fade to
 *               the brightness or not
 * - return:
 *      0: success
 *     -1: failure
 ***************************************************/
int
setBrightness(int brightness, bool fadeEnable)
{
    int ret = SUCCESS;
    lockAttrDimmer();
    if(g_brightness == brightness) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "plugin brightness %d%%, not changed", brightness);
        unlockAttrDimmer();
        return FAILURE;
    }
    if(brightness<0 || brightness>100 || (!brightness && !fadeEnable)) {
        APP_LOG("UPNPDevice", LOG_DEBUG, "plugin brightness %d%%, not changed. Input Out of Range", brightness);
        unlockAttrDimmer();
        return FAILURE;
    }
    unlockAttrDimmer();
    APP_LOG("UPNPDevice", LOG_DEBUG, "set plugin brightness to %d%%", brightness);

    /* set wasp to switch the device to the requested brightness */
    if(true == fadeEnable) {
        ret = setWaspVariable(WASP_VAR_TARGET_BRIGHTNESS, WASP_VARTYPE_UINT8, (void*)&brightness);
        /* set WASP_VAR_TARGET_BRIGHTNESS to start the device fading */
        APP_LOG("WiFiApp", LOG_DEBUG, "Setting of WASP_VAR_TARGET_BRIGHTNESS to brightness level %u.", brightness);
    } else {
        /* Running fader when brightness change request is received will be handled in pollTask */

        unsigned char varId = WASP_VAR_CURRENT_BRIGHTNESS;
        APP_LOG("WiFiApp", LOG_DEBUG, "Setting Brightness level to %u.", brightness);
        if(SUCCESS == (ret = setWaspVariable(varId, WASP_VARTYPE_UINT8, (void*)&brightness))) {
            setAttrFlagDimmer(ATTR_BRIGHTNESS, 2, 0);
            lockAttrDimmer();
            g_brightness = brightness;
            unlockAttrDimmer();
        }
    }
    return ret;
}

/**
 * - thread which takes care of running the sleep timer
     and change the device state when app has requested
     to run the timer with fade out disabled.
 ***************************************************/
void* RunSleepTimer(void *arg)
{
    if(!arg)
        return NULL;
    unsigned int sleepTime = *(unsigned int*)arg;
    free(arg);
    APP_LOG("UPNPDevice", LOG_DEBUG, "sleep time:%u", sleepTime);
    /* sleep for sleepTime seconds after which the device
       is to switch off */
    pluginUsleep(sleepTime*1000000);
    sleeptimer_thread = INVALID_THREAD_HANDLE;
    /* reset the animation active for the fader */
    setAnimation(LED_STATE_TURN_OFF);
    /* change state after sleep timer expires */
    ChangeBinaryState(POWER_OFF);
    pluginUsleep(500000);
    /* reset the animation active for the fader */
    setAnimation(LED_STATE_RULE_CLOSE);
    return NULL;
}

/**
 * setFader
 * - sets the device fader
 * - args:
 *    fader: string (faderTimeSeconds:referenceUTC:isFadeEnable)
 *    isNotificationRequired: notification to app is required or not
 * - return:
 *      0: success
 *     -1: failure
 **********************************************************************/
int
setFader(char* fader, bool isNotificationRequired)
{
    long int referenceTime;
    int fadeEnable=0;
    int ret = SUCCESS;
    int toBrightness=0;
    unsigned int faderTimeSeconds = 0;
    float delta = 0;
    unsigned char curBrightness = 0;

    APP_LOG("UPNPDevice", LOG_DEBUG, "set plugin fader to %s", fader);
    /* faderTimeSeconds is fader/sleeptimer in seconds, referenceTime is time of
       mobile device in UTC, fadeEnable is to indetify if fader is enabled/disabled */
    ret = sscanf(fader, "%u:%ld:%d:%f:%d", &faderTimeSeconds, &referenceTime, &fadeEnable, &delta, &toBrightness);

    if(ret != 5 || (toBrightness > 100)) {
        APP_LOG("UPNPDevice", LOG_ERR, "Invalid fader param value %s", fader);
        return FAILURE;
    } else
        ret=SUCCESS;

    if(-1 == referenceTime) {
        /* check if app wants to stop the timer */
        if(SUCCESS != (ret=stopFader(fadeEnable))) {
            return FAILURE;
        }
        /* reset the animation active for the fader */
        setAnimation(LED_STATE_RULE_CLOSE);
    } else {
        /* check if state is OFF, On it first and then
           run the fade out */
        if(POWER_OFF == GetCurBinaryState()) {
            if(!faderTimeSeconds) {
                APP_LOG("UPNPDevice", LOG_DEBUG, "set current brightness for zero fade time" );
                setBrightness(toBrightness, false);
            }
            ChangeBinaryState(POWER_ON);
            /* without this delay, bulb flickered and didnt turn ON properly */
            sleep(1);
        }

        /* Fader is given priority over count down timer rule.
           Hence stopping count down rule. */
        stopCountdownTimer();
        if(fadeEnable && faderTimeSeconds) {
            curBrightness = getBrightness();

            /* find out the delta to send to the app */
            delta = (float)(toBrightness-curBrightness)/faderTimeSeconds;

            /* set WASP_VAR_FADE_TIME to notify the device to start fading
               to the target brightness as soon as WASP_VAR_TARGET_LEVEL is set */
            unsigned short faderTimeWasp = (unsigned short)(faderTimeSeconds*20); /* The faderTimeWasp is in .05 second steps. */
            if(SUCCESS != setWaspVariable(WASP_VAR_FADE_TIME, WASP_VARTYPE_UINT16, (void*)&faderTimeWasp)) {
                return FAILURE;
            }
            /* set the g_faderRunning to true after the fader has started. */
            g_faderRunning = true;
            /* set the brightness to lowest to make the dimmer fade out */
            setBrightness(toBrightness, true);
#ifndef __MIPSEL__   /* code for the simulation environment */
            unsigned int *arg = malloc(sizeof(unsigned int));
            *arg = faderTimeSeconds;
            createDetachedThread(&simulation_thread, SimulateTimer, arg);
#endif
        } else if(faderTimeSeconds) {
            if(INVALID_THREAD_HANDLE != sleeptimer_thread) {
                APP_LOG("WiFiApp", LOG_DEBUG, "Another sleep timer is already in progress!!");
                return FAILURE;
            }
            unsigned int *arg = MALLOC(sizeof(unsigned int));
            *arg = faderTimeSeconds;
            createDetachedThread(&sleeptimer_thread, RunSleepTimer, arg);
        }
    }
    if(isNotificationRequired) {
        /* send notification with the reference time as current device time at which
           the fader has started. This is for app to know when the fader has been
           started on device to adjust the time accordingly on the app. */
        curBrightness = getBrightness();
        upnpFaderNotify(faderTimeSeconds, (-1 == referenceTime)?-1:GetUTCTime(), fadeEnable, delta, curBrightness);
    }
    return ret;
}

/**
 * getBrightnessr
 * - returns the dimmer brightness
 ***************************************************/
int
getBrightness(void)
{
    lockAttrDimmer();
    int brightness = g_brightness;
    unlockAttrDimmer();
    return brightness;
}

/**
 * getFader
 * - returns the dimmer fader value which was set last
 *****************************************************/
int
getFader(char *buffer)
{
    lockAttrDimmer();
    strncpy(buffer, g_fader, MAX_FADER_LENGTH-1);
    unlockAttrDimmer();
    return 0;
}

/************************************************************************
 * Function: getOverHeatState
 *    Used to fetch the OverHeat state of the device
 *  Parameters:
 *    N.A.
 *  Return:
 *    Over Heat state of the device
***********************************************************************/
int
getOverHeatState(void)
{
    return g_overTemp;
}

/************************************************************************
 * Function: blinkLights
 *    turn on and off switch multiple times in quick succession
 *  Parameters:
 *     blinkCount - number of times switch must be toggled
 *     dnd - do not disturb flag, if 1 and device is off, dont toggle
 *     onoffInterval - time in seconds between on and off
 *     blinkInterval - time in seconds between successive blinks
 *  Return:
 *     None
************************************************************************/

void blinkLights(int blinkCount, int dnd, int onoffInterval, int blinkInterval)
{
    int state = GetCurBinaryState();
    int i=0;

    if(!state && dnd) {
        APP_LOG("UPNPDevice", LOG_ERR, "Not blinking as DND is set and state is OFF");
        return;
    }

    for(i=0; i< blinkCount; i++) {
        APP_LOG("UPNPDevice", LOG_ERR, "state: %d, iteration: %d", state, i+1);
        ChangeBinaryState(!state);
        pluginUsleep(onoffInterval*1000000);

        state = GetCurBinaryState();
        APP_LOG("UPNPDevice", LOG_ERR, "state: %d", state);
        ChangeBinaryState(!state);

        pluginUsleep(blinkInterval*1000000);
        state = GetCurBinaryState();
    }
}

/************************************************************************
 * Function: checkAndStartHushMode
 *    check if the hush is active but not running. start it in that case
 *  Parameters:
 *     N.A.
 *  Return:
 *     None
************************************************************************/

void
checkAndStartHushMode(void)
{
    /* reusing lock to avoid any possible race on reboot of
       the device. */
    lockAttrDimmer();
    /* check if the hush mode is active but the thread is not
       running due to time not sync */
    if(!g_bHushAnimation) {
        /* If the HUSH_ANIMATION_END_TIME is set, the hush mode was active.
           restore that */
        char *tmp = GetBelkinParameter(HUSH_ANIMATION_END_TIME);
        if(tmp && strlen(tmp) > 0) {
            /* selectedSuspendedOption is not needed in this case as the
               time will be based on already saved endTime */
            startHushMode(ACTIVE, 0);
        } else {
            APP_LOG("WiFiApp",LOG_DEBUG, "Hush Mode is not active.");
        }
    }
    unlockAttrDimmer();
}

static void
upnpHushNotify(void)
{
    char* parameters[] = {"hushMode"};
    char *value[1];
    value[0] = g_hushAnimParam;
    APP_LOG("UPNP", LOG_DEBUG, "Notification:hushMode %s", value[0]);

    UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)parameters, (const char **)value, 1);
}

void*
hushAnimationTimer(void* args)
{
    if(!args)
        return NULL;
    unsigned int timer = *(unsigned int*)args;
    free(args);
    APP_LOG("UPNPDevice", LOG_DEBUG, "hush animation for %u seconds", timer);
    sleep(timer);
    /* after the time has elapsed, dont hush any animation */
    g_bHushAnimation = false;
    UnSetBelkinParameter(HUSH_ANIMATION_END_TIME);
    strncpy(g_hushAnimParam, "0:0:0", sizeof(g_hushAnimParam)-1);
    errorAnimRes = true;
    upnpHushNotify();

    hushAnimation_thread = INVALID_THREAD_HANDLE;
    return NULL;
}

/************************************************************************
 * Function: startHushMode
 *    start/stop the hush mode
 *  Parameters:
 *     mode - 1 to activate, 0 to deactivate
 *     selectedSuspendedOption - 1 for 1Hr, 2 or 1Day, 3 for 1Week
 *  Return:
 *     Success or Failure
************************************************************************/

int
startHushMode(int mode, int selectedSuspendedOption)
{
    unsigned long referenceUtc = 0;
    unsigned int timeToSuspend = 0;
    //char referenceUtcStr[SIZE_16B] = {'\0'};
    char *tmp = NULL;
    unsigned int *arg = NULL;
    switch( mode) { /* 1 for ACTIVE, 0 for INACTIVE */
    case ACTIVE: {
        /* If the hush is alreadt active, return failure */
        if(g_bHushAnimation) {
            APP_LOG("UPNPDevice", LOG_ERR, "Hush Animation Mode is already active. Deactivate it first!!");
            return FAILURE;
        }
        tmp = GetBelkinParameter(HUSH_ANIMATION_END_TIME);
        if(tmp && strlen(tmp) > 0) {
            sscanf(tmp, "%d:%lu:%d", &mode, &referenceUtc, &selectedSuspendedOption);
            snprintf(g_hushAnimParam, sizeof(g_hushAnimParam), "%d:%lu:%d", mode, referenceUtc, selectedSuspendedOption);
        } else {
            switch (selectedSuspendedOption) {
            case ONE_HOUR: /* ON for 1 hrs */
                timeToSuspend = 60*60;
                break;
            case ONE_DAY: /* ON for 1 day */
                timeToSuspend = 24*60*60;
                break;
            case ONE_WEEK: /* ON for 1 week */
                timeToSuspend = 7*24*60*60;
                break;
            default: /* invalid option */
                APP_LOG("UPNPDevice", LOG_ERR, "Unrecognized suspend option.");
                return FAILURE;
            }
            referenceUtc = GetUTCTime() + timeToSuspend;
            //snprintf(referenceUtcStr, sizeof(referenceUtcStr), "%lu", referenceUtc);
            snprintf(g_hushAnimParam, sizeof(g_hushAnimParam), "%d:%lu:%d", mode, referenceUtc, selectedSuspendedOption);
            SetBelkinParameter(HUSH_ANIMATION_END_TIME, g_hushAnimParam);
        }
        g_bHushAnimation = true;
        /* check if there is already an error animation being played.
           If yes, hush it. */
        hushAnimationIfActive();
        arg = MALLOC(sizeof(unsigned int));
        *arg = referenceUtc - GetUTCTime();
        createDetachedThread(&hushAnimation_thread, hushAnimationTimer, arg);
        break;
    }
    case INACTIVE: {
        /* clean the Nvram data */
        if(hushAnimation_thread != INVALID_THREAD_HANDLE) {
            pthread_cancel(hushAnimation_thread);
            hushAnimation_thread = INVALID_THREAD_HANDLE;
        }
        UnSetBelkinParameter(HUSH_ANIMATION_END_TIME);
        g_bHushAnimation = false;
        strncpy(g_hushAnimParam, "0:0:0", sizeof(g_hushAnimParam)-1);
        errorAnimRes = true;
        break;
    }
    default: /* invalid option */
        APP_LOG("UPNPDevice", LOG_ERR, "Unrecognized mode option.");
        return FAILURE;
    }
    /* notify to local as well as remote */
    upnpHushNotify();

    return SUCCESS;
}
#endif
