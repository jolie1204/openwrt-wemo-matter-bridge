/***************************************************************************
 *
 *
 * rule.c
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
#include <stdio.h>
#include <sys/time.h>
#include "utils.h"
#include "osUtils.h"
#include "rule.h"
#include "global.h"
#include "logger.h"
#include <time.h>
#include "upnpCommon.h"
#include "gpio.h"
#include "wifiSetup.h"
#include "wifiHndlr.h"
#include "controlledevice.h"
#include "plugin_ctrlpoint.h"
#include "sunriset.h"
#include "LinkedList.h"
#include "thready_utils.h"
#ifdef _OPENWRT_
#include "belkin_api.h"
#else
#include "gemtek_api.h"
#endif
#ifdef SIMULATED_OCCUPANCY
#include "simulatedOccupancy.h"
#endif
#include "sqlite3.h"
#include "WemoDB.h"
#include "remote_event_rule.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */
pthread_t g_handlerSchedulerTask = INVALID_THREAD_HANDLE;

#define WAIT_4_RULE_ENGINE_IN_SEC 1
#define MICROS_PER_SECOND 1000000
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
#define AUTOOFF_LAST_MIN "/tmp/AutoOffInLastMin"
#endif

extern unsigned long int GetNTPUpdatedTime(void);
extern void remoteAccessUpdateStatusTSParams(int status);

extern unsigned int g_ONFor;
extern int gDstSupported;
sqlite3 *g_RulesDB=NULL;
int current_date=0;
int gRulesInitialized=0;

#ifdef LONG_PRESS_SUPPORTED
SLongPressRule *gpsLongPressRule;
#endif

LinkedList  gSubscriptionList;
bool gEventRuleOverRidden = false;

fpRuleCallback gfpRuleThreadFn[e_MAX_RULE] = {
    RulesTask,
    TimerTask,
    NULL,
    InsightTask,
    AwayTask,
    NULL,
    NULL,
    CrockpotTask,
    MakersensorTask,
    NULL
};

unsigned int g_SendRuleID=0x00;

#ifdef SIMULATED_OCCUPANCY
static int gAwayRuleCleanupFlag = 0;
SimulatedDevInfo *g_devList = NULL;

/* signifies if the long press away rule is running */
int g_longPressAwayRunning = 0;
/* signifies if the long press has occurred on this device
   which can happen on LS/Dimmer devices. */
bool g_longPressOccurred = false;
/* holds the Rule ID of long press away mode rule which is active */
int g_LongPressAwayRuleID = 0;
static int selfIndex = 0;
#endif

SRuleInfo *gpsRuleList = NULL, *gpsRuleListTail = NULL;
STimerList *gpsTimerList = NULL;
SRulesQueue *gRuleQHead = NULL, *gRuleQTail = NULL;
volatile int gCountdownRuleInLastMinute = 0;
unsigned long gCountdownEndTime = 0;

/* Status of long press rule (stores rule id, if active) */
int gLongPressRuleActive = 0;
#ifdef LONG_PRESS_SUPPORTED
static pthread_t	 gLongPressTid= INVALID_THREAD_HANDLE;
#endif
/* Let RuleAutoOff be notified on app start */
int gCountdownPendingNotification = COUNTDOWN_NOTIFY_PENDING;

static pthread_mutex_t   s_timer_rule_mutex;
static pthread_mutex_t   s_rule_mutex;
static pthread_mutex_t	 s_ruleQ_mutex;
static pthread_t	 s_countdown_rule_thread = -1;
static pthread_t	 s_dst_toggle_thread = INVALID_THREAD_HANDLE;

void *RulesNtpTimeCheckTask(void *args);
bool isAwayRuleActive();
pthread_t g_handlerRuleNtpTimeCheckTask = INVALID_THREAD_HANDLE;

SRuleHandle gRuleHandle[e_MAX_RULE]; /* Includes thread handle for Scheduler at index 0, simple rules are executed by scheduler */

char* g_szRuleTypeStrings[] = {
    "Simple Switch",
    "Time Interval",
    "Motion Controlled",
    "Insight Rule",
    "Away Mode",
    "Notify Me",
    "Countdown Rule",
    "Crockpot Schedule",
    "Maker Sensor Rule",
    "Event Rule",
#if defined(LONG_PRESS_SUPPORTED)
    "Long Press"
#endif
};

char* szWeekDayName[] = {
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
    "Sunday"
};

int daySeconds(void)
{
    time_t rawTime;
    struct tm * timeInfo;
    time(&rawTime);
    timeInfo = localtime (&rawTime);
    int seconds = timeInfo->tm_hour * 60 * 60 + timeInfo->tm_min * 60 + timeInfo->tm_sec;
    return seconds;
}


void setActuation(char *str)
{
    if(!str)
        return;

    memset(g_szActuation, 0, sizeof(g_szActuation));
    strncpy(g_szActuation, str, sizeof(g_szActuation)-1);
}

void setRemote(char *str)
{
    if(!str)
        return;

    memset(g_szRemote, 0, sizeof(g_szRemote));
    strncpy(g_szRemote, str, sizeof(g_szRemote)-1);
}

void GetCalendarDayInfo(int* dayIndex, int* monthIndex, int* year, int* nowSeconds)
{
    time_t rawTime;
    struct tm * timeInfo;
    time(&rawTime);
    timeInfo = localtime (&rawTime);

    *year                = timeInfo->tm_year + 1900;
    *monthIndex          = timeInfo->tm_mon;
    int dayOfWeek        = timeInfo->tm_wday;
    current_date = timeInfo->tm_mday;

    //-map the
    if (0x00 == dayOfWeek)
        dayOfWeek = 0x06;
    else
        dayOfWeek -=1;

    *dayIndex = dayOfWeek;

    *nowSeconds = timeInfo->tm_hour * 60 * 60 + timeInfo->tm_min * 60 + timeInfo->tm_sec;

}


int GetRuleDBHandle()
{
    struct stat FileInfo;
    char tmpBuff[200];

    /*Close the previous g_RulesDB*/
    CloseDB(g_RulesDB);

    APP_LOG("GetRuleDBHandle:", LOG_DEBUG, "Removing OLD Extracted DB File");
    snprintf(tmpBuff,sizeof(tmpBuff),"rm -rf %s",RULE_EXTRACT_DIR);
    system(tmpBuff);

    /*Checking for the existence of the rules DB files in ZIPPED Form*/
    if(stat(RULE_DB_FILE_PATH, &FileInfo) == -1) {
        APP_LOG("GetRuleDBHandle:", LOG_CRIT, "!!!!Rules DB file doesn't exists");
        return 0x01;
    }

    APP_LOG("GetRuleDBHandle:", LOG_DEBUG, "Extract New DB File");

// WEMO-46985: prevent evil data from doing unexpected things.
// A valid rules.db file only as a single entry: temppluginRules.db
//	snprintf(tmpBuff,sizeof(tmpBuff),"unzip %s -d %s",RULE_DB_FILE_PATH,RULE_EXTRACT_DIR);
//	system(tmpBuff);
    mkdir(RULE_EXTRACT_DIR,0777);
    system("unzip -p " RULE_DB_FILE_PATH ">" RULE_DB_URL);

    /*Checking for the existence of the rules DB files.*/
    if(stat(RULE_DB_URL, &FileInfo) == -1) {
        APP_LOG("GetRuleDBHandle:", LOG_CRIT, "!!Extracted Rules DB file doesn't exists");
        return 0x01;
    }
    APP_LOG("GetRuleDBHandle:", LOG_DEBUG, " Init Rule DB");

    if(InitDB(RULE_DB_URL,&g_RulesDB)) {
        APP_LOG("GetRuleDBHandle:", LOG_CRIT, "Cannot Init Rules DB");
        return 0x01;

    }
    APP_LOG("GetRuleDBHandle:", LOG_DEBUG, " Init Rule DB done...");
    return 0x00;
}

int ActivateRuleEngine()
{
    pthread_attr_t rule_attr;
    pthread_attr_init(&rule_attr);
    /* WEMO-46785:detach the thread to avoid any resource leak. */
    pthread_attr_setdetachstate(&rule_attr, PTHREAD_CREATE_DETACHED);
    /*Rule task main thread*/
    int ret = pthread_create(&gRuleHandle[e_SIMPLE_RULE].ruleThreadId, &rule_attr, gfpRuleThreadFn[e_SIMPLE_RULE], 0x00);
    if (0x00 != ret) {
        APP_LOG("UPNP: Rule", LOG_CRIT, "pthread_create: Can not create rule task thread");
        resetSystem();
    } else
        APP_LOG("UPNP: Rule", LOG_DEBUG, "scheduler thread created");

    return 0;
}

void initRule()
{
    int i;

    osUtilsCreateLock(&s_rule_mutex);
    osUtilsCreateLock(&s_timer_rule_mutex);
    osUtilsCreateLock(&s_ruleQ_mutex);

    /*set rule main thread to RULE_ENGINE_RELOAD state*/
    gRestartRuleEngine = RULE_ENGINE_RELOAD;

    /*init rule Handles*/
    for(i = 0; i < e_MAX_RULE; i++) {
        gRuleHandle[i].ruleThreadId = INVALID_THREAD_HANDLE;
        gRuleHandle[i].ruleCnt = 0;
    }

    pthread_attr_t rulesNtpTimeCheck_attr;
    pthread_attr_init(&rulesNtpTimeCheck_attr);
    /* WEMO-46785:detach the thread to avoid any resource leak. */
    pthread_attr_setdetachstate(&rulesNtpTimeCheck_attr, PTHREAD_CREATE_DETACHED);
    /*NTP check should from here since need to cover the service subscription issue because of time sync*/
    pthread_create(&g_handlerRuleNtpTimeCheckTask, &rulesNtpTimeCheck_attr, RulesNtpTimeCheckTask, 0x00);
    APP_LOG("UPNP: Rule", LOG_DEBUG, "Ntp time check thread is created");

    gRulesInitialized = 1;
}

void lockRuleQueue()
{
    osUtilsGetLock(&s_ruleQ_mutex);
}

void unlockRuleQueue()
{
    osUtilsReleaseLock(&s_ruleQ_mutex);
}

void lockTimerRule()
{
    osUtilsGetLock(&s_timer_rule_mutex);
}

void unlockTimerRule()
{
    osUtilsReleaseLock(&s_timer_rule_mutex);
}

void lockRule()
{
    osUtilsGetLock(&s_rule_mutex);
}

void unlockRule()
{
    osUtilsReleaseLock(&s_rule_mutex);
}

int GetRuleIDFlag()
{
    return  g_SendRuleID;
}

void SetRuleIDFlag(int FlagState)
{
    g_SendRuleID = FlagState;
}

extern int g_isTimeSyncByMobileApp;
extern int g_eDeviceType;

void *RulesNtpTimeCheckTask(void *args)
{
    tu_set_my_thread_name( __FUNCTION__ );
    sleep(DELAY_3SEC);

    /*Loop time NTP time sync*/
    while(1) {
        sleep(DELAY_3SEC);

        /*check if NTP is updated and device is connected*/
        if(IsNtpUpdate() && getCurrentClientState()) {
            APP_LOG("Rule", LOG_DEBUG, "NTP updated, rule NTP time check task stop");

            /*Activate Rule Engine*/
            ActivateRuleEngine();
            break;
        }
    }
    return NULL;
}




#define		NTP_UPDATE_TIMEOUT     86400	//24 hours

extern int gNTPTimeSet;

int IsNtpUpdate()
{
    if (IsTimeUpdateByMobileApp() == 0x01)
        return 0x01;
    int year = 0x00, monthIndex = 0x00, seconds = 0x00, dayIndex = 0x00;

    GetCalendarDayInfo(&dayIndex, &monthIndex, &year, &seconds);

    if(year != 2000) {
        gNTPTimeSet = 1;
        APP_LOG("DEVICE:rule", LOG_DEBUG, "************* ISNTPUPDATE NOW YEAR IS NOT 2000");
        return 0x01;
    } else {
        return 0x00;
    }

}

int stopAllExecutorThreads()
{
    APP_LOG("UPNP: Rule", LOG_DEBUG, "Stopping All Executor Threads");

    ERuleType i;
    int ret;

    /*stop all type of rule executers whoes count is atleast one*/
    for(i = e_TIMER_RULE; i < e_MAX_RULE; i++) {
        /*check if rule ndle is valide*/
        if (INVALID_THREAD_HANDLE != gRuleHandle[i].ruleThreadId) {
#ifdef SIMULATED_OCCUPANCY
            if( LONG_PRESS_AWAY_ACTIVE ||
                (i == e_AWAY_RULE && gAwayRuleCleanupFlag))
                return SUCCESS;
            else if(gRuleHandle[i].ruleCnt && !gAwayRuleCleanupFlag) {
                /* set animation to reflect the AWAY mode deactivated case. */
#ifdef PRODUCT_WeMo_Dimmer
                setAnimation(LED_STATE_AWAY_CLOSING);
#elif PRODUCT_WeMo_LightV2
                SetActivityLED(5);
#endif
            }
#endif
            /*cancel thread*/
            ret = pthread_cancel(gRuleHandle[i].ruleThreadId);
            if (0x00 != ret) {
                APP_LOG("UPNP: Rule", LOG_DEBUG, "################### ithread_cancel: Couldnt stop rule thread %d #########################", i);
                pluginUsleep(10000); //10ms sleep
                ret = pthread_cancel(gRuleHandle[i].ruleThreadId);
                if (0x00 != ret)
                    APP_LOG("UPNP: Rule", LOG_DEBUG, "################### ithread_cancel: Couldnt stop rule thread %d second time #########################", i);
            } else {
                APP_LOG("UPNP: Rule", LOG_DEBUG, "################### ithread_cancel: Successfully stop rule thread %d ####################", i);
            }

            gRuleHandle[i].ruleThreadId = INVALID_THREAD_HANDLE;
        }
        //[WEMO-36379] - clear ruleCnt in case thread was already invalid
        gRuleHandle[i].ruleCnt = 0;
    }

    /*mark simple rule count to zero as simple rule are executed by main RulesTask thread which cannot be stopped*/
    gRuleHandle[e_SIMPLE_RULE].ruleCnt = 0;

    return SUCCESS;
}

void freeTimerList()
{
    APP_LOG("UPNP: Rule", LOG_DEBUG, "Free Timer Rule List Memory");
    STimerList *psTimerList = NULL, *psTimerNext = NULL;

    lockTimerRule();

    /*free timer list*/
    psTimerList = gpsTimerList;
    while(psTimerList) {
        psTimerNext = psTimerList->nextTimer;
        free(psTimerList);
        psTimerList = psTimerNext;
    }
    gpsTimerList=NULL;
    APP_LOG("UPNP: Rule", LOG_DEBUG, "Freed timer list");

    unlockTimerRule();
    return;
}

int freeRuleList()
{
    APP_LOG("UPNP: Rule", LOG_DEBUG, "Free Rule Memory");

    SRuleInfo *psRule = NULL, *psNext = NULL;

    lockRule();

    /*free RuleList*/
    psRule = gpsRuleList;
    while(psRule) {
        psNext = psRule->psNext;
        free(psRule);
        psRule = psNext;
    }
    gpsRuleList = NULL;
    APP_LOG("UPNP: Rule", LOG_DEBUG, "Freed Rule list");

    unlockRule();

    return SUCCESS;
}

int StopRuleEngine()
{

    if(gRulesInitialized) {
        /*stop all running rule executer threads*/
        stopAllExecutorThreads();
        /*Free all rule memory*/
        freeRuleList();
        /*Free all timer rule memory*/
        freeTimerList();
        /*Free rule queue*/
        destroyRuleQueue();
#ifdef SIMULATED_OCCUPANCY
        /*cleanup Away Rule data only when it isnt
                  long press away mode rule. */
        if( !LONG_PRESS_AWAY_ACTIVE && !gAwayRuleCleanupFlag)
            cleanupAwayRule(1);
#endif
    }
    return 0;
}

int loadLatLongValues(double *lat, double *lon)
{
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];

    memset(query, 0, sizeof(query));
    snprintf(query, sizeof(query), "SELECT latitude, longitude FROM LOCATIONINFO limit 1;");

    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        if(rowsRules && colsRules) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Fetched %d rows, %d columns", rowsRules, colsRules);
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No entry found");
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            return FAILURE;
        }

        APP_LOG("DEVICE:rule", LOG_DEBUG, "lat: %s, lon: %s", ppsRulesArray[2],  ppsRulesArray[3]);
        *lat = atof(ppsRulesArray[2]);
        *lon = atof(ppsRulesArray[3]);

        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
    }
    return SUCCESS;
}

int loadOffsetValues(int ruleId, int *start, int *end)
{
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];

    memset(query, 0, sizeof(query));
    snprintf(query, sizeof(query), "SELECT OnModeOffset, OffModeOffset FROM RULEDEVICES where RuleID='%d' limit 1;", ruleId);
    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        if(rowsRules && colsRules) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Fetched %d rows, %d columns", rowsRules, rowsRules);
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No entry found");
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            return FAILURE;
        }

        APP_LOG("DEVICE:rule", LOG_DEBUG, "start offset: %s, end offset: %s", ppsRulesArray[2],  ppsRulesArray[3]);
        *start = atof(ppsRulesArray[2]);
        *end = atof(ppsRulesArray[3]);

        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
    }
    return SUCCESS;
}

