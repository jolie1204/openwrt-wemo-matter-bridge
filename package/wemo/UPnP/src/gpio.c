/***************************************************************************
*
*
* gpio.c
*
* Created by Belkin International, Software Engineering on Jun 14, 2011
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
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>

#include "utils.h"
#include "gpio.h"
#include "rule.h"
#include "itc.h"
#include "controlledevice.h"
#include "belkin_api.h"

#ifdef SIMULATED_OCCUPANCY
#include "simulatedOccupancy.h"
#endif
#include "rule.h"
#include "dimmer_attr.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

#ifdef SIMULATED_OCCUPANCY
extern unsigned int gSimDevSelfOnTS;
extern int gSimulatedRuleRunning;
extern int gSimManualTrigger;
#endif

extern SRuleHandle gRuleHandle[e_MAX_RULE];

#if defined(PRODUCT_WeMo_Insight)
const char *szButtonPath = "/proc/POWER_BUTTON";
char *szNewSensorPath   = "/proc/MOTION_SENSOR_STATUS";
#define	 MOTION_SENSORED 0x01
#elif defined NEW_BOARD

const char* szButtonPath = "/proc/GPIO12";
char* szNewSensorPath   = "/proc/MOTION_SENSOR_STATUS";
#define		MOTION_SENSORED 0x01

#else
const char* szNewSensorPath = "/proc/GPIO13";

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_Light)
#define AUTOOFF_RESTART "/tmp/ButtonPressedWhenInLastMinOfAutoOff"
const char* szButtonPath = "/sys/class/gpio/gpio19/value";
const char* szNTCPath = "/sys/class/gpio/gpio18/value";
#else
const char* szButtonPath = "/proc/GPIO13";
#endif

#define		MOTION_SENSORED 	0x00


#endif
#ifdef PRODUCT_WeMo_Dimmer
const char* szNTCPath = "/sys/class/gpio/gpio18/value";
const char* szResetBottonPath = "/sys/class/gpio/gpio38/value";
#define DEVICE_RESET 1
#define NETWORK_RESET 2
#define FACTORY_RESET 3
#define HUSH_ANIM      4

#define HUSH_ANIMATION_LOOP_CNT	20
#define NO_ACTION_LOOP_CNT	40
#define RESET_DEVICE_LOOP_CNT	120
#define RESET_NETWORK_LOOP_CNT	220
#define RESET_FACTORY_LOOP_CNT  300

//thread will sleep for 50000 microseconds, a loop of 100 will produce delay for 5 seconds
#elif defined(PRODUCT_WeMo_Light) || defined(PRODUCT_WeMo_SNS)
#define RESET_NETWORK_LOOP_CNT	100
const char* szResetBottonPath = "/proc/GPIO10";

#elif defined(PRODUCT_WeMo_Insight)
#define RESET_NETWORK_LOOP_CNT	100
const char* szResetBottonPath = "/proc/GPIO17";
#endif

#if defined(PRODUCT_WeMo_Light)
int u32DimVal=0;
#endif
int g_PowerStatus  = POWER_OFF;

int g_SensorStatus = 0x00;

#define MIN_HUMAN_BEING_ACTION  3 //- seconds
#define IVALID_THREAD_HANDLE -1

static  pthread_t led_thread = IVALID_THREAD_HANDLE; //-Led thread if OnDuration and OffDuration normal (not 0xFF)

int g_cntSensorDelay = DEFAULT_SENSOR_DELAY;
int g_cntSensitivity = DEFAULT_SENSOR_SENSITIVITY;
int gButtonHealthPunch = 0;
int gSensorHealthPunch = 0;

#if !defined(PRODUCT_WeMo_SNSV2)
static int g_isButtonPressed = 0x00;
#endif
/**! track the time stamp of last message to prevent message traffic within UPnP and etc */
#define	MAX_SENSOR_MSG_TIMEOUT	DELAY_20SEC	// 20 seconds
static unsigned long	s_lLastSensoringUpdateTime 		= 0x00;
static unsigned long	s_lLastNoSensoringUpdateTime 	= 0x00;

#if defined(PRODUCT_WeMo_Insight)
#define RELAY_GPIO	"/proc/POWER_RELAY"
#define RELAY_ON		"1"
#define RELAY_OFF		"0"
#elif defined NEW_BOARD
#define		RELAY_GPIO			"/proc/GPIO13"
#define		RELAY_COMMAND_ON	"echo 1 > /proc/GPIO13 &"
#define		RELAY_COMMAND_OFF	"echo 0 > /proc/GPIO13 &"
#define		RELAY_ON			"1"
#define		RELAY_OFF			"0"
#elif defined(PRODUCT_WeMo_SNSV2)
#define		RELAY_GPIO			"/sys/class/gpio/gpio19/value"
#define		RELAY_COMMAND_ON	"echo 1 > /sys/class/gpio/gpio19/value &"
#define		RELAY_COMMAND_OFF	"echo 0 > /sys/class/gpio/gpio19/value &"
#define		RELAY_ON			"1"
#define		RELAY_OFF			"0"
#else
#define		RELAY_COMMAND_ON	"echo 0 > /proc/GPIO25 &"
#define		RELAY_COMMAND_OFF	"echo 1 > /proc/GPIO25 &"
#define		RELAY_GPIO			"/proc/GPIO25"
#define		RELAY_ON			"0"
#define		RELAY_OFF			"1"

#endif


#if defined(PRODUCT_WeMo_Insight)
#define		LED_GPIO					"/proc/RELAY_LED"
#define		RELAY_LED_ON			"0"
#define		RELAY_LED_OFF			"1"
#elif defined NEW_BOARD
#define		INDICATOR_LED_COMMAND_ON	"echo 0 > /proc/GPIO9 &"
#define		INDICATOR_LED_COMMAND_OFF	"echo 1 > /proc/GPIO9 &"
#define		LED_GPIO		"/proc/GPIO9"
#define		RELAY_LED_ON			"0"
#define		RELAY_LED_OFF			"1"
#elif defined(PRODUCT_WeMo_SNSV2)
#define		INDICATOR_LED_COMMAND_ON	"echo 0 > /sys/class/gpio/gpio17/value &"
#define		INDICATOR_LED_COMMAND_OFF	"echo 1 > /sys/class/gpio/gpio17/value &"
#define		LED_GPIO		"/sys/class/gpio/gpio17/value"
#define		RELAY_LED_ON			"0"
#define		RELAY_LED_OFF			"1"
#else
//-EVB, port different
#define		INDICATOR_LED_COMMAND_ON	"echo 0 > /proc/GPIO26 &"
#define		INDICATOR_LED_COMMAND_OFF	"echo 1 > /proc/GPIO26 &"
#define 	LED_GPIO		"/proc/GPIO26"
#define 	RELAY_LED_ON			"0"
#define 	RELAY_LED_OFF 			"1"

#endif

#if defined(PRODUCT_WeMo_Light)
#define 	NIGHTLIGHT_LED_ON			"0"
#define 	NIGHTLIGHT_LED_OFF 			"1"
#define		NIGHT_LED_GPIO				"/proc/GPIO14"
#endif

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
char g_overTemp = 0;
unsigned long int *gp_overTempTS = NULL;
#endif

extern int g_eDeviceType;
extern int g_phoneFlag;
extern int g_timerFlag;

pthread_t power_thread = -1;
pthread_t relay_thread = -1;
pthread_t ButtonTaskMonitor_thread = -1;
pthread_t SensorTaskMonitor_thread = -1;

pthread_t sensor_thread = -1;

static pthread_mutex_t   s_led_mutex;
static pthread_mutex_t   s_sensor_mutex;

int g_isInsightRuleActivated = 0x00;

int g_IsLastUserActionOn = 0x00;

#define BUTTON_MONITOR_TIMEOUT    5	//secs
#define SENSOR_MONITOR_TIMEOUT    5	//secs

#if defined(LONG_PRESS_SUPPORTED)
#define LONG_PRESS_LOOP_CNT	40  //2secs
int gLongPressEnabled = 0x01;  //enabled by default
int gButtonLongPressed = 0x00;
int gLongPressTriggered = 0x00;
int gSimulatedLongPress=0;
pthread_mutex_t gLongPressLock;

void initLongPressLock()
{
    osUtilsCreateLock(&gLongPressLock);
}

void LockLongPress()
{
    osUtilsGetLock(&gLongPressLock);
}

void UnlockLongPress()
{
    osUtilsReleaseLock(&gLongPressLock);
}
#endif

void initLED()
{
    ithread_mutexattr_t attr;
    ithread_mutexattr_init(&attr);
    ithread_mutexattr_setkind_np( &attr, ITHREAD_MUTEX_RECURSIVE_NP );
    pthread_mutex_init(&s_led_mutex, &attr);
    ithread_mutexattr_destroy(&attr);
}

void LockLED()
{
    pthread_mutex_lock(&s_led_mutex);
}

