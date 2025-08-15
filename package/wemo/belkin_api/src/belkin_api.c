/***************************************************************************
*
*
*
*
* Created by Belkin International, Software Engineering on XX/XX/XX.
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
/*
 ============================================================================
 Name        : belkin_api.c
 Author      : Abhishek.Agarwal
 Version     : 30 April' 2012
 Copyright   :
 Description :
 ============================================================================
 */

#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <sys/wait.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <ctype.h>
#include <assert.h>
#include <syslog.h>
#include "fcntl.h"
#include "belkin_api.h"
#include <sys/stat.h>
#include "ra_ioctl.h"
#include <syslog.h>
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */
#include <sys/resource.h>

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE	(!FALSE)
#endif

#define S_SIZE 20

#define NTP_DEBUG
#ifdef NTP_DEBUG
#define NTP_LOG(...) syslog(LOG_DEBUG,__VA_ARGS__)
#else
#define NTP_LOG(...)
#endif

#define FILENAME_MAC "/tmp/macAddress.txt"
#define MACADDR_LEN     18
#define HWADDR "HWaddr"
#define inaddrr(x) (*(struct in_addr *) &ifr->x[sizeof sa.sin_port])
#ifdef __MIPSEL__
#ifdef _OPENWRT_
#define IFACE_MAC "ra0"
#else
#define IFACE_MAC "apcli0"
#endif
#else
#define IFACE_MAC "br-lan"
#endif
#define TZINDEX "timezone_index"
#define RESTORE_STATE "restore_state"
#define IPADDR_LEN     18
#define SYNCTIME_LASTTIMEZONE		"LastTimeZone"
#define SYNCTIME_DSTSUPPORT		"DstSupportFlag"

#define MAX_BUFFER_LEN     256
#define MAX_VAL_LEN     128
#define NUM_SEC_IN_HR      3600

#define FILE_NTP "/tmp/NtpTimeInfo"
#define NTP_STARTUP_CMD	"/usr/sbin/ntpclient -s -l -D -p 123 -h "
char gMacAddress[MACADDR_LEN];
char gWanGWIpAddress[IPADDR_LEN];
int g_lastTimeSync = 0;
//- It is a absolute time zone, should not be relative one from mobile app
float g_lastTimeZone = 0.0;

static int gWiredStatus = 0;  // -1 => Wired link up
int g_bWiredEthernet = 0;         // 1 -> use wired Ethernet interface
char g_szApSSID[MAX_APSSID_LEN];

// ntp servers
static char* g_sGlobalNTPszServer[] = {
    "0.linksys.pool.ntp.org",
    "1.linksys.pool.ntp.org",
    "2.linksys.pool.ntp.org",
    "3.linksys.pool.ntp.org"
};

#define NTP_SERVER_NUMBER 4