int calculateSunRiseSetTimes(int date_offset, double lat, double lon, int *rise_hours, int *rise_mins, int *set_hours, int *set_mins)
{
    double trise=0x00, tset=0x00;
    int nowSeconds=0, year=0, month_index=0, dayIndex=0;
    int month;

    GetCalendarDayInfo(&dayIndex, &month_index, &year, &nowSeconds);

    if(month_index == 11) {
        month = 0;
    } else {
        month= month_index+1;
    }
    APP_LOG("Rules", LOG_DEBUG, "year: %d, month: %d, date: %d, day index: %d", year, month, current_date, dayIndex);
    /* calculate sunrise/set -- call the macro from sunriset.h */
    sun_rise_set(year, month, current_date + date_offset, lon, lat, &trise, &tset);

    APP_LOG("Rules", LOG_DEBUG, "trise:%lf, tset:%lf", trise, tset);

    *rise_hours = HOURS(trise);
    *rise_mins = MINUTES(trise);
    APP_LOG("Rules", LOG_DEBUG, "Sunrise timings for the day are: time: %d, minutes is %d, date is %d", *rise_hours,
            *rise_mins, current_date + date_offset);

    *set_hours = HOURS(tset);
    *set_mins = MINUTES(tset);
    APP_LOG("Rules", LOG_DEBUG, "Sunset timings for the day are: time: %d, minutes is %d, date is %d", *set_hours,
            *set_mins, current_date+ date_offset);
    return SUCCESS;
}
/*
This function calculate the date of the given day in week of month.
Arguments required are week,day,month and year fetch from TZ string.
source: http://cboard.cprogramming.com/c-programming/90130-determining-date-%5Bnth%5D-%5Bday%5D-%5Bmonth%5D.html
*/
int nthWeekdayOfMonth(int week, int wday, int month, int year)
{
    struct tm zero = {0};
    struct tm worker = {0};
    struct tm *result = &worker;
    time_t t;
    *result = zero;
    if(week ==5) {
        month++;
        week=-1;
    } else
        week--;
    /*
     * Build the first day of the month.
     */
    worker.tm_year = year;
    worker.tm_mon  = month;
    worker.tm_mday = 1;
    /*
     * Create the calendar time. Then normalize it back to broken-down form.
     */
    t = mktime(result);
    if ( t == (time_t)-1 ) {
        return 0;
    }
    /*
     * Go to Nth week, where N is an offset from the first week.
     *   N =  1 is the 2nd week
     *   N =  0 is the 1st week
     *   N = -1 is the last week of the previous month
     */
    if ( wday != result->tm_wday ) {
        result->tm_mday += wday - result->tm_wday + 7 * (wday < result->tm_wday);
    }
    result->tm_mday += 7 * week;
    /*
     * Re-normalize it.
     */
    t = mktime(result);
    if ( t == (time_t)-1 ) {
        return 0;
    }
    return result->tm_mday;
}
/*
This thread will be triggered only if DST transition is going to happen on current day (Time remaining < ONE_DAY_SECOND).
Sleep for Time remaining in transition , set gDstEnable, write LASTDSTENABLE in to flash and reload the rule engine.
gDstEnable = 0 (During DST phase)
gDstEnable = 1 (When DST not enabled)
*/
void *dstToggleThread(void *args)
{
    unsigned int dstToggleTime=0;
    int isDst=0;
    if(NULL != args) {
        dstToggleTime = *((int*)args);
        isDst=*((int*)args+1);
        free(args);
        args = NULL;
    } else
        return NULL;
    APP_LOG("UPNP: DEVICE",LOG_DEBUG, "DST toggle thread sleep %d to toggle dst %d",dstToggleTime, !isDst);
    while(dstToggleTime > 600) {
        pluginUsleep((unsigned int)(600*1000000));
        dstToggleTime -= 600;
        APP_LOG("UPNP: DEVICE",LOG_DEBUG, "Remaining sleep %d secs", dstToggleTime);
    }
    APP_LOG("UPNP: DEVICE",LOG_DEBUG, "Remaining sleep %d secs", dstToggleTime);
    pluginUsleep(dstToggleTime*1000000);

    char dstenable[SIZE_16B];
    memset(dstenable, 0x0, sizeof(dstenable));
    gDstEnable=isDst;
    snprintf(dstenable, sizeof(dstenable), "%d", gDstEnable);
    SetBelkinParameter(LASTDSTENABLE, dstenable);
    APP_LOG("Rule", LOG_DEBUG, "DST is going to changed today, rule to restart to get executed again");
    gRestartRuleEngine = RULE_ENGINE_RELOAD;
    s_dst_toggle_thread = INVALID_THREAD_HANDLE;
    return NULL;
}
/*
Calculate DST transition time in utc using given parameters.
*/
unsigned int calDstToggleTime(int hour, int month,int week,int day, int year)
{
    unsigned int dstDate = 0;
    struct tm mkTm = { 0 };
    //If variable hour has invalid value(-1), it will take default transition hour value equal to 2.
    if(hour==-1)
        hour=2;
    int dstdate = nthWeekdayOfMonth(week, day, month-1, year);
    if(!dstdate) {
        APP_LOG("Rule", LOG_ERR, "Unable to compute time for hr: %d, mon: %d, week: %d, day: %d, year: %d",
                hour, month, week, day, year);
        return 0;
    }
    mkTm.tm_year = year;
    mkTm.tm_wday = day;
    mkTm.tm_mday=dstdate;
    mkTm.tm_mon = month-1;
    mkTm.tm_hour = hour;
    mkTm.tm_min = 0;
    mkTm.tm_sec = 0;
    dstDate = mktime(&mkTm);
    return dstDate;
}

/*
This function is to Fetch the DST start and end time information from /etc/TZ file and use the information to calculate time remaining in transition from current utc time.
Check if DST transition is going to happen on current day (Time remaining < ONE_DAY_SECONDS).
If yes : Create a thread which will sleep until the dst transition and reload the rule engine on exact time of transition.
If NO : Exit from the function.
*/
void checkAndUpdateDststat()
{
    struct tm *info;
    time_t rawtime;
    char buffer[SIZE_128B] = {0};
    int ret=0,isDst =0,dstToggleTime=0,currentTime=0;
    int sMonth=0,sWeek=0,sDay=0,sHour=-1,adjustment=0;
    int eMonth=0,eWeek=0,eDay=0,eHour=-1,year=0;
    unsigned int dstDate = 0;
    pthread_attr_t dst_toggle_attr;
    char temp[SIZE_16B]= {0}, startTimeStr[SIZE_16B]= {0},endTimeStr[SIZE_16B]= {0};
    FILE *fp = NULL;
    currentTime = time(&rawtime);
    info = localtime( &rawtime );
    year=info->tm_year;
    isDst=info->tm_isdst;
    APP_LOG("UPNP: DEVICE",LOG_DEBUG, "System time in utc is %d and dst status is %d",currentTime,isDst);
    //Fetch dst end time using tz file
    fp = fopen("/tmp/TZ", "r");
    if(!fp) {
        APP_LOG("UPNP: DEVICE",LOG_DEBUG, "Failed to open TZ File");
        return;
    }
    //Read DST string in buffer from timezone file eg: EST5EDT,M3.2.0,M10.1.0, break it in startTimeStr,endTimeStr using M as a token.
    if(fgets(buffer, sizeof(buffer), fp)) {
        sscanf(buffer, "%[^M]M%[^M]M%[^M]",temp,startTimeStr,endTimeStr);
    }
    fclose(fp);
    //DST end time string M10.1.0
    if(3 > sscanf(endTimeStr,"%d.%d.%d/%d",&eMonth,&eWeek,&eDay,&eHour)) {
        APP_LOG("UPNP: DEVICE",LOG_DEBUG, "Failed to fetch DST end Time information");
        return;
    }
    //DST start Time string M3.2.0
    if(3 > sscanf(startTimeStr,"%d.%d.%d/%d",&sMonth,&sWeek,&sDay,&sHour)) {
        APP_LOG("UPNP: DEVICE",LOG_DEBUG, "Failed to fetch DST start Time information");
        return;
    }

    /*
    ** As we will be computing the difference between UTC seconds when DST is ON and when DST is off
    ** we should adjust one timer by 3600?? seconds to make the right comparision
    */

    adjustment = ((isDst*3600));
    dstDate = calDstToggleTime(sHour,sMonth,sWeek,sDay,year);
    if(!dstDate) {
        return;
    }
    dstDate=dstDate-adjustment;

    if(isDst == 1) {
        /* DST end lies in the next year? */
        if((sMonth > eMonth) && (currentTime > dstDate)) {
            year++;
            APP_LOG("UPNP: DEVICE",LOG_DEBUG, "DST toggle: DST transition is in next year %d",year+1900);
            return;
        }
        dstDate = calDstToggleTime(eHour,eMonth,eWeek,eDay,year);
        APP_LOG("UPNP: DEVICE",LOG_DEBUG, "DST toggle: day %d %s year %d having act time %u Current time %d", eDay, convertMonth(eMonth-1), year+1900, dstDate,currentTime);
    } else {
        if(currentTime > dstDate) {
            year++;
            APP_LOG("UPNP: DEVICE",LOG_DEBUG, "DST toggle: DST transition is in next year %d",year+1900);
            return;
        }
        dstDate = calDstToggleTime(sHour,sMonth,sWeek,sDay,year);
        APP_LOG("UPNP: DEVICE",LOG_DEBUG, "DST toggle: day %d %s year %d having act time %u Current time %d", sDay, convertMonth(sMonth-1), year+1900, dstDate,currentTime);
    }
    dstToggleTime= (dstDate - currentTime);
    APP_LOG("UPNP: DEVICE",LOG_DEBUG, "DST toggle time %d",dstToggleTime);

    if(dstToggleTime <= ONE_DAY_SECONDS) {
        adjustment = ((isDst*3600));
        dstToggleTime=dstToggleTime-adjustment;
        int *arg = (int*)CALLOC(2, sizeof(int));
        *arg = dstToggleTime;;
        arg[1]= isDst;

        if(s_dst_toggle_thread == INVALID_THREAD_HANDLE) {
            pthread_attr_init(&dst_toggle_attr);
            pthread_attr_setdetachstate(&dst_toggle_attr,PTHREAD_CREATE_DETACHED);

            ret = pthread_create(&s_dst_toggle_thread, &dst_toggle_attr, dstToggleThread, (void *)arg);
            if (0x00 != ret) {
                APP_LOG("UPNP: Rule", LOG_DEBUG, "Could not create dst toggle thread");
                resetSystem();
            } else {
                APP_LOG("UPNP: Rule", LOG_DEBUG, "dst toggle thread created");
            }
        } else
            APP_LOG("UPNP: Rule", LOG_DEBUG, "Dst toggle thread already running");
    }
    return;
}

int UpdateSunriseSunset (SRuleInfo *psRule)
{
    int rise_hours = 0x00;
    int rise_mins = 0x00;
    int set_hours = 0x00;
    int set_mins = 0x00;
    int timezone_offset = 0x00;
    int time_hours = 0x00;
    int start_offset = 0, end_offset = 0;
    float time_minutes = 0.0;
    float adjusted_time_zone = 0.0;
    int start_time = psRule->startTime;
    int end_time = 0;
    double lon=0x00, lat=0x00;
    int x=0;
    unsigned char isOvernightNow;
    if(!psRule->endTime) {
        end_time = psRule->startTime + psRule->ruleDuration; /* Old DB, No EndTime */
    } else {
        end_time = psRule->endTime; /* EndTime from new DB */
    }
    APP_LOG("Rules", LOG_DEBUG, "*************UpdateSunriseSunset***************");
    APP_LOG("DEVICE:rule", LOG_DEBUG, "initial psRule->startTime: %d, psRule->ruleDuration: %d, end_time: %d,psRule->endTime: %d",\
            psRule->startTime, psRule->ruleDuration,end_time,psRule->endTime);

    loadLatLongValues(&lat, &lon);
    APP_LOG("Rules", LOG_DEBUG, "Latitude is %lf, Longitude is %lf", lat, lon);

    loadOffsetValues(psRule->ruleId, &start_offset, &end_offset);

    /* Time zone adjustment based on stored value */
    /* As we are storing value in float ex. 5.5 for 5:30, we need to convert .5 into minutes,
     * So need to multiply with 60 before converting into Sec.
     */
    APP_LOG("DEVICE:rule", LOG_DEBUG, "g_lastTimeZone: %f, gDstSupported: %d, gDstEnable: %d", g_lastTimeZone, gDstSupported, gDstEnable);

    if(gDstSupported && !gDstEnable) {
        adjusted_time_zone =  g_lastTimeZone + 1;

        APP_LOG("DEVICE:rule", LOG_DEBUG, "g_lastTimeZone: %f, adjusted_time_zone: %f", g_lastTimeZone, adjusted_time_zone);
    } else
        adjusted_time_zone =  g_lastTimeZone;

    time_hours =  (int)adjusted_time_zone;
    time_minutes = adjusted_time_zone - time_hours;
    timezone_offset = ((time_hours * 60) +(time_minutes*60)) * 60;

    calculateSunRiseSetTimes(0, lat, lon, &rise_hours, &rise_mins, &set_hours, &set_mins);

    APP_LOG("Rules", LOG_DEBUG, "/* startTime calculation */");
    /* Check whether Sunrise timer and update new sunrise time*/
    if(start_time % UNITS_DIGIT_DET == 1) {
        psRule->startTime = timezone_offset +(((rise_hours * 60) +rise_mins) * 60);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Updated Sunrise time: %d", psRule->startTime);

        psRule->startTime += start_offset + 1; /* +1 to satisfy the condition for recalculation */
        APP_LOG("DEVICE:rule", LOG_DEBUG, "After Updating Offset time: %d", psRule->startTime);
    } else if(start_time % UNITS_DIGIT_DET == 2) {
        /* Check whether Sunset timer and update new sunset time*/
        psRule->startTime = timezone_offset +(((set_hours * 60) +set_mins) * 60);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Updated Sunset time: %d", psRule->startTime);

        psRule->startTime += start_offset + 2; /* +2 to satisfy the condition for recalculation */
        APP_LOG("DEVICE:rule", LOG_DEBUG, "After Updating Offset time: %d", psRule->startTime);
    }
    if(psRule->ruleType != e_SIMPLE_RULE) {
        /* WEMO-47250 rule will not trigger if its overnightness changed due to DST or sunset/sunrise time change */
        int ruleEndTime = end_time;
        /* calculate sunset/surise endtime for today even if its a overnight rule */
        if(end_time % UNITS_DIGIT_DET == 1) {
            ruleEndTime = timezone_offset +(((rise_hours * 60) +rise_mins) * 60) + end_offset + 1;
        } else if(end_time % UNITS_DIGIT_DET == 2) {
            ruleEndTime = timezone_offset +(((set_hours * 60) +set_mins) * 60) + end_offset + 2;
        }

        isOvernightNow = ((psRule->startTime > ruleEndTime) && (psRule->ruleDuration != ONE_DAY_SECONDS))?1:0;
        /* if overnightness changed or Rule start and end time is same */
        if((isOvernightNow != psRule->isOvernight)||(psRule->startTime==ruleEndTime)) {
            psRule->isInvalidToday = 1;
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule %d is invalid for today",psRule->ruleId);
        } else if(psRule->isInvalidToday == 1) {
            /* back to normal overnightness, so making rule valid*/
            psRule->isInvalidToday = 0;
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule %d is valid now",psRule->ruleId);
        }
    }
    /* Depend on the overnight calculation from values in DB as end time calculation depends on overnight
       flag and correctly calculating overnight flag for a particular day would require end time, thus
       causing cyclic dependency.  */

    APP_LOG("Rules", LOG_DEBUG, "/* End time calculation */");
    if(end_time % UNITS_DIGIT_DET == 1) {
        if(psRule->isOvernight) {
            calculateSunRiseSetTimes(1, lat, lon, &rise_hours, &rise_mins, &set_hours, &set_mins);
            x = ONE_DAY_SECONDS;
        }
        end_time = timezone_offset +(((rise_hours * 60) +rise_mins) * 60); /* sunrise time */
        psRule->ruleDuration = end_time + x - psRule->startTime;
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Updated Sunrise time: %d, duration: %d", end_time, psRule->ruleDuration);

        psRule->ruleDuration += end_offset + 1; /* comply with duration based calculation */
        psRule->endTime = end_time + end_offset + 1;
        APP_LOG("DEVICE:rule", LOG_DEBUG, "After Updating Offset, end time: %d, duration: %d", (end_time+end_offset), psRule->ruleDuration);
    } else if(end_time % UNITS_DIGIT_DET == 2) {
        /* Check whether Sunset timer and update new sunset time*/
        if(psRule->isOvernight) {
            calculateSunRiseSetTimes(1, lat, lon, &rise_hours, &rise_mins, &set_hours, &set_mins);
            x = ONE_DAY_SECONDS;
        }
        end_time = timezone_offset +(((set_hours * 60) +set_mins) * 60);
        psRule->ruleDuration = end_time + x - psRule->startTime;
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Updated Sunset time: %d, duration: %d", end_time, psRule->ruleDuration);

        psRule->ruleDuration += end_offset + 2; /* comply with duration based calculation */
        psRule->endTime = end_time + end_offset + 2;
        APP_LOG("DEVICE:rule", LOG_DEBUG, "After Updating Offset, end time: %d, duration: %d", (end_time+end_offset), psRule->ruleDuration);
    } else {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "fixed end time");
        if(psRule->endTime != 0) { /* New DB */
            APP_LOG("DEVICE:rule", LOG_DEBUG, "/* New DB */");
            if(psRule->isOvernight) {
                x = ONE_DAY_SECONDS;
            }
            psRule->ruleDuration = psRule->endTime - psRule->startTime + x; /* in case of fixed end time */
        } else { /* Old DB */
            APP_LOG("DEVICE:rule", LOG_DEBUG, "/* Using Old DB */");

        }
    }

    APP_LOG("DEVICE:rule", LOG_DEBUG, "psRule->endTime: %d, psRule->ruleDuration: %d, psRule->startTime: %d, X: %d, end_time: %d",\
            psRule->endTime, psRule->ruleDuration, psRule->startTime, x, end_time);
    APP_LOG("Rules", LOG_DEBUG, "*************UpdateSunriseSunset***************");

    return SUCCESS;
}

SRuleInfo* getNewRuleListNode()
{
    SRuleInfo *psRule=NULL;

    psRule = (SRuleInfo *)CALLOC(1, sizeof(SRuleInfo));
    if(!psRule) {
        APP_LOG("Rule", LOG_CRIT, "Rule node allocation failure");
        resetSystem();
    }

    lockRule();

    if(gpsRuleList == NULL) {
        gpsRuleList = psRule;
        gpsRuleListTail = psRule;
    } else {
        gpsRuleListTail->psNext = psRule;
        gpsRuleListTail = psRule;
    }

    unlockRule();

    return psRule;
}

int getRuleType(char *str, ERuleType *ruleType)
{
    int i=0;
    int retVal = FAILURE;

    for(i=e_SIMPLE_RULE; i<e_MAX_RULE; i++) {
        if(!strcmp(str, g_szRuleTypeStrings[i])) {
            *ruleType = i;
            retVal = SUCCESS;
            APP_LOG("Rule", LOG_DEBUG, "Rule type: %d for %s", i, g_szRuleTypeStrings[i]);
            break;
        }
    }

    return retVal;

}

/*
FW -  0: Mon 6: Sun
App - 1: SUN 7: SAT
*/
inline int FW_DAY_INDEX(int appDayIndex)
{
    if(appDayIndex == 1) //Sunday
        return 6;
    else
        return appDayIndex-2;
}

inline int APP_DAY_INDEX(int FwDayIndex)
{
    if(FwDayIndex == 6) //Sunday
        return 1;
    else
        return FwDayIndex+2;
}

inline int DAY_INDEX_MASK(int x)
{
    if(x<0)
        return (1<<6);
    else
        return (1<<(x));
}