void UnlockLED()
{
    pthread_mutex_unlock(&s_led_mutex);
}


void LockSensor()
{
    pthread_mutex_lock(&s_sensor_mutex);

}

void UnlockSensor()
{
    pthread_mutex_unlock(&s_sensor_mutex);
}

void ToggleUpdate(int curState)
{
#ifdef PRODUCT_WeMo_Dimmer
    setAttrFlagDimmer(ATTR_STATE, 2, 0);
#endif
    LocalBinaryStateNotify(curState);

    LocalUserActionNotify(curState + 2);
}

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
/************************************************************************
 * Function: removeHotplugButtonDriver
 *    Remove Hotplug Button Driver from kernel
 *  Parameters:
 *    None
 *  Return:
 *    None
***********************************************************************/
void removeHotplugButtonDriver()
{
    system("rmmod gpio_button_hotplug");
}

/************************************************************************
 * Function: insertHotplugButtonDriver
 *    Inserts Hotplug Button Driver into kernel
 *  Parameters:
 *    None
 *  Return:
 *    None
***********************************************************************/
void insertHotplugButtonDriver()
{
    system("modprobe gpio-button-hotplug");
}

/************************************************************************
 * Function: getOverHeatState
 *    Used to fetch the OverHeat state of the device
 *  Parameters:
 *    None
 *  Return:
 *    Over Heat state of the device
***********************************************************************/
int getOverHeatState(void)
{
    return (g_overTemp & OVERTEMP_STATE);
}

/************************************************************************
 * Function: isOverHeatStateChange
 *    Used to fetch the OverHeat state of the device
 *  Parameters:
 *    None
 *  Return:
 *    Retruns non-zero if cloud update for overheat is pending, zero otherwise
***********************************************************************/
int isOverHeatStateChange (void)
{
    return (g_overTemp & OVERTEMP_PENDING);
}

/************************************************************************
 * Function: unsetOverHeatPendingState
 *    Used to unset the OverHeat state after sending to cloud
 *  Parameters:
 *    None
 *  Return:
 *    None
***********************************************************************/
void unsetOverHeatPendingState(void)
{
    g_overTemp &= (~OVERTEMP_PENDING);
    APP_LOG ("OverHeat", LOG_DEBUG, "overtemp state to %d", g_overTemp);
}

/************************************************************************
 * Function: updateOverHeatTSFromFile
 *    Get TS info of overtemp from file, if required
 *  Parameters:
 *    None
 *  Return:
 *    None
***********************************************************************/
void updateOverHeatTSFromFile()
{
    int len = MAX_OVERTEMP_TS_IN_24HRS*sizeof(unsigned long int);
    if(gp_overTempTS == NULL) {
        gp_overTempTS = ZALLOC(MAX_OVERTEMP_TS_IN_24HRS*sizeof(unsigned long int));
        readFileToBuffer((char *)gp_overTempTS, OVERTEMP_TS_FILE, &len);
    }
}

/************************************************************************
 * Function: isOverHeat2ndWarning()
 *    Used to know if overheat is more then required number of times
 *    in last 24 Hours
 *  Parameters:
 *    None
 *  Return:
 *    TRUE if overheat is more then required number of times in last
 *    24 Hours, FALSE otherwise
***********************************************************************/
int isOverHeat2ndWarning()
{
    updateOverHeatTSFromFile();
    APP_LOG ("OverHeat", LOG_DEBUG, "TS[0] %lu TS[4] %lu,diff %lu", gp_overTempTS[0],gp_overTempTS[MAX_OVERTEMP_TS_IN_24HRS-1],(gp_overTempTS[MAX_OVERTEMP_TS_IN_24HRS-1]-gp_overTempTS[0]));
    if (gp_overTempTS && gp_overTempTS[MAX_OVERTEMP_TS_IN_24HRS-1] && ((gp_overTempTS[MAX_OVERTEMP_TS_IN_24HRS-1]-gp_overTempTS[0]) <= ONE_DAY_SECONDS)) {
        return true;
    }
    return false;
}

/************************************************************************
 * Function: setOverHeatState
 *    Used to set the OverHeat state of the device
 *    and maintain last 5 TS values for 2nd Warning
 *  Parameters:
 *    Over Heat state of the device
 *  Return:
 *    None
***********************************************************************/
void setOverHeatState(int overHeatState)
{
    int len = MAX_OVERTEMP_TS_IN_24HRS*sizeof(unsigned long int);
    if((g_overTemp & OVERTEMP_STATE)!=overHeatState) {
        int i=0;
        if (overHeatState == 1) {
            /* change led blinking as per over temp */
#if defined(PRODUCT_WeMo_LightV2)
            SetWiFiLED(RGB_OVERHEAT);
#else
            SetWiFiLED(7);
#endif
            g_overTemp = g_overTemp | overHeatState;
            setPower(POWER_OFF);
            /* make button inoperable */
            removeHotplugButtonDriver();
            /* Read TS info from file in case of wemoApp Crash */
            updateOverHeatTSFromFile();
            if(gp_overTempTS[MAX_OVERTEMP_TS_IN_24HRS-1]==0) {
                /* search and fill at empty slot */
                for (i=0; i<MAX_OVERTEMP_TS_IN_24HRS; i++) {
                    /* Update new TS on empty slot */
                    if(gp_overTempTS[i] == 0) {
                        gp_overTempTS[i] = GetUTCTime();
                        APP_LOG ("OverHeat", LOG_DEBUG, "TS[%d] %lu",i, gp_overTempTS[i]);
                        break;
                    } else {
                        APP_LOG ("OverHeat", LOG_DEBUG, "TS[%d] %lu",i, gp_overTempTS[i]);
                    }
                }
            } else {
                /* Shift Time Slots as no empty slot left */
                for (i=0; i<MAX_OVERTEMP_TS_IN_24HRS-1; i++) {
                    gp_overTempTS[i] = gp_overTempTS[i+1];
                    APP_LOG ("OverHeat", LOG_DEBUG, "TS[%d] %lu",i, gp_overTempTS[i]);
                }
                gp_overTempTS[MAX_OVERTEMP_TS_IN_24HRS-1] = GetUTCTime();
                APP_LOG ("OverHeat", LOG_DEBUG, "TS[%d] %lu",MAX_OVERTEMP_TS_IN_24HRS-1, gp_overTempTS[MAX_OVERTEMP_TS_IN_24HRS-1]);
            }
            writeBufferToFile((char *)gp_overTempTS, OVERTEMP_TS_FILE, &len);
        } else {
            /* go back to default led state after over temp reset */
#if defined(PRODUCT_WeMo_LightV2)
            SetWiFiLED(RGB_SWITCH_OFF);
#else
            SetWiFiLED(4);
#endif
            /* make button operable again */
            insertHotplugButtonDriver();
        }
        g_overTemp = overHeatState|OVERTEMP_PENDING;
        if((overHeatState ==1) && (isOverHeat2ndWarning() == true)) {
            overHeatState = 2;
        }
        /* send local notification */
        pMessage msg = createMessage(META_OVERTEMP_STATE, (void *)&overHeatState, sizeof(int));
        SendMessage2App(msg);
    }
}
#endif


void InternalLocalUpdate(int curState)
{
    pMessage msg = 0x00;

    if (0x00 == curState) {
        msg = createMessage(LOCAL_MESSAGE_OFF_IND, 0x00, 0x00);
    } else if (0x01 == curState) {
        msg = createMessage(LOCAL_MESSAGE_ON_IND, 0x00, 0x00);
    }

    SendMessage(PLUGIN_E_RELAY_THREAD, msg);
}

void InternalToggleUpdate(int curState)
{
    pMessage msg = 0x00;

    if (0x00 == curState) {
        msg = createMessage(BTN_MESSAGE_OFF_IND, 0x00, 0x00);
    } else if (0x01 == curState) {
        msg = createMessage(BTN_MESSAGE_ON_IND, 0x00, 0x00);
    }
    SendMessage(PLUGIN_E_RELAY_THREAD, msg);
}

extern void StopPowerMonitorTimer();
void togglePower()
{
    int curState = 0x00;
    APP_LOG ("Button", LOG_DEBUG, "######### g_PowerStatus ON/OFF VALUE : %d FROM BUTTON TASK", g_PowerStatus);
    /*check if it was running in last minute, if yes do not toggle relay, countdown timer restarted*/
    if(checkAndExecuteCountdownTimer(!g_PowerStatus))
        return;

    LockLED();
    g_PowerStatus = !g_PowerStatus;
    curState = g_PowerStatus;
    SetLastUserActionOnState(curState);
    UnlockLED();
#if !defined(PRODUCT_WeMo_Insight) && !defined(PRODUCT_WeMo_SNSV2)
    setPower(curState);
#endif
#ifdef PRODUCT_WeMo_Insight
    int tempInsightState = curState;
    if(g_StateLog)
        APP_LOG("Button", LOG_ALERT, "######### RELAY ON/OFF VALUE : %d FROM BUTTON TASK",curState);
    if(curState == POWER_ON) {
        LockLED();
        curState=POWER_SBY;
        g_PowerStatus = POWER_SBY;
        UnlockLED();
    }

    /* check if it was running in last minute, if yes do not toggle relay, countdown timer restarted*/
    if (checkAndExecuteCountdownTimer (g_PowerStatus)) {}
    else {
        APP_LOG ("Button", LOG_DEBUG, "&&&&& tempInsightState = [%d]",
                 tempInsightState);
        setPower (tempInsightState);
        InternalToggleUpdate (tempInsightState);
    }
#endif
#ifndef PRODUCT_WeMo_Insight
    InternalToggleUpdate(curState);
#endif
}


