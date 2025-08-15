/***************************************************************************
*
*
* logger.c
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
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <malloc.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <libnvram.h>
#include "logger.h"

extern char *program_invocation_short_name;

int gloggerOptions = 33;
int gloggerLevel = -1;

#define NVRAM_PVT_LOG_ENABLE	"PVT_LOG_ENABLE"

#define NVRAM_HIDDEN_LOGS	"CustLogLevel"
#define HIDDEN_LOGS_VAL	"OodNeBswQ"

#define NVRAM_SYNCTIME_LASTTIMEZONE "LastTimeZone"
#define NVRAM_LASTDSTENABLE "LastDstEnable"
#define NVRAM_SYSLOGLEVEL "SysLogLevel"
char *gpHiddenLogs = NULL;

char g_buffTimeZoneOffset[128] = {0};
int gTimeZoneUpdated = 0;

char *gpLogEnable=NULL;
int lenIndex;
int logCounter;

int loggerSetLogLevel (int lvl, int option)
{
    gloggerLevel = lvl;
    gloggerOptions = option;
    return 0;
}

int loggerGetLogLevel ()
{
    return gloggerLevel;
}

int get_file_size (const char * file_name)
{
    struct stat sb;
    if (stat (file_name, &sb) != 0) {
        return -1;
    }
    return sb.st_size;
}

/*
 *  Function to set g_buffTimeZoneOffset value
 *
 ******************************************/

int setTimeZoneOffset (void)
{

    int DstVal=-2;
    float localTZ=0.0;

    char bufTemp[32];
    char bufTime[32];
    char *pch = NULL;
    int tz = 0;
    int tz1 = 0;

    char *LocalTimeZone = NvramGet(NVRAM_SYNCTIME_LASTTIMEZONE);
    if((LocalTimeZone != NULL) && (strlen(LocalTimeZone) != 0))
        localTZ = atof(LocalTimeZone);

    char *LastDstValue = NvramGet(NVRAM_LASTDSTENABLE);
    if((LastDstValue != NULL) && (strlen(LastDstValue) != 0))
        DstVal = atoi(LastDstValue);

    if(DstVal == 0)
        localTZ = localTZ + 1.0;

    gTimeZoneUpdated = 0;

    memset(g_buffTimeZoneOffset, 0x00, sizeof(g_buffTimeZoneOffset));
    memset(bufTemp, 0x0, 32);
    memset(bufTime, 0x0, 32);

    snprintf(bufTemp, sizeof(bufTemp), "%f|", localTZ);

    tz = atoi(bufTemp);
    pch = strstr (bufTemp,".");
    strncpy (bufTime,pch+1,2);
    tz1 = atoi(bufTime)*60/100;

    if (bufTemp[0x00] == '-')
        snprintf(g_buffTimeZoneOffset, sizeof(g_buffTimeZoneOffset), "%03d%02d",tz,tz1);
    else
        snprintf(g_buffTimeZoneOffset, sizeof(g_buffTimeZoneOffset), "+%02d%02d",tz,tz1);

    return 0;
}

/************************************************************************
 * Function: pluginLog
 *     Interface to be used by disfferent modules to write logs to syslog.
 *  Parameters:
 *     logIdentifier - mainly module name.
 *     logLevel - severity
 *     pLogStr - string to written to syslog
 *  Return:
 *     None
 ************************************************************************/