int LoadRulesTable(char *pszruleId, char *pszruleType, char *pszDeviceId)
{
    char query[SIZE_256B] = {0,};
    char processDB = true;
    ERuleType ruleType=0, j=0;
    SRuleInfo* psRule=NULL;
    int ruleDevicesArraySize=0, rowsRuleDevices=0, colsRuleDevices=0;
    char  **ppsRuleDevicesArray=NULL;
    int ret = -1;
    unsigned char queryMod = false;

    if(!pszDeviceId) {
        APP_LOG("DEVICE:rule", LOG_ERR, "Invalid UDN");
        return FAILURE;
    }

    memset(query, 0, sizeof(query));
    sqlite3_snprintf(sizeof(query), query,
#if defined(PRODUCT_WeMo_Dimmer)
                     "SELECT DayID, StartTime, RuleDuration, EndTime, ZBCapabilityStart, ZBCapabilityEnd FROM RULEDEVICES WHERE RuleID='%q' AND DeviceId='%q';",
#else
                     "SELECT DayID, StartTime, RuleDuration, StartAction, EndAction, EndTime FROM RULEDEVICES WHERE RuleID='%q' AND DeviceId='%q';",
#endif
                     pszruleId, pszDeviceId);
    APP_LOG("DEVICE:rule", LOG_ERR, "query:%s", query);

    if(FAILURE == getRuleType(pszruleType, &ruleType)) {
        APP_LOG("DEVICE:rule", LOG_ERR, "Get Rule type failed");
        return FAILURE;
    }

    ret = WeMoDBGetTableData(&g_RulesDB, query, &ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices);

    /* If 1st query fails */
    if (ret) {
        APP_LOG("DEVICE:rule", LOG_ERR, "Get Table data failed with EndTime. Changing the query");

        /* modify query */
        memset(query, 0, sizeof(query));
        sqlite3_snprintf(sizeof(query), query,
#if defined(PRODUCT_WeMo_Dimmer)
                         "SELECT DayID, StartTime, RuleDuration, ZBCapabilityStart, ZBCapabilityEnd FROM RULEDEVICES WHERE RuleID='%q' AND DeviceId='%q';",
#else
                         "SELECT DayID, StartTime, RuleDuration, StartAction, EndAction FROM RULEDEVICES WHERE RuleID='%q' AND DeviceId='%q';",
#endif
                         pszruleId, pszDeviceId);
        APP_LOG("DEVICE:rule", LOG_ERR, "query:%s", query);
        queryMod = true;

        if (WeMoDBGetTableData (&g_RulesDB, query, &ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices)) {
            processDB = false;
            APP_LOG ("DEVICE:rule", LOG_ERR, "Get Table data failed without EndTime");
        }
    }

    if (true == processDB) {
        if(rowsRuleDevices && colsRuleDevices) {
#if defined(LONG_PRESS_SUPPORTED)
            if(e_LONGPRESS_RULE == ruleType) {
                /* If we find an active long press rule, simply start control point */
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Long press rule: %s found", pszruleId);

                /* Rule id can't be zero and there can be only 1 active long press rule per device,
                   store in active status for quick query later */
                gLongPressRuleActive = atoi(pszruleId);
                simulatedStartControlPoint();
                CtrlPointDiscoverDevices();
                EnableContrlPointRediscover(true);
            }
#endif
            psRule = getNewRuleListNode();

            if(psRule) {
                j=colsRuleDevices;
                psRule->ruleType = ruleType;
                psRule->ruleId = atoi(pszruleId);
                psRule->isInvalidToday = 0;/* all rules are valid for today by default */
                psRule->startTime = atoi(ppsRuleDevicesArray[++j]);
                psRule->ruleDuration = atoi(ppsRuleDevicesArray[++j]);
#ifdef SIMULATED_OCCUPANCY
                if(ruleType == e_AWAY_RULE) {
                    /* init is required here because this is place where we get Away mode rule type for first time */
                    simulatedOccupancyInit();
                }
#endif
#ifndef PRODUCT_WeMo_Dimmer

                psRule->startAction = atoi(ppsRuleDevicesArray[++j]);
                psRule->endAction = atoi(ppsRuleDevicesArray[++j]);
#endif
                if (true == queryMod) {
                    /* Initializing the endTime to 0 when EndTime is not read
                     * from the DB since further calculation depends this.
                     */
                    psRule->endTime = 0;
                    psRule->isOvernight = (((psRule->startTime + psRule->ruleDuration)> ONE_DAY_SECONDS) && (psRule->ruleDuration != ONE_DAY_SECONDS))?1:0;
                } else {
                    psRule->endTime = atoi(ppsRuleDevicesArray[++j]);
                    psRule->isOvernight = ((psRule->startTime > psRule->endTime) && (psRule->ruleDuration != ONE_DAY_SECONDS))?1:0;
                }
#ifdef PRODUCT_WeMo_Dimmer
                /* extract state:brightness:fader set in ZBCapabilityStart */
                char *attrStr = ppsRuleDevicesArray[++j];
                char *delim = ":";
                char *token = NULL;

                if(attrStr && strlen(attrStr)>0
                   && strcmp(attrStr, "-1")) {
                    token = strtok(attrStr, delim);
                    if(token)
                        psRule->startAction = atoi(token);
                    else {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid start action, assuming 1");
                        psRule->startAction = 1;
                    }
                    token = strtok(NULL, delim);
                    if(token)
                        psRule->startBrightness = atoi(token);
                    else {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid start brightness, assuming 100");
                        psRule->startBrightness= 100;
                    }
                    token = strtok(NULL, delim);
                    if(token)
                        psRule->startFader = atoi(token);
                    else {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid start fader, assuming 0");
                        psRule->startFader = 0;
                    }
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Start state:%d brightness:%u fader:%u", psRule->startAction, psRule->startBrightness, psRule->startFader);
                }

                attrStr = ppsRuleDevicesArray[++j];
                if(attrStr && strlen(attrStr)>0
                   && strcmp(attrStr, "-1")) {
                    token = strtok(attrStr, delim);
                    if(token)
                        psRule->endAction = atoi(token);
                    else {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid end action, assuming 0");
                        psRule->endAction = 0;
                    }
                    token = strtok(NULL, delim);
                    if(token)
                        psRule->endBrightness = atoi(token);
                    else {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid end brightness, assuming 1");
                        psRule->endBrightness = 1;
                    }
                    token = strtok(NULL, delim);
                    if(token)
                        psRule->endFader = atoi(token);
                    else {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid end fader, assuming 0");
                        psRule->endFader = 0;
                    }
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "End state:%d brightness:%u fader:%u", psRule->endAction, psRule->endBrightness, psRule->endFader);
                }
                else {
                    /* App doesn't want to fix this, so needs to catch the wrong value sent, and adjust it */
                    /* if ZBCapabilityEnd on/off field is -1, then ignore end action */
                    /* by setting ruleDuration to 0 */
                    psRule->endAction = -1;
                    psRule->ruleDuration = 0;
                    if (psRule->ruleType == e_TIMER_RULE) {
                        psRule->ruleType = e_SIMPLE_RULE;
                    }
                }
#endif

                psRule->isSunriseSunset = ((psRule->startTime % UNITS_DIGIT_DET == 1) || (psRule->startTime % UNITS_DIGIT_DET == 2) \
                                           || (psRule->ruleDuration % UNITS_DIGIT_DET == 1)|| (psRule->ruleDuration % UNITS_DIGIT_DET == 2) \
                                           || (psRule->endTime % UNITS_DIGIT_DET == 1)|| (psRule->endTime % UNITS_DIGIT_DET == 2));

#if defined(LONG_PRESS_SUPPORTED)
                if(ruleType == e_LONGPRESS_RULE) {
                    psRule->activeDays = 0x7F; //All day rule
                } else
#endif
                {
                    ruleDevicesArraySize = (rowsRuleDevices+1) * colsRuleDevices;
                    for(j=colsRuleDevices; j < ruleDevicesArraySize; j+=colsRuleDevices) {
                        psRule->activeDays |= (1<<FW_DAY_INDEX((atoi(ppsRuleDevicesArray[j]))));
                    }
                }

                APP_LOG("DEVICE:rule", LOG_DEBUG,\
                        "RULE id: %d, type: %d, start: %d, dur: %d, stAct: %d, endAct: %d, night: %d, sun: %d, active:%02X, endTime : %d",\
                        psRule->ruleId,  psRule->ruleType, psRule->startTime, psRule->ruleDuration,\
                        psRule->startAction, psRule->endAction, psRule->isOvernight, psRule->isSunriseSunset, psRule->activeDays,psRule->endTime);
            }
        }

        WeMoDBTableFreeResult(&ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices);
    }


    return SUCCESS;
}

int FetchTargetDeviceId(char *psRuleId, char *psDeviceId)
{
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];

    memset(query, 0, sizeof(query));
    sqlite3_snprintf(sizeof(query), query, "SELECT DeviceID FROM devicecombination WHERE SensorID='%q' AND RuleID='%q' limit 1;", g_szUDN_1, psRuleId);

    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        if(rowsRules && colsRules) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Device Id found: %s", ppsRulesArray[1]);
            strncpy(psDeviceId, ppsRulesArray[1], SIZE_256B);  // <-----<<< DANGEROUS

            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            return FAILURE;
        }
    }

    return SUCCESS;
}

#if defined(PRODUCT_WeMo_Dimmer)
void addNewTimerInSortedTimerList(int time, int action, unsigned int brightness, unsigned int faderTime)
#else
void addNewTimerInSortedTimerList(int time, int action)
#endif
{
    STimerList *psTimerList = NULL;
    STimerList *psNewTimerNode = NULL;

    lockTimerRule();
    psNewTimerNode = (STimerList*)CALLOC(1, sizeof(STimerList));
    if(!psNewTimerNode) {
        APP_LOG("Rule", LOG_CRIT, "Memory allocation failure");
        resetSystem();
    }

    psNewTimerNode->time = time;
    psNewTimerNode->action = action;
#ifdef PRODUCT_WeMo_Dimmer
    psNewTimerNode->brightness = brightness;
    psNewTimerNode->faderTime = faderTime;
#endif
    psNewTimerNode->nextTimer = NULL;

    psTimerList = gpsTimerList;
    /*add first node*/
    if(NULL == psTimerList) {
        gpsTimerList = psNewTimerNode;
        goto RETURN;
    }
    /*add at the begining*/
    else if(time < psTimerList->time) {
        psNewTimerNode->nextTimer = psTimerList;
        gpsTimerList = psNewTimerNode;
        goto RETURN;
    }
    /*add in between nodes*/
    else {
        while(NULL != psTimerList->nextTimer) {
            if((time >= psTimerList->time) && (time < psTimerList->nextTimer->time)) {
                psNewTimerNode->nextTimer = psTimerList->nextTimer;
                psTimerList->nextTimer = psNewTimerNode;
                goto RETURN;
            }
            psTimerList = psTimerList->nextTimer;
        }
    }
    /*add at the end*/
    psTimerList->nextTimer = psNewTimerNode;

RETURN:
    unlockTimerRule();
    return;
}

int createTimerRuleList(int dayIndex)
{
    SRuleInfo *psRuleInfo = NULL;
    STimerList *psTimerList = NULL;
    int time = 0, action = 0;

    /*Free all timer rule memory*/
    freeTimerList();

    /*initialise ruleinfo pointers with global pointer*/
    psRuleInfo = gpsRuleList;

    /*iterate till last node and it is timer rule type*/
    while(psRuleInfo) {
        /*check if it is timer rule*/
        if(e_TIMER_RULE == psRuleInfo->ruleType) {
            /*check if overnight rule present*/
            if(psRuleInfo->isOvernight) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "overnight timer rule");
                /*check if overnight rule coming for previous day*/
                if(psRuleInfo->activeDays & DAY_INDEX_MASK(dayIndex-1)) {
                    /*add only end time and action in timer list*/
                    time = (psRuleInfo->startTime + psRuleInfo->ruleDuration) - ONE_DAY_SECONDS;
                    action = psRuleInfo->endAction;
#if defined(PRODUCT_WeMo_Dimmer)
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "overnight: time:%d, action:%d, fadeOutTime:%u", time, action, psRuleInfo->endFader);
                    addNewTimerInSortedTimerList(time, action, psRuleInfo->endBrightness, psRuleInfo->endFader);
#else
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "overnight: time:%d, action:%d", time,action);
                    addNewTimerInSortedTimerList(time, action);
#endif
                }
            }

            /*if rule present in active day*/
            if(psRuleInfo->activeDays & DAY_INDEX_MASK(dayIndex)) {
                /*add start time and action in timer list*/
                time = psRuleInfo->startTime;
                /* WEMO-52151 : adding midnight start time 20 sec delayed so that it will get executed for sure as timer list will be created newly @ 00:00:11 time*/
                if(time == 0) {
                    time+=20;
                }
                action = psRuleInfo->startAction;
#if defined(PRODUCT_WeMo_Dimmer)
                APP_LOG("DEVICE:rule", LOG_DEBUG, "overnight: time:%d, action:%d, fadeInTime:%u, brightness to fade-in to:%u", time, action, psRuleInfo->startFader, psRuleInfo->startBrightness);
                addNewTimerInSortedTimerList(time, action, psRuleInfo->startBrightness, psRuleInfo->startFader);
#else
                APP_LOG("DEVICE:rule", LOG_DEBUG, "start: time:%d, action:%d", time,action);
                addNewTimerInSortedTimerList(time, action);
#endif //PRODUCT_WeMo_Dimmer


                if (psRuleInfo->endTime) {

                    if (psRuleInfo->startTime <= psRuleInfo->endTime) {
                        psRuleInfo->ruleDuration =  psRuleInfo->endTime - psRuleInfo->startTime;
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "startTime %d endTime %d ruleDuration %d",
                                psRuleInfo->startTime,psRuleInfo->endTime, psRuleInfo->ruleDuration);
                    } else {
                        psRuleInfo->ruleDuration =  ONE_DAY_SECONDS - (psRuleInfo->startTime - psRuleInfo->endTime);
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "startTime %d endTime %d ruleDuration %d",
                                psRuleInfo->startTime,psRuleInfo->endTime, psRuleInfo->ruleDuration);
                    }

                }

                /*add end time and action in timer list*/
                time = psRuleInfo->startTime + psRuleInfo->ruleDuration;
                action = psRuleInfo->endAction;
#if defined(PRODUCT_WeMo_Dimmer)
                APP_LOG("DEVICE:rule", LOG_DEBUG, "overnight: time:%d, action:%d fadeOutTime:%u", time, action, psRuleInfo->endFader);
                addNewTimerInSortedTimerList(time, action, psRuleInfo->endBrightness, psRuleInfo->endFader);
#else
                APP_LOG("DEVICE:rule", LOG_DEBUG, "end: time:%d, action:%d", time,action);
                addNewTimerInSortedTimerList(time, action);
#endif //PRODUCT_WeMo_Dimmer
            }
        }
        /*Move to next rule node*/
        psRuleInfo = psRuleInfo->psNext;
    }

    /*printf gpsTimerList*/
    psTimerList = gpsTimerList;
    while(psTimerList) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "TimerList:  time:%d, action:%d", psTimerList->time,psTimerList->action);
        psTimerList = psTimerList->nextTimer;
    }

    return SUCCESS;
}

void sendLongPressNotify(int action, int count, int state, char *targets)
{
    char *parameter[4] = {"longPressRuleDeviceCnt", "longPressRuleAction", "longPressRuleState", "longPressRuleDeviceUdn"};
    char *value[4];

    value[0] = (char *)MALLOC(sizeof(int));
    if (value[0] == NULL) {
        APP_LOG("DEVICE:rule", LOG_ERR, "MALLOC failed!!");
        return;
    }
    value[1] = (char *)MALLOC(sizeof(int));
    if (value[1] == NULL) {
        free(value[0]);
        APP_LOG("DEVICE:rule", LOG_ERR, "MALLOC failed!!");
        return;
    }
    value[2] = (char *)MALLOC(sizeof(int));
    if (value[2] == NULL) {
        free(value[0]);
        free(value[1]);
        APP_LOG("DEVICE:rule", LOG_ERR, "MALLOC failed!!");
        return;
    }

    if (value[0] == NULL) {
        free(value[0]);
        free(value[1]);
        free(value[2]);
        APP_LOG("DEVICE:rule", LOG_ERR, "MALLOC failed!!");
        return;
    }

    snprintf(value[0], sizeof(int), "%d", count);
    snprintf(value[1], sizeof(int), "%d", action);
    snprintf(value[2], sizeof(int), "%d", state);
    value[3] = strdup(targets);

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Target device lists: %s", value[3]);
    UpnpNotify(device_handle,
               SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].UDN,
               SocketDevice.service_table[PLUGIN_E_RULES_SERVICE].ServiceId,
               (const char **)parameter,
               (const char **)value,
               4);

    free(value[0]);
    free(value[1]);
    free(value[2]);
    free(value[3]);
}

#ifdef SIMULATED_OCCUPANCY

/************************************************************************
 * Function: cleanUpForLongPressAway
 *    cleans up for long press away rule related data.
 *  Parameters:
 *    N.A.
 *  Return:
 *    N.A.
***********************************************************************/
void
cleanUpForLongPressAway(void)
{
    if(g_longPressAwayRunning) {
        g_longPressAwayRunning = 0;
        SetBelkinParameter(LONG_PRESS_AWAY_MODE_STATE, "0");
#ifdef PRODUCT_WeMo_Dimmer
        /* set animation to reflect the AWAY mode deactivated case. */
        setAnimation(LED_STATE_AWAY_CLOSING);
#elif PRODUCT_WeMo_LightV2
        SetActivityLED(5);
#endif
    }
    if(g_longPressOccurred) {
        g_longPressOccurred = false;
        SetBelkinParameter(LONG_PRESS_HAS_OCCURRED, "0");
    }
    g_LongPressAwayRuleID = 0;
    SetBelkinParameter(LONG_PRESS_AWAY_RULE_ID, "0");
    APP_LOG("DEVICE:rule", LOG_DEBUG, "Cleaning up for Long Press Away Rule Done!!");
#ifdef LONG_PRESS_SUPPORTED
    if (gpsLongPressRule) {
        int count = (gpsLongPressRule != NULL)?gpsLongPressRule->count:0;
        char *udn = (count != 0)?gpsLongPressRule->pszUDNList:" ";
        /* possible long press action values=> 0:OFF, 1:ON, 2:Toggle, 3:AwayMode, -1:Invalid */
        int action = (gpsLongPressRule != NULL)?gpsLongPressRule->action:-1;
        int longpress_state = (gpsLongPressRule != NULL)?gpsLongPressRule->state:-1;
        sendLongPressNotify(action, count, longpress_state, udn);
    }
#endif
}