/*
   Daylight savings time rule lookup table.

   ******** Unfortunately this is currently wrong by design ********

   WeMo sets its timezone from information sent to it by the user's smart
   phone via Upnp.  The Upnp call provides the timezone's current offset
   from UTC, a flag indicating if there the timezone has daylight savings
   time, and a flag indicating if daylight savings time is currently in effect.

   The timezone offset UTC essentially gives us the timezone's longitude,
   but no information is given to allow us to determine the latitude.
   To say the least latitude makes a HUGE difference to daylight savings
   time calculations.  For example daylight savings time is active in the
	summer which is around June in the northern hemisphere and around December
	in the southern hemisphere.

   This table does the best it can by picking a daylight saving time
   rule that is correct for maximum number of potential customers.

   The data in this table was mined from the IANA time zone database
   (AKA tzdata, tz database, or Olson database) that shipped with
   Ubuntu Linux 14.04.

   This table contains a single entry per timezone offset, the
   entries that are commented out are either duplicates of the active entry
   or are (hopefully) rules for areas that are likely to have fewer
   WeMo users that the selected entry.

   Ticket WEMO-36023 has been created to track this issue.

   Excerpt from 'man tzset' reproduced here for convenience.

   The std string specifies the name of the timezone and must be three or more
   alphabetic characters. The offset string immediately follows std and
   specifies the time value to be added to the local time to get
   Coordinated Universal Time (UTC). The offset is positive if the local
   timezone is west of the Prime Meridian and negative if it is east. The hour
   must be between 0 and 24, and the minutes and seconds 0 and 59.

   The second format is used when there is daylight saving time:
   	std offset dst [offset],start[/time],end[/time]

   There are no spaces in the specification. The initial std and offset
   specify the standard timezone, as described above. The dst string and
   offset specify the name and offset for the corresponding daylight
   saving timezone. If the offset is omitted, it default to one hour ahead of
	standard time.

   The start field specifies when daylight saving time goes into effect and
   the end field specifies when the change is made back to standard time.

   Mm.w.d This specifies day d (0 <= d <= 6) of week w
   (1 <= w <= 5) of month m (1 <= m <= 12).
   Week 1 is the first week in which day d occurs and week 5 is the last week
   in which day d occurs. Day 0 is a Sunday.

   The time fields specify when, in the local time currently in effect,
   the change to the other time occurs. If omitted, the default is 02:00:00.

   Here is an example for New Zealand, where the standard time (NZST) is 12
   hours ahead of UTC, and daylight saving time (NZDT), 13 hours ahead of UTC,
   runs from the first Sunday in October to the third Sunday in March, and the
   changeovers happen at the default time of 02:00:00:

	TZ="NZST-12:00:00NZDT-13:00:00,M10.1.0,M3.3.0"
*/
#define MINS(x,y)	((x*60) + y)
struct {
    int GmtOffset;
    const char *TzString;
} DstLookup[] = {
    {-MINS(13,0),",M9.5.0/3,M4.1.0/4"},	//  Apia: WSST-13WSDT
    {-MINS(12,45),",M9.5.0/2:45,M4.1.0/3:45"},	//  CHAT: CHAST-12:45CHADT

    {-MINS(12,0),",M9.5.0,M4.1.0/3"},	//  NZ: NZST-12NZDT
// {-MINS(12,0),",M11.1.0,M1.3.4/75"},	//  Fiji: FJT-12FJST

// NB: the DST adjustment for Lord Howe island is 30 minutes,
// not the default 1 hour!
    {-MINS(10,30),"-11,M10.1.0,M4.1.0"},//  Lord_Howe: LHST-10:30LHDT-11

    {-MINS(10,0),",M10.1.0,M4.1.0/3"},	//  Currie: AEST-10AEDT
#if 0
    {-MINS(10,0),",M10.1.0,M4.1.0/3"},	//  Hobart: AEST-10AEDT
    {-MINS(10,0),",M10.1.0,M4.1.0/3"},	//  NSW: AEST-10AEDT
    {-MINS(10,0),",M10.1.0,M4.1.0/3"},	//  Victoria: AEST-10AEDT
#endif

    {-MINS(9,30),",M10.1.0,M4.1.0/3"},	//  Adelaide: ACST-9:30ACDT
//	{-MINS(9,30),",M10.1.0,M4.1.0/3"},	//  Yancowinna: ACST-9:30ACDT
    {-MINS(4,0),",M3.5.0/4,M10.5.0/5"},	//  Baku: AZT-4AZST

//	{-MINS(2,0),",M3.4.4/26,M10.5.0"},	//  Israel: IST-2IDT
    {-MINS(2,0),",M3.5.0/0,M10.5.0/0"},	//  Beirut: EET-2EEST
#if 0
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Athens: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Bucharest: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Chisinau: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  EET: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Kiev: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Mariehamn: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Nicosia: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Riga: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Sofia: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Tallinn: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Turkey: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Uzhgorod: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Vilnius: EET-2EEST
    {-MINS(2,0),",M3.5.0/3,M10.5.0/4"},	//  Zaporozhye: EET-2EEST

    {-MINS(2,0),",M3.5.4/24,M10.5.5/1"},	//  Amman: EET-2EEST
    {-MINS(2,0),",M3.5.4/24,M9.3.6/144"},//  Gaza: EET-2EEST
    {-MINS(2,0),",M3.5.4/24,M9.3.6/144"},//  Hebron: EET-2EEST
    {-MINS(2,0),",M3.5.5/0,M10.5.5/0"},	//  Damascus: EET-2EEST
    {-MINS(2,0),",M4.5.5/0,M9.5.4/24"},	//  Egypt: EET-2EEST
#endif

    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Amsterdam: CET-1CEST
#if 0
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Andorra: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Belgrade: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Berlin: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Bratislava: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Brussels: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Budapest: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  CET: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Ceuta: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Copenhagen: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Gibraltar: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Jan_Mayen: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Luxembourg: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Madrid: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Malta: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  MET: MET-1MEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Monaco: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Paris: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Poland: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  San_Marino: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Stockholm: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Tirane: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Vienna: CET-1CEST
    {-MINS(1,0),",M3.5.0,M10.5.0/3"},		//  Zurich: CET-1CEST
    {-MINS(1,0),",M9.1.0,M4.1.0"},			//  Windhoek: WAT-1WAST
#endif

    {MINS(0,0),",M3.5.0/1,M10.5.0"},		//  Eire: GMT0BST
#if 0
    {MINS(0,0),",M3.5.0/1,M10.5.0"},		//  Eire: GMT0IST
    {MINS(0,0),",M3.5.0/1,M10.5.0"},		//  Faeroe: WET0WEST
    {MINS(0,0),",M3.5.0/1,M10.5.0"},		//  Madeira: WET0WEST
    {MINS(0,0),",M3.5.0/1,M10.5.0"},		//  Portugal: WET0WEST
    {MINS(0,0),",M3.5.0/1,M10.5.0"},		//  WET: WET0WEST
    {MINS(0,0),",M3.5.0/1,M10.5.0/3"},	//  Troll: UTC0CEST-2
    {MINS(0,0),",M3.5.0,M10.5.0/3"},		//  El_Aaiun: WET0WEST
    {MINS(0,0),",M3.5.0,M10.5.0/3"},		//  Casablanca: WET0WEST
#endif

    {MINS(1,0),",M3.5.0/0,M10.5.0/1"},	//  Azores: AZOT1AZOST
//	{MINS(1,0),",M3.5.0/0,M10.5.0/1"},	//  Scoresbysund: EGT1EGST

    {MINS(3,0),",M10.3.0/0,M2.3.0/0"},	//  Sao_Paulo: BRT3BRST
#if 0
    {MINS(3,0),",M3.5.0/-2,M10.5.0/-1"},	//  Godthab: WGT3WGST
    {MINS(3,0),",M3.2.0,M11.1.0"},			//  Miquelon: PMST3PMDT
    {MINS(3,0),",M10.1.0,M3.2.0"},			//  Montevideo: UYT3UYST
#endif

    {MINS(3,30),",M3.2.0,M11.1.0"},		//  Newfoundland: NST3:30NDT

    {MINS(4,0),",M3.2.0,M11.1.0"},			//  Atlantic: AST4ADT
#if 0
    {MINS(4,0),",M3.2.0,M11.1.0"},			//  Bermuda: AST4ADT
    {MINS(4,0),",M3.2.0,M11.1.0"},			//  Glace_Bay: AST4ADT
    {MINS(4,0),",M3.2.0,M11.1.0"},			//  Goose_Bay: AST4ADT
    {MINS(4,0),",M3.2.0,M11.1.0"},			//  Moncton: AST4ADT
    {MINS(4,0),",M3.2.0,M11.1.0"},			//  Thule: AST4ADT
    {MINS(4,0),",M9.1.6/24,M4.4.6/24"},	//  Palmer: CLT4CLST
    {MINS(4,0),",M9.1.6/24,M4.4.6/24"},	//  Santiago: CLT4CLST
    {MINS(4,0),",M10.1.0/0,M3.4.0/0"},	//  Asuncion: PYT4PYST
    {MINS(4,0),",M10.3.0/0,M2.3.0/0"},	//  Campo_Grande: AMT4AMST
    {MINS(4,0),",M10.3.0/0,M2.3.0/0"},	//  Cuiaba: AMT4AMST
#endif

    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Detroit: EST5EDT
#if 0
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Eastern: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  EST5EDT: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Fort_Wayne: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Iqaluit: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Louisville: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Marengo: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Monticello: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Montreal: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Nassau: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  New_York: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Nipigon: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Pangnirtung: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Petersburg: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  posixrules: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Prince: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Thunder_Bay: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Vevay: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Vincennes: EST5EDT
    {MINS(5,0),",M3.2.0,M11.1.0"},			//  Winamac: EST5EDT
    {MINS(5,0),",M3.2.0/0,M11.1.0/1"},	//  Cuba: CST5CDT
#endif

    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Center: CST6CDT
#if 0
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Central: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Chicago: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  CST6CDT: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Knox_IN: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Matamoros: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Menominee: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Beulah: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  New_Salem: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Rainy_River: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Rankin_Inlet: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Resolute: CST6CDT
    {MINS(6,0),",M3.2.0,M11.1.0"},			//  Tell_City: CST6CDT
    {MINS(6,0),",M4.1.0,M10.5.0"},			//  Bahia_Banderas: CST6CDT
    {MINS(6,0),",M4.1.0,M10.5.0"},			//  Cancun: CST6CDT
    {MINS(6,0),",M4.1.0,M10.5.0"},			//  Merida: CST6CDT
    {MINS(6,0),",M4.1.0,M10.5.0"},			//  Mexico_City: CST6CDT
    {MINS(6,0),",M4.1.0,M10.5.0"},			//  Monterrey: CST6CDT
    {MINS(6,0),",M9.1.6/22,M4.4.6/22"},	//  Easter: EAST6EASST
#endif

    {MINS(7,0),",M3.2.0,M11.1.0"},			//  Boise: MST7MDT
#if 0
    {MINS(7,0),",M3.2.0,M11.1.0"},			//  Cambridge_Bay: MST7MDT
    {MINS(7,0),",M4.1.0,M10.5.0"},			//  Chihuahua: MST7MDT
    {MINS(7,0),",M3.2.0,M11.1.0"},			//  Inuvik: MST7MDT
    {MINS(7,0),",M4.1.0,M10.5.0"},			//  Mazatlan: MST7MDT
    {MINS(7,0),",M3.2.0,M11.1.0"},			//  Mountain: MST7MDT
    {MINS(7,0),",M3.2.0,M11.1.0"},			//  MST7MDT: MST7MDT
    {MINS(7,0),",M3.2.0,M11.1.0"},			//  Navajo: MST7MDT
    {MINS(7,0),",M3.2.0,M11.1.0"},			//  Ojinaga: MST7MDT
    {MINS(7,0),",M3.2.0,M11.1.0"},			//  Yellowknife: MST7MDT
#endif
    {MINS(8,0),",M3.2.0,M11.1.0"},			//  Los_Angeles: PST8PDT
#if 0
    {MINS(8,0),",M3.2.0,M11.1.0"},			//  Dawson: PST8PDT
    {MINS(8,0),",M3.2.0,M11.1.0"},			//  Ensenada: PST8PDT
    {MINS(8,0),",M3.2.0,M11.1.0"},			//  Pacific: PST8PDT
    {MINS(8,0),",M3.2.0,M11.1.0"},			//  PST8PDT: PST8PDT
    {MINS(8,0),",M3.2.0,M11.1.0"},			//  Yukon: PST8PDT
    {MINS(8,0),",M4.1.0,M10.5.0"},			//  Santa_Isabel: PST8PDT
#endif

    {MINS(9,0),",M3.2.0,M11.1.0"},			//  Anchorage: AKST9AKDT
#if 0
    {MINS(9,0),",M3.2.0,M11.1.0"},			//  Juneau: AKST9AKDT
    {MINS(9,0),",M3.2.0,M11.1.0"},			//  Nome: AKST9AKDT
    {MINS(9,0),",M3.2.0,M11.1.0"},			//  Sitka: AKST9AKDT
    {MINS(9,0),",M3.2.0,M11.1.0"},			//  Yakutat: AKST9AKDT
    {MINS(10,0),",M3.2.0,M11.1.0,0"},		//  Atka: HAST10HADT
    {MINS(10,0),",M3.2.0,M11.1.0"},
#endif

    {0,NULL}	// end of table
};
#undef MINS

static void Minutes2HrsMinutes(int TotalMinutes,int *pHours,int *pMinutes);
static void WriteTzInfo(char *TZ,int MinutesWest);
static int convertMon(char *mon);
static int convertDay(char *day);
int static TzOffset2MinutesWest(char *Offset);
int System(const char *command);
void SetTimeZone();

static int chkMACAddress(const char * mac_addr)
{
    int i = 0;
    int s = 0;

    while( *mac_addr ) {
        if( isxdigit(*mac_addr) ) {
            i++;
        } else if( *mac_addr == ':' || *mac_addr == '-' ) {
            if( i == 0 || i / 2 - 1 != s )
                break;
            ++s;
        } else {
            s = -1;
        }
        ++mac_addr;
    }

    return (i == 12 && (s == 5 || s == 0));
}

