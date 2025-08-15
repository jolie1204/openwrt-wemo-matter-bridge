/***************************************************************************
*
*
* Plugin.c
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
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include "thready_utils.h"
#include "itc.h"
#include "utils.h"
#include "libnvram.h"
#include "logger.h"
#include "libhkstore.h"
#include "belkin_api.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

int main(int argc, char **argv )
{
    tu_set_my_thread_name( __FUNCTION__ );

    /*
     * libNvramInit initialize the semaphore in OpenWRT NVRAM.
     * This should be called before any APP_LOG call, otherwise OpenWRT PVT build will break
     */
    libNvramInit();
    /* Initialize homekitstore library */
    libhomekitstoreInit();
    /*start logger*/
    initLogger();

    /* serial no change was made to treat LS as Dimmer */
    char srlNo[15];
    memset(srlNo, 0, sizeof(srlNo));
    strncpy(srlNo, GetSerialNumber(), sizeof(srlNo));
    srlNo[0] = '2';
    srlNo[1] = '4';
    srlNo[7] = '1';
    srlNo[8] = '5';
    SetSerialNumber(srlNo);

    core_init_early();

    /*
    ** core initialization - part 2: Init WiFi, start UPnP and start required threads
    ** argument (1/0) tells whether force start of Control point is desired or not respectively
    */
    core_init_late(1);

    while(1) {
        pNode node = readMessage(PLUGIN_E_MAIN_THREAD);
        ProcessItcEvent(node);
    }

    /*close syslog logging*/
    deInitLogger();

    return EXIT_SUCCESS;
}