/************************************************************************
 * Function: handleLongPressForAway
 *    Send the action to start the AwayTask for the devices participating in
 *    the long press away rule.
 *  Parameters:
 *    deviceList - list of the devices from DB
 *    nDevices - number of devices
 *    enable - enable or disable Away Mode Rule
 *  Return:
 *    N.A.
***********************************************************************/
static void
handleLongPressForAway(char** deviceList, int nDevices, int enable)
{
    char *paramNames[] = {"EnableAwayTask", "RuleID"};
    char *paramValue[2];
    paramValue[0] = (char *)MALLOC(SIZE_4B);
    paramValue[1] = (char *)MALLOC(SIZE_4B);

    /* discover devices before sending SetAwayRuleTask action. */
    discoverDevices();

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Sending the SetAwayRuleTask action:%d to all the devices participating in the Long Press Away Mode Rule", enable);
    /* Start the Away mode executor task on the devices participating in the rules. */
    snprintf(paramValue[0], SIZE_4B, "%d", enable);
    snprintf(paramValue[1], SIZE_4B, "%d", gLongPressRuleActive);
    PluginCtrlPointSendActionToList(PLUGIN_E_EVENT_SERVICE, "SetAwayRuleTask", (const char**)paramNames, paramValue, 2, deviceList, nDevices);

    /* check if the dimmer is part of the away mode rule */
    if(selfIndex>=0 && deviceNodeInList(g_szUDN_1, deviceList, nDevices) == SUCCESS) {
        if(enable) {
#ifdef PRODUCT_WeMo_Dimmer
            /* stop the night mode if active */
            stopNightMode();
#endif
            /* action on self */
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Starting the long press away rule on self");
            /* Start the Away mode executor task for itself */
            if(g_longPressAwayRunning || gRuleHandle[e_AWAY_RULE].ruleCnt) {
                stopExecutorThread(e_AWAY_RULE);
            }
            g_longPressAwayRunning = 1;
            startExecutorThread(e_AWAY_RULE);
        } else {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Stopping the long press away rule on self");
            stopExecutorThread(e_AWAY_RULE);
        }
    } else if(!enable) {
        cleanupAwayRule(0);
    }

    free(paramValue[0]);
    free(paramValue[1]);
}

/************************************************************************
 * Function: DisableLongPressAwayIfRunning
 *    Function is used to disable the long press
 *    RuleData
 * Parameters:
 *    void.
 * Return:
 *    Returns SUCCESS or FAILURE
************************************************************************/
int
DisableLongPressAwayIfRunning(void)
{
    if(g_longPressOccurred || g_longPressAwayRunning) {
        if(gpSimulatedDevice) {
            int i=0;
            int deviceCount = gpSimulatedDevice->totalCount;
            char **list = MALLOC(deviceCount*sizeof(char*));
            for(i=0; i<deviceCount; i++) {
                list[i] = MALLOC(SIZE_UDN*sizeof(char));
                strncpy(list[i], g_devList[i].UDN, SIZE_UDN-1);
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Disabling Long Press on plugin with UDN:%s", list[i]);
            }
            /* call handleLongPressForAway with enable flag as 0 to stop the running
               AwayTask */
            handleLongPressForAway(list, deviceCount, 0);
            for(i=0; i<deviceCount; i++) {
                if(list[i]) {
                    free(list[i]);
                }
            }
            if(list) {
                free(list);
            }
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "Unexpected!! The device list looks to be emmpty.");
            return FAILURE;
        }
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Long press away rule Disabled.");
    } else {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Long press away rule not running.");
    }
    return SUCCESS;
}
#endif

/************************************************************************
 * Function: parseTargetDevicesList
 *    Function is used to read the TARGETDEVICES
 *    table from the database and store the required fields
 *    (UDN/Index) in SimulatedDevInfo data structure.
 *    Done to avoid the overhead with the call to SimulatedRuleData
 * Parameters:
 *    allDevices - If the devices has to be parsed even if the uuid does
 *                 not match.
 *    ruleID - Long Press Away Rule Id to parse the data for
 * Return:
 *    Returns SUCCESS or FAILURE
************************************************************************/
static int
parseTargetDevicesList(bool allDevices, int ruleID)
{
    int rowsRules=0, colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];
    int ret = 0;
    int i=0, j=0;
    bool found = false;

    snprintf(query, sizeof(query), "SELECT DeviceID, DeviceIndex FROM TARGETDEVICES WHERE RuleID='%d';", ruleID);
    APP_LOG("DEVICE:parseTargetDevicesList", LOG_DEBUG, "query:%s", query);

    /* Read TARGETDEVICES table to get required data(DeviceID/DeviceIndex) */
    if(SUCCESS == (ret = WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray, &rowsRules, &colsRules))) {
        if(0 == rowsRules && 0 == colsRules) {
            APP_LOG("DEVICE:parseTargetDevicesList", LOG_DEBUG, "Empty TARGETDEVICES table.");
            WeMoDBTableFreeResult(&ppsRulesArray, &rowsRules, &colsRules);
            return FAILURE;
        }
        APP_LOG("DEVICE:parseTargetDevicesList", LOG_DEBUG, "rowsRules:%d colsRules:%d", rowsRules, colsRules);

        for(i=0; i<rowsRules; i++) {
            if(0 == strcmp(g_szUDN_1, ppsRulesArray[colsRules*(i+1)])) {
                found = true;
                /* fill the selfIndex if the device is found */
                selfIndex = atoi(ppsRulesArray[colsRules*(i+1)+1]);
            }
        }
        if(allDevices || found) {
            int totalCount = rowsRules;
            g_devList = (SimulatedDevInfo *)CALLOC(totalCount, sizeof(SimulatedDevInfo));
            for(j=0; j<rowsRules; j++) {
                /* copy the data for caller */
                strncpy(g_devList[j].UDN, ppsRulesArray[colsRules*(j+1)], sizeof(g_devList[j].UDN)-1);
                g_devList[j].devIndex = atoi(ppsRulesArray[colsRules*(j+1)+1]);
                APP_LOG("DEVICE:parseTargetDevicesList", LOG_DEBUG, "Target Device UDN:%s and index:%d", g_devList[j].UDN, g_devList[j].devIndex);
            }
            LockSimulatedOccupancy();
            gpSimulatedDevice->pDevInfo = g_devList;
            gpSimulatedDevice->selfIndex = selfIndex;
            gpSimulatedDevice->totalCount = totalCount;
            APP_LOG("RULE", LOG_DEBUG,"!!!!!!!!! SELF INDEX: %d !!!!!!!!!", gpSimulatedDevice->selfIndex);
            UnlockSimulatedOccupancy();
        }
    }
    WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
    /* if the device is not found, return
       fauilure to avoid running the AwayTask. */
    if(!found) return FAILURE;

    return ret;
}

#if defined(LONG_PRESS_SUPPORTED)

/************************************************************************
 * Function: getLongPressRuleDetails
 *     Function to collect current long press rule details in a global
	structure for later sharing with app
 *  Parameters:
 *     None
 *  Return:
 *     Returns SUCCESS or FAILURE
************************************************************************/

int getLongPressRuleDetails()
{
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];
    int i=0;
    int udnArraySize=0;

    memset(query, 0, sizeof(query));
    sqlite3_snprintf(sizeof(query), query, "SELECT rd.StartAction, td.DeviceID, r.State from RULEDEVICES rd, RULES r, TARGETDEVICES td WHERE rd.RuleID=r.RuleID and r.Type='Long Press' and rd.DeviceID='%q' and td.RuleID=rd.RuleID and STATE='1'",g_szUDN_1);
    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

    /*Read RULES table to get long press rule from DB*/
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        if(rowsRules && colsRules) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "rowRules: %d, colsRules: %d", rowsRules, colsRules);
        } else {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "No long press rule for this device");
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);

#ifdef SIMULATED_OCCUPANCY
            /* disable the long press away rule on all participating
               devices if the rule has been deleted from the DB. */
            DisableLongPressAwayIfRunning();
#endif
            if(gpsLongPressRule) {
#ifdef PRODUCT_WeMo_Dimmer
                /* scheduleNightMode night mode thread after the LP
                   away mode ends, if nightMode is enabled. */
                if(gpsNightMode && gpsNightMode->nightMode) {
                    scheduleNightMode();
                }
#endif
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Free long press rule data");
                if(gpsLongPressRule->pszUDNList)
                    free(gpsLongPressRule->pszUDNList);
                free(gpsLongPressRule);
                gpsLongPressRule=NULL;
                gLongPressRuleActive=0;
                /* clear the nvram variable LONG_PRESS_NEXT_TOGGLE_STATE
                   which keeps track of the next state to toggle to in case
                   of e_LONG_PRESS_ACTION_TOGGLE rule. */
                UnSetBelkinParameter(LONG_PRESS_NEXT_TOGGLE_STATE);

                /* stop control point if it can be*/
                if(!gProcessData && !gRuleHandle[e_AWAY_RULE].ruleCnt && !gpSimulatedDevice)
                    StopPluginCtrlPoint();
            }
            return FAILURE;
        }

        /* We got some data for long press rule on this device */

        if(!gpsLongPressRule)
            gpsLongPressRule = MALLOC(sizeof(SLongPressRule));

        gpsLongPressRule->count = rowsRules;
        /* possible long press action values=> 0:OFF, 1:ON, 2:Toggle, 3:AwayMode, -1:Invalid */
        gpsLongPressRule->action = atoi(ppsRulesArray[3]);
        /* possible long press state values=> 0:Disabled, 1:Enabled, -1:Invalid */
        gpsLongPressRule->state = atoi(ppsRulesArray[5]);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Long Press Rule Action:%d", atoi(ppsRulesArray[3]));

#ifdef SIMULATED_OCCUPANCY
        /* start the away mode task if the wemoApp has somehow restarted and it was
           running before. Also when the long press rule has been edited. */
        if(e_LONG_PRESS_ACTION_AWAYMODE == gpsLongPressRule->action) {
            bool restartLongPress = false;
            char *longPressOccurredStr = GetBelkinParameter(LONG_PRESS_HAS_OCCURRED);
            if(longPressOccurredStr && strlen(longPressOccurredStr)>0 &&
               1 == atoi(longPressOccurredStr))
                restartLongPress = true;

            /* disable the long press away on the list of the target devices. */
            DisableLongPressAwayIfRunning();
            if(1 == gpsLongPressRule->state) {
                if(restartLongPress)
                    handleLongPressRule();
            }
        }
#endif

        /* rowRules UDNs + rowRules-1 comma characters + 1 byte string termination */
        udnArraySize = SIZE_UDN*rowsRules + rowsRules;

        gpsLongPressRule->pszUDNList = (char*) ZALLOC(udnArraySize);

        for(i=4; i<(rowsRules+1)*3; i+=3) {
            int bytesLeft = udnArraySize-strlen(gpsLongPressRule->pszUDNList);
            APP_LOG("DEVICE:rule", LOG_DEBUG, "target UDN %d:%s, bytesLeft: %d", i, ppsRulesArray[i], bytesLeft);
            strncat(gpsLongPressRule->pszUDNList, ppsRulesArray[i],bytesLeft-1);

            /* If there is one more UDN in the list, append comma */
            if(i+3 < (rowsRules+1)*3) {
                bytesLeft = udnArraySize-strlen(gpsLongPressRule->pszUDNList);
                strncat(gpsLongPressRule->pszUDNList, ",", bytesLeft-1);
            }
        }
        APP_LOG("DEVICE:rule", LOG_DEBUG, "target UDN list: %s, len: %d", gpsLongPressRule->pszUDNList, strlen(gpsLongPressRule->pszUDNList));

        /* notify anyone interest */
        LongPressRuleNotify();

        /*free memory*/
        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
    } else {
        APP_LOG("DEVICE:rule", LOG_ERR, "Get Table data failed");
        return FAILURE;
    }

    return SUCCESS;
}
#endif

int LoadRulesInMemory(void)
{
    int rowsRules=0,colsRules=0;
    int rulesArraySize=0;
    char **ppsRulesArray=NULL;
    char deviceId[SIZE_256B];
    int i=0;
    char query[SIZE_256B];
    char *UDN=NULL;

    /* initialize long press active rule */
    gLongPressRuleActive = 0;

    memset(query, 0, sizeof(query));
    snprintf(query, sizeof(query), "SELECT Type, RuleID FROM RULES WHERE STATE='1';");
    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

    /*Read RULES table to get all rules in the DB*/
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        if(rowsRules && colsRules) {
            rulesArraySize = (rowsRules + 1)*colsRules;
        } else {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Empty RULES table");
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            return FAILURE;
        }

        /*Start loading Active days rules*/
        for(i=colsRules; i<rulesArraySize; i+=colsRules) {
            UDN = NULL;

            /*If device type is sensor*/
            if (DEVICE_SENSOR == g_eDeviceType) {
                /*if rule type is sensor*/
                if(!strcmp(g_szRuleTypeStrings[e_SENSOR_RULE], ppsRulesArray[i])) {
                    memset(deviceId, 0, sizeof(deviceId));

                    /*fetch device ID from table on witch sensor has to notify*/
                    FetchTargetDeviceId(ppsRulesArray[i+1], deviceId);

                    /*load rules info of that device in rulesinfo structure*/
                    if(strlen(deviceId))
                        UDN = deviceId;
                }
                /*if rule type is notification*/
                else if(!strcmp(g_szRuleTypeStrings[e_NOTIFICATION_RULE], ppsRulesArray[i])) {
                    UDN = g_szUDN_1;
                }
            }
            /*If device type is not sensor*/
            else {
                /*if rule type not sensor*/
                if(strcmp(g_szRuleTypeStrings[e_SENSOR_RULE], ppsRulesArray[i])) {
                    UDN = g_szUDN_1;
                }
            }

            LoadRulesTable(ppsRulesArray[i+1], ppsRulesArray[i], UDN);
        }

        /*free memory*/
        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
    } else {
        APP_LOG("DEVICE:rule", LOG_ERR, "Get Table data failed");
        return FAILURE;
    }

    return SUCCESS;
}

int prepareLinkDeviceList(int ruleId, char ***devList, int *num)
{
    char query[SIZE_256B];
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char **deviceIds = NULL;
    int numDevices = 0;
    int ret=0;
    int i=1;

    /* Find out how many end devices are part of this rule */
    memset(query, 0, sizeof(query));
    snprintf(query, sizeof(query), "SELECT DISTINCT DeviceID FROM RULEDEVICES where RuleID='%d';", ruleId);
    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

    /*execute database query*/
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        /*check if we got the data*/
        if(rowsRules && colsRules) {
            numDevices = rowsRules;

            /* allocate size for device Id array */
            deviceIds =(char **) malloc(sizeof(char *)* numDevices);

            if(!deviceIds) {
                APP_LOG("DEVICE:rule", LOG_ERR, "Out of memory for %d pointers", numDevices);
                ret = FAILURE;
                goto exit_fn;
            } else
                APP_LOG("DEVICE:rule", LOG_ERR, "Allocated memory for %d pointers, deviceIds: %p", numDevices, deviceIds);

            /* save the device ids */
            while(i <= rowsRules) {
                deviceIds[i-1]  = calloc(1, SIZE_256B);
                if(! deviceIds[i-1]) {
                    APP_LOG("DEVICE:rule", LOG_ERR, "Out of memory");
                    ret = FAILURE;
                    goto exit_fn;

                } else {
                    strncpy(deviceIds[i-1], ppsRulesArray[i], SIZE_256B-1);
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Saved device%d as %s at %p", i-1, deviceIds[i-1], deviceIds[i-1]);
                    i++;
                }
            }
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No devices found");
            ret = FAILURE;
            goto exit_fn;
        }
    } else {
        APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
        return FAILURE;
    }

exit_fn:
    /*free database buffer*/
    if(ppsRulesArray) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Freeing DB table");
        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
        ppsRulesArray = NULL;
    }

    if(ret) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Freeing device list");
        for(i=0; i<numDevices; i++)
            if(deviceIds[i])
                free(deviceIds[i]);

        if(deviceIds)
            free(deviceIds);

        return FAILURE;
    }

    *num = numDevices;
    *devList = deviceIds;


    APP_LOG("DEVICE:rule", LOG_DEBUG, "num: %p, devList: %p, deviceIds: %p ", num, devList, deviceIds);
    return SUCCESS;
}

int checkAndExecuteSRule(int aDayIndex, int aNowTime)
{
    SRuleInfo* psRule = gpsRuleList;
    int sleepTime=0;

    while(psRule != NULL) {
        //APP_LOG("DEVICE:rule", LOG_ERR, "rule- type:%d|startTime:%d|aNowTime:%d|isActive:%d", psRule->ruleType, psRule->startTime,
        //	    aNowTime, psRule->isActive);
        if((e_SIMPLE_RULE == psRule->ruleType) && (aNowTime >= psRule->startTime) && (aNowTime  < (psRule->startTime + RULE_TASK_FREQUENCY)) &&
           (psRule->activeDays & DAY_INDEX_MASK(aDayIndex))) {
            sleepTime = psRule->startTime - aNowTime;

            if(sleepTime > 0)
                sleep(sleepTime);
            /* Do this only for products other than Insight, since Insight has
             * separate functionality to invoke the countdown timer
             * functionality when the state changes from STDBY to ON.
             */
            /* Check if Countdown timer rule is active and not in its last
             * minute. If so, start the Countdown timer thread.
             */

            APP_LOG ("DEVICE:rule", LOG_DEBUG,
                     "*** gCountdownRuleInLastMinute *** = [%d]"
                     "*** ruleCnt *** = [%d]",
                     gCountdownRuleInLastMinute,
                     gRuleHandle[e_COUNTDOWN_RULE].ruleCnt);

            if (gRuleHandle[e_COUNTDOWN_RULE].ruleCnt &&
                (1 == gCountdownRuleInLastMinute)) {
                APP_LOG ("DEVICE:rule", LOG_DEBUG,
                         "Countdown timer was in last minute, Do not toggle!");
            } else {
                /* Check the current state of the device. If the current state
                 * is the same as the requested state then do not reset the
                 * Countdown Timer.
                 */
                int deviceCurrState = -1;
                deviceCurrState = GetCurBinaryState();

                APP_LOG ("DEVICE:rule", LOG_DEBUG, "deviceCurrState = [%d]",
                         deviceCurrState);

                if (deviceCurrState != psRule->startAction)
                    checkAndExecuteCountdownTimer (psRule->startAction);
            }

#if defined(PRODUCT_WeMo_Dimmer)
            SetDimmerAttributes(psRule->startBrightness, psRule->startFader, psRule->startAction, ACTUATION_TIME_RULE);
#else
            /* Perform timer action */
            SetRuleAction (psRule->startAction, ACTUATION_TIME_RULE);
#endif
            APP_LOG ("DEVICE:rule", LOG_DEBUG, "Executed Timer rule "
                     "action:%d after %d secs", psRule->startAction, sleepTime);

            break;
        }
        psRule = psRule->psNext;
    }

    return sleepTime;
}