void *LedAutoToggleLoop(void *args)
{
    static int counter = 0x00;

    while (1) {
        sleep(DELAY_5SEC);
        APP_LOG("Button",LOG_DEBUG, "Automatically toggle power");
        counter++;
        togglePower();

        APP_LOG("Button", LOG_DEBUG, "########## Try %d ON/OFF ############", counter);

        if (counter > 5000)
            return NULL;
    }
}

#if !defined(GROUND_TRUTH) && !defined(PRODUCT_WeMo_Dimmer)
void *PowerButtonTask(void *args)
{
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    struct stat buf;
    int ret;
#endif
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("Button", LOG_DEBUG, "##### button task running: %s ##############", szButtonPath);
#if defined(LONG_PRESS_SUPPORTED)
    int CounterForLongPress = 0;
#endif

    while (1) {
        FILE * pButtonFile = 0x00;
        int isToggled = 0x00;
        gButtonHealthPunch++;
#ifdef __MIPSEL__
        pButtonFile = fopen(szButtonPath, "r");
        if (pButtonFile == 0x00) {
            APP_LOG("Socket Button:", LOG_DEBUG, "####### open sensor: %s error", szButtonPath);
            return 0x00;
        }
#else
        pButtonFile = fopen("/tmp/button", "r");
        if (pButtonFile == 0x00) {
            system("echo -n 1 > /tmp/button");
            pButtonFile = fopen("/tmp/button", "r");
            if (pButtonFile == 0x00) {
                APP_LOG("Socket Button:", LOG_DEBUG, "X86 socket file not working");
                return 0x00;
            }
        }
#endif
        char szflag[SIZE_4B];
        memset(szflag, 0x00, sizeof(szflag));

#if defined(PRODUCT_WeMo_SNSV2)
        LockLED();
#endif
        char* pResult = fgets(szflag, sizeof(szflag), pButtonFile);

        if (pResult != 0x00) {
            int command = BUTTON_RELEASED;

            if (0x0 != strlen(szflag))
                command = atoi(szflag);
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
            /* command is actually the power state for SNSv2 */
            /* since we're monitoring relay */
            if (command != g_PowerStatus) {
                isToggled = 1;
            } else {
                isToggled = 0;
            }
#else // #ifdef PRODUCT_WeMo_SNSV2

            if (GPIO_BUTTON_PRESSED == command) {
#ifndef __MIPSEL__
                system("echo 1 > /tmp/button");
#endif
#ifdef PRODUCT_WeMo_Insight
                if (g_isButtonPressed) {
                    //released
                    g_isButtonPressed = 0x00;
                    isToggled = 0x01;
                }
#else
                g_isButtonPressed = 0x01;
#if defined(LONG_PRESS_SUPPORTED)
                if(CounterForLongPress == 0) {
                    APP_LOG("APP", LOG_DEBUG, "Button pressed");
                }
                if(!gSimulatedLongPress)
                    CounterForLongPress += 1;
                else
                    CounterForLongPress = LONG_PRESS_LOOP_CNT;

                if (gLongPressEnabled && (LONG_PRESS_LOOP_CNT == CounterForLongPress)) { //Long Press for 2.0 seconds detected
                    APP_LOG("Button",LOG_DEBUG,"Long Press for 2.0 seconds detected... CounterForLongPress: %d", CounterForLongPress);
                    APP_LOG("APP", LOG_DEBUG, "-------->>>>> POWER_ON: ACTIVITY LED STATE 0 -------->>>>>");

                    SetActivityLED(0x00);
                    LockLongPress();
                    gButtonLongPressed = 0x01;
                    gLongPressTriggered = 0x01;
                    UnlockLongPress();

                    /* gLongPressTriggered is cleared after sending remote notification, handle long press rules here */
                    if(gLongPressRuleActive) {
                        handleLongPressRule();
                    }

                    gSimulatedLongPress=0;

                    APP_LOG("Button",LOG_DEBUG,"set gButtonLongPressed: %d and gLongPressTriggered: %d", gButtonLongPressed, gLongPressTriggered);
                }
#endif
#endif
            } else {
#ifdef PRODUCT_WeMo_Insight
                g_isButtonPressed = 0x01;
#else
                if (g_isButtonPressed) {
#if defined(LONG_PRESS_SUPPORTED)
                    CounterForLongPress = 0;
                    if(gLongPressEnabled && gButtonLongPressed) {
                        //released
                        g_isButtonPressed = 0x00;
                        APP_LOG("APP", LOG_DEBUG, "-------->>>>> POWER_ON: ACTIVITY LED STATE 1 -------->>>>>");
                        SetActivityLED(0x01);
                        if(g_PowerStatus) {
                            usleep(50000 * 26);	//1.3 secs
                            system("echo 0 > /proc/GPIO9"); //to keep LED ON, if relay state is ON
                        }
                        LockLongPress();
                        gButtonLongPressed = 0x00;
                        UnlockLongPress();
                        APP_LOG("Button",LOG_DEBUG,"set gButtonLongPressed to: %d", gButtonLongPressed);
                    } else {
                        //released
                        g_isButtonPressed = 0x00;
                        isToggled = 0x01;
                    }
#else
                    //released
                    g_isButtonPressed = 0x00;
                    isToggled = 0x01;
#endif
                }
#endif
            }
#endif // #ifdef PRODUCT_WeMo_SNSV2
        }
#ifdef PRODUCT_WeMo_SNSV2
        UnlockLED();
#endif
        fclose(pButtonFile);
        if (isToggled) {
            setActuation(ACTUATION_MANUAL_DEVICE);

#ifdef PRODUCT_WeMo_Dimmer
            if(gNightModeActive && (g_PowerStatus == POWER_OFF)) {
                /* Device being turned ON and stop night mode timer and apply modified brightness, if any */
                stop_night_mode_timer();
            }
#endif
            togglePower();

#ifndef __MIPSEL__
            if (g_PowerStatus) {
                system("echo On > /tmp/buttonStatus");
            } else {
                system("echo Off > /tmp/buttonStatus");
            }
#endif

        }
#if defined(PRODUCT_WeMo_SNSV2)
        ret = stat(AUTOOFF_RESTART, &buf);
        if(ret == SUCCESS) {
            unlink(AUTOOFF_RESTART);
            executeCountdownRule(0);
        }
#endif
        /* Increase the original 50ms delay to 500ms, since we're monitoring relay gpio. */
        /* 50ms is too short for wemoApp to properly update g_PowerStatus */
        /* and raising false event */
        usleep(500000);
        isToggled = 0x00;
    }

}

void *ButtonTaskMonitorThread(void *arg)
{
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("ButtonMonitor",LOG_CRIT,"ButtonTaskMonitorThread  started...");

    while(1) {
        pluginUsleep(BUTTON_MONITOR_TIMEOUT * 1000000);
        /* WEMO-47850:exit the thread to avoid reset during firmware update */
        if(IS_FIRMWARE_FLASHING) {
            APP_LOG("WiFiApp", LOG_DEBUG, "ButtonTaskMonitor Thread exiting....");
            pthread_exit(NULL);
        }
        if(gButtonHealthPunch == 0) {
            APP_LOG("ButtonMonitor",LOG_CRIT,"ButtonTaskMonitorThread detected bad health so resetSystem ...");
            resetSystem();
        } else {
            //APP_LOG("ButtonMonitor",LOG_DEBUG,"Button Task thread health OK [%d]...", gButtonHealthPunch);
            gButtonHealthPunch = 0;
        }
    }
    return NULL;
}
#endif

void *SensorTaskMonitorThread(void *arg)
{
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("SensorMonitor",LOG_CRIT,"SensorTaskMonitorThread  started...");

    while(1) {
        pluginUsleep(SENSOR_MONITOR_TIMEOUT * 1000000);
        /* WEMO-47850:exit the thread to avoid reset during firmware update */
        if(IS_FIRMWARE_FLASHING) {
            APP_LOG("WiFiApp", LOG_DEBUG, "SensorGPIOTaskMonitor Thread exiting....");
            pthread_exit(NULL);
        }
        if(gSensorHealthPunch == 0) {
            APP_LOG("SensorMonitor",LOG_CRIT,"SensorGPIOTaskMonitorThread detected bad health ...");
            resetSystem();
        } else {
            //APP_LOG("SensorMonitor",LOG_DEBUG,"Sensor Task thread health OK [%d]...", gSensorHealthPunch);
            gSensorHealthPunch = 0;
        }
    }
    return NULL;
}