static int change_mac_addr(const char * mac_addr)
{
    char buf[MAX_VAL_LEN];
    int retVal = BELKIN_SUCCESS;

    memset(buf, 0x0, sizeof(buf));

    retVal = chkMACAddress(mac_addr);

    if( !retVal ) {
        return BELKIN_FAILURE;
    }

    snprintf(buf, sizeof(buf), "ifconfig %s down", IFACE_MAC);
    retVal = system(buf);

    memset(buf, 0x0, sizeof(buf));
    snprintf(buf, sizeof(buf), "ifconfig %s hw ether %s", IFACE_MAC, mac_addr);
    retVal = system(buf);

    memset(buf, 0x0, sizeof(buf));
    snprintf(buf, sizeof(buf), "ifconfig %s up", IFACE_MAC);
    retVal = system(buf);

    strncpy(gMacAddress, mac_addr, sizeof(gMacAddress));

    return BELKIN_SUCCESS;
}

static int check_parametername(char* ParameterName)
{
    assert(ParameterName);
    return ParameterName ? 0 : 1; //0 means success
}

static int check_parametervalue(char* ParameterValue)
{
    assert(ParameterValue);
    if (strlen(ParameterValue) > MAX_BUFFER_LEN - 1) {
        return BELKIN_FAILURE;
    }
    return BELKIN_SUCCESS; //0 means success
}

//START: Interfaces with NTP

// This function does nothing, it is provided for compatibility with the old
// libgemtek_api.so.  OpenWRT based code calls SetTimeAndTZ() instead.
int SetNTP(char *ServerIP, int Index, int EnableDaylightSaving)
{
    return 0;
}

/*
Usage: ntpclient [-c count] [-d] [-f frequency] [-g goodness] -h hostname
        [-i interval] [-l] [-p port] [-q min_delay] [-r] [-s] [-t]
*/
// Return zero if we set time and left ntpclient as a daemon
int RunNTP()
{
    time_t TimeUTC = 0;
    FILE *fp;
    struct tm timeinfo;
    char buf[MAX_BUFFER_LEN] = {'\0'};
    int date, year, hr, min, sec;
    char day[8] = {'\0'};
    char mon[8] = {'\0'};
    struct stat fileInfo;
    int serverIndex = 0;
    int GmtOffset;
    int Ret = BELKIN_FAILURE;	// Assume the worse
    char *last_time_zone = NULL;
    
    last_time_zone = GetBelkinParameter(SYNCTIME_LASTTIMEZONE);

    if ((last_time_zone == NULL) || (strlen(last_time_zone) == 0)) {
        GmtOffset = 0;
    }
    else {
        GmtOffset = -TzOffset2MinutesWest(last_time_zone);
    }

    NTP_LOG("%s: GmtOffset: %d\n",__FUNCTION__,GmtOffset);
    do {
        unlink(FILE_NTP);
        // kill off any ntpclients running in daemon mode
        system("killall -KILL ntpclient");

        // Try all available servers
        for(serverIndex = 0; serverIndex < NTP_SERVER_NUMBER; serverIndex++) {
            memset(buf, 0x00, sizeof(buf));
            snprintf(buf,sizeof(buf),"ntpclient -s -l -c 1 -h %s -i 1",
                     g_sGlobalNTPszServer[serverIndex]);
            NTP_LOG("%s: Trying server %d, IP: %s",__FUNCTION__, serverIndex,
                    g_sGlobalNTPszServer[serverIndex]);
            if(system(buf) == 0) {
                lstat(FILE_NTP, &fileInfo);
                if((off_t)fileInfo.st_size != 0) {
                    syslog(LOG_INFO,
                           "%s: time successfully retrieved from ntp server %s\n",
                           __FUNCTION__,g_sGlobalNTPszServer[serverIndex]);

                    if((fp = fopen(FILE_NTP, "r")) == NULL) {
                        perror("File opening error");
                        return 0;
                    }

                    fscanf(fp,"%s %s %d %d:%d:%d %d",day,mon,&date,&hr,&min,&sec,&year);
                    fclose(fp);

                    unlink(FILE_NTP);

                    timeinfo.tm_sec = sec;
                    timeinfo.tm_min = min;
                    timeinfo.tm_hour = hr;
                    timeinfo.tm_mday = date;
                    timeinfo.tm_year = year - 1900;
                    timeinfo.tm_mon = convertMon(mon);
                    timeinfo.tm_wday = convertDay(day);

                    TimeUTC = mktime(&timeinfo);
                    if(TimeUTC == 0) {
                        syslog(LOG_ERR,"%s#%d: Internal error\n",__FUNCTION__,__LINE__);
                    }
                    break;
                }
            }
        }
    } while(FALSE);


    if(TimeUTC == 0) {
        syslog(LOG_INFO,"%s: NTP unable to set time\n",__FUNCTION__);
    } else do {
            int MallocSize;
            char *CmdLine;

            // Start ntpclient running in daemon mode to keep the time synced

            // The OpenWRT startup script (ntpclient.hotplug) starts ntpclient like this:
            // $NTPC ${COUNT:+-c $COUNT} ${INTERVAL:+-i $INTERVAL} -s -l -D -p $PORT -h $SERVER 2> /dev/null
            // Do the same more or less

            MallocSize = strlen(NTP_STARTUP_CMD) +
                         strlen(g_sGlobalNTPszServer[serverIndex]) + 1;
            if((CmdLine = (char *) malloc(MallocSize)) == NULL) {
                syslog(LOG_ERR,"%s#%d: malloc failed\n",__FUNCTION__,__LINE__);
                break;
            }
            strcpy(CmdLine,NTP_STARTUP_CMD);
            strcat(CmdLine,g_sGlobalNTPszServer[serverIndex]);

            NTP_LOG("%s: running %s\n",__FUNCTION__,CmdLine);
            if(System(CmdLine) != 0) {
                syslog(LOG_ERR,"%s: running ntpclient in daemon mode failed - %m\n",
                       __FUNCTION__);
            }
            free(CmdLine);
            Ret = BELKIN_SUCCESS;

            /* NTP will set time, set Timezone info to /tmp/TZ->/etc/TZ */
            SetTimeZone();
        } while(FALSE);

    return Ret;
}

#define NUM_MONTHS	12
#define NUM_DAYS	7
int convertMon(char *mon)
{
    int i;
    char *months[NUM_MONTHS] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};

    for( i = 0; i < NUM_MONTHS; i++ ) {
        if( (strcasecmp(mon, months[i])) == 0 )
            break;
    }
    return i;
}

static int convertDay(char *day)
{

    char *days[NUM_DAYS] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    int i;

    for(i=0; i<NUM_DAYS; i++)
        if((strcasecmp(day, days[i])) == 0)
            break;

    return i;
}

#define FILE_NTP "/tmp/NtpTimeInfo"
// Return system time.  This function exists for compatibility with old
// code that was written to use the GemTek API which set system time to
// local time so the standard POSIX time functions didn't work correctly.
unsigned long int GetUTCTime(void)
{
    return (unsigned long int) time(NULL);
}
//END: Interfaces with NTP

//Start: Interfaces with Firmware Update
//remaining
int Firmware_Update(char* File_Path)
{
    char update_command[MAX_BUFFER_LEN] = {0,};
    int  system_rtn = 256;
    /*
     * If request lands here, we are definitely upgrading OpenWRT to OpenWRT firmware image
     * Do not mind the file extension here, its a upd image and not the gpg image
     */
    if(!File_Path || !strlen(File_Path)) {
        printf("Invalid file path..\n");
        return BELKIN_FAILURE;
    }

    memset(update_command, 0x0, sizeof(update_command));
    snprintf(update_command, sizeof(update_command), "sh -x /sbin/firmware_update.sh %s", File_Path);

    system_rtn = system(update_command);
    if (system_rtn == 0)
        return BELKIN_SUCCESS;
    else
        return BELKIN_FAILURE;
}
//END: Interfaces with Firmware Update

//Start: Interfaces with New_Firmware Update
int New_Firmware_Update(char* File_Path, int is_signed)
{
    char update_command[MAX_BUFFER_LEN] = { 0x00 };
    int  system_rtn = 256;

    if(!File_Path || !strlen(File_Path)) {
        fprintf(stderr, "Invalid file path..\n");
        return BELKIN_FAILURE;
    }

    /*
     * If the image is not signed, it should be named "/tmp/firmware.img".
     * The update script(firmware_update.sh) skips signature checking if file is "/tmp/firmware.img".
    */
    if (is_signed)
        snprintf(update_command, sizeof(update_command), "sh -x /sbin/firmware_update.sh %s", File_Path);
    else
        snprintf(update_command, sizeof(update_command), "mv %s /tmp/firmware.img; sh -x /sbin/firmware_update.sh /tmp/firmware.img", File_Path);

    system_rtn = system(update_command);
    if (system_rtn == 0)
        return BELKIN_SUCCESS;
    else
        return BELKIN_FAILURE;
}
//END: Interfaces with New_Firmware Update

