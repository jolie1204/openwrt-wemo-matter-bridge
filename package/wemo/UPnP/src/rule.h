/***************************************************************************
*
*
* rule.h
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
#ifndef		__RULE__H__
#define		__RULE__H__

#include <stdbool.h>
typedef enum {
    WEEK_MON_E = 0x00,
    WEEK_THUE_E,
    WEEK_WEN_E,
    WEEK_THURS_E,
    WEEK_FRI_E,
    WEEK_SAT_E,
    WEEK_SUN_E,
    WEEK_DAYS_NO
} DAY_OF_WEEK_INDEX;

#include <ithread.h>
#include "wemodefs.h"
#include "LinkedList.h"

#define ONE_DAY_SECONDS 86400 //-24 * 60 * 60

#define INVALID_THREAD_HANDLE 		(pthread_t)-1

#define UNITS_DIGIT_DET     		10
#define RULE_DB_URL             	"/tmp/rules/temppluginRules.db"
#define RULE_DB_FILE_NAME       	"rules.db"
#define RULE_DB_FILE_PATH       	"/tmp/Belkin_settings/" RULE_DB_FILE_NAME
#define RULE_EXTRACT_DIR        	"/tmp/rules/"

#define RULE_TASK_FREQUENCY		10
#define SET_RULE_FLAG       1
#define RESET_RULE_FLAG     0

extern unsigned int g_SendRuleID;
extern LinkedList  gSubscriptionList;

#ifdef PRODUCT_WeMo_Insight
#define MAX_APNS_SUPPORTED    210 //(7*MAX_TIMER60)/2
#define INSIGHT_TASK_POLL_FREQ	10

typedef enum {
    E_EQUAL = 0x00,
    E_LARGER,
    E_LESS,
    E_EQUAL_OR_LARGER,
    E_EQUAL_OR_LESS,
    E_WRONG_OPCODE
} CONDITION_OPCODE;

typedef enum {
    E_COST = 0x00,
    E_ON_DURATION,
    E_OFF_DURATION,
    E_SBY_DURATION,
    E_STATE,
    E_POWER,
    E_INVALID
} INSIGHT_RULE_PARAMS;

struct __InsightCondition {
    int        ParamCode;      //Prameter Code
    int        OPCode;           // Operation Code
    int        OPVal;            //Operation Value
    int        isTriggered;      //isTrigger Flag when condition is true
    int        SendApnsFlag;      //Send APNS Flag for that rule
    int        isActive;      //Check if rule is active
};

typedef struct __InsightCondition InsightCondition;
struct __InsightActiveState {
    unsigned int         RuleId;      //Rule Id
    char		 isActive;       // is rule active flag
};
typedef struct __InsightActiveState InsightActiveState;
InsightActiveState	InsightActive[MAX_APNS_SUPPORTED];

void processInsightNotification(int insightRuleType, int ruleCurValue);
#endif

extern int gLongPressRuleActive;

typedef enum ruleType {
    e_SIMPLE_RULE = 0,
    e_TIMER_RULE,
    e_SENSOR_RULE,
    e_INSIGHT_RULE,
    e_AWAY_RULE,
    e_NOTIFICATION_RULE,
    e_COUNTDOWN_RULE,
    e_CROCKPOT_RULE,
    e_MAKERSENSOR_RULE,
    e_EVENT_RULE,
#if defined(LONG_PRESS_SUPPORTED)
    e_LONGPRESS_RULE,
#endif
    e_MAX_RULE
} ERuleType;

#if defined(LONG_PRESS_SUPPORTED)
typedef enum {
    e_LONG_PRESS_ACTION_OFF,
    e_LONG_PRESS_ACTION_ON,
    e_LONG_PRESS_ACTION_TOGGLE,
    e_LONG_PRESS_ACTION_AWAYMODE
} ELongPressRuleAction;

typedef struct longPressRule {
    int count;
    char *pszUDNList; /* comma seperated list of target devices */
    int action;
    int state;
} SLongPressRule;

extern SLongPressRule *gpsLongPressRule;


/* NvRam variable to keep track of the next state to toggle
   the devices participating in the rule to */
#define LONG_PRESS_NEXT_TOGGLE_STATE "longPressNextToggleState"

int handleLongPressRule(void);
#endif

#ifdef SIMULATED_OCCUPANCY
#define LONG_PRESS_AWAY_MODE_STATE "longPressAwayModeState"
#define LONG_PRESS_HAS_OCCURRED "longPressHasOccurred"
#define LONG_PRESS_AWAY_RULE_ID "longPressAwayRuleID"

extern int g_longPressAwayRunning;
extern bool g_longPressOccurred;
extern int g_LongPressAwayRuleID;

#define LONG_PRESS_AWAY_ACTIVE  (g_longPressOccurred || g_longPressAwayRunning)
#endif

#define THREAD_HANDLE_INVALID		((pthread_t)-1)

#define RULE_ENGINE_DEFAULT		0
#define RULE_ENGINE_SCHEDULED		1
#define RULE_ENGINE_RELOAD		2
#define RULE_ENGINE_RUNNING		3