void AsyncUPnPNotify(int msgID)
{
    pMessage msg = 0x00;

    msg = createMessage(msgID, 0x00, 0x00);

    SendMessage2App(msg);
}

int ProcessRelayEvent(pNode node)
{
    if (0x00 == node)
        return 0x01;

    if (0x00 == node->message)
        return 0x01;

    switch(node->message->ID) {

    case BTN_MESSAGE_ON_IND:
        APP_LOG("ITC:LED", LOG_DEBUG, "BTN_MESSAGE_ON_IND");
        ToggleUpdate(0x01);
#ifdef SIMULATED_OCCUPANCY
        if(LONG_PRESS_AWAY_ACTIVE ||
           ((gRuleHandle[e_AWAY_RULE].ruleCnt && gpSimulatedDevice && gpSimulatedDevice->ruleEndTime)
            && (DEVICE_INSIGHT != g_eDeviceTypeTemp))) {
            APP_LOG("ITC:LED", LOG_DEBUG, "simulated rule: manual toggle, BTN_MESSAGE_ON_IND");
            notifyManualToggle();
        }
#endif
        break;
    case BTN_MESSAGE_OFF_IND:
        APP_LOG("ITC:LED", LOG_DEBUG, "BTN_MESSAGE_OFF_IND");
        ToggleUpdate(0x00);
#ifdef SIMULATED_OCCUPANCY
        if(LONG_PRESS_AWAY_ACTIVE ||
           (gRuleHandle[e_AWAY_RULE].ruleCnt && (gpSimulatedDevice && gpSimulatedDevice->ruleEndTime))) {
            APP_LOG("ITC:LED", LOG_DEBUG, "simulated rule: manual toggle, BTN_MESSAGE_OFF_IND");
            notifyManualToggle();
        }
#endif
#ifdef PRODUCT_WeMo_Insight
        if(POWER_SBY != g_APNSLastState)
            processInsightNotification(E_STATE, 0x00);
#endif
        break;
    case UPNP_MESSAGE_ON_IND:
        AsyncUPnPNotify(UPNP_MESSAGE_ON_IND);
        APP_LOG("ITC:LED", LOG_DEBUG, "UPNP_MESSAGE_ON_IND");
        break;
    case UPNP_MESSAGE_OFF_IND:
        AsyncUPnPNotify(UPNP_MESSAGE_OFF_IND);
        APP_LOG("ITC:LED", LOG_DEBUG, "UPNP_MESSAGE_OFF_IND");
        break;
    case RULE_MESSAGE_OFF_IND:
        APP_LOG("ITC:LED", LOG_DEBUG, "RULE_MESSAGE_OFF_IND");
        LocalBinaryStateNotify(0x00);
#ifdef PRODUCT_WeMo_Insight
        if(POWER_SBY != g_APNSLastState)
            processInsightNotification(E_STATE, 0x00);
#endif
        break;
    case RULE_MESSAGE_ON_IND:
        APP_LOG("ITC:LED", LOG_DEBUG, "RULE_MESSAGE_ON_IND");
        LocalBinaryStateNotify(0x01);
        break;
    case META_FULL_RESET:
        APP_LOG("ITC:LED", LOG_DEBUG, "META_FULL_RESET");
        ControlleeDeviceStop();
        break;
#ifdef PRODUCT_WeMo_Insight
    case UPNP_MESSAGE_SBY_IND:
        AsyncUPnPNotify(UPNP_MESSAGE_SBY_IND);
        APP_LOG("ITC: LED", LOG_DEBUG, "UPNP_MESSAGE_SBY_IND");
        break;
    case BTN_MESSAGE_SBY_IND:
        APP_LOG("ITC:LED", LOG_DEBUG, "BTN_MESSAGE_SBY_IND");
        ToggleUpdate(0x08);
#ifdef SIMULATED_OCCUPANCY
        if(LONG_PRESS_AWAY_ACTIVE ||
           (gRuleHandle[e_AWAY_RULE].ruleCnt && (gpSimulatedDevice && gpSimulatedDevice->ruleEndTime))) {
            APP_LOG("ITC:LED", LOG_DEBUG, "simulated rule: manual toggle, BTN_MESSAGE_SBY_IND");
            notifyManualToggle();
        }
#endif
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

void *RelayControlTask(void *args)
{
    pNode node = 0x00;
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("Relay", LOG_CRIT, "####### relay task running #######");

    while(1) {
        node = readMessage(PLUGIN_E_RELAY_THREAD);
        ProcessRelayEvent(node);
    }
    return NULL;
}

#if defined(PRODUCT_WeMo_SNSV2) || defined (PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
void *ntcTask(void *args)
{
    tu_set_my_thread_name( __FUNCTION__ );

    char prevOverTemp = g_overTemp;
    FILE * fpNTCFile = NULL;
    char szflag[SIZE_4B] = {'\0',};
    char* pResult = NULL;
#if defined (PRODUCT_WeMo_Dimmer)
    bool prevHushState = g_bHushAnimation;
    unsigned char errStatus = 0;
#endif
    unsigned char once=1;

    APP_LOG("NTC", LOG_CRIT, "NTC task running");

    while (1) {
        fpNTCFile = fopen(szNTCPath, "r");

        if (fpNTCFile == NULL) {
            APP_LOG("NTC", LOG_CRIT, "Open ntc: %s error %s", szNTCPath,strerror(errno));
            return NULL;
        }

        pResult = fgets(szflag, sizeof(szflag), fpNTCFile);

        if (pResult != NULL) {
#if defined (PRODUCT_WeMo_Dimmer)
            //temperature sensor input is active low
            g_overTemp = !(atoi(szflag));
            if(prevHushState != g_bHushAnimation && prevOverTemp == g_overTemp && g_overTemp) {
                if(!g_bHushAnimation)
                    setAnimation(LED_STATE_OVER_HEAT);
                prevHushState = g_bHushAnimation;
            }
            //update the WASP_VAR_ERR_STATUS in first iteration to take care of wemoApp restarts or processor reboots

            if((prevOverTemp != g_overTemp) || once)
#else
            int command = atoi(szflag);
            if((prevOverTemp != command) || once)
#endif

            {
                /* Change in over temp condition noted */
                APP_LOG("NTC", LOG_DEBUG, "g_overTemp: %d, prevOverTemp: %d, once: %d", g_overTemp, prevOverTemp, once);
                if(once)
                    once=0;

#if defined (PRODUCT_WeMo_Dimmer)
                if(g_overTemp) {
                    addVarToWaspList(WASP_VAR_ERR_STATUS, "1");
                    setAnimation(LED_STATE_OVER_HEAT);
                } else {
                    addVarToWaspList(WASP_VAR_ERR_STATUS, "0");
                    setAnimation(LED_STATE_CANCEL_ERR);
                }

                errStatus = g_overTemp?DPR_ERR_OVR_TEMP:0;
                setWaspVariable(WASP_VAR_ERR_STATUS, WASP_VARTYPE_UINT8, (void*)&errStatus);

                /*save the current value */
                prevOverTemp = 	g_overTemp;
#else
                setOverHeatState(command);
                /*save the current value */
                prevOverTemp = command;
#endif
            }
        }

        /* To avoid opening and closing file every time, just set the file pointer to beginning of file */
        //lseek(fpNTCFile, 0, SEEK_SET);

        fclose(fpNTCFile);
        pluginUsleep(DELAY_NTCPOLL);
    }

    //fclose(fpNTCFile);
    return NULL;
}
#endif


void *sensorGPIOTask(void *args)
{
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("Sensor", LOG_CRIT, "Sensor task running");

    while (1) {
        FILE * pSensorFile = 0x00;
        gSensorHealthPunch++;

        pSensorFile = fopen(szNewSensorPath, "r");

        if (pSensorFile == 0x00) {
            APP_LOG("Sensor", LOG_CRIT, "Open sensor: %s error", szNewSensorPath);

            return 0x00;
        }

        char szflag[SIZE_128B];
        char* pResult = fgets(szflag, sizeof(szflag), pSensorFile);

        if (pResult != 0x00) {
            int command = atoi(szflag);

            //-Get now time
            struct timeval tv;
            time_t curTime;
            gettimeofday(&tv, NULL);
            curTime = tv.tv_sec;

            if (MOTION_SENSORED == command) {
                if (curTime - s_lLastSensoringUpdateTime >= MAX_SENSOR_MSG_TIMEOUT) {
                    //- Add one more trace here indicating system status
                    APP_LOG("Sensor", LOG_DEBUG, "Sensor status changed, to notify");
                    MotionSensorInd();
                    s_lLastSensoringUpdateTime 		= curTime;
                    s_lLastNoSensoringUpdateTime 	= 0x00;
                    LockSensor();
                    g_SensorStatus = 0x01;
                    UnlockSensor();
                }
            } else {
                if (curTime - s_lLastNoSensoringUpdateTime >= MAX_SENSOR_MSG_TIMEOUT) {
                    NoMotionSensorInd();
                    s_lLastNoSensoringUpdateTime = curTime;
                    s_lLastSensoringUpdateTime	 = 0x00;
                    LockSensor();
                    g_SensorStatus = 0x00;
                    UnlockSensor();
                }
            }
        }

        fclose(pSensorFile);

        sleep(DELAY_1SEC);
    }

    return NULL;
}

int setPower(int command)
{
    FILE* pfRelay = 0x00;
#if !defined(PRODUCT_WeMo_SNSV2)
    FILE* pfLed   = 0x00;
#endif
#if defined(PRODUCT_WeMo_Light)
    FILE *pfNLed = 0x00;
#endif

    pfRelay = fopen(RELAY_GPIO, "w");
    if (0x00 == pfRelay) {
        APP_LOG("GPIO", LOG_ERR, "######## relay open failure #######");
        return 0x01;
    }

#if !defined(PRODUCT_WeMo_SNSV2)
    pfLed   = fopen(LED_GPIO, "w");
    if (0x00 == pfLed) {
        APP_LOG("GPIO", LOG_ERR, "######## led open failure #######");
        fclose(pfRelay);
        return 0x01;
    }
#endif

#if defined(PRODUCT_WeMo_Light)
    pfNLed   = fopen(NIGHT_LED_GPIO, "w");
    if (0x00 == pfNLed) {
        APP_LOG("GPIO", LOG_ERR, "######## Night led open failure #######");
        fclose(pfRelay);
        fclose(pfLed);
        return 0x01;
    }
#endif

    if (POWER_ON == command) {
        //- Set to ON
        fputs(RELAY_ON, pfRelay);
#if !defined(PRODUCT_WeMo_SNSV2)
        fputs(RELAY_LED_ON, pfLed);
#else // if PRODUCT_WeMo_SNSV2
        /*WEMO-51984:LED Behaviour SwitchV2: change led only when not in error state */
        system("amber_saved=$(cat /sys/class/leds/amber/trigger | cut -d'[' -f2 | cut -d']' -f1);\n"
               "if [ $amber_saved == \"none\" ]; then  \n"
               "echo default-on > /sys/class/leds/white/trigger; \n"
               "fi");
#endif
#if defined(PRODUCT_WeMo_LightV2)
        system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
               "if [ $pwm_saved == \"9\" ]; then  \n"
               "echo 4 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
               "fi");
        APP_LOG("GPIO", LOG_ERR, "######## Night led OFF #######");
#endif
#ifdef SIMULATED_OCCUPANCY
        if(gRuleHandle[e_AWAY_RULE].ruleCnt && (gpSimulatedDevice &&
                                                gpSimulatedDevice->ruleEndTime)) {
            time_t curTime;
            curTime = (int) GetUTCTime();
            gpSimulatedDevice->onTS = curTime;
            APP_LOG("GPIO", LOG_DEBUG, "######## Simulated device self ON TS: %d #######", gpSimulatedDevice->onTS);
        }
#endif
    } else if (POWER_OFF == command) {
        //- Set to OFF
        fputs(RELAY_OFF, pfRelay);
#if !defined(PRODUCT_WeMo_SNSV2)
        fputs(RELAY_LED_OFF, pfLed);
#else // if PRODUCT_WeMo_SNSV2
        /*WEMO-51984:LED Behaviour SwitchV2: change led only when not in error state */
        system("amber_saved=$(cat /sys/class/leds/amber/trigger | cut -d'[' -f2 | cut -d']' -f1);\n"
               "if [ $amber_saved == \"none\" ]; then  \n"
               "echo none > /sys/class/leds/white/trigger; \n"
               "fi");

#endif
#if defined(PRODUCT_WeMo_Light)
        if(u32DimVal == 0) {
            APP_LOG("GPIO", LOG_ERR, "######## Night led ON #######");
            fputs(NIGHTLIGHT_LED_ON, pfNLed);
        } else if(u32DimVal == 2) {
            fputs(NIGHTLIGHT_LED_OFF, pfNLed);
            APP_LOG("GPIO", LOG_ERR, "######## Night led OFF #######");
        }
#endif
    }


    //- Close handle
    fclose(pfRelay);
#if !defined(PRODUCT_WeMo_SNSV2)
    fclose(pfLed);
#endif

#if defined(PRODUCT_WeMo_Light)
    fclose(pfNLed);
#endif
    return 0x00;
}

int setLED(LED_ID id, BYTE OnDuration, BYTE OffDuration, int counter)
{
    if ( IVALID_THREAD_HANDLE != led_thread) {
        int rect = ithread_cancel(led_thread);
        led_thread = IVALID_THREAD_HANDLE;
        if (0x00 != rect) {
            APP_LOG("DeviceControl",LOG_ERR, "LED thread cancellation failure\n");
        }
    }

    return 0x00;
}


int SaveDeviceConfig(const char*  szKey, const char* szValue)
{
    SetBelkinParameter((char *)szKey, (char *)szValue);
    SaveSetting();
    return 0x00;
}

/**************************************************************
 * GetDeviceConfig:
 * 	Call file system API to get value of key
 *
 *
 *
 *
 * ************************************************************/
char* GetDeviceConfig(const char*  szKey)
{
    return GetBelkinParameter((char *)szKey);
}

int GetCurBinaryState()
{
    FILE* relay = 0x00;
    char buffer[4];

    memset(buffer, 0, sizeof(buffer));

    relay = fopen(RELAY_GPIO, "r");
    if (0x00 == relay) {
        APP_LOG("GPIO", LOG_ERR, "relay (%s) open failure", RELAY_GPIO);
        return g_PowerStatus;
    }

    fgets(buffer, 4, relay);

    g_PowerStatus = atoi(buffer);

    fclose(relay);
    return g_PowerStatus;
}


void SetCurBinaryState(int toState)
{
    g_PowerStatus = toState;
    SetLastUserActionOnState(g_PowerStatus);
}


void SetSensorConfig(int  delay, int sensitivity)
{
#ifdef NEW_BOARD
    g_cntSensorDelay = delay;
    g_cntSensitivity = sensitivity;
    SetMotionSensorDelay(g_cntSensorDelay, g_cntSensitivity);
#endif
}




void StartSensorTask()
{
    //-Double check
    initSensor();
    initSensorStateQueueLock();

    if (-1 != sensor_thread) {
        ithread_cancel(sensor_thread);
        sensor_thread = -1;
    }

#ifdef NEW_BOARD
    EnableMotionSensorDetect(0x01);
    SetMotionSensorDelay(g_cntSensorDelay, g_cntSensitivity);
#endif

    pthread_create(&sensor_thread, NULL, sensorGPIOTask, NULL);

    if (-1 == sensor_thread) {
        //- If can not be created, it is lik DEAD for sensor, so reboot again
        //- Though this is almost "impossible", but to ensure
        APP_LOG("Sensor",LOG_ERR, "@@@@@@@@@@@@@@@@@@@ Sensor task can not be created, this should not happen @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
        APP_LOG("Sensor",LOG_CRIT, "Sensor task can not be created, this should not happen");
    } else {
        //- success
        pthread_detach(sensor_thread);
    }
}

void StopSensorTask()
{
#ifdef NEW_BOARD
    EnableMotionSensorDetect(0x00);
#endif
    if (-1 != sensor_thread) {
        ithread_cancel(sensor_thread);
        sensor_thread = -1;
    }

}
/************************************************************************
 * Function: processAction
 *     Handle SetBinary State action here.
 *
 *  Parameters:
 *     toState - Expected State after this call.
 *     controlOrigin - Local/Remote
 *     attrSet - Additional information related to this action.
 *     toBrightness - the brightness value to set
 *     sensorTrigger - whether triggered by sensor or not

 *  Return:
 *     Returns SUCCESS or ATTR_STATE in case Set State fails or ATTR_BRIGHTNESS if Set brightness fails.
************************************************************************/
int processAction (int toState,int controlOrigin,int attrSet,int toBrightness, bool sensorTrigger)
{
    int retVal = SUCCESS, ret = SUCCESS;
    int countdownRuleLastMinStatus = 0;
#ifdef PRODUCT_WeMo_Dimmer
    int bret = SUCCESS;
#endif

    /*get contdown timer last minute running state*/
    countdownRuleLastMinStatus =  gCountdownRuleInLastMinute;

    if(attrSet & ATTR_STATE) {
        int state = toState;
#ifdef PRODUCT_WeMo_Insight
        if(POWER_ON == toState)
            state = POWER_SBY;
#endif
        executeCountdownRule(state);
    }

#ifdef SIMULATED_OCCUPANCY
    if(g_longPressAwayRunning) {
#ifdef PRODUCT_WeMo_Dimmer
        /* scheduleNightMode night mode thread after the LP
           away mode ends, if nightMode is enabled. */
        if(gpsNightMode && gpsNightMode->nightMode) {
            scheduleNightMode();
        }
#endif
    }
    if(LONG_PRESS_AWAY_ACTIVE ||
       (gRuleHandle[e_AWAY_RULE].ruleCnt && (gpSimulatedDevice && gpSimulatedDevice->ruleEndTime))) {
        notifyManualToggle();
    }
#endif

#ifdef PRODUCT_WeMo_Dimmer
    /* Brightness should be set before changing the binary state to take effect */
    if((attrSet & ATTR_BRIGHTNESS)) {
        bret = setBrightness(toBrightness, false);
        if(SUCCESS != bret) {
            retVal |= ATTR_BRIGHTNESS;
        } else {
            if((0 == GetCurBinaryState()) && gNightModeActive) {
                /* Brightness modified when night mode is active and device is OFF */
                // don't start timer if this command also turns on the device
                if(!((attrSet & ATTR_STATE) && (toState != POWER_OFF)))
                    start_night_mode_timer();
            }
        }
    }
#endif
    /*this API will start/restart/stop countdown timer dependng upon if
      coundown timer is not_running/runing_in_last_minute/running_not_in_last_ minute*/
    /*check if it was running in last minute, if yes do not toggle relay, countdown timer restarted*/
    if(gRuleHandle[e_COUNTDOWN_RULE].ruleCnt && countdownRuleLastMinStatus) {
        APP_LOG("Button", LOG_DEBUG, "Countdown timer was in last minute, Do not toggle!");
        retVal |= ATTR_STATE;
    } else {
#ifdef PRODUCT_WeMo_Dimmer
        if((attrSet & ATTR_STATE)) {
            if(POWER_ON == toState) {
                if(checkIfFaderRunning()) {
                    g_faderRunning=0;
                    cancelSleepTimer();
                    /* Send the stop fader notification */
                    sendFaderStopNotification(1);
                    /* reset the animation active for the fader */
                    setAnimation(LED_STATE_RULE_CLOSE);
                    /* sleep to let the user see ON as well after the
                       RULE_CLOSE animation. */
                    pluginUsleep(500000);
                }
            }
            if(gNightModeActive && (toState != POWER_OFF)) {
                /* brightness was modified in active night mode */
                stop_night_mode_timer();
            }
            /*
              If night mode was disabled when device was ON, next turn OFF operation should restore old brightness
              In case device was OFF when night mode was disabled, saved brightness will be restored during disable
            */
            if(!gNightModeActive && (toState == POWER_OFF)) {
                char *pBrightness = GetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);
                if(pBrightness && strlen(pBrightness)) {
                    /* apply last stored brightness */
                    APP_LOG("UPNPDevice", LOG_DEBUG, "Smart brightness change to %s", pBrightness);
                    setBrightness(atoi(pBrightness), false);
                    UnSetBelkinParameter(BRIGHTNESS_BEFORE_NIGHT_MODE);
                }
            }
        }
#endif
        if((attrSet & ATTR_STATE)) {
#ifdef PRODUCT_WeMo_Dimmer
            /* check if the state change is sensor triggered or not,
               set the animation accordingly. */
            if(sensorTrigger) {
                if(POWER_ON == toState)
                    setAnimation(LED_STATE_RULE_OPEN);
                else
                    setAnimation(LED_STATE_RULE_CLOSE);
            } else {
                /* play POWER_ON animation. Dont play when there is countDown
                   rule active */
                if(POWER_ON == toState && !isCountDownRuleActive())
                    setAnimation(LED_STATE_TURN_ON);
                else if(POWER_OFF == toState)
                    setAnimation(LED_STATE_TURN_OFF);
            }
#endif
            ret = ChangeBinaryState(toState);
        }

        if(SUCCESS != ret) {
            retVal |= ATTR_STATE;
        }
        if(CONTROL_LOCAL == controlOrigin)
            setRemote("0"); //Will be set even though it's triggered from Sensor.
        else if (CONTROL_REMOTE == controlOrigin) {
#ifndef PRODUCT_WeMo_Dimmer
            UPnPInternalToggleUpdate(toState);
#endif
            setRemote("1");
	}

    }
    return retVal;
}

int ChangeBinaryState(int newState)
{
    int ret = 0x01;
#if defined(PRODUCT_WeMo_SNSV2)
    if (getOverHeatState()) {
        APP_LOG ("DeviceControl", LOG_DEBUG, "exting ChangeBinaryState because of overheat");
        return ret;
    }
#endif
    LockLED();
    int curState = 0x00;
    curState = g_PowerStatus;

    APP_LOG ("DeviceControl", LOG_DEBUG, "In ChangeBinaryState");

    if ((0x00 == newState) || (0x01 == newState)) {
        APP_LOG("DeviceControl", LOG_DEBUG, "------------->>>>>>curState:%d newState:%d",curState, newState);
        //        if (curState != newState) {
#ifdef PRODUCT_WeMo_Insight
            if(newState == POWER_ON) {
                g_PowerStatus = POWER_SBY;
                g_APNSLastState = curState;//used to restrict sending APNS in case of OFF->SBY and SBY->OFF
            } else {
                g_PowerStatus = POWER_OFF;
                g_APNSLastState = curState;//used to restrict sending APNS in case of OFF->SBY and SBY->OFF
            }
#elif !defined(PRODUCT_WeMo_Dimmer) && !defined(PRODUCT_WeMo_SNSV2)
            g_PowerStatus = newState;
#ifndef __MIPSEL__
            if (g_PowerStatus) {
                system("echo On > /tmp/buttonStatus");
            } else {
                system("echo Off > /tmp/buttonStatus");
            }
#endif
#endif
            ret = 0x00;
            //        }
    } else {
        APP_LOG("DeviceControl", LOG_DEBUG, "state request incorrect:%d", newState);
    }

#ifndef PRODUCT_WeMo_SNSV2
    UnlockLED();
#endif
    if (0x00 == ret) {
#ifdef PRODUCT_WeMo_Dimmer
        /* set variable WASP_VAR_ON_OFF to change the
           state of the dimmer device. */
        if(SUCCESS == (ret = setWaspVariable(WASP_VAR_ON_OFF, WASP_VARTYPE_BOOL, (void*)&newState))) {
            setAttrFlagDimmer(ATTR_STATE, 1, 0);
            LockLED();
            g_PowerStatus = newState;
            UnlockLED();
            if(POWER_OFF == newState && checkIfFaderRunning()) {
                g_faderToTimer = false;
                g_faderRunning=0;
                cancelSleepTimer();
                /* Send the stop fader notification */
                sendFaderStopNotification(1);
                /* sleep to let the user see OFF as well before the
                   RULE_CLOSE animation. */
                pluginUsleep(500000);
                /* reset the animation active for the fader */
                setAnimation(LED_STATE_RULE_CLOSE);
            }
        }
#else
        setPower(newState);
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
        /* reversing the order of global variable and power change so that relay thread will not kick in */
        g_PowerStatus = newState;
#endif
#endif
    }
#ifdef PRODUCT_WeMo_SNSV2
    UnlockLED();
#endif
    APP_LOG ("DeviceControl", LOG_DEBUG, "Exiting ChangeBinaryState");

    return ret;
}


void initSensor()
{
    ithread_mutexattr_t attr;
    ithread_mutexattr_init(&attr);
    ithread_mutexattr_setkind_np( &attr, ITHREAD_MUTEX_RECURSIVE_NP );
    pthread_mutex_init(&s_sensor_mutex, &attr);
    ithread_mutexattr_destroy(&attr);

    APP_LOG("DeviceControl", LOG_CRIT, "sensor resource initialized with success");

}


int GetSensorState()
{
    return g_SensorStatus;
}

/**
 *	Reset sensor configuration to default
 *
 *
 *
 */
void ResetSensor2Default()
{
    if (DEVICE_SENSOR == g_eDeviceType) {
        //- If sensor, applied
        g_cntSensitivity = DEFAULT_SENSOR_SENSITIVITY;
        g_cntSensorDelay = DEFAULT_SENSOR_DELAY;
        SetSensorConfig(g_cntSensorDelay, g_cntSensitivity);
        APP_LOG("Sensor", LOG_DEBUG, "delay:%d, densitivity:%d", g_cntSensorDelay, g_cntSensitivity);
    }
}

#ifdef PRODUCT_WeMo_Dimmer
int WaspVar2String(WaspVariable *pVar,char *String,int MaxLen)
{
    int Ret = 0;
    switch(pVar->Type) {
    case WASP_VARTYPE_ENUM:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.Enum);
        break;

    case WASP_VARTYPE_PERCENT:
        Ret = snprintf(String,MaxLen,"%hu",pVar->Val.Percent);
        break;

    case WASP_VARTYPE_TEMP:
        Ret = snprintf(String,MaxLen,"%hd",pVar->Val.Temperature);
        break;

    case WASP_VARTYPE_TIME32:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.TimeTenths);
        break;

    case WASP_VARTYPE_TIME16:
        Ret = snprintf(String,MaxLen,"%hu",pVar->Val.TimeSecs);
        break;

    case WASP_VARTYPE_BOOL:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.Boolean);
        break;

    case WASP_VARTYPE_STRING:
        Ret = snprintf(String,MaxLen,"%s",pVar->Val.String);
        break;

    case WASP_VARTYPE_BLOB:
        Ret = snprintf(String,MaxLen,"%s",pVar->Val.Blob.Data);
        break;

    case WASP_VARTYPE_UINT8:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.U8);
        break;

    case WASP_VARTYPE_INT8:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.I8);
        break;

    case WASP_VARTYPE_UINT16:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.U16);
        break;

    case WASP_VARTYPE_INT16:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.I16);
        break;

    case WASP_VARTYPE_UINT32:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.U32);
        break;

    case WASP_VARTYPE_INT32:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.I32);
        break;

    case WASP_VARTYPE_TIME_M16:
        Ret = snprintf(String,MaxLen,"%d",pVar->Val.TimeMins);
        break;

    default:
        APP_LOG("UPNPDevice", LOG_ERR, "Invalid attribute type 0x%x",pVar->Type);
        break;
    }

    return Ret;
}
#endif