//Start: Interfaces with Update System Time

#define ADD_TZ(...) 										\
	Wrote = snprintf(&TZ[Next],Left,__VA_ARGS__);\
	Next += Wrote;											\
	Left -= Wrote;											\
	if(Left <= 1 || Next >= sizeof(TZ)) {  		\
		break;												\
	}															\
 
// Example TZ format: "NZST-12:00:00NZDT-13:00:00,M10.1.0,M3.3.0"
void SetTimeZone()
{
    char *Offset;
    int bEnableDST;
    int Wrote;
    char TZ[80];
    int Next = 0;
    int Left = 80;
    int Hours;
    int Minutes;
    int MinutesWest;
    int i;

    Offset = GetBelkinParameter(SYNCTIME_LASTTIMEZONE);
    if(strlen(Offset) == 0) {
        // Not set, assume UTC
        Offset = "0";
    }
    g_lastTimeZone = atof(Offset);
    bEnableDST = *GetBelkinParameter(SYNCTIME_DSTSUPPORT) == '1' ? TRUE : FALSE;
    NTP_LOG("%s: Offset: %s, bEnableDST: %d\n",
            __FUNCTION__, Offset, bEnableDST);

    MinutesWest = TzOffset2MinutesWest(Offset);
    Minutes2HrsMinutes(MinutesWest,&Hours,&Minutes);

    do {
        ADD_TZ("STD%d:%02d",Hours,Minutes);

        if(bEnableDST) {
            // This timezone has daylight saving time
            for(i = 0; DstLookup[i].TzString != NULL; i++) {
                if(DstLookup[i].GmtOffset == MinutesWest) {
                    ADD_TZ("DST%s",DstLookup[i].TzString);
                    break;
                }
            }
            if(DstLookup[i].TzString == NULL) {
                syslog(LOG_ERR,"%s#%d: Internal error, couldn't find offset %d",
                       __FUNCTION__,__LINE__,MinutesWest);
            }
        }

        NTP_LOG("%s: TZ=%s\n",__FUNCTION__,TZ);
        WriteTzInfo(TZ,MinutesWest);
    } while(FALSE);

    if(Left <= 1 || Next >= sizeof(TZ)) {
        syslog(LOG_ERR,"%s#%d: Internal error, Left: %d, Next: %d\n",
               __FUNCTION__,__LINE__,Left,Next);
    }
}

// Set the system time and C run time library's TZ variable
// NB: Index and EnableDaylightSaving are ignored, they
// are present for compatibility with the old libgemtek_api.so
int SetTime(unsigned int SecondsUTC, int Index, int bEnableDST)
{
    struct timeval now;
    time_t TimeNow;
    time_t DeltaT;
    int Ret = 0;	// assume the best

    // Set global values for IsApplyTimeSync()
    g_lastTimeSync = (int) SecondsUTC;

    SetTimeZone();

    TimeNow = time(NULL);

    if(TimeNow > SecondsUTC) {
        DeltaT = TimeNow - SecondsUTC;
    } else {
        DeltaT = SecondsUTC - TimeNow;
    }

    // If time being set is "close" to the system time then don't set the
    // system time.  The assumption is that ntpd is running and it can set
    // a better time than the App can via UPnP calls over the WiFi network.
    if(DeltaT > 60) {
        now.tv_sec = SecondsUTC;
        now.tv_usec = 0;

        if(settimeofday(&now,NULL) != 0) {
            syslog(LOG_ERR,"%s#%d: settimeofday failed - %m\n",
                   __FUNCTION__,__LINE__);
            Ret = -1;
        }
    } else {
        syslog(LOG_INFO,"%s: DeltaT %d, not setting system time\n",__FUNCTION__,
               (int) DeltaT);
    }

    return Ret;
}

//END: Interfaces with Update System Time

//Start: Interfaces with Device Info
char* GetMACAddress(void)
{
    char *p = NULL;
    FILE *fp =  NULL;
    char buf[MAX_VAL_LEN] = {0,};
    char line[MAX_BUFFER_LEN] = {0,};

    if( strlen(gMacAddress) && strcmp(gMacAddress, "00:00:00:00:00:00") ) {
        printf("Cached MAC Address = %s \n", gMacAddress);
        return gMacAddress;
    }

    snprintf(buf, sizeof(buf), "ifconfig %s > %s", IFACE_MAC, FILENAME_MAC);

    system(buf);

    fp = fopen(FILENAME_MAC, "rb");
    if( !fp ) {
        perror("File opening error");
        strncpy(gMacAddress, "00:00:00:00:00:00", sizeof(gMacAddress));
        return gMacAddress;
    }

    memset(line, 0x0, MAX_BUFFER_LEN);
    while( fgets(line, MAX_BUFFER_LEN, fp) ) {
        if( (p = strstr(line, HWADDR)) ) {
            break;
        }
        memset(line, 0x0, MAX_BUFFER_LEN);
    }

    if( !p ) {
        //printf("NO MAC address is configured\n");
        if( fp ) {
            fclose(fp);
        }

        strncpy(gMacAddress, "00:00:00:00:00:00", sizeof(gMacAddress));

        memset(buf, 0x0, MAX_VAL_LEN);
        snprintf(buf, sizeof(buf) , "rm %s", FILENAME_MAC);
        system(buf);

        return gMacAddress;
    }

    p += strlen(HWADDR);
    p++;
    p[MACADDR_LEN-1] = '\0';

    strncpy(gMacAddress, p, sizeof(gMacAddress));

    if( fp ) {
        fclose(fp);
    }

    memset(buf, 0x0, MAX_VAL_LEN);
    snprintf(buf, sizeof(buf), "rm %s", FILENAME_MAC);
    system(buf);

    printf("MAC Address: %s\n", gMacAddress);

    return gMacAddress;
}

/*
 * Get the MAC address of interface.
 *
 * intf is the interface name
 *
 * Returns null terminated string containing MAC with ":"'s.
 * To maintain compatibility with previous version, also stores string
 * in global variable gMacAddress.  :-(
 * If there is a problem, it returns (and stores) an all zeros address
 * @return The MAC address (or "00:...")
 */
#ifndef MAX_MAC_LEN
#define MAX_MAC_LEN        20
#endif
void GetMACAddress_ext(char * intf, char *buff)
{
    struct ifreq s;
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    strcpy(s.ifr_name, intf);
    if (0 == ioctl(fd, SIOCGIFHWADDR, &s)) {
        const unsigned char* mac=(unsigned char*)s.ifr_hwaddr.sa_data;
        snprintf( buff, MAX_MAC_LEN,
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strcpy(buff, "00:00:00:00:00:00");
    }

    return;
}

int SetMACAddress(char *mac_addr)
{
    int retVal = BELKIN_SUCCESS;

    retVal = change_mac_addr(mac_addr);

    return retVal;
}

char* GetSerialNumber(void)
{
    char *paramN = "SerialNumber";
    char *paramV = NULL;

    paramV = GetBelkinParameter(paramN);
    return paramV;
}

int SetSerialNumber(char *serial_number)
{
    char *paramN = "SerialNumber";

    return (SetBelkinParameter(paramN, serial_number));
}
//End: Interfaces with Device Info

//Start: Interfaces with Device LAN Info
#define LAN_IFACE "ra0"
#ifdef __MIPSEL__
#define WAN_IFACE "apcli0"
#else
#define WAN_IFACE "br-lan"
#endif
#ifdef MT7688
#define WIRED_IFACE  "br-lan"
#else
#define WIRED_IFACE  "eth2"
#endif

static int getIPAddress(char *if_name,char *ip_adr,int Len)
{
    int sock=-1;
    struct ifreq *ifr;
    struct ifreq ifrr;
    struct sockaddr_in sa;
    int Ret = 0;   // assume the best

    *ip_adr = 0;
    ifr = &ifrr;
    ifrr.ifr_addr.sa_family = AF_INET;
    strncpy(ifrr.ifr_name, if_name, sizeof(ifrr.ifr_name));

    do {
        if((sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_IP)) < 0) {
            Ret = errno;
            syslog(LOG_ERR,"%s: socket failed - %m",__FUNCTION__);
            break;
        }

        if(ioctl(sock,SIOCGIFADDR,ifr) < 0 ) {
            Ret = errno;
            syslog(LOG_ERR,"%s: ioctl(SIOCGIFADDR) - %m",__FUNCTION__);
            break;
        }
        strncpy(ip_adr,inet_ntoa(inaddrr(ifr_addr.sa_data)),Len);
    } while(FALSE);

    if(sock >= 0) {
        close(sock);
    }

    return Ret;
}

