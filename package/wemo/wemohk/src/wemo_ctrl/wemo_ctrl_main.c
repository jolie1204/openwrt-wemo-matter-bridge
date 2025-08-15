/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * - Neither name of Intel Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "ctrlpt_util.h"
#include "wemo_ctrl.h"
#include "wemo_event_ctrl.h"
#include "wemo_device_db.h"
#include "wemo_ipc_server.h"
#include <libnvram.h>
#include "logger.h"

enum cmdloop_cmds {
    PRTHELP = 0,
    PRTFULLHELP,
    POWERON,
    POWEROFF,
    GETPOWER,
    SETLEVEL,
    GETLEVEL,
    PRTDEV,
    LSTDEV,
    REFRESH,
    EXITCMD
};

/*
   Data structure for parsing commands from the command line
 */
struct cmdloop_commands {
    char *str;                  // the string
    int cmdnum;                 // the command
    int numargs;                // the number of arguments
    char *args;                 // the args
} cmdloop_commands;

/*
   Mappings between command text names, command tag,
   and required command arguments for command line
   commands
 */
static struct cmdloop_commands cmdloop_cmdlist[] = {
    {"help", PRTHELP, 1, ""},
    {"helpfull", PRTFULLHELP, 1, ""},
    {"listdev", LSTDEV, 1, ""},
    {"refresh", REFRESH, 1, ""},
    {"printdev", PRTDEV, 2, "<devnum>"},
    {"poweron", POWERON, 2, "<devnum>"},
    {"poweroff", POWEROFF, 2, "<devnum>"},
    {"getpower", GETPOWER, 2, "<devnum>"},
    {"setlevel", SETLEVEL, 3, "<devnum>"},
    {"getlevel", GETLEVEL, 2, "<devnum>"},
    {"exit", EXITCMD, 1, ""}
};

#define CONFIG_FILE_PATH "/etc/wemo_ctrl.conf"
#define DEFAULT_WEMO_DEVICE_DB "/tmp/wemo_device.db"
#define DEFAULT_WEMO_STATE_DB "/tmp/wemo_state.db"
#define DEFAULT_IFNAME "lo"

char wemo_device_db[128];
char wemo_state_db[128];
char upnp_ifname[16];
int discover = 0;

sqlite3 *ctrlpt_dev_db = NULL;
sqlite3 *ctrlpt_state_db = NULL;

static int run_cmdloop = 1;

void
linux_print( const char *string )
{
    char buf[128];
    time_t curtime;
    struct tm *loc_time;
    curtime = time(NULL);
    loc_time = localtime(&curtime);
    strftime(buf, 128, "%D %H:%M:%S : ", loc_time);
    fprintf(stdout, "\x1B[34m");
    fprintf(stdout, "%s", buf);
    puts(string);
    fprintf(stdout, "\x1B[0m");
    fflush(stdout);
}

/********************************************************************************
 * wemoCtrlPointPrintHelp
 *
 * Description:
 *       Print help info for this application.
 ********************************************************************************/