/*!
 *	\function
 *		SetLastUserActionOnState
 *
 *	\brief
 *		This is to track the user action so that sensor rule will not override the user action as a basic requirement
 *		Note, once any user action OFF. the flag should be reset
 *	\param
 *		state: the last user action ON/OFF: 0x01/0x00
 *
 *	\return
 *		void
 */
extern void StopPowerMonitorTimer();
void SetLastUserActionOnState(int state)
{
    //- Add thread protection here
    g_IsLastUserActionOn = state;
    StopPowerMonitorTimer();
}

/*!
 *	\function
 *		IsLastUserActionOn
 *
 *	\brief
 *		return the last user action
 *	\param
 *
 *
 *	\return
 *		int
 */
int IsLastUserActionOn()
{
    return g_IsLastUserActionOn;
}


int toggleBootState()
{
    FILE    *f = NULL;
    char    cmd[SIZE_32B] = "fw_printenv -n bootstate\0";
    int     ok = 0, bootstate = 0;

    APP_LOG("ResetAction", LOG_DEBUG, "##### Reset button task Action ##############");

    f = popen(cmd, "r");
    if ( f != NULL ) {
        memset(cmd, 0, sizeof(cmd));
        ok = fread(cmd, 1, sizeof(cmd), f);
        pclose(f);
    }
    if (ok) {
        bootstate = atoi(cmd);
        APP_LOG("ResetAction", LOG_DEBUG, "Current bootstate: %d", bootstate);
    } else {
        APP_LOG("ResetAction", LOG_ERR, "Could not fetch bootstate from fw_printenv");
        return FAILURE;
    }

    memset(cmd, 0, sizeof(cmd));
    if (bootstate == 0)
        snprintf(cmd, SIZE_32B, "fw_setenv bootstate 2");
    else if( bootstate == 2)
        snprintf(cmd, SIZE_32B, "fw_setenv bootstate 0");
    else {
        APP_LOG("ResetAction", LOG_ERR, "Invalid bootstate [%d]", bootstate);
        return FAILURE;
    }

    system(cmd);
    APP_LOG("ResetAction", LOG_DEBUG, "Executed command: %s..", cmd);
    system("reboot");
    return SUCCESS;
}