char* GetLanIPAddress(void)
{
    static char IP_Adr[IPADDR_LEN] = {0};

    getIPAddress(LAN_IFACE,IP_Adr,IPADDR_LEN);
    return IP_Adr;
}
//END: Interfaces with Device LAN Info

//Start: Interfaces with Device WAN Info
char* GetWanIPAddress(void)
{
    static char IP_Adr[IPADDR_LEN] = {0};

    getIPAddress(GetLanDeviceName(),IP_Adr,IPADDR_LEN);
    if(!strlen(IP_Adr))
        strcpy(IP_Adr, "0.0.0.0");

    return IP_Adr;
}


#define CMD_GW "route -n | grep UG > /tmp/gateway.txt"
#define FILENAME_GW "/tmp/gateway.txt"
#define DEFAULT "0.0.0.0"

#define CMD_DEF_GW "route -n | grep 'UG[ \t]' | awk '{print $2}' > /tmp/gateway.txt"

char* UpdateWanDefaultGateway(void)
{
    char line[MAX_BUFFER_LEN];
    char command[MAX_BUFFER_LEN];

    memset(command, 0x00, sizeof(command));
    snprintf(command, sizeof(command), "%s", CMD_DEF_GW);
    system(CMD_DEF_GW);

    strncpy(gWanGWIpAddress, "0.0.0.0", sizeof(gWanGWIpAddress));

    FILE *fp = fopen(FILENAME_GW, "rb");

    if( fp ) {
        while( fgets(line, MAX_BUFFER_LEN, fp) ) {
            printf("UpdateWanDefaultGateway: line = %s\n", line);
            memset(gWanGWIpAddress, 0x00, sizeof(gWanGWIpAddress));
            strncpy(gWanGWIpAddress, line, sizeof(gWanGWIpAddress));
            break;
        }
        fclose(fp);
    }

    printf("UpdateWanDefaultGateway: GATEWAY = %s\n", gWanGWIpAddress);
    return gWanGWIpAddress;
}
char* GetWanDefaultGateway(void)
{
    FILE *fp;
    int result;

    char iface[64];
    unsigned long destination, gateway, mask;
    int flags, refcnt, use, metric, mtu, window, irtt;

    struct in_addr ip_address;

    memset(gWanGWIpAddress, 0, IPADDR_LEN);
    strcpy(gWanGWIpAddress, "0.0.0.0");

    fp = fopen("/proc/net/route", "r");
    if (!fp) {
        return gWanGWIpAddress;
    }
    /* steal from busybox */
    if (fscanf(fp, "%*[^\n]\n") < 0) { /* Skip the first line. */
        /* Empty or missing line, or read error. */
        fclose(fp);
        return gWanGWIpAddress;
    }

    while((result = fscanf(fp, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n",
                           iface, &destination, &gateway, &flags, &refcnt,
                           &use, &metric, &mask,
                           &mtu, &window, &irtt)) != EOF) {
        if (result != 11) {
            /* should not come here, but route table isn't complete */
            break;
        }
        /* 3 = 1 (route usable) | 2 (destination is gateway) */
        if (flags == 3) {
            /* found the gateway */
            ip_address.s_addr = gateway;
            strcpy(gWanGWIpAddress, inet_ntoa(ip_address));
            break;
        }
    }
    fclose(fp);

    printf("GetWanDefaultGateway: GATEWAY = %s\n", gWanGWIpAddress);
    return gWanGWIpAddress;
}

//END: Interfaces with Device WAN Info

//Start: Interfaces with AP
int GetEnableAP(void)
{
    return BELKIN_SUCCESS;
}
//END: Interfaces with AP

//Start: Interfaces with Save Belkin Parameters
//Set the values in NVRAM. The name=value format
int SetBelkinParameter(char* ParameterName, char* ParameterValue)
{
    int retVal = BELKIN_SUCCESS;
    int argCount = 3;
    char *argValue[3];
    int i = 0, len =0;

    printf("%s - ParameterName = %s , ParameterValue = %s\n", __FUNCTION__, ParameterName, ParameterValue);

    retVal = check_parametername(ParameterName);
    if( retVal ) {
        return BELKIN_FAILURE;
    }

    retVal = check_parametervalue(ParameterValue);
    if( retVal ) {
        return BELKIN_FAILURE;
    }

    argValue[0] = (char *)malloc(10);
    strcpy(argValue[0], "nvram_set");

    argValue[1] = (char *)malloc((strlen(ParameterName)+1));
    strcpy(argValue[1], ParameterName);

    len = strlen(ParameterValue);
    if( len ) {
        argValue[2] = (char *)malloc((len+3));
        strcpy(argValue[2], ParameterValue);
    } else {
        --argCount;
    }

    nvramset(argCount, argValue);

    for( i = 0; i < argCount; i++ ) {
        free(argValue[i]);
    }

    return retVal;
}

char persistenceVal[MAX_BUFFER_LEN][MAX_BUFFER_LEN];
static int indexVal;

char* GetBelkinParameter(char* ParameterName)
{
    char *argValue[] = { "nvram_get", ParameterName };
    static size_t argCount = sizeof( argValue ) / sizeof( argValue[0] );
    char paramVal[MAX_BUFFER_LEN] = {};  /* Empty braces cause zero-fill */
    char *retVal = "";

    if (!check_parametername(ParameterName)) {
        nvramget(argCount, argValue, paramVal);
        if (strlen(paramVal) > MAX_BUFFER_LEN - 1) {
            return retVal;
        }
        if ( *paramVal ) {
            retVal = strncpy( persistenceVal[indexVal],
                              paramVal,
                              sizeof( persistenceVal[indexVal] ) - 1 );
            /* Assuming max MAX_BUFFER_LEN variables are to be saved */
            indexVal = (indexVal + 1) % MAX_BUFFER_LEN;
        }
    }

    return retVal;
}

int UnSetBelkinParameter(char* ParameterName)
{
    int retVal = BELKIN_SUCCESS;
    int i;
    int argCount = 2;
    char *argValue[2];

    //printf("%s - ParameterName = %s\n", __FUNCTION__, ParameterName);

    retVal = check_parametername(ParameterName);

    if( retVal ) {
        return BELKIN_FAILURE;
    }

    argValue[0] = (char *)malloc(10);
    strcpy(argValue[0], "nvram_set");

    argValue[1] = (char *)malloc((strlen(ParameterName)+1));
    strcpy(argValue[1], ParameterName);

    nvramset(argCount, argValue);

    for( i = 0; i < argCount; i++ ) {
        free(argValue[i]);
    }
    return BELKIN_SUCCESS;
}
//END: Interfaces with Save Belkin Parameters

//Start: Interfaces with Save Setting and Reboot
int ResetToFactoryDefault(int runScript)
{
    char restoreBuf[10] = {'\0'};

    system("nvram restore");

    sprintf(restoreBuf, "%d", runScript);
    SetBelkinParameter(RESTORE_STATE, restoreBuf);

    system("/sbin/jffs2reset -y");
    system("/sbin/mtd erase Belkin_settings");
    sleep(5);
    system("reboot");

    return BELKIN_SUCCESS;
}

//remaining
int SaveSetting(void)
{
    NvramCommit();
    return BELKIN_SUCCESS;
}
//END: Interfaces with Save Setting and Reboot