void
wemoCtrlPointPrintShortHelp( void )
{
    APP_LOG("WEMOHK", LOG_DEBUG,  "Commands:" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  Help" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  HelpFull" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  ListDev" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  Refresh" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  PrintDev      <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  PowerOn       <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  PowerOff      <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  GetPower      <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  SetLevel      <devnum> <level>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  GetLevel      <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  Exit" );
}

void
wemoCtrlPointPrintLongHelp( void )
{
    APP_LOG("WEMOHK", LOG_DEBUG,  "" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "******************************" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "* WEMO Control Point Help Info *" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "******************************" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "Commands:" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  Help" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Print this help info." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  ListDev" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Print the current list of TV Device Emulators that this" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         control point is aware of.  Each device is preceded by a" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         device number which corresponds to the devnum argument of" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         commands listed below." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  Refresh" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Delete all of the devices from the device list and issue new" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         search request to rebuild the list from scratch." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  PrintDev       <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Print the state table for the device <devnum>." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         e.g., 'PrintDev 1' prints the state table for the first" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         device in the device list." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  PowerOn        <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Sends the PowerOn action to the Control Service of" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         device <devnum>." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  PowerOff       <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Sends the PowerOff action to the Control Service of" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         device <devnum>." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  GetPower       <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       get current power state of" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         device <devnum>." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  SetLevel       <devnum> <level>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Sends power level of" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         device <devnum> <level>." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  GetLevel       <devnum>" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       get current power level of" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "         device <devnum>." );
    APP_LOG("WEMOHK", LOG_DEBUG,  "  Exit" );
    APP_LOG("WEMOHK", LOG_DEBUG,  "       Exits the control point application." );
}

/********************************************************************************
 * wemoCtrlPointPrintCommands
 *
 * Description:
 *       Print the list of valid command line commands to the user
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
void
wemoCtrlPointPrintCommands()
{
    int i;
    int numofcmds = sizeof( cmdloop_cmdlist ) / sizeof( cmdloop_commands );

    APP_LOG("WEMOHK", LOG_DEBUG,  "Valid Commands:" );
    for( i = 0; i < numofcmds; i++ ) {
        APP_LOG("WEMOHK", LOG_DEBUG,  "  %-14s %s", cmdloop_cmdlist[i].str,
                          cmdloop_cmdlist[i].args );
    }
    APP_LOG("WEMOHK", LOG_DEBUG,  "" );
}

/********************************************************************************
 * wemoCtrlPointCommandLoop
 *
 * Description:
 *       Function that receives commands from the user at the command prompt
 *       during the lifetime of the control point, and calls the appropriate
 *       functions for those commands.
 *
 * Parameters:
 *    None
 *
 ********************************************************************************/
void *
wemoCtrlPointCommandLoop( void *args )
{
    char cmdline[100];

    while(run_cmdloop) {
        APP_LOG("WEMOHK", LOG_DEBUG,  "\n>> " );
        fgets( cmdline, 100, stdin );
        wemoCtrlPointProcessCommand( cmdline );
    }

    return NULL;
}

int
wemoCtrlPointProcessCommand( char *cmdline )
{
    char cmd[100];
    int arg_val_err = -99999;
    int arg1 = arg_val_err;
    int arg2 = arg_val_err;
    int cmdnum = -1;
    int numofcmds = sizeof( cmdloop_cmdlist ) / sizeof( cmdloop_commands );
    int cmdfound = 0;
    int i;
    int invalidargs = 0;
    int validargs;

    validargs = sscanf( cmdline, "%s %d %d", cmd, &arg1, &arg2 );

    for( i = 0; i < numofcmds; i++ ) {
        if( strcasecmp( cmd, cmdloop_cmdlist[i].str ) == 0 ) {
            cmdnum = cmdloop_cmdlist[i].cmdnum;
            cmdfound++;
            if( validargs != cmdloop_cmdlist[i].numargs )
                invalidargs++;
            break;
        }
    }

    if( !cmdfound ) {
        APP_LOG("WEMOHK", LOG_DEBUG,  "Command not found; try 'Help'" );
        return CTRLPT_SUCCESS;
    }

    if( invalidargs ) {
        APP_LOG("WEMOHK", LOG_DEBUG,  "Invalid arguments; try 'Help'" );
        return CTRLPT_SUCCESS;
    }

    switch ( cmdnum ) {
        case PRTHELP:
            wemoCtrlPointPrintShortHelp();
            break;

        case PRTFULLHELP:
            wemoCtrlPointPrintLongHelp();
            break;

        case POWERON:
            wemoCtrlPointSendPowerOn(arg1, 1);
            break;

        case POWEROFF:
            wemoCtrlPointSendPowerOff(arg1, 1);
            break;

        case GETPOWER:
            wemoCtrlPointGetPower(arg1);
            break;

        case SETLEVEL:
            wemoCtrlPointSetLevel(arg1, arg2, 1);
            break;

        case GETLEVEL:
            wemoCtrlPointGetLevel(arg1);
            break;

        case PRTDEV:
            wemoCtrlPointPrintDevice( arg1 );
            break;

        case LSTDEV:
            wemoCtrlPointPrintList();
            break;

        case REFRESH:
            wemoCtrlPointRefresh();
            break;

        case EXITCMD:
            run_cmdloop = 0;
            break;

        default:
            APP_LOG("WEMOHK", LOG_DEBUG,  "Command not implemented; see 'Help'" );
            break;
    }

    if( invalidargs )
        APP_LOG("WEMOHK", LOG_DEBUG,  "Invalid args in command; see 'Help'" );

    return CTRLPT_SUCCESS;
}

static void strip_string(char *str)
{
    int str_size = strlen(str);
    if (str_size > 0) {
        if (str[str_size - 1] == '\n') {
            str[str_size - 1] = 0;
            if (str[str_size - 2] == '\r') {
                str[str_size - 2] = 0;
            }
        }
    }
}

int process_config()
{
    FILE *conf = fopen (CONFIG_FILE_PATH, "r");

    if (conf != NULL) {
        char line[256];

        while(fgets(line, sizeof(line), conf) != NULL) {
            char *token;
            token = strtok(line, "= ");
            if (!strcasecmp("wemo_device_db", token)) {
                token = strtok(NULL, "= ");
                if (token != NULL) {
                    strcpy(wemo_device_db, token);
                    strip_string(wemo_device_db);
                    APP_LOG("WEMOHK", LOG_DEBUG, "wemo_device_db = %s\n", wemo_device_db);
                }
                else {
                    APP_LOG("WEMOHK", LOG_DEBUG, "error parsing wemo_device_db!\n");
                }
            }
            else if (!strcasecmp("wemo_state_db", token)) {
                token = strtok(NULL, "= ");
                if (token != NULL) {
                    strcpy(wemo_state_db, token);
                    strip_string(wemo_state_db);
                    APP_LOG("WEMOHK", LOG_DEBUG, "wemo_state_db = %s\n", wemo_state_db);
                }
                else {
                    APP_LOG("WEMOHK", LOG_DEBUG, "error parsing wemo_state_db!\n");
                }
            }
            else if (!strcasecmp("ifname", token)) {
                token = strtok(NULL, "= ");
                if (token != NULL) {
                    strcpy(upnp_ifname, token);
                    strip_string(upnp_ifname);
                    APP_LOG("WEMOHK", LOG_DEBUG, "upnp interface name= %s\n", upnp_ifname);
                }
                else {
                    APP_LOG("WEMOHK", LOG_DEBUG, "error parsing ifname!\n");
                }
            }
            else {
                APP_LOG("WEMOHK", LOG_DEBUG, "unknown item %s\n", token);
            }
        }
        fclose(conf);
    }
    else {
        APP_LOG("WEMOHK", LOG_DEBUG, "No configuration file (/etc/wemo_ctrl.conf)...\n");
        APP_LOG("WEMOHK", LOG_DEBUG, "using default DB path /tmp/wemo_device.db and /tmp/wemo_state.db\n");
        return CTRLPT_ERROR;
    }

    return CTRLPT_SUCCESS;
 }

int main( int argc, char **argv )
{
    int rc;
    int sig;
    sigset_t sigs_to_catch;

    printf("Copyright (c) 2000-2003 Intel Corporation\n");
    printf("All rights reserved.\n");

    NvramInit(0, NULL);
    initLogger();

    /* use default values if any one of config item is not found */
    strcpy(wemo_device_db, DEFAULT_WEMO_DEVICE_DB);
    strcpy(wemo_state_db, DEFAULT_WEMO_STATE_DB);
    strcpy(upnp_ifname, DEFAULT_IFNAME);

    /*
       Catch Ctrl-C and properly shutdown
     */
    sigemptyset(&sigs_to_catch);
    //sigaddset(&sigs_to_catch, SIGINT);
    //    sigaddset(&sigs_to_catch, SIGTERM);
    sigaddset (&sigs_to_catch, SIGQUIT);
    sigaddset (&sigs_to_catch, SIGINT);
    sigaddset (&sigs_to_catch, SIGTERM);
    sigaddset (&sigs_to_catch, SIGUSR1);
    pthread_sigmask (SIG_BLOCK, &sigs_to_catch, NULL);

    if (process_config() != CTRLPT_SUCCESS) {
        APP_LOG("WEMOHK", LOG_DEBUG, "/etc/wemo_ctrl.conf not found using default DB paths\n");
    }
    rc = wemo_dev_db_init(&ctrlpt_dev_db, &ctrlpt_state_db);

    if(rc != CTRLPT_SUCCESS) {
    	APP_LOG("WEMOHK", LOG_DEBUG, "Error initializing sqlite DB");
    	return rc;
    }
    wemo_ipc_server_init();

    rc = wemoCtrlPointStart(upnp_ifname, linux_print, NULL );
    if( rc != CTRLPT_SUCCESS ) {
        APP_LOG("WEMOHK", LOG_DEBUG,  "Error starting UPnP WEMO Control Point" );
        return rc;
    }

    /* start a command loop thread */
    //    int code;
    //    ithread_t cmdloop_thread;
    //    code = ithread_create( &cmdloop_thread, NULL, wemoCtrlPointCommandLoop, NULL );
    //    ithread_join(cmdloop_thread, NULL);
    //    wemoCtrlPointCommandLoop(NULL);

    while (1) {
        sigwait(&sigs_to_catch, &sig );
        if (sig == SIGUSR1) {
            APP_LOG("WEMOHK", LOG_DEBUG, "Received SIGUSR1, calling discover...");
            discover = 1;
        } else {
            break;
        }
    }

    APP_LOG("WEMOHK", LOG_DEBUG,  "Shutting down on signal %d...\n", sig );

    rc = wemoCtrlPointStop();
    wemo_dev_db_finish(ctrlpt_dev_db, ctrlpt_state_db);
    wemo_ipc_server_finish();
    return rc;
}