void resetWiFiSettings()
{
#ifdef PRODUCT_WeMo_Dimmer
    /* set animation to reflect the LED_STATE_WIFI_RESET state. */
    setAnimation(LED_STATE_WIFI_RESET);
#endif
    /* Remove saved IP from flash */
    UnSetBelkinParameter ("wemo_ipaddr");
    ControlleeDeviceStop();
    UnSetBelkinParameter(WIFI_CLIENT_SSID);

    /* Bring down both apcli0 & ra0 to reset driver by unload/reload the driver (mt7628/rt2860v2_ap). */
    APP_LOG("ResetWiFiSettings", LOG_DEBUG, "Bringing down apcli0 & ra0...");
    system("nvram commit;ifconfig apcli0 down; ifconfig ra0 down");

    APP_LOG("ResetWiFiSettings", LOG_DEBUG, "Reloading wifi driver...");
    system("rmmod mt7628");
    system("modprobe mt7628");
    //    system("ifconfig ra0 up");
    system("iwpriv ra0 set airplayEnable=1");
    APP_LOG("ResetWiFiSettings", LOG_INFO, "Exit wemoApp...");
    //    system("sleep 2;echo ##killall wemo processes;killall -9 wemoApp;killall -9 wemohap;killall -9 wemo_remote;killall -9 wemo_ctrl;echo ##killall completes");
    system("/sbin/restart_services.sh");
    resetSystem();
}