#if defined(PRODUCT_WeMo_LightV2)
/* RGB LED behavior
   Command echo <number> > /sys/class/pwm/pwmchip0/pwm1/period
        1: Connection established - LED White ON

        2: Ready to Connect - Slow Blink - RGB 1 Blue

        3: Identify Hardware (HomeKit) - Fast Blink - RGB 1 Green

        41: Connection Found - Solid 100% - #4A - RGB 1 White

        42: Connection Found - Solid 100% - #4B - RGB 1 Blue

        5: Turn on - Solid 100% - RGB 1 White

        6: Turn off - Dimmed - White LED 1

        7: Power Button Control - Fast Blink - RGB 1 Green

        8. Schedule Rule - solid 100% - RGB 1 Green

        9. Auto Off Timer Rule - solid 100% / slow blink to fast blink  - RGB 1 Green

        10. Long Press Rule - Fast Blink - RGB 1 Green

        11. Away Rule - solid 100% - RGB 1 Green

        12. Service Integration - Fast Blink - RGB 1 Green

        131. Error Detected - Solid 100%- RGB 1 Red

        132. Error Detected - Slow Blink- RGB 1 Red
        
        14. Press Button 2 - Restart - Reset Wifi - Factory Restore - Slow Blink - RGB 1 White, Blue, and Red

        15. Firmware Update - low Blink - LED 1
*/
int SetWiFiLED(int state)
{
    char cmd[256] = {0,};

    if ((state == RGB_SWITCH_ON) || (state == RGB_SWITCH_OFF)) {
        system ("echo none > /sys/class/leds/amber/trigger; \n"
                "status=$(cat /sys/class/gpio/gpio19/value)\n"\
                "if [ $status -eq 0 ]; then \n"
                "echo 6  > /sys/class/pwm/pwmchip0/pwm1/period;"\
                "else \n"\
                "echo 5 > /sys/class/pwm/pwmchip0/pwm1/period; \n"\
                "fi");
    }
    else {
        sprintf(cmd, "echo %d > /sys/class/pwm/pwmchip0/pwm1/period", state);
        system (cmd);
    }

    return BELKIN_SUCCESS;
}

#elif defined(PRODUCT_WeMo_SNSV2) // not PRODUCT_WeMo_LightV2
/* Start: Interfaces with WiFi LED Status Control
   SNS LED behavour:
0: Startup/Connecting     - LED Blue Blink, 700 ms ON, 700 ms OFF
1: Connection established - LED Blue 30s ON, then OFF
2: Poor connection        - LED Amber Solid ON
3: No Connection          - LED Amber Blink, 700 ms ON, 700 ms OFF
4: On & Ok                - No LED
5: Setup mode             - 700 ms LED Blue, 700 ms LED Amber
6: Restore Mode		      - LED Amber Blink, 300 ms ON, 300 ms OFF


   Wemo Smart Module LED behavour (2/11/2013 spec):
0: Startup/Connecting     - LED Green Blink, 700 ms ON, 700 ms OFF
1: Connection established - LED Green ON
2: Poor connection        - LED Amber Solid ON
3: No Connection          - LED Amber Blink, 700 ms ON, 700 ms OFF
4: On & Ok                - LED Green ON
5: Setup mode             - 700 ms LED Green, 700 ms LED Amber

*/
/* Start: Interfaces with WiFi LED Status Control
Wemo snsv2 Module LED behaviour (25/7/2016)
0: Startup/Connecting     - LED White Blink, 700 ms ON, 700 ms OFF
1: Connection established - LED White ON
2: Poor connection        - LED Amber Solid ON
3: No Connection          - LED Amber Blink, 700 ms ON, 700 ms OFF
4: On & Ok                - LED White ON
5: Setup mode             - 700 ms LED White, 700 ms LED Amber
6: Restore Mode           - LED White Blink, 360 ms OFF, 140 ms ON
7. OverTemp Mode          - LED Amber Blink, 200 ms ON,  200 ms OFF
*/
int SetWiFiLED(int state)
{
    /* /sys/class/led/{amber&white} */
    /* trigger : none, default-on, timer */
    switch(state) {
    case 0:
        system ("echo timer > /sys/class/leds/white/trigger;"\
                "echo none > /sys/class/leds/amber/trigger");
        break;
    case 1:
        system ("echo none > /sys/class/leds/amber/trigger; "\
                "echo default-on > /sys/class/leds/white/trigger;");
        break;
    case 2:
        system ("echo none > /sys/class/leds/white/trigger;"\
                "echo default-on > /sys/class/leds/amber/trigger");
        break;
    case 3:
        system ("echo none > /sys/class/leds/white/trigger;"\
                "echo timer > /sys/class/leds/amber/trigger;");
        break;
    case 4:
        system ("echo none > /sys/class/leds/amber/trigger; \n"
                "status=$(cat /sys/class/gpio/gpio19/value)\n"\
                "if [ $status -eq 0 ]; then \n"
                "echo none > /sys/class/leds/white/trigger;"\
                "else \n"\
                "echo default-on > /sys/class/leds/white/trigger; \n"\
                "fi");
        break;
    case 5:
        system ("echo none > /sys/class/leds/white/trigger;"\
                "echo none > /sys/class/leds/amber/trigger;"\
                "echo timer > /sys/class/leds/amber/trigger;"\
                "sleep 0.69 ; \n"                             \
                "echo timer > /sys/class/leds/white/trigger;");
        break;
    case 6:
        system ("echo timer > /sys/class/leds/white/trigger;"\
                "echo 360 > /sys/class/leds/white/delay_on;"\
                "echo 140 > /sys/class/leds/white/delay_off;"\
                "echo none > /sys/class/leds/amber/trigger");
        break;
    case 7:
        system ("echo none > /sys/class/leds/white/trigger;"\
                "echo timer > /sys/class/leds/amber/trigger;"\
                "echo 200 > /sys/class/leds/amber/delay_on;"\
                "echo 200 > /sys/class/leds/amber/delay_off");
        break;
    default:
        break;
    }
    return BELKIN_SUCCESS;
}
//END: Interfaces with WiFi LED Status Control
#endif // PRODUCT_WeMo_LightV2

//Start: Interfaces with Motion Sensor Status Control
/*
/proc/MOTION_SENSOR_STATUS
/proc/MOTION_SENSOR_SET_DELAY
/proc/MOTION_SENSOR_SET_SENSITIVITY
*/
int EnableMotionSensorDetect(int isEnable)
{
    /* Funtion: MOTION_SENSOR_STATUS_write_proc.
       sensor_enable==1 implies START MOTION SENSOR DETECT
     */
    FILE *fp;
    char *fname = "/proc/MOTION_SENSOR_STATUS";

    fp = fopen(fname, "w");
    if( !fp ) {
        return BELKIN_FAILURE;
    }

    fprintf(fp, "%d", isEnable);
    fclose(fp);

    return BELKIN_SUCCESS;
}

int SetMotionSensorDelay(int Delay_Seconds, int Sensitivity_Percent)
{
    /*Will  try to set in "/proc/MOTION_SENSOR_SET_DELAY"
    Function: MOTION_SENSOR_SET_DELAY_proc
     */
    FILE *fp,*fp1;
    char *fname = "/proc/MOTION_SENSOR_SET_DELAY";
    char *fname1 = "/proc/MOTION_SENSOR_SET_SENSITIVITY";

    fp = fopen(fname, "w");
    if(!fp) {
        return BELKIN_FAILURE;
    }

    fprintf(fp, "%d", Delay_Seconds);
    fclose(fp);

    fp1 = fopen(fname1, "w");
    if(!fp1) {
        return BELKIN_FAILURE;
    }

    fprintf(fp1, "%d", Sensitivity_Percent);
    fclose(fp1);

    return BELKIN_SUCCESS;
}
//END: Interfaces with Motion Sensor Status Control

//Start: Interfaces with Motion Enable/Disable Watch Dog
#define DEVICE_FILE_NAME "/dev/watchdog"
int EnableWatchDog(int isEnable, int seconds)
{
    int fd, ret_val;

    fd = open(DEVICE_FILE_NAME, O_WRONLY);

    if( fd < 0 ) {
        perror("watchdog");
        printf("???????????????????? Unable to open watchdog: Device busy ??????????????????\n");
        return BELKIN_FAILURE;
    }

    if( isEnable ) {
        if( ioctl(fd, WDIOC_SETTIMEOUT, &seconds) ) {
            perror("Set Timeout");
            if( fd ) {
                close(fd);
            }
            return(-1);
        }
        printf("The timeout was set to %d seconds\n", seconds);
        if( ioctl(fd, WDIOC_GETTIMEOUT, &ret_val) ) {
            perror("Get Timeout");
            if( fd ) {
                close(fd);
            }
            return BELKIN_FAILURE;
        }
        printf("GETTIMEOUT Returned %d seconds\n", ret_val);
        if( ret_val != seconds ) {
            perror("Get Timeout is not equal to Set Timeout");
            if( (write(fd,"V",1)) == -1 ) {
                perror("Magic Close");
                printf("!!!!!!!!Magic Close Failed\n");
            } else {
                printf("Magic Close Success\n");
            }
            if(close(fd) == -1) {
                printf("!!!!!Watchdog Not Closed\n");
            }
            return BELKIN_FAILURE;
        }
        if( close(fd) == -1 ) {
            printf("!!!!!Watchdog Not Closed\n");
        }
    } else {
        if( (write(fd,"V",1)) == -1 ) {
            perror("Magic Close");
            printf("!!!!!!!!Magic Close Failed\n");
        } else {
            printf("Magic Close Success\n");
        }
        if( close(fd) == -1 ) {
            printf("!!!!!Watchdog Not Closed\n");
        }
    }

    return BELKIN_SUCCESS;
}

