/***************************************************************************
 *
 *
 * dimmer_attr.h
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
#ifndef DIMMER_ATTR_H_
#define DIMMER_ATTR_H_


#ifdef PRODUCT_WeMo_Dimmer
#include "controlledevice.h"
#include <wasp_api.h>
#include <wasp_vars_dimmer.h>

#define MAX_FADER_LENGTH 32
#define ATTR_STATE 1<<0
#define ATTR_BRIGHTNESS 1<<1
#define ATTR_FADER 1<<2
#define ATTR_OVERHEAT 1<<3

#define HUSH_ANIMATION_END_TIME "hushAnimationEndTime"

#define ACTIVE    1
#define INACTIVE  0

#define ONE_HOUR  1
#define ONE_DAY   2
#define ONE_WEEK  3

extern bool g_bHushAnimation;
extern char g_hushAnimParam[];

extern int gLocalAttrSet;
extern int gRemoteAttrSet;

extern int g_brightness;
extern char g_fader[MAX_FADER_LENGTH];
extern char g_overTemp;

/* variable to check if fader is active or not,
   to avoid calling WASP for the same */
extern bool g_faderRunning;
extern bool g_faderToTimer;

void initAttrNotifyLockDimmer(void);
void LockAttrDimmer(void);
void UnLockAttrDimmer(void);

/**
 * checkIfFaderRunning
 * - Function to check if fader is running
 * - return:
 *      true: If running
 *      false: If not running
 ***************************************************/
bool
checkIfFaderRunning(void);

int cancelFaderAndNotify(void);
int cancelSleepTimer(void);

void sendFaderStopNotification(int stopTimer);
void upnpFaderNotify(unsigned int fadeTime, long int utcTime, bool fadeEnable,
                     float delta, unsigned char brightness);
int setAttrFlagDimmer(int flag, int how, int reset);
int setBrightness(int brightness, bool fadeEnable);
int setFader(char* fader, bool isNotificationRequired);

int getBrightness(void);
int getFader(char *buf);
int getOverHeatState(void);
void lockAttrDimmer(void);
void unlockAttrDimmer(void);
void blinkLights(int blinkCount, int dnd, int onoffInterval, int blinkInterval);
int startHushMode(int mode, int selectedSuspendedOption);
void checkAndStartHushMode(void);
#endif

#endif /* DIMMER_ATTR_H_*/