#if defined(PRODUCT_WeMo_Light) && !defined(PRODUCT_WeMo_Dimmer)
int ChangeNightLight(int type)
{
    int err, fd;
    int reg1_gpio_dir=0;
    int reg1_gpio_data=0;
    if( (fd = open("/dev/gpio", O_RDWR)) < 0 ) {
        APP_LOG("ChangeNightLight", LOG_DEBUG, "Open /dev/gpio failed");
        return 1;
    }
    u32DimVal = type;
    if(type == 1) {

        APP_LOG("ChangeNightLight", LOG_DEBUG, "*****  DIMING THE NIGHT LIGHT LED *****");
        reg1_gpio_dir = 0xFFFFABFF;
        err = ioctl(fd,RALINK_GPIO_SET_DIR_IN , (void *)&reg1_gpio_dir);
        if( err < 0 ) {
            APP_LOG("ChangeNightLight", LOG_DEBUG, "Ralink RALINK_GPIO_SET_DIR_IN failed");
            close(fd);
            return err;
        }
        reg1_gpio_dir = 0x00002A80;
        err = ioctl(fd,RALINK_GPIO_SET_DIR_OUT , (void *)&reg1_gpio_dir);
        if( err < 0 ) {
            APP_LOG("ChangeNightLight", LOG_DEBUG, "Ralink RALINK_GPIO_SET_DIR_OUT failed");
            close(fd);
            return err;
        }
        err = ioctl(fd,RALINK_GPIO_READ , (void *)&reg1_gpio_data);
        if( err < 0 ) {
            APP_LOG("ChangeNightLight", LOG_DEBUG, "Ralink RALINK_GPIO_READ failed");
            close(fd);
            return err;
        }
        APP_LOG("ChangeNightLight", LOG_DEBUG, "Read RALINK_REG_PIODATA :[0x%08X]",reg1_gpio_data);
    } else if ((type == 0) || (type == 2)) {
        APP_LOG("ChangeNightLight", LOG_DEBUG, "***** Removing DIMING OF NIGHT LIGHT LED *****");
        reg1_gpio_dir = 0xFFFFEBFF;
        err = ioctl(fd,RALINK_GPIO_SET_DIR_IN , (void *)&reg1_gpio_dir);
        if( err < 0 ) {
            APP_LOG("ChangeNightLight", LOG_DEBUG, "Ralink RALINK_GPIO_SET_DIR_IN failed");
            close(fd);
            return err;
        }
        reg1_gpio_dir = 0x00006A80;
        err = ioctl(fd,RALINK_GPIO_SET_DIR_OUT , (void *)&reg1_gpio_dir);
        if( err < 0 ) {
            APP_LOG("ChangeNightLight", LOG_DEBUG, "Ralink RALINK_GPIO_SET_DIR_OUT failed");
            close(fd);
            return err;
        }
        err = ioctl(fd,RALINK_GPIO_READ , (void *)&reg1_gpio_data);
        if( err < 0 ) {
            APP_LOG("ChangeNightLight", LOG_DEBUG, "Ralink RALINK_GPIO_READ failed");
            close(fd);
            return err;
        }
        APP_LOG("ChangeNightLight", LOG_DEBUG, "Read RALINK_REG_PIODATA :[0x%08X]",reg1_gpio_data);
        setPower(g_PowerStatus);
    } else {
        APP_LOG("ChangeNightLight", LOG_DEBUG, "Invalid type value : %d",type);
        return 1;
    }

    return 0;

}
#endif