int SyncWatchDogTimer(void)
{
    int fd;

    fd = open(DEVICE_FILE_NAME, O_RDWR);

    if( fd < 0 ) {
        printf("IN SyncWatchDogTimer: ???????????? Unable to open watchdog: Device busy ??????????\n");
        perror("watchdog");
        return BELKIN_FAILURE;
    }

    if( ioctl(fd, WDIOC_KEEPALIVE, 0) ) {
        perror("keepalive");
    }

    write(fd, "w", 1);

    close(fd);

    printf("%s Success\n", __FUNCTION__);

    return BELKIN_SUCCESS;
}

int GetRebootStatus(int *Status, unsigned long int *UTC_seconds)
{
    int fd;
    unsigned long argp;

    fd = open(DEVICE_FILE_NAME, O_RDONLY);

    if( fd < 0 ) {
        perror("watchdog");
        return BELKIN_FAILURE;
    }

    if( ioctl(fd, WDIOC_GETBOOTSTATUS, &argp) ) {
        perror("WDIOC_GETBOOTSTATUS");
    }

    close(fd);

    Status = (int *)argp;
    UTC_seconds = (unsigned long int *)argp;

    return BELKIN_SUCCESS;
}

int SetActivityLED(int state)
{
    /* 2 & 3 are for count down timer */
    /* 4 & 5 are for away mode */
    NTP_LOG("%s: activity state: %d\n", __FUNCTION__, state);

#if defined(PRODUCT_WeMo_LightV2)
    int nway = 2;
    char *nway_str = NULL;

    nway_str = GetBelkinParameter("nWay");

    if ((nway_str == NULL) || (strlen(nway_str) == 0)) {
        nway = 2;
    }
    else {
        nway = atoi(nway_str);
    }
#endif

    switch(state) {
    /* Auto off timer activity */
    case 2:
#if defined(PRODUCT_WeMo_SNSV2)
        system ("amber_saved=$(cat /sys/class/leds/amber/trigger | cut -d'[' -f2 | cut -d']' -f1);\n"
                "if [ $amber_saved == \"none\" ]; then  \n"
                "echo none > /sys/class/leds/amber/trigger;"\
                "echo timer > /sys/class/leds/white/trigger;"\
                "echo 1000 > /sys/class/leds/white/delay_on;"\
                "echo 500 > /sys/class/leds/white/delay_off;\n"
                "fi");
#endif
#if defined(PRODUCT_WeMo_LightV2)
        system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
               "if [ $pwm_saved = \"23\" ] || [ $pwm_saved = \"41\" ] || [ $pwm_saved == \"5\" ] || [ $pwm_saved == \"6\" ] || [ $pwm_saved == \"9\" ]; then  \n"
               "echo 9 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
               "fi");
#endif
        break;
    case 3:
#if defined(PRODUCT_WeMo_SNSV2)
        system ("amber_saved=$(cat /sys/class/leds/amber/trigger | cut -d'[' -f2 | cut -d']' -f1);\n"
                "if [ $amber_saved == \"none\" ]; then  \n"\
                "echo default-on > /sys/class/leds/white/trigger;\n"\
                "fi");
#endif
#if defined(PRODUCT_WeMo_LightV2)
        if (nway == 3) {
            system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
                   "binary_saved=$(cat /tmp/notify_change);\n"
                   "if [ $pwm_saved == \"9\" ]; then  \n"
                   "if [ $binary_saved == \"0\" ]; then \n"
                   "echo 6 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "else \n"
                   "echo 5 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "fi \n"
                   "fi");
        }
        else {
            system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
                   "binary_saved=$(cat /sys/class/gpio/gpio19/value);\n"
                   "if [ $pwm_saved == \"9\" ]; then  \n"
                   "if [ $binary_saved == \"0\" ]; then \n"
                   "echo 6 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "else \n"
                   "echo 5 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "fi \n"
                   "fi");
        }
#endif
        break;
#if defined(PRODUCT_WeMo_LightV2)
    /* schedule timer activity */
    case 4:
        system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
               "if [ $pwm_saved == \"41\" ] || [ $pwm_saved == \"5\" ] || [ $pwm_saved == \"6\" ]; then  \n"
               "echo 8 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
               "fi");
        break;
    /* discard rule related LED setting and back to normal operation */
    case 5:
        if (nway == 3) {
            system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
                   "binary_saved=$(cat /tmp/notify_change);\n"
                   "if [ $binary_saved == \"0\" ]; then \n"
                   "echo 6 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "else \n"
                   "echo 5 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "fi");
        }
        else {
            system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
                   "binary_saved=$(cat /sys/class/gpio/gpio19/value);\n"
                   "if [ $binary_saved == \"0\" ]; then \n"
                   "echo 6 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "else \n"
                   "echo 5 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
                   "fi");
        }
        break;
    /* away mode activity */
    case 6:
        system("pwm_saved=$(cat /sys/class/pwm/pwmchip0/pwm1/period);\n"
               "if [ $pwm_saved == \"41\" ] || [ $pwm_saved == \"5\" ] || [ $pwm_saved == \"6\" ]; then  \n"
               "echo 11 > /sys/class/pwm/pwmchip0/pwm1/period; \n"
               "fi");
        break;
#endif
    default:
        return BELKIN_FAILURE;
    }
    return BELKIN_SUCCESS;
}
//END: Interfaces with Motion Enable/Disable Watch Dog