/* Auto-off timer states */
#define COUNTDOWN_NOTIFY_DEFAULT	0
#define COUNTDOWN_NOTIFY_PENDING	1
#define COUNTDOWN_NOTIFY_IN_PROGRESS	2

extern int gCountdownPendingNotification;

#define MAX_EVENT_VALUE 15

#define PARTIAL_XML 1
#define FULL_XML 0
#define NO_XML (-1)

typedef struct ruleInfo {
    struct ruleInfo	*psNext;
    ERuleType	ruleType;
    unsigned int 	ruleId;
    unsigned int	startTime;
    unsigned int 	ruleDuration;
    unsigned int 	startAction;
    unsigned int 	endAction;
    unsigned int	endTime;
    unsigned char	activeDays;
    unsigned char	isActive;
    unsigned char	isOvernight;
    unsigned char	isSunriseSunset;
    unsigned char	isDayChange;
    unsigned char   isInvalidToday; /* WEMO-47250 rule will not trigger if its overnightness changed due to DST or sunset/sunrise time change */
    unsigned char	pad[2];
    /* other dimmer attributes */
#ifdef PRODUCT_WeMo_Dimmer
    unsigned int    startBrightness;
    unsigned int    endBrightness;
    unsigned int    startFader;
    unsigned int    endFader;
#endif
} SRuleInfo;

/* read the brightness and fader from timer list and execute */
typedef enum subsStatus {
    SUBSCRIBE,
    SUBSCRIBING,
    SUBSCRIBED,
    UNSUBSCRIBE,
    UNSUBSCRIBING
} ESubsStatus;

typedef struct eventSubsNode {
    int eventType;
    ESubsStatus status;
    unsigned long int eventTS;
    int ruleID;
    char eventProducer[SIZE_64B];
    char eventValue[MAX_EVENT_VALUE];
} SEventSubsNode;

typedef struct timerList {
    unsigned int time;
    unsigned int action;
    /* other dimmer attributes */
#ifdef PRODUCT_WeMo_Dimmer
    unsigned int brightness;
    unsigned int faderTime;
#endif
    struct timerList *nextTimer;
} STimerList;

extern STimerList *gpsTimerList;
typedef void* (*fpRuleCallback)(void *) ;

typedef struct ruleHandle {
    pthread_t 	ruleThreadId;
    unsigned int 	ruleCnt;
} SRuleHandle;

typedef struct rulesQueue {
    unsigned int ruleID;
    unsigned int notifyRuleID;
    unsigned int ruleFreq;
    unsigned long int ruleTS;
    char *ruleMSG;
    struct rulesQueue *next;
} SRulesQueue;

extern pthread_t g_handlerSchedulerTask;
extern volatile int gRestartRuleEngine;
extern volatile int gCountdownRuleInLastMinute;
extern unsigned long gCountdownEndTime;
extern SRuleInfo *gpsRuleList;

void *RulesTask(void *args);
void *TimerTask(void *args);
void *InsightTask(void *args);
void *AwayTask(void *args);
void *CrockpotTask(void *args);
void *MakersensorTask(void *args);

extern fpRuleCallback gfpRuleThreadFn[e_MAX_RULE];
extern SRuleHandle gRuleHandle[e_MAX_RULE]; /* Includes thread handle for Scheduler at index 0, simple rules are executed by scheduler */

void GetCalendarDayInfo(int* dayIndex, int* monthIndex, int* year, int* seconds);

int IsNtpUpdate();

void *RulesNtpTimeCheckTask(void *args);

#if defined(PRODUCT_WeMo_Insight) || defined(PRODUCT_WeMo_SNS)
int GetRuleIDFlag();
void SetRuleIDFlag(int FlagState);
#endif

void initRule();

void lockRule();
void unlockRule();

int ActivateRuleEngine();
int daySeconds(void);

void RuleToggleLed(int curState);
void resetSystem(void);
void executeSensorRule(void);
void executeNotifyRule(void);
void executeCountdownRule(int deviceState);
void stopCountdownTimer(void);
int checkAndExecuteCountdownTimer(int deviceState);

void enqueueRuleQ(SRulesQueue *qNode);
SRulesQueue *dequeueRuleQ(void);
void destroyRuleQueue(void);
int SetRuleAction(unsigned int ruleAction, char *actuation);

int GetRuleDBHandle();
void startExecutorThread(ERuleType i);
void stopExecutorThread(ERuleType i);

#ifdef PRODUCT_WeMo_Dimmer
/* function to set other dimmer attributes(brightness and fader). */
int SetDimmerAttributes(unsigned int brightness, unsigned int fader, unsigned int action, char* actuation);
int scheduleNightMode(void);
void stopNightMode(void);
void nightModeConfigurationNotify();
#endif

#ifdef SIMULATED_OCCUPANCY
bool isAwayRuleInExecution(void);
void
cleanUpForLongPressAway(void);
#endif
bool isCountDownRuleActive();

bool isCountdownRuleActive(void);
#endif