#if defined(PRODUCT_WeMo_Dimmer)
void *ResetButtonTask(void *args)
{
    unsigned int CounterForButtorPressed = 0;
    FILE * pButtonFile = 0x00;
    int reset = 0x00;
    char szflag[SIZE_4B];
    char* pResult = 0x00;
    int command = BUTTON_RELEASED;
    int InsideResetFlag = 1;

    tu_set_my_thread_name( __FUNCTION__ );
    APP_LOG("ResetButtonTask", LOG_DEBUG, "##### Reset button task running: %s ##############", szResetBottonPath);

    pButtonFile = fopen(szResetBottonPath, "r");
    if (pButtonFile == 0x00) {
        APP_LOG("ResetButtonTask:", LOG_DEBUG, "####### open reset button: %s error", szResetBottonPath);
        return 0x00;
    }

    //checking if thread has entered with button already pressed, if so wait for the button to get released
    while (1) {
        memset(szflag, 0x00, sizeof(szflag));
        fseek(pButtonFile, 0, SEEK_SET);
        pResult = fgets(szflag, sizeof(szflag), pButtonFile);
        if (pResult != 0x00) {
            if (0x0 != strlen(szflag))
                command = atoi(szflag);
            if (GPIO_BUTTON_PRESSED == command) {
                APP_LOG("ResetButtonTask:", LOG_DEBUG, "button prsd on entry: %s error", szResetBottonPath);
                usleep(50000);
                continue;
            } else {
                APP_LOG("ResetButtonTask:", LOG_DEBUG, "starting RB Task : %s", szResetBottonPath);
                break;
            }
        }
        usleep(50000);
    }

    while (1) {
        memset(szflag, 0x00, sizeof(szflag));
        fseek(pButtonFile, 0, SEEK_SET);
        pResult = fgets(szflag, sizeof(szflag), pButtonFile);
        if (pResult != 0x00) {
            if (0x0 != strlen(szflag))
                command = atoi(szflag);
            if (GPIO_BUTTON_PRESSED == command) {
                if (InsideResetFlag) {
                    InsideResetFlag = 0;
                    APP_LOG("ResetButtonTask:", LOG_DEBUG, "Inside Reset");
                }
                CounterForButtorPressed += 1;
#if defined(PRODUCT_WeMo_Dimmer)
                if (HUSH_ANIMATION_LOOP_CNT >= CounterForButtorPressed) {
                    reset = HUSH_ANIM;
                } else if (HUSH_ANIMATION_LOOP_CNT < CounterForButtorPressed && NO_ACTION_LOOP_CNT >= CounterForButtorPressed) {
#else
                if (NO_ACTION_LOOP_CNT >= CounterForButtorPressed) {
#endif
                    /* nothing required */
                    reset = 0;
                } else if (NO_ACTION_LOOP_CNT < CounterForButtorPressed && RESET_DEVICE_LOOP_CNT >= CounterForButtorPressed) {
                    if(DEVICE_RESET != reset) {
                        /* set animation to reflect that the reset button is pressed and
                           2 secs have passed. */
                        setAnimation(LED_STATE_BAR_PRESSED);
                        /* reboot */
                        reset = DEVICE_RESET;
                    }
                } else if (RESET_DEVICE_LOOP_CNT < CounterForButtorPressed && RESET_NETWORK_LOOP_CNT >= CounterForButtorPressed) {
                    /* reset wifi */
                    reset = NETWORK_RESET;
                } else if (RESET_NETWORK_LOOP_CNT < CounterForButtorPressed && RESET_FACTORY_LOOP_CNT >= CounterForButtorPressed) {
                    /* factory reset */
                    reset = FACTORY_RESET;
                } else {
                    reset = 0;
                }
            } else {
#if defined(PRODUCT_WeMo_Dimmer)
                if(HUSH_ANIM == reset) {
                    /* If the reset bar is pressed and released within 1 sec,
                       hush the error animation if it is not already */
                    if(g_bHushAnimation) {
                        APP_LOG("ResetButtonTask:", LOG_DEBUG, "Button tapped to deactivate hush animation.");
                        /* set mode as 0 to resume the hushed animations */
                        startHushMode(INACTIVE, 0);
                    } else {
                        APP_LOG("ResetButtonTask:", LOG_DEBUG, "Button tapped to activate hush animation.");
                        /* set mode as 1 and suspend option as 2
                           to hush animations for 24 hrs */
                        startHushMode(ACTIVE, ONE_DAY);
                    }
                    reset = 0;
                }
#endif
                CounterForButtorPressed = 0;
                if(!InsideResetFlag && !reset) {
                    /* set animation to reflect that the reset button is released. */
                    setAnimation(LED_STATE_BAR_RELEASED);
                }
                if(InsideResetFlag || !reset) {
                    InsideResetFlag = 1;
                    usleep(50000);
                    continue;
                }
                APP_LOG("ResetButtonTask:", LOG_DEBUG, "Reset Button Released.");
                break;
            }
        }
        usleep(50000);
    }
    if (DEVICE_RESET == reset) {
        /* set animation to reflect the LED_STATE_RESTART state. */
        setAnimation(LED_STATE_RESTART);
        APP_LOG("ResetButtonTask:", LOG_DEBUG, "reset state set as DEVICE_RESET");
        APP_LOG("UPNP", LOG_DEBUG, "System rebooting........");
        /* sleep to let the user see the animation before
           device reboot */
        sleep(2);
        system("reboot");
    } else if (NETWORK_RESET == reset) {
        /* animation is played inside the resetWiFiSettings */
        APP_LOG("ResetButtonTask:", LOG_DEBUG, "reset state set as NETWORK_RESET");
        resetWiFiSettings();
    } else if(FACTORY_RESET == reset) {
        /* set animation to reflect the LED_STATE_FACTORY_RESTORE state. */
        setAnimation(LED_STATE_FACTORY_RESTORE);
        APP_LOG("ResetButtonTask:", LOG_DEBUG, "reset state set as FACTORY_RESET");
        pMessage msg = 0x00;
        msg = createMessage(META_FULL_RESET, 0, 0);
        SendMessage2App(msg);
    }
    APP_LOG("UPNP", LOG_DEBUG, "Exiting ResetButtonTask thread........");
    return NULL;
}

#elif defined(PRODUCT_WeMo_Light) || defined(PRODUCT_WeMo_SNS) || defined(PRODUCT_WeMo_Insight)
#ifndef PRODUCT_WeMo_SNSV2
void *ResetButtonTask(void *args)
{
    unsigned int CounterForButtorPressed = 0;
    FILE * pButtonFile = 0x00;
    int isResetOccurred = 0x00;
    char szflag[SIZE_4B];
    char* pResult = 0x00;
    int command = BUTTON_RELEASED;
    int InsideResetFlag = 1;
    tu_set_my_thread_name( __FUNCTION__ );

    APP_LOG("ResetButtonTask", LOG_DEBUG, "##### Reset button task running: %s ##############", szResetBottonPath);

    pButtonFile = fopen(szResetBottonPath, "r");
    if (pButtonFile == 0x00) {
        APP_LOG("ResetButtonTask:", LOG_DEBUG, "####### open reset button: %s error", szResetBottonPath);
        return 0x00;
    }

    //checking if thread has entered with button already pressed, if so wait for the button to get released
    while (1) {
        memset(szflag, 0x00, sizeof(szflag));
        fseek(pButtonFile, 0, SEEK_SET);
        pResult = fgets(szflag, sizeof(szflag), pButtonFile);
        if (pResult != 0x00) {
            if (0x0 != strlen(szflag))
                command = atoi(szflag);

            if (GPIO_BUTTON_PRESSED == command) {
                APP_LOG("ResetButtonTask:", LOG_DEBUG, "button prsd on entry: %s error", szResetBottonPath);
                usleep(50000);
                continue;
            } else {
                APP_LOG("ResetButtonTask:", LOG_DEBUG, "starting RB Task : %s", szResetBottonPath);
                break;
            }
        }
    }
    while (1) {
        memset(szflag, 0x00, sizeof(szflag));
        fseek(pButtonFile, 0, SEEK_SET);
        pResult = fgets(szflag, sizeof(szflag), pButtonFile);
        if (pResult != 0x00) {
            if (0x0 != strlen(szflag))
                command = atoi(szflag);

            if (GPIO_BUTTON_PRESSED == command) {
                if (InsideResetFlag) {
                    InsideResetFlag = 0;
                    APP_LOG("ResetButtonTask:", LOG_DEBUG, "Inside Reset");
#ifndef PRODUCT_WeMo_Dimmer
                    SetWiFiLED(0x06);
#endif
                }
                CounterForButtorPressed += 1;
                if (RESET_NETWORK_LOOP_CNT == CounterForButtorPressed) {
                    isResetOccurred = 0x01;
                }
            } else {
                InsideResetFlag = 1;
                CounterForButtorPressed = 0;
            }
        }
        if (isResetOccurred) {
#ifndef PRODUCT_WeMo_Dimmer
            SetWiFiLED(0x05);
#endif
            isResetOccurred = 0x0;
            fclose(pButtonFile);
            resetWiFiSettings();
            break;
        }
        usleep(500000);
    }
    APP_LOG("ResetButtonTask:", LOG_DEBUG, "####### Reset Occurred");
    return NULL;
}
#endif
#endif