/*
   Test if a carrier is present on the wired Ethernet port (eth2).
   We can't do this in a generic way because the Ralink drivers don't
   implement carrier status.  We use Ralink IOCTL to read the status
   directly from the PHY.

   Since no WeMo products to date use any PHYs other than 0 we will
   power them down unconditionally when this routine is called.  The
   Ralink SOC runs HOT, powering down the unused PHYS saves a significant
   amount of power which results in a cooler and more green product.

   If bPowerDown is true we will also power down PHY0 if carrier is not
   present when this routine is called, otherwise it is lft powered up.

   When the Ethernet jack is hand wired to an external jack the wires
   are too long and the link is very marginal.  It basically doesn't work
   at all at 100BaseT.
   Configuring the Ethernet PHY to force 10BaseT half duplex helps a *LOT*.

   Phy register 0 bits:
   15: 1 - reset
   14: 1 - loopback
   13: 0 - 10 Mbps, 1 - 100Mbps (when bit 12 == 0)
   12: 0 - normal, 1 - auto negotiate
   11: 0 - normal, 1 - power down
   10: reserved
   9: 0 - normal, 1  restart negotiation
   8: 0 - half duplex, 1 - full duplex (when bit 12 == 0)

   0x3100 - normal default = auto negotiation
   0x0000 - 10 Mbps, half duplex

   returns:
      0 - Link Down
      -1 = Link up
      > 0 - error
*/
#ifndef MT7688
int WiredEthernetUp(int bPowerDown)
{
    int Fd;
    struct ifreq ifr;
    ra_mii_ioctl_data mii;
    int loops = 0;
    int i;
    int Ret = 0;

    strncpy(ifr.ifr_name,WIRED_IFACE,sizeof(ifr.ifr_name));
    ifr.ifr_data = (void *)&mii;

    if((Fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        Ret = errno;
    } else {
        // Power down all PHYs except 0 to save power
        mii.reg_num = 0;  // control register
        for(i = 1; i < 5; i++) {
            mii.phy_id = (__u16) i;
            mii.val_in = (__u16) (i == 0 ? 0x3100 : 0x3900);

            if((Ret = ioctl(Fd,RAETH_MII_WRITE,&ifr)) < 0) {
                Ret = errno;
                break;
            }
        }
    }

    while(Ret == 0) {
        mii.phy_id = 0;   // Phy 0
        mii.reg_num = 1;  // status register
        if(ioctl(Fd,RAETH_MII_READ,&ifr) < 0) {
            Ret = errno;
            break;
        }
        // Check if link_status (bit 2) is set in the MII status register
        if(mii.val_out & 0x4) {
            // Link is up
            Ret = -1;    // return -1 on good link
            break;
        } else if(loops++ >= 10) {
            // Give up after one second
            // if there's no link disable the PHY to save power.
            if(bPowerDown) {
                mii.val_in = (__u16) 0x3900;
                mii.reg_num = 0;
                syslog(LOG_ERR,"%s: powering down phy 0",__FUNCTION__);
                if(ioctl(Fd,RAETH_MII_WRITE,&ifr) < 0) {
                    Ret = errno;
                    break;
                }
            }
            // return 0 on link down
            Ret = 0;
            break;
        } else {
            // Sleep for 100 millseconds and then try again
            usleep(100000);   // 100 milliseconds
        }
    }

    if(Fd >= 0) {
        close(Fd);
    }

    gWiredStatus = Ret;
    syslog(LOG_ERR,"%s: link is %s",__FUNCTION__,
           gWiredStatus == -1 ? "up" : "down");
    return Ret;
}

#else // MT7688 Based products

int WiredEthernetUp(int bPowerDown)
{
   int i;
   char Line[80];
   int Err = system("swconfig dev switch0 port 0 get link | grep :up");

   switch(WEXITSTATUS(Err)) {
      case 0:  // Link is up
         gWiredStatus = -1;
         break;

      case 1:  // Link is down
         gWiredStatus = 0;
         if(bPowerDown) {
         // Power down the Ethernet PHYs
            for(i = 0; i < 8; i++) {
               switch(i) {
                  default: // disable PHY ports 0 -> 5
                     snprintf(Line,sizeof(Line),
                              "swconfig dev switch0 port %d set disable 1",i);
                     break;

                  case 6:
                     sprintf(Line,"swconfig dev switch0 set enable_vlan 0");
                     break;

                  case 7:
                     sprintf(Line,"swconfig dev switch0 set apply");
                     break;
               }
               memset(Line, 0, 80);
               if((Err = system(Line)) != 0) {
                  syslog(LOG_ERR,"WiredEthernetUp - '%s' failed: %d",Line,Err);
               }
            }
         }
         break;

      default:
         syslog(LOG_ERR,
                "swconfig failed testing wired Ethernet port status (%d)",Err);
         break;

   }

   syslog(LOG_ERR,"%s: link is %s",__FUNCTION__,
          gWiredStatus == -1 ? "up" : "down");
   return gWiredStatus;
}
#endif
static void Minutes2HrsMinutes(int TotalMinutes,int *pHours,int *pMinutes)
{
    *pHours = TotalMinutes / 60;
    *pMinutes = TotalMinutes % 60;
    if(TotalMinutes < 0) {
        *pMinutes = -*pMinutes;
    }
}

// Note: The compiler generations warnings about a NULL argument for the
// first argument to settimeofday() in the following function, however
// a null argument is correct when setting just the  timezone.
// Disable the warning for this function
#pragma GCC diagnostic ignored "-Wnonnull"

// Write TZ string to /tmp/TZ, add it to the environment and
// set the timezone in the kernel.
static void WriteTzInfo(char *TZ,int MinutesWest)
{
    struct timezone TimeZone;
    FILE *fp = fopen("/tmp/TZ","w");
    if(fp != NULL) {
        if(fprintf(fp,"%s\n",TZ) <= 0) {
            syslog(LOG_ERR,"%s: fprintf failed - %m",__FUNCTION__);
        }
        fclose(fp);
    } else {
        syslog(LOG_ERR,"%s: fopen failed - %m",__FUNCTION__);
    }

// Set TZ in the environment and then call tzset to ensure
// the run time library's idea of the time zone is up to date.
    setenv("TZ",TZ,1);
    tzset();

    memset(&TimeZone,0,sizeof(TimeZone));

// workaround for warp_clock() on first invocation
    settimeofday(NULL,&TimeZone);

    TimeZone.tz_minuteswest = MinutesWest;
    if(strstr(TZ,"DST") != NULL) {
        TimeZone.tz_dsttime = 1;
    }

    if(settimeofday(NULL,&TimeZone) != 0) {
        syslog(LOG_ERR,"%s#%d: settimeofday failed - %m\n",
               __FUNCTION__,__LINE__);
    }

#ifdef NTP_DEBUG
    if(gettimeofday(NULL,&TimeZone) != 0) {
        syslog(LOG_ERR,"%s#%d: gettimeofday failed - %m\n",
               __FUNCTION__,__LINE__);
    } else {
        NTP_LOG("%s: gettimeofday returned tz_minuteswest: %d\n",__FUNCTION__,
                TimeZone.tz_minuteswest);
    }
#endif
}
// Turn warnings about NULL arguments back on
#pragma GCC diagnostic warning "-Wnonnull"


// Offset - number of hours time timezone is CURRENTLY offset from UTC
// bIsDst - TRUE if DST is currently active
// bEnableDST - time zone has daylight savings time
int SetTimeAndTZ(time_t TimeUTC,char *Offset,int bIsDst,int bEnableDST)
{
    int Hours;
    char TZ[16];
    char *cp = strchr(Offset,'.');
    int Ret = FALSE;	// assume the worse

    NTP_LOG("%s: Offset: %s, bIsDst: %d, bEnableDST: %d\n",__FUNCTION__,
            Offset,bIsDst,bEnableDST);

    do {
        if(sscanf(Offset,"%d",&Hours) != 1) {
            syslog(LOG_ERR,"%s: Unable to parse TimeZone '%s'",__FUNCTION__,Offset);
            break;
        }

        if(bIsDst) {
            // The phone sends the *current* offset from UTC, reverse the
            // daylight savings time adjustment if it's in effect

            // NB: this NOT correct everywhere in the world since some
            // countries have daylight savings time offsets that aren't an
            // hour, but we don't have enough information to do it correctly.
            Hours--;
        }

        // Save absolute timezone offset from UTC in flash
        if(cp != NULL) {
            snprintf(TZ,sizeof(TZ),"%d%s",Hours,cp);
        } else {
            snprintf(TZ,sizeof(TZ),"%d.0",Hours);
        }

        NTP_LOG("%s: Setting " SYNCTIME_LASTTIMEZONE " to %s\n",__FUNCTION__,TZ);
        SetBelkinParameter(SYNCTIME_LASTTIMEZONE,TZ);
        // save Daylight savings enabled in flash
        NTP_LOG("%s: Setting " SYNCTIME_DSTSUPPORT " to %d\n",__FUNCTION__,
                bEnableDST);
        SetBelkinParameter(SYNCTIME_DSTSUPPORT,bEnableDST ?  "1" : "0");
        Ret = SetTime(TimeUTC,0,0);
    } while(FALSE);

    return Ret;
}

int static TzOffset2MinutesWest(char *Offset)
{
    int Hours;
    int MinutesEast = 0;
    int i;
    char *cp = strchr(Offset,'.');

    do {
        if(cp != NULL) {
            // Convert the fractional part
            cp++;
            if(sscanf(cp,"%d",&MinutesEast) != 1) {
                syslog(LOG_ERR,"%s#%d: Unable to parse TimeZone '%s'",
                       __FUNCTION__,__LINE__,Offset);
                break;
            }
            MinutesEast *= 60;
            i = strlen(cp);

            while(i-- > 0) {
                MinutesEast /= 10;
            }
        }
        if(sscanf(Offset,"%d",&Hours) != 1) {
            syslog(LOG_ERR,"%s#%d: Unable to parse TimeZone '%s'",
                   __FUNCTION__,__LINE__,Offset);
            break;
        }
        Hours *= 60;
        if(Hours < 0) {
            MinutesEast = Hours - MinutesEast;
        } else {
            MinutesEast += Hours;
        }
    } while(FALSE);

    return -MinutesEast;
}
// Wrapper for system command that closes all handles before calling system()
// to prevent the child process from inheriting open file handles and sockets.
int System(const char *command)
{
    struct rlimit Limits;
    int i;
    pid_t pid;
    int Ret = 0;

    if((pid = fork()) < 0) {
        syslog(LOG_ERR,"%s: fork failed - %m",__FUNCTION__);
    } else if(pid == 0) {
        // child, close all handles
        Limits.rlim_cur = 1024; // just a default in case getrlimit fails
        getrlimit(RLIMIT_NOFILE,&Limits);

        for(i = 0; i < Limits.rlim_cur; i++) {
            // No need to log errors, the handles might not actually be open
            // since we're just blindly closing all possible handles
            close(i);
        }
        // run the command and pass on the exit status our parent
        exit(system(command));
    } else {
        // Parent
        if(waitpid(pid,&Ret,0) == -1) {
            syslog(LOG_ERR,"%s: waitpid failed - %m",__FUNCTION__);
            Ret = errno;
        }
    }

    return Ret;
}

/*
   Return the name of the interface that's connected to the client's LAN
   Please use this function instead of hardcoding apcli0 to avoid breaking
   wired Ethernet support.
*/
char *GetLanDeviceName()
{
    return g_bWiredEthernet ? WIRED_IFACE : WAN_IFACE;
}

