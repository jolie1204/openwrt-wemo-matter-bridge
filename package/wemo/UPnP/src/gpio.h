/***************************************************************************
*
*
* gpio.h
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
#ifndef __GPIO_H__
#define __GPIO_H__

#include <stdio.h>
#include <stdbool.h>
#include <ithread.h>
#include "global.h"
#include "logger.h"
#include "itc.h"

#define DEFAULT_SENSOR_DELAY 			1
#define DEFAULT_SENSOR_SENSITIVITY 		10

extern pthread_t power_thread;
extern pthread_t sensor_thread;
extern pthread_t relay_thread;
extern int gButtonHealthPunch;
extern pthread_t ButtonTaskMonitor_thread;
extern pthread_t SensorTaskMonitor_thread;


#ifdef PRODUCT_WeMo_Dimmer
void *ntcTask(void *args);
#endif
typedef int LED_ID;
typedef unsigned char BYTE;


#define         POWER_ON        1
#define         POWER_OFF       0
#ifdef PRODUCT_WeMo_Insight
#define         POWER_SBY       8
#endif

#if defined (PRODUCT_WeMo_SNSV2)
#define BUTTON_RELEASED 0x00
#define GPIO_BUTTON_PRESSED	0x01
#else
#define BUTTON_RELEASED 0x01
#define GPIO_BUTTON_PRESSED	0x00
#endif

#define DEVICE_UNKNOWN	    0x00
#define DEVICE_SOCKET 	    0x01
#define DEVICE_SENSOR 	    0x02
#define DEVICE_BABYMON 		0x03
#define DEVICE_STREAMING	0x04
#define DEVICE_BRIDGE       0x05
#define DEVICE_INSIGHT      0x06

/* Device Types for Jarden Products */
#define DEVICE_CROCKPOT  	0x07	/* Device type for CrockPot */
#define DEVICE_LIGHTSWITCH  	0x08
#define DEVICE_NETCAM		0x09
//#define FAKE_WNC_TYPE_AS_NETCAM
#if defined(FAKE_WNC_TYPE_AS_NETCAM)
#  define DEVICE_LINKSYS_WNC_CAM DEVICE_NETCAM
#else
#  define DEVICE_LINKSYS_WNC_CAM 0x0A
#endif
#define LINKSYSWNC_NAME "LinksysWNCSensor"

#define DEVICE_SBIRON       	0x0B
#define DEVICE_MRCOFFEE         0x0C    /* Device type for Coffee */
#define DEVICE_PETFEEDER    	0x0D
#define DEVICE_SMART            0x0E
#define DEVICE_MAKER            0x0F
#define DEVICE_ECHO            	0x10
#define DEVICE_DIMMER    	0x11
#define DEVICE_LIGHTSWITCHV2    0x12
#define DEVICE_LIGHTSWITCH3WAY  0x13
#define STATUS_TS "StatusTS"
extern int g_PowerStatus;


#if defined(LONG_PRESS_SUPPORTED)
extern int gSimulatedLongPress;
#endif

//---- Sensor ------
void *PowerButtonTask(void *args);
extern void *ButtonTaskMonitorThread(void *arg);

void *sensorGPIOTask(void *args);
extern void *SensorTaskMonitorThread(void *arg);

//- LED
void *LedAutoToggleLoop(void *args);


int setPower(int command);

#ifdef PRODUCT_WeMo_Light
void *ResetButtonTask(void *args);
int ChangeNightLight(int type);
int u32DimVal;
#endif

//------ LED -------------
void initLED();

void *WiFiLedTask(void *args);

#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_Dimmer) || defined(PRODUCT_WeMo_LightV2)
#define OVERTEMP_STATE 0x1
#define OVERTEMP_PENDING 0x2
#define MAX_OVERTEMP_TS_IN_24HRS 5
#define OVERTEMP_TS_FILE "/tmp/OverTempTS"
#define DELAY_NTCPOLL 1000000
int getOverHeatState(void);
int isOverHeat2ndWarning();
void setOverHeatState(int overHeatState);
int isOverHeatStateChange (void);
void unsetOverHeatPendingState(void);
void *ntcTask(void *args);
#endif
/**
 * LED_ID: ID of the LED
 *
 * OnDuration: on interval, 0xFF solid
 *
 * OffDuration: off interval, 0xFF OFF
 *
 * counter: 0x00, forever until next change request coming
 * ***/
int setLED(LED_ID id, BYTE OnDuration, BYTE OffDuration, int counter);

/************************************************************
 * SaveDeviceConfig:
 * 	Call file system API to save key
 *
 *
 *
 *
 *
 * **********************************************************/
int SaveDeviceConfig(const char*  szKey, const char* szValue);

/**************************************************************
 * GetDeviceConfig:
 * 	Call file system API to get value of key
 *
 *
 *
 *
 * ************************************************************/
char* GetDeviceConfig(const char*  szKey);


int GetCurBinaryState();

void SetCurBinaryState(int toState);

void StartSensorTask();
void StopSensorTask();

/**
 *
 *	To change relay state, uniform the interface through local button,
 *  mobile app, and cloud
 *
 *
 *
 *
 ******************************************/
int ChangeBinaryState(int newState);


void LockLED();
void UnlockLED();

void LockSensor();
void UnlockSensor();

void togglePower();

#define SENSORING_ON	0x01
#define SENSORING_OFF	0x00


void initSensor();

extern int g_cntSensorDelay;
extern int g_cntSensitivity;

void SetSensorConfig(int  delay, int sensitivity);


extern int g_isInsightRuleActivated;

void InternalToggleUpdate(int curState);
void ToggleUpdate(int curState);

void *RelayControlTask(void *args);
int ProcessRelayEvent(pNode node);

void ResetSensor2Default();

int GetSensorState(void);

void SetLastUserActionOnState(int state);

int IsLastUserActionOn();

void resetWiFiSettings();

int processAction(int,int,int,int, bool);
void LockLongPress(void);
void UnlockLongPress(void);
#endif /* GPIO_H_ */