void startExecutorThread(ERuleType i)
{
    if((i != e_SIMPLE_RULE) && (i != e_SENSOR_RULE) && (i != e_NOTIFICATION_RULE) && (i != e_COUNTDOWN_RULE) \
       && (gRuleHandle[i].ruleThreadId == INVALID_THREAD_HANDLE) && (gfpRuleThreadFn[i] != NULL) ) {
        pthread_attr_t rule_attr;
        pthread_attr_init(&rule_attr);
        /* WEMO-46785:detach the thread to avoid any resource leak. */
        pthread_attr_setdetachstate(&rule_attr, PTHREAD_CREATE_DETACHED);

        int ret = pthread_create(&gRuleHandle[i].ruleThreadId, &rule_attr, gfpRuleThreadFn[i], 0x00);
        if (0x00 != ret) {
            APP_LOG("UPNP: Rule", LOG_DEBUG, "Could not create rule thread: %d", i);
            resetSystem();
        } else
            APP_LOG("UPNP: Rule", LOG_DEBUG, "Rule task : %d created", i);
    }

    if(i==e_SENSOR_RULE) {
        APP_LOG("UPNP: Rule", LOG_DEBUG,"enable sensor control point inside sensor rule");
        char *ip_address = NULL;
        ip_address = wifiGetIP(INTERFACE_CLIENT);
        if (ip_address && (0x00 != strcmp(ip_address, DEFAULT_INVALID_IP_ADDRESS))) {
            APP_LOG("UPNP: Rule", LOG_DEBUG,"start for control point");
            int ret=StartPluginCtrlPoint(GetLanDeviceName(), 0x00);
            if(UPNP_E_INIT_FAILED==ret) {
                APP_LOG("UPNP", LOG_DEBUG,"UPNP on error: %d", ret);
                APP_LOG("UPNP", LOG_DEBUG,"################### Wemo App going to be reset ###################");
                resetSystem();
            }
            EnableContrlPointRediscover(true);
        }
    }
}

void stopExecutorThread(ERuleType i)
{

    if((i != e_SIMPLE_RULE) && (i != e_SENSOR_RULE) && (i != e_NOTIFICATION_RULE) && (i != e_TIMER_RULE) && (i != e_COUNTDOWN_RULE) \
       && (gRuleHandle[i].ruleThreadId != INVALID_THREAD_HANDLE)) {
#ifdef SIMULATED_OCCUPANCY
        if((i == e_AWAY_RULE) && gAwayRuleCleanupFlag)
            return;
#endif

        int ret = ithread_cancel(gRuleHandle[i].ruleThreadId);

        if (0x00 != ret) {
            APP_LOG("UPNP: Rule", LOG_DEBUG, "################### ithread_cancel: Couldnt stop rule thread %d #########################", i);
            if(i == e_AWAY_RULE) {
                gRuleHandle[i].ruleThreadId = INVALID_THREAD_HANDLE;
                return;
            }
        } else {
            APP_LOG("UPNP: Rule", LOG_DEBUG, "################### ithread_cancel: Successfully stop rule thread %d ####################", i);
        }
#ifdef PRODUCT_WeMo_LightV2
        SetActivityLED(5);
#endif
    }

    /* Invalidate the handle for all threads so that stopEngine doesnt invoke cancel for simple/sensor thread */
    if(i != e_TIMER_RULE)
        gRuleHandle[i].ruleThreadId = INVALID_THREAD_HANDLE;

    if((i == e_SENSOR_RULE) && (0 == (gRuleHandle[e_SENSOR_RULE].ruleCnt))) {
        if(!gProcessData) {
            APP_LOG("UPNP: Rule", LOG_DEBUG,"stop control point in maker sensor rule");
            StopPluginCtrlPoint();
        }
    }

    if(i == e_AWAY_RULE) {
#ifdef SIMULATED_OCCUPANCY
        cleanupAwayRule(0);
#endif
        if(gRuleHandle[e_TIMER_RULE].ruleCnt)
            startExecutorThread(e_TIMER_RULE);
    }
}



int updateRuleActiveStatus(int aDayIndex, int aNowSeconds)
{
    int delta=-1;
    SRuleInfo* psRule=NULL;
    int ret=0;

    lockRule();
    psRule = gpsRuleList;


    while(psRule != NULL) {
        delta = -1;

        if(psRule->isDayChange && (psRule->activeDays & DAY_INDEX_MASK(aDayIndex-1)))
            delta = ONE_DAY_SECONDS;
        else if(!psRule->isDayChange && (psRule->activeDays & DAY_INDEX_MASK(aDayIndex)))
            delta = 0;

        if(delta == -1) {
            psRule->isActive = 0;
            psRule->isDayChange = 0;
        } else {
            /*
                This if condition handles the end timer. One second delay helps in marking the status as inactive
                in this iteration itself avoiding delay of 10 seconds. It also allows the individual rules tasks
                like Timer Task to exit gracefully
            */
            if(!ret && (e_SIMPLE_RULE != psRule->ruleType) && ((aNowSeconds + delta) == (psRule->startTime + psRule->ruleDuration))) {
                sleep(1);
                aNowSeconds++;
                ret=1;
            }

            /* start time of sunrise/sunset rule has 1/2 respectively.
               It is okay to delay marking the rule as inactive as timer task will do
               its job at the right time
            */

            if(( (aNowSeconds + delta) >= (psRule->startTime - (psRule->startTime%10)))
               && ((aNowSeconds + delta) <= (psRule->startTime + psRule->ruleDuration)) ) {
                if(e_AWAY_RULE == psRule->ruleType && LONG_PRESS_AWAY_ACTIVE) {
                    psRule->isInvalidToday = 1;
                    psRule->isActive = 0;
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Long Press Away is active. Away Rule: %d not started. Invalid for today!!", psRule->ruleId);
                } else if(!psRule->isActive && !psRule->isInvalidToday) {
                    psRule->isActive = 1;

                    /*update rule count corresponding to its rule type*/
                    gRuleHandle[psRule->ruleType].ruleCnt++;
                    if(gRuleHandle[psRule->ruleType].ruleCnt == 1) {
                        startExecutorThread(psRule->ruleType);
                    }
                    if(psRule->ruleType == e_EVENT_RULE) {
                        gEventRuleOverRidden = false;
                        RuleOverrideNotify(0);
                        /* if event rule is going active, stop timer task*/
                        if(gRuleHandle[e_TIMER_RULE].ruleCnt) {
                            APP_LOG("DEVICE:rule", LOG_DEBUG, "Stopping timer task");
                            stopExecutorThread(e_TIMER_RULE);
                            freeTimerList();
                        }
                        /* if event rule is going active, stop away task*/
                        if(gRuleHandle[e_AWAY_RULE].ruleCnt) {
                            APP_LOG("DEVICE:rule", LOG_DEBUG, "Stopping away task");
                            stopExecutorThread(e_AWAY_RULE);
                        }
                    }
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Ruleid: %d going active", psRule->ruleId);
                }
            } else {
                if(psRule->isActive) {
                    psRule->isActive = 0;
                    if(psRule->isOvernight) {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Ruleid: %d dayChange zero", psRule->ruleId);
                        psRule->isDayChange = 0;
                    }

                    /*update rule count corresponding to its rule type*/
                    if(gRuleHandle[psRule->ruleType].ruleCnt > 0)
                        gRuleHandle[psRule->ruleType].ruleCnt--;

                    if(gRuleHandle[psRule->ruleType].ruleCnt == 0) {
                        stopExecutorThread(psRule->ruleType);
                        if(psRule->ruleType == e_EVENT_RULE) {
                            /* if event rule is going inactive, start away task*/
                            if(isAwayRuleActive()==true) {
                                APP_LOG("DEVICE:rule", LOG_DEBUG, "Starting away task");
                                startExecutorThread(e_AWAY_RULE);
                            } else if(gRuleHandle[e_TIMER_RULE].ruleCnt)
                                /* if event rule is going inactive, start timer task (only if away task is not created)*/
                            {
                                APP_LOG("DEVICE:rule", LOG_DEBUG, "Starting timer task");
                                startExecutorThread(e_TIMER_RULE);
                            }
                        } else if(psRule->ruleType == e_AWAY_RULE) {
                            APP_LOG("DEVICE:rule", LOG_DEBUG, "Resetting Overridden Flag, so nest rule can run.");
                            gEventRuleOverRidden = false;
                            RuleOverrideNotify(0);
                        }
                    }

                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Ruleid: %d going inactive", psRule->ruleId);
                }
            }
        }

        psRule = psRule->psNext;
    }
    unlockRule();

    return ret;
}

void updateOnActiveDayChange()
{
    SRuleInfo* psRule=NULL;

    checkAndUpdateDststat();

    lockRule();
    psRule = gpsRuleList;
    while(psRule != NULL) {
        /* update start time and duration for sunrise sunset rules */
        if(psRule->isSunriseSunset) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Update sunrise sunset time for the day");
            UpdateSunriseSunset(psRule);
        }

        if(psRule->isOvernight) {
            psRule->isDayChange=true;
        }
        if(e_AWAY_RULE == psRule->ruleType) {
            psRule->isInvalidToday = 0;
        }

        psRule = psRule->psNext;
    }
    unlockRule();
}
extern int gPrintThreadList;
void *RulesTask(void *args)
{
    int nowSeconds=0, last_seconds=0, year=0, monthIndex=0, dayIndex=0;
    SRuleInfo* psRule=NULL;
    int retVal = 0;
    APP_LOG("DEVICE:rule", LOG_DEBUG, "starting rules task");
    if (g_eDeviceType == DEVICE_SOCKET) {
        createSubsListLock();
        retVal = ListInit(&gSubscriptionList, NULL, free );
        if (retVal < 0) {
            APP_LOG("DEVICE:rule",LOG_CRIT, "SubscriptionList Initialization failed <%d>\n",retVal);
            resetSystem();
        }
    }
    /*
     ** Load all rules from DB in to data structure and create the linked list of RuleInfo nodes
     ** Take care to load only supported rules in memory - for eg. sensor device should load just
     ** motion sensor and APNS rules in the list based on rule type check
     */

    while(1) {
        if(gRestartRuleEngine == RULE_ENGINE_RELOAD) {
            /*Kill all running executor threads*/
            StopRuleEngine();

            /*get rule DB handle*/
            if(GetRuleDBHandle()) {
                APP_LOG("DEVICE:rule", LOG_CRIT, "InitDB failed...");
                gRestartRuleEngine = RULE_ENGINE_DEFAULT;
                continue;
            }

            /*Load all active day rules*/
            if (LoadRulesInMemory() == SUCCESS) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "RELOADED RULES DONE");
            }
            else {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "RELOADED RULES FAILED");
                /*Close the previous g_RulesDB*/
                CloseDB(g_RulesDB);
                g_RulesDB = NULL;
                unlink(RULE_DB_FILE_PATH);
                UnSetBelkinParameter(RULE_DB_VERSION_KEY);
                gRestartRuleEngine = RULE_ENGINE_DEFAULT;
                //RuleDBVersionNotify();
                continue;
            }

            /* LoadRulesTable loads rules that are in ENABLED state,
               for long press rule we need to provide details to App in GetInformation even when it is disabled */
#if defined(LONG_PRESS_SUPPORTED)
            APP_LOG("DEVICE:rule", LOG_DEBUG, "gLongPressRuleActive: %d", gLongPressRuleActive);

            getLongPressRuleDetails();
#endif

#ifdef SIMULATED_OCCUPANCY
            /* load the longPress away rule. Exits if the rule was running and wemoApp
                restated for some reason */
            char *longPressAwayRuleId = GetBelkinParameter(LONG_PRESS_AWAY_RULE_ID);
            if(longPressAwayRuleId && strlen(longPressAwayRuleId)>0) {
                g_LongPressAwayRuleID = atoi(longPressAwayRuleId);
            }
            /* start the away mode task if the wemoApp has somehow restarted and it was
               running before. */
            char *longPressAwayState = GetBelkinParameter(LONG_PRESS_AWAY_MODE_STATE);
            if(longPressAwayState && strlen(longPressAwayState)>0 &&
               1 == atoi(longPressAwayState)) {
                g_longPressAwayRunning = 1;
                startExecutorThread(e_AWAY_RULE);
            }
#endif

            nowSeconds = daySeconds();
            /* update event subscription for event rules */
            if((g_eDeviceType == DEVICE_SOCKET) ) {
                ManageEventSubscriptionRequest(&gSubscriptionList);
            }
            /* update start time and duration for sunrise sunset rules */
            psRule = gpsRuleList;
            while(psRule != NULL) {
                /* update start time and duration for sunrise sunset rules */
                if(psRule->isSunriseSunset) {
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Update sunrise sunset time for the day");
                    UpdateSunriseSunset(psRule);
                }

                /* take care of overnight timer rule */
                if(psRule->isOvernight) {
                    int end = psRule->startTime + psRule->ruleDuration - ONE_DAY_SECONDS;

                    if((nowSeconds < psRule->startTime) && (nowSeconds <= end)) {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Set dayChange, ruleid: %d, start: %d, end: %d, now: %d",
                                psRule->ruleId,  psRule->startTime, end, nowSeconds);

                        psRule->isDayChange=1;
                    }
                }
                psRule = psRule->psNext;
            }

            gRestartRuleEngine = RULE_ENGINE_RUNNING;
            RuleDBVersionNotify();
            /* adjust scheduler close to 10 second boundary every time */
            nowSeconds = daySeconds();
            if(nowSeconds%10) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Adjusting Rule boundary: %d", nowSeconds);
                sleep(10-(nowSeconds%10));
            }

            APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule engine running now");
            checkAndUpdateDststat();
        }

        if(gRestartRuleEngine == RULE_ENGINE_RUNNING) {
            last_seconds = nowSeconds;

            GetCalendarDayInfo(&dayIndex, &monthIndex, &year, &nowSeconds);

            if(last_seconds > nowSeconds) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Update sunset/sunrise values and overnight flag on active day change...");
                updateOnActiveDayChange();
            }

            updateRuleActiveStatus(dayIndex, nowSeconds);

            /*Execute Simple timer*/
#ifdef SIMULATED_OCCUPANCY
            if((NULL == gpSimulatedDevice) || (0 == gpSimulatedDevice->ruleEndTime))
#endif
                checkAndExecuteSRule(dayIndex, nowSeconds);

        }

        nowSeconds = daySeconds();
        sleep(RULE_TASK_FREQUENCY-(nowSeconds%10));

        if(gPrintThreadList == 1) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Printing thread list");
            PrintThreadList();
            gPrintThreadList=0;
        }

    }

    if (g_eDeviceType == DEVICE_SOCKET) {
        destroySubsListLock();
        retVal = ListDestroy(&gSubscriptionList, 1 );
        if (retVal < 0) {
            APP_LOG("DEVICE:rule",LOG_CRIT, "SubscriptionList Initialization failed <%d>\n",retVal);
            resetSystem();
        }
    }

    return NULL;
}

void ClearRuleFromFlash(void)
{
    char buf[SIZE_64B];

    StopRuleEngine();

    /*stop Countdown Timer*/
    stopCountdownTimer();

    memset (buf,0,SIZE_64B);
    sprintf(buf,"rm -f %s",RULE_DB_PATH);
    system(buf);
    pluginUsleep(500000);
    APP_LOG("Rule", LOG_DEBUG, "Remove rule db");

    SaveDeviceConfig(RULE_DB_VERSION_KEY, "");
    RuleDBVersionNotify();
    system("rm -f /tmp/Belkin_settings/simulatedRule.txt");
    UnSetBelkinParameter (SIM_DEVICE_COUNT);
}

void moveToTimerNodeToExecute(unsigned int nowSeconds)
{
    STimerList *psTimerList = NULL;

    if(!gpsTimerList) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Timer List Empty!");
        return;
    }

    lockTimerRule();
    psTimerList = gpsTimerList;
    /*first node*/
    if((nowSeconds <= psTimerList->time)) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Node Set: nowSeconds:%d, node time:%d, node action:%d", nowSeconds,psTimerList->time,psTimerList->action);
        goto RETURN;
    } else {
        while(NULL != psTimerList) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Timer Node: nowSeconds:%d, node time:%d, node action:%d", nowSeconds,psTimerList->time,psTimerList->action);
            /* take care of entries missed in the last iteration */
            if(nowSeconds > psTimerList->time + RULE_TASK_FREQUENCY) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Move to next Timer node");
                /*move to next timer*/
                gpsTimerList = gpsTimerList->nextTimer;
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Node Set: nowSeconds:%d, node time:%d, node action:%d", nowSeconds,psTimerList->time,psTimerList->action);
                free(psTimerList);
                psTimerList = gpsTimerList;
            } else
                goto RETURN;
        }

    }

RETURN:
    unlockTimerRule();
    return;
}

void *TimerTask(void *args)
{
    APP_LOG("DEVICE:rule", LOG_DEBUG, "In Timer Task");
    int nowSeconds = 0, year = 0, monthIndex = 0, dayIndex = 0;
    int sleepTime;
    int skip=0;
    unsigned char action;
    STimerList *psTimerList = NULL;

#ifdef SIMULATED_OCCUPANCY
    if(gpSimulatedDevice && gpSimulatedDevice->ruleEndTime) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Away Task is running");
        gRuleHandle[e_TIMER_RULE].ruleThreadId = INVALID_THREAD_HANDLE;
        return NULL;
    }