void pluginLog(char* logIdentifier, int logLevel, const char* pLogStr, ...)
{
    char	buff[1024];
    char	nbuff[1024+32];

    va_list	args;
    struct timeval tv;
    struct tm *now;

    /* don't go ahead if it is a PVT build and PVT_LOG_ENABLE is not set to 1 */
#ifndef DEBUG_ENABLE
    if(!gpLogEnable || !strlen(gpLogEnable) || (0 == atoi(gpLogEnable))) {
        return;
    }
#endif

    memset(buff, 0x0, 1024);
    memset(nbuff, 0x0, 1024+32);

    int hiddenSet = 0;
    if (logLevel == LOG_HIDE) {
        gpHiddenLogs = NvramGet(NVRAM_HIDDEN_LOGS);
        if ((0x00 != gpHiddenLogs) && (0x00 != strlen(gpHiddenLogs))) {
            if (!strncmp(gpHiddenLogs, HIDDEN_LOGS_VAL, strlen(HIDDEN_LOGS_VAL))) {
                hiddenSet = 1;
            }
        }

        if (hiddenSet) {
            logLevel = LOG_DEBUG;
        }
    }

    switch (logLevel) {
    case LOG_DEBUG:
        fprintf(stderr, "\x1B[0m");
        break;
    case LOG_INFO:
        fprintf(stderr, "\x1B[32m");
        break;
    case LOG_NOTICE:
    case LOG_WARNING:
        fprintf(stderr, "\x1B[35m");
        break;
    case LOG_ERR:
        fprintf(stderr, "\x1B[31m");
        break;
    case LOG_CRIT:
    case LOG_EMERG:
    case LOG_ALERT:
        fprintf(stderr, "\x1B[1m\x1B[31m");
        break;
    }

    va_start(args, pLogStr);
    vsnprintf(buff, sizeof (buff), pLogStr, args);
    va_end(args);

    gettimeofday(&tv,NULL);
    now = localtime(&tv.tv_sec);

    snprintf((char*)nbuff,sizeof(nbuff),"[%04d-%02d-%02d][%02d:%02d:%02d.%06d]",now->tm_year+1900,now->tm_mon+1,now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec, (int)tv.tv_usec);
    strncat(nbuff, (char*)buff, sizeof(nbuff)-strlen(nbuff)-1);

    if (gloggerLevel >= logLevel) {
        syslog(logLevel, "%s:%s\n", program_invocation_short_name, nbuff);
    }

    va_end(args);
    // Reset color.
    fprintf(stderr, "\x1B[0m");
    return;
}

void setLogLevel(void)
{
    char *loggerlevel = NvramGet(NVRAM_SYSLOGLEVEL);
    if (0x00 != loggerlevel && (0x00 != strlen(loggerlevel))) {
        gloggerLevel = atoi(loggerlevel);
        APP_LOG("UPNP", LOG_ALERT,"setting gloggerLevel to: %d from flash... success", gloggerLevel);
    } else {
        gloggerLevel = DEFAULT_LOG_LEVEL;
        char lvl[2];
        memset(lvl, 0, sizeof(lvl));
        snprintf(lvl, sizeof(lvl), "%d", gloggerLevel);
        NvramSet(NVRAM_SYSLOGLEVEL, lvl, 1);
        APP_LOG("UPNP", LOG_ALERT,"setting default gloggerLevel: %d", gloggerLevel);
    }
    setlogmask(LOG_UPTO(gloggerLevel));
}

void initLogger(void)
{
#ifdef DEBUG_ENABLE
    /*Open syslog for logging*/
    openlog(NULL, LOG_NDELAY|LOG_PERROR, LOG_USER);
#else   /* BOARD_TYPE: PVT */
    gpLogEnable = NvramGet(NVRAM_PVT_LOG_ENABLE);
    if ((0x00 != gpLogEnable) && (0x00 != strlen(gpLogEnable))) {
        /*Open syslog for logging*/
        openlog(NULL, LOG_NDELAY|LOG_PERROR, LOG_USER);
    }
#endif

    setLogLevel();
}

void deInitLogger(void)
{
    /*Close syslog*/
    closelog();
}

#ifndef DEBUG_ENABLE
void onOffPvtUploadLogs(int enable)
{
    if(enable == 2)
        NvramSet(NVRAM_HIDDEN_LOGS, HIDDEN_LOGS_VAL, 1);
    if(enable) {
        NvramSet(NVRAM_PVT_LOG_ENABLE, "1", 1);
        /*start logger*/
        initLogger();
    } else {
        NvramUnset(NVRAM_HIDDEN_LOGS, 1);
        NvramUnset(NVRAM_PVT_LOG_ENABLE, 1);
        gpLogEnable = NULL;
        /* disbaling log so changing logger level to default */
        gloggerLevel = -1;
        deInitLogger();
    }
}
#endif