#endif

    if(gRuleHandle[e_EVENT_RULE].ruleCnt) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Nest Rule is running");
        gRuleHandle[e_TIMER_RULE].ruleThreadId = INVALID_THREAD_HANDLE;
        return NULL;
    }

    /*stop countdown timer task if it is running*/
    stopCountdownTimer();

    /*main loop*/
    while(1) {
        /*get todays time in seconds*/
        GetCalendarDayInfo(&dayIndex, &monthIndex, &year, &nowSeconds);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "createTimerRuleList for day index:%d, nowSeconds:%d", dayIndex,nowSeconds);

        /*create the sorted timer list*/
        createTimerRuleList(dayIndex);

        APP_LOG("DEVICE:rule", LOG_DEBUG, "created Timer Rule List...");

        /*move to start node*/
        moveToTimerNodeToExecute(nowSeconds);

        psTimerList = gpsTimerList;
        /*This loop execute all timer rules for active day*/
        while(1) {
            /*check if all timer rules executed*/
            if(psTimerList) {
                /*calculate sleep time*/
                sleepTime = psTimerList->time - nowSeconds;
                APP_LOG("DEVICE:rule", LOG_DEBUG, "sleepTime:%d,psTimerList->time:%d,nowSeconds:%d", sleepTime,psTimerList->time,nowSeconds);

                if((sleepTime + nowSeconds) > ONE_DAY_SECONDS) {
                    /*overnight rule is active, break! create and reload timerList*/
                    break;
                }

                /*check if rule start time has already passed*/
                if(0 > sleepTime) {
                    /*No sleep, get start timer rule action*/
                    action = psTimerList->action;
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "No sleep execute timer rule:%d", action);

                    if(g_iDstNowTimeStatus) {
                        skip=1;
                        g_iDstNowTimeStatus = 0x00;
                    }
                } else {
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Sleep for %d seconds", sleepTime);
                    /*sleep for sleepTime seconds*/
                    sleep(sleepTime);
                    if(gpsTimerList == NULL) {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Away Task or nest rule is running");
                        gRuleHandle[e_TIMER_RULE].ruleThreadId = INVALID_THREAD_HANDLE;
                        if(gRuleHandle[e_TIMER_RULE].ruleCnt)
                            startExecutorThread(e_TIMER_RULE);
                        return NULL;
                    }
                    /*get timer rule action*/
                    action = psTimerList->action;
                    nowSeconds = psTimerList->time;
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Continue after sleep and execute timer action:%d", action);
                }

                if(!skip) {
#if defined(PRODUCT_WeMo_Dimmer)
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Execute Dimmer Timer rule action!");
                    SetDimmerAttributes(psTimerList->brightness, psTimerList->faderTime, action, ACTUATION_TIME_RULE);
#else
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Execute Timer rule action!");
#ifdef PRODUCT_WeMo_LightV2
                    if (action) {
                        /*Perform timer action*/
                        SetRuleAction(action, ACTUATION_TIME_RULE);
                        SetActivityLED(4);
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "sleep 3 sec...");
                        sleep(3);
                        /* according to RGB spec, LED should return to normal after 3000 ms */
                        SetActivityLED(5);
                    }
                    else {
                        SetActivityLED(4);
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "sleep 3 sec before expire...");
                        sleep(3);
                        SetActivityLED(5);
                        /*Perform timer action*/
                        SetRuleAction(action, ACTUATION_TIME_RULE);
                    }
#else
                    /*Perform timer action*/
                    SetRuleAction(action, ACTUATION_TIME_RULE);
#endif // PRODUCT_WeMo_LightV2
#endif //PRODUCT_WeMo_Dimmer
                }
                skip = 0;
                /*check and update timer list*/
                lockTimerRule();
                gpsTimerList = gpsTimerList->nextTimer;
                free(psTimerList);
                psTimerList = gpsTimerList;
                unlockTimerRule();
            } else {
                /*all timer rule for the day executed*/
                break;
            }
        }
        /*time left for active day change plus 10 second time to load active days timerlist*/
        sleepTime = ONE_DAY_SECONDS - nowSeconds + DELAY_10SEC + 1;
        /*all active days timer rules got executed sleep till active day change.*/
        APP_LOG("DEVICE:rule", LOG_DEBUG, "All timer rules executed! sleep for leftover sleepTime:%d", sleepTime);
        sleep(sleepTime);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Continue after day change sleep time!");
    }
    gRuleHandle[e_TIMER_RULE].ruleThreadId = INVALID_THREAD_HANDLE;
    return NULL;
}

int getSensorRuleData(int ruleId, char *deviceId, char *startAction, char *endAction, char *duration)
{
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];

    /*get sensor duration this rule id*/
    memset(query, 0, sizeof(query));
    sqlite3_snprintf(sizeof(query), query,"SELECT StartAction, EndAction, SensorDuration FROM RULEDEVICES WHERE RuleID='%d' AND DeviceID='%q' limit 1;", ruleId,deviceId);
    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

    /*execute database query*/
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        /*check if we got the data*/
        if(rowsRules && colsRules) {
            /*calculate sensor duration*/
            snprintf(startAction, SIZE_8B, "%d", atoi(ppsRulesArray[colsRules]));
            snprintf(endAction, SIZE_8B, "%d", atoi(ppsRulesArray[colsRules+1]));
            snprintf(duration, SIZE_8B, "%d", atoi(ppsRulesArray[colsRules+2]));
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
            /*free database buffer*/
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            return FAILURE;
        }
    } else {
        APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
        return FAILURE;
    }

    /* free database table result */
    WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
    return SUCCESS;
}

void executeSensorRule()
{
    char startAction[SIZE_8B],duration[SIZE_8B],endAction[SIZE_8B], fullUdn[SIZE_UDN];
    char *paramNames[] = {"BinaryState", "Duration", "EndAction", "UDN"};
    char *values[4] = {startAction,duration,endAction, fullUdn};
    int i=0,rowsRules=0,colsRules=0,rulesArraySize=0,deviceIndex=0,retVal=-1;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];
    SRuleInfo *psRule = gpsRuleList;
    char szUDN[SIZE_256B] = {'\0'};
    char *udn=NULL;

    if(gRuleHandle[e_SENSOR_RULE].ruleCnt) {
        while(psRule != NULL) {
            if((e_SENSOR_RULE == psRule->ruleType) && (psRule->isActive)) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule ID %d is Active, Execute it!", psRule->ruleId);

                /*get device IDs of all devices which are controlled by this rule id or by this sensor*/
                memset(query, 0, sizeof(query));
                snprintf(query, sizeof(query), "SELECT DeviceID FROM devicecombination where RuleID='%d';", psRule->ruleId);
                APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

                /*execute database query*/
                if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
                    /*check if we got the data*/
                    if(rowsRules && colsRules) {
                        /*calculate array size*/
                        rulesArraySize = (rowsRules + 1)*colsRules;
                    } else {
                        APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
                        /*free database buffer*/
                        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
                        return;
                    }
                } else {
                    APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
                    return;
                }

                /*send sensor notify to all device IDS*/
                for(i = 1; i < rulesArraySize; i++) {
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Try to send notify to Device: %s", ppsRulesArray[i]);
                    udn = ppsRulesArray[i];

                    memset(szUDN, 0, sizeof(szUDN));
                    if(strstr(udn, "uuid:Bridge") != NULL) {
                        strncpy(szUDN, udn, BRIDGE_UDN_LEN);
                    } else if(strstr(udn, "uuid:Maker") != NULL) {
                        strncpy(szUDN, udn, MAKER_UDN_LEN);
                        strncat(szUDN, ":sensor:switch", sizeof(szUDN)-strlen(szUDN)-1);
                    } else
                        strncpy(szUDN, udn, sizeof(szUDN)-1);

                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Input UDN: %s, Converted UDN: %s", ppsRulesArray[i], szUDN);

                    LockDeviceSync();
                    /*get the device ID index in deive discoved list */
                    deviceIndex = GetDeviceIndexByUDN(szUDN);
                    UnlockDeviceSync();

                    /*check if device id discovered*/
                    if (0x00 != deviceIndex) {
                        memset(startAction, 0, sizeof(startAction));
                        memset(duration, 0, sizeof(duration));
                        memset(endAction, 0, sizeof(endAction));
                        memset(fullUdn, 0, sizeof(fullUdn));

                        /*get sensor duration*/
                        if(SUCCESS != getSensorRuleData(psRule->ruleId, ppsRulesArray[i], startAction, endAction, duration)) {
                            APP_LOG("DEVICE:rule", LOG_ERR, "Fetching sensor rule data failed");
                        }

                        /*make notification data*/
                        strncpy(fullUdn, udn, sizeof(fullUdn)-1);

                        APP_LOG("DEVICE:rule", LOG_DEBUG, "start action: %s, sensor duration: %s, stop action: %s",
                                startAction, duration, endAction);

                        /*send sensor notify*/
                        retVal = PluginCtrlPointSendAction(PLUGIN_E_EVENT_SERVICE, deviceIndex, "SetBinaryState", (const char **)&paramNames, (char **)&values, 4);
                        if (retVal != UPNP_E_SUCCESS) {
                            APP_LOG("DEVICE:rule", LOG_DEBUG, "Sensor command sending failed: %s", ppsRulesArray[i]);
                        }
                    } else {
                        APP_LOG("DEVICE:rule", LOG_DEBUG, "Device:%s  not in the device list", ppsRulesArray[i]);
                        CtrlPointDiscoverDevices();
                    }

                }

                /*free database buffer*/
                WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            }
            psRule = psRule->psNext;
        }
    }
    return;
}

void enqueueRuleQ(SRulesQueue *qNode)
{
    SRulesQueue *tempQNode = NULL;

    if(NULL == qNode) {
        APP_LOG("DEVICE:rule",LOG_ERR,"Invalide node to queue, Not adding!");
        return;
    }

    lockRuleQueue();
    /*update only timestamp if rule id already in the queue to avoide sending stale notifications*/
    tempQNode = gRuleQHead;
    while(NULL != tempQNode) {
        if(tempQNode->ruleID == qNode->ruleID) {
            /*update timestamp*/
            tempQNode->ruleTS = qNode->ruleTS;
            unlockRuleQueue();
            APP_LOG("DEVICE:rule",LOG_DEBUG,"Found Rule ID %d, Updated TS!", qNode->ruleID);
            return;
        }
        tempQNode = tempQNode->next;
    }

    /*empty queue*/
    if(NULL == gRuleQHead) {
        gRuleQHead = qNode;
        gRuleQTail = gRuleQHead;
    }
    /*queue it*/
    else {
        gRuleQTail->next = qNode;
        gRuleQTail = qNode;
    }
    unlockRuleQueue();
    APP_LOG("DEVICE:rule",LOG_DEBUG,"rule ID %d queued!", qNode->ruleID);
}

void destroyRuleQueue(void)
{
    SRulesQueue *qNode = NULL;

    lockRuleQueue();
    while(gRuleQHead) {
        qNode = gRuleQHead;
        gRuleQHead = gRuleQHead->next;
        free(qNode);
    }
    gRuleQTail = NULL;
    unlockRuleQueue();
    APP_LOG("DEVICE:rule",LOG_DEBUG,"Rule Queue Destroyed!");
}

SRulesQueue *dequeueRuleQ(void)
{
    SRulesQueue *qNode = NULL;

    lockRuleQueue();
    qNode = gRuleQHead;
    if(NULL != gRuleQHead) {
        if(gRuleQHead == gRuleQTail) {
            gRuleQHead = NULL;
            gRuleQTail = NULL;
        } else {
            gRuleQHead = gRuleQHead->next;
        }
        APP_LOG("DEVICE:rule",LOG_DEBUG,"dequeued rule id %d", qNode->ruleID);
    }
    unlockRuleQueue();
    return qNode;
}
/*
* This function will return true, if any Away rule is active or false, otherwise
*/
bool isAwayRuleActive()
{
#ifdef SIMULATED_OCCUPANCY
    SRuleInfo *psRule = gpsRuleList;
    while(psRule != NULL) {
        if((e_AWAY_RULE == psRule->ruleType) && (psRule->isActive)) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule ID %d is Active.", psRule->ruleId);
            return true;
        }
        psRule = psRule->psNext;
    }
#endif
    return false;
}
#ifdef SIMULATED_OCCUPANCY
/**
 * isAwayRuleInExecution
 * - checks if away rule is active and running
 * - args:
         None
 * - returns:
         true if away rule is active and is in execution
         false otherwise
 ***************************************************/
bool isAwayRuleInExecution(void)
{
    if(isAwayRuleActive() && gRuleHandle[e_AWAY_RULE].ruleCnt)
        return true;
    return false;
}
#endif

bool isCountdownRuleActive()
{
    SRuleInfo *psRule = gpsRuleList;
    if(gRuleHandle[e_COUNTDOWN_RULE].ruleCnt && !(gRuleHandle[e_TIMER_RULE].ruleCnt) && !(gRuleHandle[e_AWAY_RULE].ruleCnt)) {
        while(psRule != NULL) {
            if((e_COUNTDOWN_RULE == psRule->ruleType) && (psRule->isActive)) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Countdown Rule ID %d is Active", psRule->ruleId);
                return true;
            }
            psRule = psRule->psNext;
        }
    }

    return false;

}

void *AwayTask(void *args)
{
    APP_LOG("DEVICE:rule", LOG_DEBUG, "In Away Task");
#ifdef SIMULATED_OCCUPANCY
    SRuleInfo *psRule = gpsRuleList;
    int starttime = 0, endtime = 0, sleepTime = 0;

    /* parse here if the device has not been long pressed */
    if(!g_longPressOccurred) {
        int ruleId = 0;
        /* if for long press away mode rule else for normal
           away mode rule. */
        if(g_longPressAwayRunning) {
            ruleId = g_LongPressAwayRuleID;
        } else {
            while(psRule != NULL) {
                if(e_AWAY_RULE == psRule->ruleType && (psRule->isActive)) {
                    ruleId = psRule->ruleId;
                    break;
                }
                psRule = psRule->psNext;
            }
        }
        /* init is required here because we free all away task memory on exit of this task, so everytime when checker
           starts this task, a new memory is required */
        simulatedOccupancyInit();

        /* parse the TARGETDEVICES table and fill the SimulatedDevInfo
           structure for device list and index. If this call fails, it
           means the app is old so parseSimulatedRule call has to be made. */
        int ret = parseTargetDevicesList(false, ruleId);
        if(SUCCESS == ret) {
            APP_LOG("Rule", LOG_DEBUG, "Parsing of TARGETDEVICES table is successful.");
        }
        /* parseTargetDevicesList fails in case the old application
           is being used which does not have TARGETDEVICES table.
           So call parseSimulatedRule in that case. */
        else if(parseSimulatedRule() != SUCCESS) {
            APP_LOG("Rule", LOG_DEBUG, "Simulated rule file parse failed...");
            gRuleHandle[e_AWAY_RULE].ruleThreadId = INVALID_THREAD_HANDLE;
            /* Away Task is going to exit, setting the ruleCnt to zero */
            gRuleHandle[e_AWAY_RULE].ruleCnt = 0;
            if(gpSimulatedDevice && gpSimulatedDevice->pDevInfo) { /* free simulated device list */
                /* freeing up the memory allcated during in simulatedOccupancyInit
                   in case of failure. */
                free(gpSimulatedDevice->pDevInfo);
                gpSimulatedDevice->pDevInfo= NULL;
                g_devList = NULL;
            }
            return NULL;
        }
    }

    if(gRuleHandle[e_EVENT_RULE].ruleCnt) {
        gEventRuleOverRidden = true;
        RuleOverrideNotify(1);
    }

    if(g_longPressAwayRunning) {
        starttime = 60;  /* Long Press Away Rule is all day daily rule. */
        endtime = ONE_DAY_SECONDS;
        gpSimulatedDevice->ruleEndTime = endtime;
        APP_LOG("DEVICE:rule", LOG_DEBUG, "start time: %d and end time: %d", starttime, endtime);
    } else {
        while(psRule != NULL) {
            if(e_AWAY_RULE == psRule->ruleType && (psRule->isActive)) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule ID %d is Active, Execute it!", psRule->ruleId);
                break;
            }
            psRule = psRule->psNext;
        }
        starttime = psRule->startTime;
        /* endTime is needed to be greater than startTime for the randomtimer algorithm to work.
           It will the case for an overnight SS-SR rule wherein the endtime from the
           DB is less than the startTime. So, the enTime is calculated here by adding the ruleDuration in
           startTime */
        endtime = psRule->startTime + psRule->ruleDuration;
        /* update end time, in case of across day rule. This condition does not meet for an OverNight SS-Sr rule.
           And it gets true at midnight when the day changes. */
        if(psRule->isDayChange) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "across day rule update endtime");
            endtime = endtime - ONE_DAY_SECONDS;
        }

        gpSimulatedDevice->ruleEndTime = endtime;
        APP_LOG("DEVICE:rule", LOG_DEBUG, "start time: %d and end time: %d", starttime, endtime);
        /* check for last manual toggle date only when it is not
           long press away mode rule. Saved manual toggle state check */
        if(!checkLastManualToggleState()) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule last manually toggled...");
            goto on_return;
        }
    }

    if(gRuleHandle[e_TIMER_RULE].ruleCnt) {
        stopExecutorThread(e_TIMER_RULE);
        freeTimerList();
    }

    /*check if countdown timer task is running, if yes stop it*/
    stopCountdownTimer();

    /* first random sleepTime seconds */
    sleepTime = adjustFirstTimer(starttime, endtime);
    simulatedStartControlPoint();
#ifdef PRODUCT_WeMo_Dimmer
    /* set animation to reflect the AWAY mode activated case. */
    setAnimation(LED_STATE_AWAY_OPENING);
#elif PRODUCT_WeMo_LightV2
    SetActivityLED(6);
#endif

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Sleep for %d seconds", sleepTime);
    sleep(sleepTime);

    sleepTime = 1;
    while(g_longPressAwayRunning || psRule->isActive) {
        starttime = daySeconds();
        /* update start time, in case of 12 AM */
        if(!starttime) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "12 AM case update starttime");
            starttime = (3*SIMULATED_DURATION_ADDLTIME);
            sleep(starttime);
        }

        /* update end time, in case of across day rule */
        if(!g_longPressAwayRunning &&
           psRule->isDayChange && endtime > ONE_DAY_SECONDS) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "across day rule update endtime");
            endtime = endtime - ONE_DAY_SECONDS;
            gpSimulatedDevice->ruleEndTime = endtime;
        }

        APP_LOG("DEVICE:rule", LOG_DEBUG, "now start time: %d and end time: %d", starttime, endtime);

        sleepTime = evaluateNextActions(starttime, endtime);

        /*sleep for later random sleepTime seconds*/
        if(!sleepTime) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "manual toggled or rule over... break");
            break;
        } else {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Sleep for %d seconds", sleepTime);
            sleep(sleepTime);
        }
    }

on_return:
    gAwayRuleCleanupFlag = 1;
    cleanupAwayRule(0);
    gAwayRuleCleanupFlag = 0;
    gRuleHandle[e_AWAY_RULE].ruleThreadId = INVALID_THREAD_HANDLE;

    if(gRuleHandle[e_TIMER_RULE].ruleCnt)
        startExecutorThread(e_TIMER_RULE);

#endif
#ifdef PRODUCT_WeMo_LightV2
    SetActivityLED(5);
#endif
    APP_LOG("DEVICE:rule", LOG_DEBUG, "Away Task done...");
    return NULL;
}


void enqueNotificationRule(int ruleId, int ruleType)
{
    SRulesQueue *qNode = NULL;
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];

    /*get Notification Data for this rule id*/
    memset(query, 0, sizeof(query));

    if(ruleType == e_NOTIFICATION_RULE)
        snprintf(query, sizeof(query), "SELECT NotifyRuleID, NotificationMessage, NotificationDuration FROM SENSORNOTIFICATION where RuleID='%d' limit 1;", ruleId);
    else
        snprintf(query, sizeof(query), "SELECT NotifyRuleID, Message,Frequency FROM RULESNOTIFYMESSAGE WHERE RuleID='%d' limit 1;", ruleId);

    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

    /*execute database query*/
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        /*check if we got the data*/
        if(rowsRules && colsRules) {
            /*fill notification data*/
            qNode =  (SRulesQueue*)CALLOC(1, sizeof(SRulesQueue));
            if(NULL == qNode) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Memory Allocation Failed!");
                resetSystem();
            }

            qNode->ruleID = ruleId;
            qNode->notifyRuleID = atoi(ppsRulesArray[colsRules]);

            if( !(qNode->ruleMSG = STRDUP( ppsRulesArray[colsRules+1] ))) {
                APP_LOG("DEVICE:rule", LOG_ERR, "Memory Allocation Failed!");
                resetSystem();
            }
            qNode->ruleFreq = atoi(ppsRulesArray[colsRules+2]);

            qNode->ruleTS =  GetUTCTime();
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No valid data");
            /*free database buffer*/
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            return;
        }
    } else {
        APP_LOG("DEVICE:rule", LOG_ERR, "No valid data");
        return;
    }

    /* free database table result */
    WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule Queue Data\nruleID:%d\nnotifyRuleID:%d\nruleMSG:%s\nruleFreq:%d\nruleTS:%lu\n", qNode->ruleID,qNode->notifyRuleID,qNode->ruleMSG,qNode->ruleFreq,qNode->ruleTS);
    enqueueRuleQ(qNode);

    return;
}

int isExecuteInsightRule(int curValue, int ruleValue, int condition)
{
#ifdef PRODUCT_WeMo_Insight
    // compare the value of the parameter with condition value using the OPCode mentioned in the rule.
    switch(condition) {
    case E_EQUAL:
        if(curValue == ruleValue) {
            return(1);
        }
        break;
    case E_LARGER:
        if((curValue >= ruleValue) && (curValue < (ruleValue + INSIGHT_TASK_POLL_FREQ))) {
            return(1);
        }
        break;
    case E_LESS:
        if(curValue < ruleValue) {
            return(1);
        }
        break;
    case E_EQUAL_OR_LARGER:
        if(curValue >= ruleValue) {
            return(1);
        }
        break;
    case E_EQUAL_OR_LESS:
        if(curValue <= ruleValue) {
            return(1);
        }
        break;
    default:
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Wrong Insight condition: %d",condition);
        return (0);
        break;
    }
#endif
    return(0);
}

#ifdef PRODUCT_WeMo_Insight
void processInsightNotification(int insightRuleType, int ruleCurValue)
{
    int rowsRules=0,colsRules=0;
    unsigned int type, value, condition;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];
    SRuleInfo *psRule = gpsRuleList;

    if(gRuleHandle[e_INSIGHT_RULE].ruleCnt) {
        int currentState = GetCurBinaryState();
        while(psRule != NULL) {
            if((e_INSIGHT_RULE == psRule->ruleType) && (psRule->isActive)) {
                //APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule ID %d is Active, Execute it!", psRule->ruleId);

                /*get rule type for this insight rule*/
                memset(query, 0, sizeof(query));
                snprintf(query, sizeof(query), "SELECT Type,Value,Level FROM RULEDEVICES where RuleID='%d' AND DayID=-1;", psRule->ruleId);
                //APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

                /*execute database query*/
                if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
                    /*check if we got the data*/
                    if(rowsRules && colsRules) {
                        /*get rule ID values*/
                        type =  atoi(ppsRulesArray[colsRules]);
                        condition =  atoi(ppsRulesArray[colsRules+1]);
                        value =  atoi(ppsRulesArray[colsRules+2]);

                        //[WEMO-29853] - Append E_STATE case for APNS rule
                        //APNS rule case for POWER ONFOR
                        //if(type == insightRuleType)
                        if(E_ON_DURATION == type) {
                            if(isExecuteInsightRule(g_RuleONFor, value, condition)) {
                                APP_LOG("DEVICE:rule", LOG_DEBUG, "Type and condition Matched, insightRuleType:%d,ruleCurValue:%d,RuleValue:%d", type, g_RuleONFor, value);
                                enqueNotificationRule(psRule->ruleId, psRule->ruleType);
                            }
                        }
                        //APNS rule case for Sensing POWER ON/OFF
                        else if (E_STATE == type) {

                            //Restriction to have 1 notification for each state change
                            if (currentState != g_APNSLastState) {
                                //[WEMO-31158] - Added case for Rule Off when change state from ON -> SBY
                                if ((g_APNSLastState == POWER_ON) && (currentState == POWER_SBY)) {
                                    if(value == POWER_OFF) {
                                        value = POWER_SBY;
                                    }
                                }
                                //[WEMO-31158] - Ignore the case state change from SBY -> OFF
                                if((currentState == POWER_OFF) && (g_APNSLastState == POWER_SBY)) {
                                    condition = E_WRONG_OPCODE;
                                }
                                if (isExecuteInsightRule(currentState, value, condition)) {
                                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Matched, currentState: %d, currentValue:%d, condition:%d", currentState, value, condition);
                                    enqueNotificationRule(psRule->ruleId, psRule->ruleType);
                                }
                            }
                        }
                    } else {
                        APP_LOG("DEVICE:rule", LOG_ERR, "No entry for rule: %d", psRule->ruleId);
                        /*free database buffer*/
                        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
                    }
                } else {
                    APP_LOG("DEVICE:rule", LOG_ERR, "No entry for rule: %d", psRule->ruleId);
                }

                /*free database buffer*/
                WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            }
            psRule = psRule->psNext;
        }
        //update state to make sure 1 notification sent
        g_APNSLastState = currentState;
    }
    return;
}
#endif

void *InsightTask(void *args)
{
#ifdef PRODUCT_WeMo_Insight
    int nowSeconds=0, year=0, month_index=0, dayIndex=0;

    GetCalendarDayInfo(&dayIndex, &month_index, &year, &nowSeconds);

    if(nowSeconds%INSIGHT_TASK_POLL_FREQ) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Adjusting Rule boundary: %d", nowSeconds);
        sleep(INSIGHT_TASK_POLL_FREQ-(nowSeconds%INSIGHT_TASK_POLL_FREQ));
    }


    while(1) {

        //[WEMO-29853] - Update called method to add more APNS rules case
        /*check and process if any insight rule is active*/
        processInsightNotification(0, 0);

        /*sleep for INSIGHT_TASK_POLL_FREQ second*/
        sleep(INSIGHT_TASK_POLL_FREQ);
    }
#endif
    return NULL;
}

void executeNotifyRule(void)
{
    SRuleInfo *psRule = gpsRuleList;

    if(gRuleHandle[e_NOTIFICATION_RULE].ruleCnt) {
        while(psRule != NULL) {
            if((e_NOTIFICATION_RULE == psRule->ruleType) && (psRule->isActive)) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule ID %d is Active, Execute it!", psRule->ruleId);
                enqueNotificationRule(psRule->ruleId, psRule->ruleType);
            }
            psRule = psRule->psNext;
        }
    }
}

int checkAndExecuteCountdownTimer(int powerStatus)
{
    int retVal = 0;
#ifdef PRODUCT_WeMo_Dimmer
    /* do not execute countDown rule if fader is active */
    if(checkIfFaderRunning()) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Fader is active. Skipping count down rule execution!!!");
        return 1;
    }
#endif
    /*get contdown timer last minute running state*/
    int countdownRuleLastMinStatus =  gCountdownRuleInLastMinute;
    /*this API will start/restart/stop countdown timer dependng upon if coundown timer is not_running/runing_in_last_minute/running_not_in_last_ minute*/
    executeCountdownRule(powerStatus);
    /*check if it was running in last minute, if yes do not toggle relay, countdown timer restarted*/
    if(gRuleHandle[e_COUNTDOWN_RULE].ruleCnt && countdownRuleLastMinStatus) {
        APP_LOG("Button", LOG_DEBUG, "Countdown timer was in last minute, Do not toggle!");
#ifdef PRODUCT_WeMo_Insight
        if (POWER_OFF == g_PowerStatus)
            g_PowerStatus = POWER_ON;
#endif
        retVal = 1;
    }

    return retVal;
}

void stopCountdownTimer()
{
    int ret = -1;

    /*check if thread not exists*/
    if(s_countdown_rule_thread == INVALID_THREAD_HANDLE) {
        APP_LOG("UPNP: Rule", LOG_DEBUG, "Countdown _rule thread not running!");
        return;
    }

    /*clear gCountdownEndTime*/
    gCountdownEndTime = 0;

    /*cancel thread*/
    ret = pthread_cancel(s_countdown_rule_thread);
    if (0x00 != ret) {
        APP_LOG("UPNP: Rule", LOG_DEBUG, "thread_cancel: Could not stop countdown rule thread");
    } else {
        /*send local UPnP notification*/
        if(!gCountdownRuleInLastMinute) {
            APP_LOG("UPNP: Rule", LOG_DEBUG, "Not sending notify for gCountdownRuleInLastMinute %d",
                    gCountdownRuleInLastMinute);
            LocalCountdownTimerNotify();
        }
        APP_LOG("UPNP: Rule", LOG_DEBUG, "thread_cancel: Successfully stopped countdown rule thread");
    }

    /*mark invalid handle*/
    s_countdown_rule_thread = INVALID_THREAD_HANDLE;

    /*check the last minute status, if in last minute set power led  status to ON */
    if(gCountdownRuleInLastMinute) {
#ifdef PRODUCT_WeMo_Dimmer
        /* set animation to reflect end of the countdown timer. */
        setAnimation(LED_STATE_AUTO_OFF_RESET);
#else
#ifdef PRODUCT_WeMo_SNSV2
        system("rm " AUTOOFF_LAST_MIN);
#endif
        /*set LED status to on*/
        /*stop LED toggle by 1s ON 500ms OFF*/
        SetActivityLED(0x03);
#endif
        gCountdownRuleInLastMinute = 0;
    }

    return;
}

int getDataFromRuleDevicesTable(unsigned int ruleId, const char *field, char *buf)
{
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B];
    int ret = SUCCESS;
    /*get sensor duration this rule id*/
    memset(query, 0, sizeof(query));
    sqlite3_snprintf(sizeof(query), query, "SELECT %q FROM RULEDEVICES WHERE RuleID='%d' AND DeviceID='%q' limit 1;", field, ruleId, g_szUDN_1);
    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);

    /*execute database query*/
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        /*check if we got the data*/
        if(rowsRules && colsRules) {
            strcpy(buf, ppsRulesArray[colsRules]);
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Requested field:%s and value:%s", field, ppsRulesArray[colsRules]);
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
            ret = FAILURE;
        }
        /*free database buffer*/
        WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
    } else {
        APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
        ret = FAILURE;
    }
    return ret;
}

bool isCountDownRuleActive()
{
    SRuleInfo *psRule = gpsRuleList;
    while(psRule != NULL) {
        if((e_COUNTDOWN_RULE == psRule->ruleType) && (psRule->isActive)) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule ID %d is Active!", psRule->ruleId);
            return true;
        }
        psRule = psRule->psNext;
    }
    return false;
}

#if defined(PRODUCT_WeMo_LightV2)
void *countdownTriggerMonitor(void *args)
{
    unsigned int ruleId = 0;

    if(args) {
        /*get rule id from argument*/
        ruleId = *((int*) args);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "ruleId passed = %d", ruleId);
    } else {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "ruleId not available...exit...");
        return NULL;
    }

    APP_LOG("DEVICE:rule", LOG_DEBUG, "%s thread started...", __FUNCTION__);
    while (gCountdownRuleInLastMinute) {
        if(access( "/tmp/ButtonPressedWhenInLastMinOfAutoOff", F_OK ) == 0) {
            remove("/tmp/ButtonPressedWhenInLastMinOfAutoOff");
            executeCountdownRule(0);

            char buf[20];
            int duration = 0;
            if(SUCCESS == getDataFromRuleDevicesTable(ruleId, "CountdownTime", buf)) {
                duration = atoi(buf);

                if (duration > 60) {
                    SetWiFiLED(RGB_SWITCH_ON);
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Exit %s thread (extend timer to %d) ...", __FUNCTION__, duration);
                    return NULL;
                }
            }
        }
        usleep(50000);
    }
    APP_LOG("DEVICE:rule", LOG_DEBUG, "%s thread finished...", __FUNCTION__);
    return NULL;
}
#endif

void *startCountdownTimer(void *args)
{
    unsigned int ruleId = 0;
    unsigned int duration = 0;
    int sleepTime = 0;
    char buf[20];
    if(NULL != args) {
        /*get rule id from argument*/
        ruleId = *((int*)args);
        free(args);
        args = NULL;
    } else
        return NULL;

    APP_LOG("DEVICE:rule", LOG_DEBUG, "In Start Countdown Timer for rule Id:%d", ruleId);
    gCountdownRuleInLastMinute = 0;

    /*get countdown duration from DB*/
    memset(buf, 0, sizeof(buf));
    if(SUCCESS == getDataFromRuleDevicesTable(ruleId, "CountdownTime", buf)) {
        duration = atoi(buf);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Countdown Timer set to:%d", duration);
    }
#ifdef PRODUCT_WeMo_Dimmer
    int fadeOutTime=0;
    memset(buf, 0, sizeof(buf));
    if(SUCCESS == getDataFromRuleDevicesTable(ruleId, "ZBCapabilityStart", buf)) {
        char *tmp = strrchr(buf, ':');
        if(tmp) {
            fadeOutTime = atoi(tmp+1);
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Fade-out Time set to:%d", fadeOutTime);
        }
    }
#endif
    /*save countdown timer end utc time*/
    gCountdownEndTime = GetUTCTime() + duration;

    /*send local UPnP notification*/
    LocalCountdownTimerNotify();

    /*calculate sleep time less then get countdown duration from DB*/
    sleepTime = duration - DELAY_60SEC;
    if(0 < sleepTime) {
#if defined(PRODUCT_WeMo_LightV2)
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Setting RGB LED to solid green...");
        system("echo 23 > /sys/class/pwm/pwmchip0/pwm1/period &");
#endif
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Sleeping countdown rule for %d seconds", sleepTime);
        sleep(sleepTime);
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Continue after sleep");
    }

    gCountdownRuleInLastMinute = 1;

#if defined(PRODUCT_WeMo_LightV2)
    char *nWay_str = GetBelkinParameter("nWay");

    if(nWay_str && strlen(nWay_str) > 0) {
        if (atoi(nWay_str) == 3) {
            pthread_t trigger_monitor_thread = -1;
            pthread_attr_t countdowntriggermonitor_attr;
            pthread_attr_init(&countdowntriggermonitor_attr);
            pthread_attr_setdetachstate(&countdowntriggermonitor_attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&trigger_monitor_thread, &countdowntriggermonitor_attr, countdownTriggerMonitor, &ruleId);
        }
    }
#endif

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Start last minute LED toggle for 60 second");
#ifdef PRODUCT_WeMo_Dimmer
    unsigned char inhibitTimer = 60; /*1 minute*/
    /*set the WASP variable WASP_VAR_BUTTON_INHIBIT_TIMER to inhibit the on/off button from toggling the dimmer state */
    if(SUCCESS != setWaspVariable(WASP_VAR_BUTTON_INHIBIT_TIMER, WASP_VARTYPE_UINT8, (void*)&inhibitTimer)) {
        APP_LOG("waspPollTask", LOG_DEBUG, "Setting the inhibit timer failed!!");
    }

    /* set animation to reflect start of last minute countdown timer. */
    setAnimation(LED_STATE_AUTO_OFF);
#else
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    system("touch " AUTOOFF_LAST_MIN);
#endif
    /*start LED toggle by 1s ON 500ms OFF*/
    SetActivityLED(2);
#endif
    sleep(DELAY_60SEC);
    APP_LOG("DEVICE:rule", LOG_DEBUG, "last minute LED toggle done");

    /*stop power monitor thread*/
    StopPowerMonitorTimer();

    /*clear globals*/
    gCountdownRuleInLastMinute = 0;
    gCountdownEndTime = 0;

    /* This countdown may be ending after rule has already gone inactive, update cloud */
    gCountdownPendingNotification = COUNTDOWN_NOTIFY_PENDING;

#ifdef PRODUCT_WeMo_Dimmer
    /* turn off the device */
    setActuation(ACTUATION_COUNTDOWN_TIMER_RULE);
    ChangeBinaryState(POWER_OFF);
#else
#if defined(PRODUCT_WeMo_SNSV2) || defined(PRODUCT_WeMo_LightV2)
    system("rm " AUTOOFF_LAST_MIN);
#endif
    /*power off device*/
    SetRuleAction(POWER_OFF, ACTUATION_COUNTDOWN_TIMER_RULE);
#endif
    /*mark invalid handle*/
    s_countdown_rule_thread = INVALID_THREAD_HANDLE;

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Exiting countdown timer rule thread!");
    return NULL;
}

void startCountdownTimerThread(unsigned int ruleId)
{
    int *arg = (int*)CALLOC(1, sizeof(int));
    if(NULL == arg) {
        APP_LOG("UPNP: Rule", LOG_DEBUG, "Memory allocation failed");
        resetSystem();
    }
    *arg = ruleId;

    if(s_countdown_rule_thread != INVALID_THREAD_HANDLE) {
        /* If ON command is received and countdowntimer is already running, stop it and
           let the ON command override */
        APP_LOG("UPNP: Rule", LOG_DEBUG, "Countdown rule thread already running, Stopping It!");
        stopCountdownTimer();
        /* return if not sensor trigger */
        if(0 != strcmp(g_szActuation, ACTUATION_SENSOR_RULE)) {
            return;
        }
    }
    pthread_attr_t s_countdown_rule_attr;
    pthread_attr_init(&s_countdown_rule_attr);
    /* WEMO-46785:detach the thread to avoid any resource leak. */
    pthread_attr_setdetachstate(&s_countdown_rule_attr, PTHREAD_CREATE_DETACHED);
    int ret = pthread_create(&s_countdown_rule_thread, &s_countdown_rule_attr, startCountdownTimer, arg);
    if (0x00 != ret) {
        APP_LOG("UPNP: Rule", LOG_DEBUG, "Could not create countdown rule thread");
        resetSystem();
    } else
        APP_LOG("UPNP: Rule", LOG_DEBUG, "countdown Rule thread created");

    return;
}

void executeCountdownRule(int deviceNewState)
{
    SRuleInfo *psRule = gpsRuleList;
    int countdownRuleLastMinStatus = 0;

    APP_LOG ("DEVICE:rule", LOG_DEBUG, "In executeCountdownRule");

    APP_LOG ("DEVICE:rule", LOG_DEBUG, "gCountdownRuleInLastMinute = [%d]",
             gCountdownRuleInLastMinute);

#ifdef PRODUCT_WeMo_Insight
    /* If the Insight device state is POWER_SBY, do not trigger the countdown
     * timer rule.
     */
    if ((POWER_SBY == deviceNewState) && (0 == gCountdownRuleInLastMinute))
        return;
#endif
    /* WEMO-52323 : Adding condition, so that long press away rule superseeds auto off for insight as well */
    if(gRuleHandle[e_COUNTDOWN_RULE].ruleCnt && !(gRuleHandle[e_TIMER_RULE].ruleCnt) && !(gRuleHandle[e_AWAY_RULE].ruleCnt) && !LONG_PRESS_AWAY_ACTIVE) {
        while(psRule != NULL) {
            if((e_COUNTDOWN_RULE == psRule->ruleType) && (psRule->isActive)) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Rule ID %d is Active, Execute it! deviceNewState:%d", psRule->ruleId, deviceNewState);

                /*if new state is POWER_OFF*/
                if((POWER_ON != deviceNewState)) {
                    /*get contdown timer last minute running state*/
                    countdownRuleLastMinStatus =  gCountdownRuleInLastMinute;

                    /*stop countdown timer thread*/
                    stopCountdownTimer();

                    /*check if it was running in last minute, if yes restart countdown timer thread*/
                    if(countdownRuleLastMinStatus) {
#ifdef PRODUCT_WeMo_Dimmer
                        unsigned char inhibitTimer = 0;
                        /* stop the inhibit timer by writing 0 to it */
                        if(SUCCESS != setWaspVariable(WASP_VAR_BUTTON_INHIBIT_TIMER, WASP_VARTYPE_UINT8, (void*)&inhibitTimer)) {
                            APP_LOG("waspPollTask", LOG_DEBUG, "Setting the inhibit timer failed!!");
                        } else {
                            APP_LOG("waspPollTask", LOG_DEBUG, "inhibit timer stopped!!");
                        }
#endif
                        startCountdownTimerThread(psRule->ruleId);
                    }
                } else {
#ifdef PRODUCT_WeMo_Dimmer
                    /* device is getting ON with countdown rule active.
                       play LED_STATE_RULE_OPEN animation */
                    setAnimation(LED_STATE_RULE_OPEN);
#endif
                    /*restart countdown timer again*/
                    startCountdownTimerThread(psRule->ruleId);
                }

                break;
            }
            psRule = psRule->psNext;
        }
    } else
        stopCountdownTimer();

    APP_LOG ("DEVICE:rule", LOG_DEBUG, "Exiting executeCountdownRule");
    return;
}

void *CrockpotTask(void *args)
{
    return NULL;
}

void *MakersensorTask(void *args)
{
    return NULL;
}

void RuleToggleLed(int curState)
{
    pMessage msg = 0x00;

    if (0x00 == curState) {
        msg = createMessage(RULE_MESSAGE_OFF_IND, 0x00, 0x00);
    } else if (0x01 == curState) {
        msg = createMessage(RULE_MESSAGE_ON_IND, 0x00, 0x00);
    }

    SendMessage(PLUGIN_E_RELAY_THREAD, msg);
    SetLastUserActionOnState(curState);

    APP_LOG("Button", LOG_DEBUG, "rule ON/OFF message sent out");
}

#ifdef PRODUCT_WeMo_Dimmer
int SetDimmerAttributes(unsigned int brightness, unsigned int fader, unsigned int action, char* actuation)
{
    APP_LOG("DEVICE:rule", LOG_DEBUG, "Set fader time:%u to reach to brightness:%u on action: %u", fader, brightness, action);
    int ret = SUCCESS;
    char szFader[MAX_FADER_LENGTH] = {'\0',};
    int curState = -1;

    APP_LOG("DEVICE:rule", LOG_DEBUG, "set actuation to %s", actuation);
    setActuation(actuation);

    if(0 == strcmp(actuation, ACTUATION_TIME_RULE)) {
        if(POWER_ON == action) {
            setAnimation(LED_STATE_RULE_OPEN);
        } else {
            setAnimation(LED_STATE_RULE_CLOSE);
        }
    }

    curState = GetCurBinaryState();
    /* Either state is changing or Both current state and action are ON, handle brightness fade */
    if((curState != action) || (action && (brightness != getBrightness()))) {
        /* If rule action is to turn OFF without fade-out, turn OFF immediately */
        if(action == POWER_OFF && !fader)
            ChangeBinaryState(POWER_OFF);
        else {
            /*If current state is off, set current brightness start fade in from 1% */
            if(curState == POWER_OFF && fader)
                setBrightness(1, false);

            snprintf(szFader, sizeof(szFader), "%u:%ld:%d:%f:%d", fader, GetUTCTime(), 1, 0.0, brightness);
            setFader(szFader, 1);
        }
    }
    return ret;
}
#endif

int SetRuleAction(unsigned int ruleAction, char *actuation)
{
    int retVal;
    unsigned int ruleActionToApply = ruleAction;

    APP_LOG("DEVICE:rule", LOG_DEBUG, "set actuation to %s", actuation);
    setActuation(actuation);
    /* check if timer rule, set the animation to
       reflect the POWER_ON/POWER_OFF state */
#ifdef PRODUCT_WeMo_LightV2
    system("touch /tmp/rule_toggle");
#endif
    retVal = ChangeBinaryState(ruleActionToApply);
    if (0 == retVal) {
#ifdef PRODUCT_WeMo_Insight
        if(POWER_ON == ruleActionToApply) {
            ruleActionToApply = POWER_SBY;
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Changed ON State To %d", ruleActionToApply);
        }
#endif
        UPnPInternalToggleUpdate(ruleActionToApply);
    }
    return retVal;
}

#ifdef LONG_PRESS_SUPPORTED

/************************************************************************
 * Function: handleLongPressForToggle
 *     Decides on what action to take for the devices participating in
 *     the long press toggle rule and sends action to them.
 *  Parameters:
 *     deviceList - list of the devices from DB
 *     nDevices - number of devices
 *  Return:
 *     N.A.
************************************************************************/

static void
handleLongPressForToggle(char** deviceList, int nDevices)
{
    char *paramNames[] = {"BinaryState"};
    char *paramValue[1];
    char longPressNextToggleState[SIZE_4B]= {0};
    int nextState = 0;
    int longPressTargetState = 0;
    paramValue[0] = (char *)MALLOC(SIZE_4B);

    /* check if the LONG_PRESS_NEXT_TOGGLE_STATE is having the
       next state to toggle to. */
    char *toState = GetBelkinParameter(LONG_PRESS_NEXT_TOGGLE_STATE);
    if(deviceNodeInList(g_szUDN_1, deviceList, nDevices) == SUCCESS) {
        if(toState && strlen(toState)>0) {
            longPressTargetState = atoi(toState);
        } else {
            int curDimmerState = GetCurBinaryState();
            /* As per the UX, if the Dimmer is part of the long press toggle rule
               in which case the first toggle for all devices is the opposite of the
               current state of the controlling Dimmer. */
            longPressTargetState = !curDimmerState;
        }
#ifdef SIMULATED_OCCUPANCY
        /* cancel any away mode rule which is in execution. */
        if(isAwayRuleInExecution()) {
            notifyManualToggle();
        }
#endif
#ifdef PRODUCT_WeMo_Dimmer
        /* call cancelFaderAndNotify which cancels the fader if running */
        cancelFaderAndNotify();
#endif
        /*get contdown timer last minute running state*/
        int countdownRuleLastMinStatus =  gCountdownRuleInLastMinute;
        /* set the last user action as longPressRuleAction. This also
           stops power monitor thread */
        SetLastUserActionOnState(longPressTargetState);
        executeCountdownRule(longPressTargetState);
        /* if the count down timer is in last minute there is no need to toggle the
           device state. */
        if(gRuleHandle[e_COUNTDOWN_RULE].ruleCnt && countdownRuleLastMinStatus) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Countdown timer was in last minute, Do not toggle!");
        } else {
            /*action on self*/
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Processing action on self: %d", longPressTargetState);
            if(0 == ChangeBinaryState(longPressTargetState))
                UPnPInternalToggleUpdate(longPressTargetState);
        }
    } else {
        if(toState && strlen(toState)>0) {
            longPressTargetState = atoi(toState);
        } else {
            /* if the dimmer is not part of the rule, on first long press,
               POWER_OFF all the devices which are part of the long press rule. */
            longPressTargetState = POWER_OFF;
        }
    }

    snprintf(paramValue[0], SIZE_4B, "%d", longPressTargetState);
    nextState = !longPressTargetState;

    snprintf(longPressNextToggleState, SIZE_4B, "%d", nextState);
    SetBelkinParameter(LONG_PRESS_NEXT_TOGGLE_STATE, longPressNextToggleState);

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Sending SetBinaryState action: %s to devices participating in the long press rule.", paramValue[0]);
    PluginCtrlPointSendActionToList(PLUGIN_E_EVENT_SERVICE, "SetBinaryState", (const char **)paramNames, paramValue, 1, deviceList, nDevices);
    free(paramValue[0]);
}

/************************************************************************
 * Function: executeLongPressRule
 *     Long press rule handler thread entry point
 *  Parameters:
 *     arg - parameters to this thread
 *  Return:
 *     Returns NULL
************************************************************************/

void* executeLongPressRule(void *arg)
{
    /* try again in case rule engine was reloading */
    while(gRestartRuleEngine != RULE_ENGINE_RUNNING) {
        APP_LOG("Device:rule", LOG_DEBUG, "Waiting for %d sec so that RULE_ENGINE_RELOAD finishes.",WAIT_4_RULE_ENGINE_IN_SEC);
        pluginUsleep(WAIT_4_RULE_ENGINE_IN_SEC * MICROS_PER_SECOND );
    }
    int ruleId = gLongPressRuleActive;
    char query[SIZE_256B] = {0,};
    int rowsRuleDevices=0, colsRuleDevices=0;
    char  **ppsRuleDevicesArray=NULL;
    ELongPressRuleAction longPressRuleAction=0;

    char *targetDeviceList = NULL;

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Processing Long press rule %d", ruleId);

    /* Fetch long press rule action */
    snprintf(query, sizeof(query), "SELECT StartAction FROM RULEDEVICES WHERE RuleID='%d' limit 1;", ruleId);
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices)) {
        if(rowsRuleDevices && colsRuleDevices) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Fetched %d rows, %d columns", rowsRuleDevices, colsRuleDevices);
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No entry found");
            WeMoDBTableFreeResult(&ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices);
            return NULL;
        }

        APP_LOG("DEVICE:rule", LOG_DEBUG, "StartAction %s: %s", ppsRuleDevicesArray[0], ppsRuleDevicesArray[1]);
        longPressRuleAction = atoi(ppsRuleDevicesArray[1]);

        WeMoDBTableFreeResult(&ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices);

    }


    /* Fetch long press rule target devices */
    memset(query, 0, sizeof(query));

    switch(longPressRuleAction) {
    case e_LONG_PRESS_ACTION_OFF:
    case e_LONG_PRESS_ACTION_ON:
    case e_LONG_PRESS_ACTION_TOGGLE:
    case e_LONG_PRESS_ACTION_AWAYMODE:
        snprintf(query, sizeof(query), "SELECT DeviceID FROM TARGETDEVICES WHERE RuleID='%d';", ruleId);
        break;
    default:
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid Long press rule action: %d", longPressRuleAction);
        return NULL;
    }

    APP_LOG("DEVICE:rule", LOG_DEBUG, "Query: %s", query);

    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices)) {
        if(rowsRuleDevices && colsRuleDevices) {
            int i = 0;
            int location = 0;
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Fetched %d rows, %d columns", rowsRuleDevices, colsRuleDevices);
            for(i = 0; i < rowsRuleDevices; i++) {
                APP_LOG("DEVICE:rule", LOG_DEBUG, "Target device: %s", ppsRuleDevicesArray[i+1]);
                targetDeviceList = realloc(targetDeviceList, location + strlen(ppsRuleDevicesArray[i + 1]) + 1);
                if (targetDeviceList == NULL) {
                    APP_LOG("DEVICE:rule", LOG_ERR, "mem alloc failed for targetDeviceList");
                    return NULL;
                }
                memcpy((char *)(targetDeviceList + location), ppsRuleDevicesArray[i + 1], strlen(ppsRuleDevicesArray[i + 1]));
                location += (strlen(ppsRuleDevicesArray[i + 1]) + 1);
                targetDeviceList[location - 1] = ',';
            }
            if (targetDeviceList) {
                targetDeviceList[location - 1] = 0;
            }
        } else {
            APP_LOG("DEVICE:rule", LOG_ERR, "No entry found");
            WeMoDBTableFreeResult(&ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices);
            return NULL;
        }

        /* ppsRuleDevicesArray has the list of target devices */
        switch(longPressRuleAction) {
        case e_LONG_PRESS_ACTION_OFF:
        case e_LONG_PRESS_ACTION_ON: {
            char *paramNames[] = {"BinaryState"};
            char *paramValue[1];

            paramValue[0] = (char *)MALLOC(SIZE_4B);
            snprintf(paramValue[0], SIZE_4B, "%d", longPressRuleAction);

            if(deviceNodeInList(g_szUDN_1, &ppsRuleDevicesArray[1], rowsRuleDevices) == SUCCESS) {
#ifdef SIMULATED_OCCUPANCY
                /* cancel any away mode rule which is in execution. */
                if(isAwayRuleInExecution()) {
                    notifyManualToggle();
                }
#endif
#ifdef PRODUCT_WeMo_Dimmer
                /* call cancelFaderAndNotify which cancels the fader if running */
                cancelFaderAndNotify();
#endif
                /*get contdown timer last minute running state*/
                int countdownRuleLastMinStatus =  gCountdownRuleInLastMinute;
                /* set the last user action as longPressRuleAction. This also
                   stops power monitor thread */
                SetLastUserActionOnState(longPressRuleAction);
                executeCountdownRule(longPressRuleAction);
                /* if the count down timer is in last minute there is no need to toggle the
                   device state. */
                if(gRuleHandle[e_COUNTDOWN_RULE].ruleCnt && countdownRuleLastMinStatus) {
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Countdown timer was in last minute, Do not toggle!");
                } else {
                    /*action on self*/
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "Processing action on self: %d", longPressRuleAction);
                    if(0 == ChangeBinaryState(longPressRuleAction))
                        UPnPInternalToggleUpdate(longPressRuleAction);
                    /* TBD: manage related functionality */
                }
            }

            PluginCtrlPointSendActionToList(PLUGIN_E_EVENT_SERVICE, "SetBinaryState", (const char **)paramNames, paramValue, 1, &ppsRuleDevicesArray[1], rowsRuleDevices);
            free(paramValue[0]);
        }
        break;
        case e_LONG_PRESS_ACTION_TOGGLE:
            handleLongPressForToggle(&ppsRuleDevicesArray[1], rowsRuleDevices);
            break;
        case e_LONG_PRESS_ACTION_AWAYMODE: {
#ifdef SIMULATED_OCCUPANCY
            /* If any away mode rule is already running, send notifyManualToggle
               so that all the participating devices stop the away mode rule before
               starting LP away */
            if(isAwayRuleInExecution()) {
                notifyManualToggle();
            }
            /* If LP away is running on this device as a target device, consider this long
               press as an intervetion to stop the already running LP away rule. */
            if(g_longPressAwayRunning && !g_longPressOccurred)
                notifyManualToggle();

            if(!g_longPressOccurred) {
                simulatedOccupancyInit();
                parseTargetDevicesList(true, gLongPressRuleActive);
                /* if dimmer is part of the long press rule, then check if fader is running. If so,
                   cancel the fader and wait for some time to let the brightness change due to fader
                   take place. This is done before processing the LP away rule to  avoid considering
                   this brightness change as manual intervention */
                if(selfIndex>=0 && deviceNodeInList(g_szUDN_1, &ppsRuleDevicesArray[1], rowsRuleDevices) == SUCCESS) {
#ifdef PRODUCT_WeMo_LightV2
                    /* If this device is part of target list, show the LED to away sooner */
                    APP_LOG("DEVICE:rule", LOG_DEBUG, "selfIndex: %d, starting LED to away", selfIndex);
                    SetActivityLED(6);
#endif

#ifdef PRODUCT_WeMo_Dimmer
                    if(checkIfFaderRunning()) {
                        /* call cancelFaderAndNotify which cancels the fader if running */
                        cancelFaderAndNotify();
                        /* wait until the brightness get updated after fader stops before starting
                           the Away Task */
                        sleep(5);
                    }
#endif
                    /* stop the power monitor task on long press. set the last user action to OFF.
                       This is done because state change due to away rule is not a user action and to let the
                       power monitor thread work as default. */
                    SetLastUserActionOnState(POWER_OFF);
                }
                g_longPressOccurred = !g_longPressOccurred;
                SetBelkinParameter(LONG_PRESS_HAS_OCCURRED, "1");
                handleLongPressForAway(&ppsRuleDevicesArray[1], rowsRuleDevices, g_longPressOccurred);
            } else {
#ifdef PRODUCT_WeMo_Dimmer
                /* scheduleNightMode night mode thread after the LP
                   away mode ends, if nightMode is enabled. */
                if(gpsNightMode && gpsNightMode->nightMode) {
                    scheduleNightMode();
                }
#endif
                /* long press has occurred again. Consider
                   this as manual intervention. notifyManualToggle
                   also cleans the long press away data. */
                notifyManualToggle();
            }
#endif
        }
        break;
        default:
            APP_LOG("DEVICE:rule", LOG_DEBUG, "Invalid Long press rule action: %d", longPressRuleAction);
            return NULL;
        }

        WeMoDBTableFreeResult(&ppsRuleDevicesArray,&rowsRuleDevices,&colsRuleDevices);

        sendLongPressNotify(longPressRuleAction, rowsRuleDevices, 1, targetDeviceList);

        if (targetDeviceList)
            free(targetDeviceList);
    }

    gLongPressTid = INVALID_THREAD_HANDLE;
    return NULL;
}

/************************************************************************
 * Function: handleLongPressRule
 *     Long press rule handler
 *  Parameters:
 *     None
 *  Return:
 *     Returns value returned by createDetachedThread
************************************************************************/

int handleLongPressRule(void)
{
    if(INVALID_THREAD_HANDLE != gLongPressTid) {
        APP_LOG("DEVICE:rule", LOG_DEBUG, "Long press handle thread already running. Returning!!");
        return FAILURE;
    }
    return createDetachedThread(&gLongPressTid, executeLongPressRule, NULL);
}

#endif
