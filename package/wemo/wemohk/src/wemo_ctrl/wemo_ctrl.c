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
#define _GNU_SOURCE
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/ip_icmp.h>

#include "wemo_ctrl.h"
#include "wemo_event_ctrl.h"
#include "wemo_device_db.h"
#include "wemo_ipc_server.h"
#include "wemo_dev_info.h"
#include "logger.h"

/* dummy values to avoid compile errors */
int gWebIconVersion = 0;
volatile int gRestartRuleEngine = 0;

extern sqlite3 *ctrlpt_dev_db;
extern sqlite3 *ctrlpt_state_db;

/*!
   Mutex for protecting the global device list
   in a multi-threaded, asynchronous environment.
   All functions should lock this mutex before reading
   or writing the device list.
 */
ithread_mutex_t DeviceListMutex;

UpnpClient_Handle ctrlpt_handle = -1;

char wemoDeviceType[] = "urn:Belkin:device:controllee:1";
char targetServiceType[] = "urn:Belkin:service:basicevent:1";
char *wemoServiceType[] = {
    "urn:Belkin:service:WiFiSetup:1",
    "urn:Belkin:service:timesync:1",
    "urn:Belkin:service:basicevent:1",
    "urn:Belkin:service:firmwareupdate:1",
    "urn:Belkin:service:rules:1",
    "urn:Belkin:service:metainfo:1",
    "urn:Belkin:service:deviceinfo:1",
    "urn:Belkin:service:smartsetup:1",
    "urn:Belkin:service:manufacture:1",
    "urn:Belkin:service:insight:1",
};
char *wemoServiceName[] = {
    "WiFiSetup",
    "timesync",
    "basicevent",
    "firmwareupdate",
    "rules",
    "metainfo",
    "deviceinfo",
    "smartsetup",
    "manufacture",
    "insight",
};

/*!
   Global arrays for storing variable names and counts for
   wemoControl
 */

char *wemoVarName[WEMO_SERVICE_COUNT][CTRLPT_MAXVARS] = {
    {"NetworkStatus", ""},
    {"", ""},
    {"BinaryState", "Brightness", "FriendlyName", "OverTemp",
     "CountdownEndTime", "RuleOverrideStatus",
     "Fader", "hushMode", "nightMode", "startTime", "endTime", "nightModeBrightness",
     "EnergyPerUnitCost"},
    {"FirmwareUpdateStatus", "CurrTimeStamp", "FWDownloadTimeStamp"},
    {"RulesDBVersion", "longPressRuleDeviceCnt", "longPressRuleAction", "longPressRuleState", "longPressRuleDeviceUdn", "RuleMessage"},
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
    {"InsightParams", "PowerThreshold"},
};
char wemoVarCount[WEMO_SERVICE_COUNT] =
    { 1, 0, 13, 3, 6, 0, 0, 0, 0, 2 };

/*!
   Timeout to request during subscriptions
 */
int default_timeout = 1801;

/*!
   The first node in the global device list, or NULL if empty
 */
struct wemoDeviceNode *GlobalDeviceList = NULL;

static char serial_number[32] = {0};
static char DeviceUDN[64] = {0};

static void wemoCtrlPointPopulateServices(IXML_Document *DescDoc, const char *location, struct wemoDeviceNode *deviceNode)
{
    int service;
    int var;
    char *serviceId = NULL;
    char *eventURL = NULL;
    char *controlURL = NULL;

    for(service = 0; service < WEMO_SERVICE_COUNT; service++) {
        if(!ctrlpt_util_FindAndParseService(DescDoc, location, wemoServiceType[service],
                                            &serviceId, &eventURL, &controlURL)) {
            APP_LOG("WEMOHK", LOG_ERR, "Error: Could not find Service: %s",
                               wemoServiceType[service] );
        }
        else {
            if (serviceId != NULL) {
                strcpy(deviceNode->device.wemoService[service].ServiceId,
                       serviceId);
            }
            if (wemoServiceType[service] != NULL) {
                strcpy(deviceNode->device.wemoService[service].ServiceType,
                       wemoServiceType[service] );
            }
            if (controlURL != NULL) {
                strcpy(deviceNode->device.wemoService[service].ControlURL,
                       controlURL);
            }
            if (eventURL != NULL) {
                strcpy(deviceNode->device.wemoService[service].EventURL,
                       eventURL);
            }

            for( var = 0; var < wemoVarCount[service]; var++ ) {
                deviceNode->device.wemoService[service].VariableStrVal[var] =
                    ( char * )malloc( WEMO_MAX_VAL_LEN );
                strcpy(deviceNode->device.wemoService[service].
                       VariableStrVal[var], "" );
            }

            if(serviceId) {
                free(serviceId);
                serviceId = NULL;
            }
            if(controlURL) {
                free(controlURL);
                controlURL = NULL;
            }
            if(eventURL) {
                free(eventURL);
                eventURL = NULL;
            }
        }
    }
}

/********************************************************************************
 * wemoCtrlPointAddDevice
 *
 * Description:
 *       If the device is not already included in the global device list,
 *       add it.  Otherwise, update its advertisement expiration timeout.
 *
 * Parameters:
 *   DescDoc -- The description document for the device
 *   location -- The location of the description document URL
 *   expires -- The expiration time for this advertisement
 *
 ********************************************************************************/

void wemoCtrlPointAddDevice( IXML_Document *DescDoc,
                      const char *location,
                      int expires )
{
    char *deviceType = NULL;
    char *friendlyName = NULL;
    char presURL[200];
    char *baseURL = NULL;
    char *relURL = NULL;
    char *UDN = NULL;
    char *manufacturer = NULL;
    char *modelName = NULL;
    char *serialNumber = NULL;
    char *binaryState = NULL;
    char *brightness = NULL;
    char *firmwareversion = NULL;
    char ipaddr[128];
    int TimeOut = default_timeout;
    struct wemoDeviceNode *deviceNode;
    struct wemoDeviceNode *tmpdevnode;
    int ret = 1;
    int found = 0;
    int service;

    /* Read key elements from description document */
    if ((UDN = ctrlpt_util_GetFirstDocumentItem( DescDoc, "UDN" )) == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "UDN retrieval failed for %s", location);
        goto add_failed;
    }
    if (strcasestr(UDN, serial_number) == NULL) {
        // APP_LOG("WEMOHK", LOG_DEBUG, "UDN : %s not for serial #: %s", UDN, serial_number);
        goto add_failed;
    }
    if ((deviceType = ctrlpt_util_GetFirstDocumentItem( DescDoc, "deviceType" )) == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "deviceType retrieval failed for %s", location);
        goto add_failed;
    }
    if ((friendlyName = ctrlpt_util_GetFirstDocumentItem( DescDoc, "friendlyName" )) == NULL) {
        APP_LOG("WEMOHK", LOG_ERR,"friendlyName retrieval failed for %s", location);
        goto add_failed;
    }
    baseURL = ctrlpt_util_GetFirstDocumentItem( DescDoc, "URLBase" );

    if ((relURL = ctrlpt_util_GetFirstDocumentItem( DescDoc, "presentationURL" )) == NULL) {
        // APP_LOG("WEMOHK", LOG_DEBUG, "presentationURL retrieval failed for %s", location);
        goto add_failed;
    }

    if ((manufacturer = ctrlpt_util_GetFirstDocumentItem( DescDoc, "manufacturer")) == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "manufacturer retrieval failed for %s", location);
        goto add_failed;
    }
    if ((modelName = ctrlpt_util_GetFirstDocumentItem( DescDoc, "modelName")) == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "modelName retrieval failed for %s", location);
        goto add_failed;
    }
    if ((serialNumber = ctrlpt_util_GetFirstDocumentItem( DescDoc, "serialNumber")) == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "serialNumber retrieval failed for %s", location);
        goto add_failed;
    }
    binaryState = ctrlpt_util_GetFirstDocumentItem( DescDoc, "binaryState");
    brightness = ctrlpt_util_GetFirstDocumentItem( DescDoc, "brightness");
    if ((firmwareversion = ctrlpt_util_GetFirstDocumentItem( DescDoc, "firmwareVersion")) == NULL) {
        //APP_LOG("WEMOHK", LOG_ERR,"firmwareVersion retrieval failed for %s", location);
        goto add_failed;
    }

    ret = UpnpResolveURL(( baseURL ? baseURL : location ), relURL, presURL);

    if( UPNP_E_SUCCESS != ret )
        APP_LOG("WEMOHK", LOG_ERR, "Error generating presURL from %s + %s",
                           baseURL,
                           relURL );
    ithread_mutex_lock( &DeviceListMutex );

    if( strstr( deviceType, "urn:Belkin:device:" )) {
        // Check if this device is already in the list
        tmpdevnode = GlobalDeviceList;
        while( tmpdevnode ) {
            if( strcmp( tmpdevnode->device.UDN, UDN ) == 0 ) {
                found = 1;
                break;
            }
            tmpdevnode = tmpdevnode->next;
        }

        if( found ) {
            // The device is already there, so just update
            // the advertisement timeout field
            tmpdevnode->device.AdvrTimeOut = expires;
            printf("tmpdevnode->device.DescDocURL = %s, location = %s\n", tmpdevnode->device.DescDocURL, location);
            if (strcmp(tmpdevnode->device.DescDocURL, location)) {
                strcpy(tmpdevnode->device.DescDocURL, location );
                if (ctrlpt_util_retrieve_ip_from_url(location, ipaddr)) {
                    strcpy(tmpdevnode->device.ipaddr, ipaddr);
                }
                /* Unsubscribe if there's any existing subscriptions */
                for( service = 0; service < WEMO_SERVICE_COUNT; service++ ) {
                    if (strlen(tmpdevnode->device.wemoService[service].SID) > 0) {
                        ret = UpnpUnSubscribe( ctrlpt_handle, tmpdevnode->device.wemoService[service].SID );
                        if( UPNP_E_SUCCESS == ret ) {
                            APP_LOG("WEMOHK", LOG_INFO, "Unsubscribed from WEMO %s EventURL with SID=%s",
                                  tmpdevnode->device.wemoService[service].EventURL,
                                  tmpdevnode->device.wemoService[service].SID );
                            memset(tmpdevnode->device.wemoService[service].SID, 0, NAME_SIZE);
                        } else {
                            APP_LOG("WEMOHK", LOG_INFO, "Ignoring error unsubscribing to WEMO %s EventURL with SID = %s -- %d",
                                  tmpdevnode->device.wemoService[service].EventURL,
                                  tmpdevnode->device.wemoService[service].SID, ret );
                            memset(tmpdevnode->device.wemoService[service].SID, 0, NAME_SIZE);
                        }
                    }
                }
                /* update device node */
                wemoCtrlPointPopulateServices(DescDoc, location, tmpdevnode);

                /* subscribe to event with updated location */
                for( service = 0; service < WEMO_SERVICE_COUNT; service++ ) {
                    if ((service == WEMO_SERVICE_WIFISETUP) ||
                        (service == WEMO_SERVICE_BASICEVENT) ||
                        (service == WEMO_SERVICE_FIRMWAREUPDATE) ||
                        (service == WEMO_SERVICE_RULES) ||
                        (service == WEMO_SERVICE_INSIGHT)) {
                        APP_LOG("WEMOHK", LOG_INFO,"Subscribing to EventURL %s...", tmpdevnode->device.wemoService[service].EventURL);
                        ret = UpnpSubscribe(ctrlpt_handle,
                                            tmpdevnode->device.wemoService[service].EventURL,
                                            &expires,
                                            tmpdevnode->device.wemoService[service].SID);

                        if( ret == UPNP_E_SUCCESS ) {
                            APP_LOG("WEMOHK", LOG_INFO, "Subscribed to EventURL with SID=%s",
                                               tmpdevnode->device.wemoService[service].SID );
                        } else {
                            APP_LOG("WEMOHK", LOG_INFO,"Error Subscribing to EventURL -- %d", ret );
                        }
                    }
                }
            }
        } else {
            APP_LOG("WEMOHK", LOG_INFO,"Found WEMO device: %s", location);

            /*
              Create a new device node
            */
            deviceNode = (struct wemoDeviceNode *) malloc(sizeof(struct wemoDeviceNode ));
            memset(deviceNode, 0, sizeof(struct wemoDeviceNode));

            strcpy(deviceNode->device.UDN, UDN );
            strcpy(deviceNode->device.DescDocURL, location );
            if (ctrlpt_util_retrieve_ip_from_url(location, ipaddr)) {
                strcpy(deviceNode->device.ipaddr, ipaddr);
            }
            strcpy(deviceNode->device.FriendlyName, friendlyName );
            strcpy(deviceNode->device.PresURL, presURL );
            deviceNode->device.AdvrTimeOut = expires;

            strcpy(deviceNode->device.manufacturer, manufacturer );
            strcpy(deviceNode->device.modelName, modelName );
            strcpy(deviceNode->device.serialNumber, serialNumber );
            strcpy(deviceNode->device.firmwareVersion, firmwareversion);

            wemoCtrlPointPopulateServices(DescDoc, location, deviceNode);

            deviceNode->next = NULL;

            // Insert the new device node in the list
            if( ( tmpdevnode = GlobalDeviceList ) ) {

                while(tmpdevnode ) {
                    if( tmpdevnode->next ) {
                        tmpdevnode = tmpdevnode->next;
                    } else {
                        tmpdevnode->next = deviceNode;
                        break;
                    }
                }
            } else {
                GlobalDeviceList = deviceNode;
            }

            //Notify New Device Added
            ctrlpt_util_StateUpdate(NULL, NULL, deviceNode->device.UDN,
                                     DEVICE_ADDED );
            /* subscribe event */
            for( service = 0; service < WEMO_SERVICE_COUNT; service++ ) {
                if ((service == WEMO_SERVICE_WIFISETUP) ||
                    (service == WEMO_SERVICE_BASICEVENT) ||
                    (service == WEMO_SERVICE_FIRMWAREUPDATE) ||
                    (service == WEMO_SERVICE_RULES) ||
                    (service == WEMO_SERVICE_INSIGHT)) {
                    //APP_LOG("WEMOHK", LOG_DEBUG,"Subscribing to EventURL %s...", eventURL[service] );

                    ret = UpnpSubscribe(ctrlpt_handle,
                                        deviceNode->device.wemoService[service].EventURL,
                                        &TimeOut,
                                        deviceNode->device.wemoService[service].SID);

                    if( ret == UPNP_E_SUCCESS ) {
                        APP_LOG("WEMOHK", LOG_INFO, "Subscribed to EventURL with SID=%s", 
                                           deviceNode->device.wemoService[service].SID);
                    } else {
                        APP_LOG("WEMOHK", LOG_INFO,"Error Subscribing to EventURL -- %d", ret );
                    }

                }
            }
            wemo_dev_db_insert(ctrlpt_dev_db, &(deviceNode->device));

            int id = 0;

            id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, deviceNode->device.UDN);

            if (id) {
                char buffer[256];
                memset(buffer, 0, 256);
                if (brightness) {
                    sprintf(buffer, "\'%d=%s:%d=%s\'",
                            CAP_BINARY, binaryState,
                            CAP_LEVEL, brightness);
                }
                else {
                    sprintf(buffer, "\'%d=%s\'",
                            CAP_BINARY, binaryState);
                }

                wemo_dev_statedb_insert(ctrlpt_state_db, id, 1, buffer);

                struct we_state state_buffer;
                state_buffer.is_online = 1;
                state_buffer.state = atoi(binaryState);
                if (brightness) {
                    state_buffer.level = atoi(brightness);
                }
                else {
                    state_buffer.level = -1;
                }
                APP_LOG("WEMOHK", LOG_INFO,"sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                        id,
                        state_buffer.is_online,
                        state_buffer.state,
                        state_buffer.level);
                wemo_ipc_send_event(id, &state_buffer);
            }
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

 add_failed:
    if( deviceType )
        free( deviceType );
    if( friendlyName )
        free( friendlyName );
    if( UDN )
        free( UDN );
    if( baseURL )
        free( baseURL );
    if( relURL )
        free( relURL );
    if(manufacturer)
    	free(manufacturer);
    if(modelName)
    	free(modelName);
    if (serialNumber)
    	free(serialNumber);
    if (binaryState)
    	free(binaryState);
    if (brightness)
        free(brightness);
    if (firmwareversion)
        free(firmwareversion);

}

/********************************************************************************
 * wemoCtrlPointDeleteNode
 *
 * Description:
 *       Delete a device node from the global device list.  Note that this
 *       function is NOT thread safe, and should be called from another
 *       function that has already locked the global device list.
 *
 * Parameters:
 *   node -- The device node
 *
 ********************************************************************************/

int
wemoCtrlPointDeleteNode( struct wemoDeviceNode *node )
{
    int rc,
        service,
        var;

    if( NULL == node ) {
        APP_LOG("WEMOHK", LOG_ERR, "ERROR: wemoCtrlPointDeleteNode: Node is empty" );
        return CTRLPT_ERROR;
    }

    for( service = 0; service < WEMO_SERVICE_COUNT; service++ ) {
        /*
          If we have a valid control SID, then unsubscribe
        */
        if(strcmp( node->device.wemoService[service].SID, "" ) != 0) {
            rc = UpnpUnSubscribe( ctrlpt_handle,
                                  node->device.wemoService[service].SID );
            if( UPNP_E_SUCCESS == rc ) {
                APP_LOG("WEMOHK", LOG_INFO, "Unsubscribed from WEMO %s EventURL with SID=%s",
                      wemoServiceName[service],
                      node->device.wemoService[service].SID );
            } else {
                APP_LOG("WEMOHK", LOG_INFO, "Error unsubscribing to WEMO %s EventURL with SID = %s -- %d",
                      wemoServiceName[service],
                      node->device.wemoService[service].SID, rc );
            }
        }

        for( var = 0; var < wemoVarCount[service]; var++ ) {
            if( node->device.wemoService[service].VariableStrVal[var] ) {
                free( node->device.wemoService[service].
                      VariableStrVal[var] );
            }
        }
    }

    //Notify Device removed
    ctrlpt_util_StateUpdate( NULL, NULL, node->device.UDN, DEVICE_REMOVED );

    int wemo_id = 0;
    if ((wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, node->device.UDN))) {

        wemo_dev_statedb_update_online(ctrlpt_state_db, wemo_id, 0);

        struct we_state state_buffer;
        state_buffer.is_online = 0;
        state_buffer.state = -1;
        state_buffer.level = -1;
        APP_LOG("WEMOHK", LOG_INFO,"sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                wemo_id,
                state_buffer.is_online,
                state_buffer.state,
                state_buffer.level);
        wemo_ipc_send_event(wemo_id, &state_buffer);
    }
    free( node );
    node = NULL;

    return CTRLPT_SUCCESS;
}

/********************************************************************************
 * wemoCtrlPointRemoveDevice
 *
 * Description:
 *       Remove a device from the global device list.
 *
 * Parameters:
 *   UDN -- The Unique Device Name for the device to remove
 *
 ********************************************************************************/
int
wemoCtrlPointRemoveDevice(const char *UDN)
{
    struct wemoDeviceNode *curdevnode;
    struct wemoDeviceNode *prevdevnode;

    ithread_mutex_lock( &DeviceListMutex );

    curdevnode = GlobalDeviceList;
    if( !curdevnode ) {
        APP_LOG("WEMOHK", LOG_DEBUG, "WARNING: wemoCtrlPointRemoveDevice: Device list empty" );
    } else {
        if( 0 == strcmp( curdevnode->device.UDN, UDN ) ) {
            GlobalDeviceList = curdevnode->next;
            wemoCtrlPointDeleteNode( curdevnode );
        } else {
            prevdevnode = curdevnode;
            curdevnode = curdevnode->next;

            while( curdevnode ) {
                if( strcmp( curdevnode->device.UDN, UDN ) == 0 ) {
                    prevdevnode->next = curdevnode->next;
                    wemoCtrlPointDeleteNode( curdevnode );
                    break;
                }

                prevdevnode = curdevnode;
                curdevnode = curdevnode->next;
            }
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    return CTRLPT_SUCCESS;
}

int
wemoCtrlPointRemoveDevicebyLocation(const char *location)
{
    struct wemoDeviceNode *curdevnode;
    struct wemoDeviceNode *prevdevnode;

    ithread_mutex_lock( &DeviceListMutex );

    curdevnode = GlobalDeviceList;
    if( !curdevnode ) {
        APP_LOG("WEMOHK", LOG_DEBUG, "WARNING: wemoCtrlPointRemoveDevice: Device list empty" );
    } else {
        if( 0 == strcmp( curdevnode->device.DescDocURL, location ) ) {
            GlobalDeviceList = curdevnode->next;
            wemoCtrlPointDeleteNode( curdevnode );
        } else {
            prevdevnode = curdevnode;
            curdevnode = curdevnode->next;

            while( curdevnode ) {
                if( strcmp( curdevnode->device.DescDocURL, location ) == 0 ) {
                    prevdevnode->next = curdevnode->next;
                    wemoCtrlPointDeleteNode( curdevnode );
                    break;
                }

                prevdevnode = curdevnode;
                curdevnode = curdevnode->next;
            }
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    return CTRLPT_SUCCESS;
}
/********************************************************************************
 * wemoCtrlPointRemoveAll
 *
 * Description:
 *       Remove all devices from the global device list.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int
wemoCtrlPointRemoveAll( void )
{
    struct wemoDeviceNode *curdevnode,
        *next;

    ithread_mutex_lock( &DeviceListMutex );

    curdevnode = GlobalDeviceList;
    GlobalDeviceList = NULL;

    while( curdevnode ) {
        next = curdevnode->next;
        wemoCtrlPointDeleteNode( curdevnode );
        curdevnode = next;
    }

    ithread_mutex_unlock( &DeviceListMutex );

    return CTRLPT_SUCCESS;
}

/********************************************************************************
 * wemoCtrlPointRefresh
 *
 * Description:
 *       Clear the current global device list and issue new search
 *	 requests to build it up again from scratch.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int
wemoCtrlPointRefresh( void )
{
    int rc;

    wemoCtrlPointRemoveAll();

    /*
       Search for all devices,
       waiting for up to 5 seconds
     */
    rc = UpnpSearchAsync( ctrlpt_handle, 5, targetServiceType, NULL );
    if( UPNP_E_SUCCESS != rc ) {
        APP_LOG("WEMOHK", LOG_ERR, "Error sending search request%d", rc );
        return CTRLPT_ERROR;
    }

    return CTRLPT_SUCCESS;
}

int wemoCtrlPointRestartRule(int wemo_id)
{
    wemoCtrlPointSendAction(WEMO_SERVICE_RULES,
                            wemo_id,
                            "RestartRuleEngine",
                            NULL,
                            NULL,
                            0,
                            0);

    return 0;
}

/********************************************************************************
 * wemoCtrlPointGetVar
 *
 * Description:
 *       Send a GetVar request to the specified service of a device.
 *
 * Parameters:
 *   service -- The service
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   varname -- The name of the variable to request.
 *
 ********************************************************************************/
int
wemoCtrlPointGetVar( int service,
                   int devnum,
                   char *varname )
{
    struct wemoDeviceNode *devnode;
    int rc;

    ithread_mutex_lock( &DeviceListMutex );

    rc = wemoCtrlPointGetDevice( devnum, &devnode );

    if( CTRLPT_SUCCESS == rc ) {
        rc = UpnpGetServiceVarStatusAsync( ctrlpt_handle,
                                           devnode->device.
                                           wemoService[service].ControlURL,
                                           varname,
                                           (Upnp_FunPtr) wemoCtrlPointCallbackEventHandler,
                                           NULL );
        if( rc != UPNP_E_SUCCESS ) {
            APP_LOG("WEMOHK", LOG_ERR, "Error in UpnpGetServiceVarStatusAsync -- %d", rc );
            rc = CTRLPT_ERROR;
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    return rc;
}

int wemoCtrlPointGetDevID(int wemo_id)
{
    struct wemoDeviceNode *tmpdevnode;
    int i = 1;
    int dev_id = -1;
    char UDN[NAME_SIZE];

    if (!wemo_dev_db_retrieve_udn(ctrlpt_dev_db, wemo_id, UDN)) {
        return CTRLPT_ERROR;
    }

    ithread_mutex_lock (&DeviceListMutex);
    tmpdevnode = GlobalDeviceList;
    while (tmpdevnode) {
        if (strcmp(tmpdevnode->device.UDN, UDN)) {
            tmpdevnode = tmpdevnode->next;
            i++;
        }
        else {
            dev_id = i;
            break;
        }
    }

    ithread_mutex_unlock(&DeviceListMutex);

    return dev_id;
}

void wemoCtrlPointHandleBinaryStateResponse(struct wemoDeviceNode *devnode, int service, IXML_Document *action_result, int ack_send)
{
    char *binaryStateItem = NULL;
    char *brightnessItem = NULL;
    char *EndTimeItem = NULL;
    char *faderItem = NULL;
    int binaryState = -1;
    int brightness = -1;
    int wemo_id = 0;

    if (!action_result) {
        return;
    }

    wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, devnode->device.UDN);
    if (wemo_id == 0) {
        return;
    }

    if ((binaryStateItem = ctrlpt_util_GetFirstDocumentItem(action_result, "BinaryState"))) {
        if (!strcmp(binaryStateItem, "Error")) {
            APP_LOG("WEMOHK", LOG_DEBUG,"ERROR binaryStateItem");

            wemoCtrlPointGetPower(wemo_id);

            APP_LOG("WEMOHK", LOG_DEBUG,"ERROR binaryStateItem: wemo id = %d", wemo_id);
            free(binaryStateItem);
            return;
        }
        else {
            binaryState = strtol(binaryStateItem, NULL, 10);
            sprintf(devnode->device.wemoService[service].VariableStrVal[0],
                    "%d",
                    binaryState);
        }
        free(binaryStateItem);
    }
    if ((brightnessItem = ctrlpt_util_GetFirstDocumentItem(action_result, "brightness"))) {
        brightness = atoi(brightnessItem);
        strcpy(devnode->device.wemoService[service].VariableStrVal[1],
               brightnessItem);
        free(brightnessItem);
    }

    if ((EndTimeItem =  ctrlpt_util_GetFirstDocumentItem(action_result, "CountdownEndTime"))) {
        APP_LOG("WEMOHK", LOG_INFO, "Dev %d Received CountdownEndTime : %s",
                wemo_id,
                EndTimeItem);
        struct we_name_value name_value;

        strcpy(name_value.name, "CountdownEndTime");
        strcpy(name_value.value, EndTimeItem);
        APP_LOG("WEMOHK", LOG_DEBUG, "call wemo_ipc_send_name_value: name: %s value: %s",
                name_value.name,
                name_value.value);
        wemo_ipc_send_name_value(wemo_id, &name_value);

        free(EndTimeItem);
    }

    if ((faderItem =  ctrlpt_util_GetFirstDocumentItem(action_result, "fader"))
        || (faderItem =  ctrlpt_util_GetFirstDocumentItem(action_result, "Fader"))) {
        APP_LOG("WEMOHK", LOG_INFO, "Dev %d Received Fader : %s",
                wemo_id,
                faderItem);
        struct we_name_value name_value;

        strcpy(name_value.name, "Fader");
        strcpy(name_value.value, faderItem);
        APP_LOG("WEMOHK", LOG_DEBUG,"call wemo_ipc_send_name_value: name: %s value: %s",
                          name_value.name,
                          name_value.value);
        wemo_ipc_send_name_value(wemo_id, &name_value);

        free(faderItem);
    }

    if (binaryState != -1) {
        wemo_dev_db_update_capability(ctrlpt_state_db, wemo_id, CAP_BINARY, binaryState);
    }
    if (brightness != -1) {
        wemo_dev_db_update_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL, brightness);
        ack_send = 1;
    }
    if (ack_send) {
        struct we_state state_buffer;
        state_buffer.is_online = 1;
        if (binaryState != -1) {
            state_buffer.state = binaryState;
        } else {
            state_buffer.state = wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_BINARY);
        }

        if (strlen(devnode->device.wemoService[service].VariableStrVal[1])) {
            brightness = atoi(devnode->device.wemoService[service].VariableStrVal[1]);
        }
        if (brightness != -1) {
            state_buffer.level = brightness;
        } else {
            brightness = wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL);
        }
        APP_LOG("WEMOHK", LOG_DEBUG,"sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                          wemo_id,
                          state_buffer.is_online,
                          state_buffer.state,
                          state_buffer.level);

        wemo_ipc_send_event(wemo_id, &state_buffer);
    }

}

void wemoCtrlPointHandleActionResponse(struct wemoDeviceNode *devnode, int service, IXML_Document *action_result, int ack_send)
{
    char *binaryStateItem = NULL;
    char *pairing_status = NULL;
    char *network_status = NULL;
    char *action_response = NULL;
    char *insight_params = NULL;
    char *home_settings_version = NULL;
    char *energy_per_unit_cost = NULL;
    char *currency = NULL;

    if (!action_result) {
        return;
    }
    if ((binaryStateItem = ctrlpt_util_GetFirstDocumentItem(action_result, "BinaryState"))) {
        wemoCtrlPointHandleBinaryStateResponse(devnode, service, action_result, ack_send);
        free(binaryStateItem);
    }
    else if ((pairing_status = ctrlpt_util_GetFirstDocumentItem(action_result, "ParingStatus"))) {
        APP_LOG("WEMOHK", LOG_DEBUG, "pairing status : %s", pairing_status);
        free(pairing_status);
    }
    else if ((network_status = ctrlpt_util_GetFirstDocumentItem(action_result, "NetworkStatus"))) {
        struct we_network_status net_state;
        APP_LOG("WEMOHK", LOG_DEBUG, "network status : %s", network_status);
        int wemo_id = 0;
        if ((wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, devnode->device.UDN))) {
            net_state.connection_state = atoi(network_status);
            APP_LOG("WEMOHK", LOG_DEBUG,"call wemo_ipc_send_netstate : %s", network_status);
            wemo_ipc_send_netstate(wemo_id, &net_state);
        }
        free(network_status);
    }
    else if ((action_response = ctrlpt_util_GetFirstDocumentItem(action_result, "Information"))) {
        int wemo_id = 0;

        if (!(wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, devnode->device.UDN))) {
            return;
        }

        APP_LOG("WEMOHK", LOG_DEBUG,"call wemo_ipc_send_devinfo...");

        wemo_ipc_send_devinfo(wemo_id, action_response);
    }
    else if ((home_settings_version = ctrlpt_util_GetFirstDocumentItem(action_result, "HomeSettingsVersion")) &&
             (energy_per_unit_cost = ctrlpt_util_GetFirstDocumentItem(action_result, "EnergyPerUnitCost")) &&
             (currency = ctrlpt_util_GetFirstDocumentItem(action_result, "Currency"))) {
        struct we_insight_home_settings settings;
        int wemo_id = 0;

        if (!(wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, devnode->device.UDN))) {
            return;
        }

        APP_LOG("WEMOHK", LOG_DEBUG,"call wemo_ipc_send_insight_home_settings...");
        strcpy(settings.HomeSettingsVersion, home_settings_version);
        strcpy(settings.energyPerUnitCost, energy_per_unit_cost);
        strcpy(settings.Currency, currency);
        wemo_ipc_send_insight_home_settings(wemo_id, &settings);
    }
    else if ((insight_params = ctrlpt_util_GetFirstDocumentItem(action_result, "InsightParams"))) {
        struct we_name_value name_value;
        int wemo_id = 0;

        if (!(wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, devnode->device.UDN))) {
            return;
        }

        strcpy(name_value.name, "InsightParams");
        strcpy(name_value.value, insight_params);
        APP_LOG("WEMOHK", LOG_DEBUG, "call wemo_ipc_send_name_value: name: %s, value: %s",
                name_value.name, name_value.value);
        wemo_ipc_send_name_value(wemo_id, &name_value);
    }
}

int wemoCtrlPointDeleteDevice(int wemo_id)
{
    char udn[256];
    int return_code = CTRLPT_SUCCESS;

    memset(udn, 0, 256);

    if (wemo_dev_db_retrieve_udn(ctrlpt_dev_db, wemo_id, udn)) {
        wemoCtrlPointRemoveDevice(udn);
    }
    if (wemo_dev_db_delete_row(ctrlpt_dev_db, wemo_id)) {
        return_code = CTRLPT_SUCCESS;
    } else {
        return_code = CTRLPT_ERROR;
    }
    if (wemo_dev_statedb_delete_row(ctrlpt_state_db, wemo_id)) {
        return_code = CTRLPT_SUCCESS;
    } else {
        return_code = CTRLPT_ERROR;
    }
    return return_code;
}

/********************************************************************************
 * wemoCtrlPointSendAction
 *
 * Description:
 *       Send an Action request to the specified service of a device.
 *
 * Parameters:
 *   service -- The service
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   actionname -- The name of the action.
 *   param_name -- An array of parameter names
 *   param_val -- The corresponding parameter values
 *   param_count -- The number of parameters
 *
 ********************************************************************************/
int
wemoCtrlPointSendAction( int service,
                       int devnum,
                       char *actionname,
                       char **param_name,
                       char **param_val,
                        int param_count,
                        int async)
{
    struct wemoDeviceNode *devnode;
    IXML_Document *actionNode = NULL;
    int rc = CTRLPT_SUCCESS;
    int param;

    ithread_mutex_lock( &DeviceListMutex );

    rc = wemoCtrlPointGetDevice( devnum, &devnode );
    if( CTRLPT_SUCCESS == rc ) {
        if( 0 == param_count ) {
            actionNode = UpnpMakeAction(actionname,
                                        wemoServiceType[service],
                                        0,
                                        NULL);
        }
        else {
            for( param = 0; param < param_count; param++ ) {
                if( UpnpAddToAction
                    ( &actionNode, actionname, wemoServiceType[service],
                      param_name[param],
                      param_val[param] ) != UPNP_E_SUCCESS ) {
                    APP_LOG("WEMOHK", LOG_ERR, "ERROR: wemoCtrlPointSendActionAsync: Trying to add action param" );
                }
            }
        }

        if (async) {
            rc = UpnpSendActionAsync( ctrlpt_handle,
                                      devnode->device.wemoService[service].ControlURL,
                                      wemoServiceType[service],
                                      NULL,
                                      actionNode,
                                      (Upnp_FunPtr) wemoCtrlPointCallbackEventHandler,
                                      NULL);
            if( rc != UPNP_E_SUCCESS ) {
                APP_LOG("WEMOHK", LOG_ERR, "Error in UpnpSendActionAsync -- %d", rc );
                rc = CTRLPT_ERROR;
            }
        }
        else {
            IXML_Document *action_result = NULL;
            rc = UpnpSendAction(ctrlpt_handle,
                                devnode->device.wemoService[service].ControlURL,
                                wemoServiceType[service],
                                NULL, actionNode,
                                &action_result);
            if( rc != UPNP_E_SUCCESS ) {
                APP_LOG("WEMOHK", LOG_ERR, "Error in UpnpSendAction -- %d", rc );
                rc = CTRLPT_ERROR;
            }
            if (action_result) {
                wemoCtrlPointHandleActionResponse(devnode, service, action_result, 0);
                ixmlDocument_free(action_result);
            }
        }
    }

    ithread_mutex_unlock( &DeviceListMutex );

    if (rc == CTRLPT_ERROR) {
        APP_LOG("WEMOHK", LOG_ERR, "Error - udn: %s action: %s", devnode->device.UDN, actionname);
    }
    if( actionNode )
        ixmlDocument_free( actionNode );

    return rc;
}

int wemoCtrlPointSendActionEx(int service,
                              int devnum,
                              char *actionname,
                              char **param_name,
                              char **param_val,
                              int param_count,
                              int async)
{
    int tried = 0;
    int ret = 0;

    do {
        ret = wemoCtrlPointSendAction(service, devnum, actionname, param_name, param_val, param_count, async);
        if (tried > 1) {
            break;
        }
        tried++;
        if (ret == CTRLPT_ERROR)
            sleep(2);
    } while (ret == CTRLPT_ERROR);

    return ret;
}

/********************************************************************************
 * wemoCtrlPointSendActionNumericArg
 *
 * Description:Send an action with one argument to a device in the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list, starting with 1)
 *   service -- WEMO_SERVICE_CONTROL or WEMO_SERVICE_PICTURE
 *   actionName -- The device action, i.e., "SetChannel"
 *   paramName -- The name of the parameter that is being passed
 *   paramValue -- Actual value of the parameter being passed
 *
 ********************************************************************************/
int
wemoCtrlPointSendActionNumericArg( int devnum,
                                 int service,
                                 char *actionName,
                                 char *paramName,
                                  int paramValue,
                                  int async)
{
    char param_val_a[50];
    char *param_val = param_val_a;

    sprintf( param_val_a, "%d", paramValue );

    return wemoCtrlPointSendAction( service, devnum, actionName, &paramName,
                                   &param_val, 1, async);
}

/********************************************************************************
 * wemoCtrlPointGetDevice
 *
 * Description:
 *       Given a list number, returns the pointer to the device
 *       node at that position in the global device list.  Note
 *       that this function is not thread safe.  It must be called
 *       from a function that has locked the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   devnode -- The output device node pointer
 *
 ********************************************************************************/
int
wemoCtrlPointGetDevice( int devnum,
                      struct wemoDeviceNode **devnode )
{
    int count = devnum;
    struct wemoDeviceNode *tmpdevnode = NULL;

    if( count )
        tmpdevnode = GlobalDeviceList;

    while( --count && tmpdevnode ) {
        tmpdevnode = tmpdevnode->next;
    }

    if( !tmpdevnode ) {
        APP_LOG("WEMOHK", LOG_ERR, "Error finding wemo Device number -- %d", devnum );
        return CTRLPT_ERROR;
    }

    *devnode = tmpdevnode;
    return CTRLPT_SUCCESS;
}

/********************************************************************************
 * wemoCtrlPointPrintList
 *
 * Description:
 *       Print the universal device names for each device in the global device list
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int
wemoCtrlPointPrintList()
{
    struct wemoDeviceNode *tmpdevnode;
    int i = 0;

    ithread_mutex_lock( &DeviceListMutex );

    APP_LOG("WEMOHK", LOG_DEBUG, "wemoCtrlPointPrintList:" );
    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        APP_LOG("WEMOHK", LOG_DEBUG, " %3d -- %s (%s)",
                           ++i,
                           tmpdevnode->device.UDN,
                           tmpdevnode->device.ipaddr);
        tmpdevnode = tmpdevnode->next;
    }
    ithread_mutex_unlock( &DeviceListMutex );

    return CTRLPT_SUCCESS;
}

/********************************************************************************
 * wemoCtrlPointPrintDevice
 *
 * Description:
 *       Print the identifiers and state table for a device from
 *       the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *
 ********************************************************************************/
int
wemoCtrlPointPrintDevice( int devnum )
{
    struct wemoDeviceNode *tmpdevnode;
    int i = 0,
      service,
      var;
    char spacer[15];

    if( devnum <= 0 ) {
        APP_LOG("WEMOHK", LOG_ERR, "Error in wemoCtrlPointPrintDevice: invalid devnum = %d", devnum );
        return CTRLPT_ERROR;
    }

    ithread_mutex_lock( &DeviceListMutex );

    APP_LOG("WEMOHK", LOG_DEBUG, "wemoCtrlPointPrintDevice:" );
    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        i++;
        if( i == devnum )
            break;
        tmpdevnode = tmpdevnode->next;
    }

    if( !tmpdevnode ) {
        APP_LOG("WEMOHK", LOG_DEBUG, "Error in wemoCtrlPointPrintDevice: invalid devnum = %d  --  actual device count = %d",
              devnum, i );
    } else {
        APP_LOG("WEMOHK", LOG_DEBUG, "  WEMO Device -- %d", devnum );
        APP_LOG("WEMOHK", LOG_DEBUG, "    |                  " );
        APP_LOG("WEMOHK", LOG_DEBUG, "    +- UDN        = %s",
                          tmpdevnode->device.UDN );
        APP_LOG("WEMOHK", LOG_DEBUG, "    +- DescDocURL     = %s",
                          tmpdevnode->device.DescDocURL );
        APP_LOG("WEMOHK", LOG_DEBUG, "    +- FriendlyName   = %s",
                          tmpdevnode->device.FriendlyName );
        APP_LOG("WEMOHK", LOG_DEBUG, "    +- PresURL        = %s",
                          tmpdevnode->device.PresURL );
        APP_LOG("WEMOHK", LOG_DEBUG, "    +- Adver. TimeOut = %d",
                          tmpdevnode->device.AdvrTimeOut );

        for( service = 0; service < WEMO_SERVICE_COUNT; service++ ) {
            if( service < WEMO_SERVICE_COUNT - 1 )
                sprintf( spacer, "    |    " );
            else
                sprintf( spacer, "         " );
            APP_LOG("WEMOHK", LOG_DEBUG, "    |                  " );
            APP_LOG("WEMOHK", LOG_DEBUG, "    +- WEMO %s Service",
                              wemoServiceName[service] );
            APP_LOG("WEMOHK", LOG_DEBUG, "%s+- ServiceId       = %s", spacer,
                              tmpdevnode->device.wemoService[service].
                              ServiceId );
            APP_LOG("WEMOHK", LOG_DEBUG, "%s+- ServiceType     = %s", spacer,
                              tmpdevnode->device.wemoService[service].
                              ServiceType );
            APP_LOG("WEMOHK", LOG_DEBUG, "%s+- EventURL        = %s", spacer,
                              tmpdevnode->device.wemoService[service].
                              EventURL );
            APP_LOG("WEMOHK", LOG_DEBUG, "%s+- ControlURL      = %s", spacer,
                              tmpdevnode->device.wemoService[service].
                              ControlURL );
            APP_LOG("WEMOHK", LOG_DEBUG, "%s+- SID             = %s", spacer,
                              tmpdevnode->device.wemoService[service].SID );
            APP_LOG("WEMOHK", LOG_DEBUG, "%s+- ServiceStateTable", spacer );

            for( var = 0; var < wemoVarCount[service]; var++ ) {
                APP_LOG("WEMOHK", LOG_DEBUG, "%s     +- %-10s = %s", spacer,
                                  wemoVarName[service][var],
                                  tmpdevnode->device.wemoService[service].
                                  VariableStrVal[var] );
            }
        }
    }

    APP_LOG("WEMOHK", LOG_DEBUG, "" );
    ithread_mutex_unlock( &DeviceListMutex );

    return CTRLPT_SUCCESS;
}

/********************************************************************************
 * wemoStateUpdate
 *
 * Description:
 *       Update a wemo state table.  Called when an event is
 *       received.  Note: this function is NOT thread save.  It must be
 *       called from another function that has locked the global device list.
 *
 * Parameters:
 *   UDN     -- The UDN of the parent device.
 *   Service -- The service state table to update
 *   ChangedVariables -- DOM document representing the XML received
 *                       with the event
 *   State -- pointer to the state table for the wemo service
 *            to update
 *
 ********************************************************************************/
void
wemoStateUpdate( char *UDN,
               int Service,
               IXML_Document * ChangedVariables,
               char **State )
{
    IXML_NodeList *properties, *variables;
    IXML_Element *property, *variable;
    int length, length1;
    int i, j;
    char *tmpstate = NULL;
    int send_ack = 0;
    int wemo_id = 0;
    struct we_state state_buffer;

    state_buffer.is_online = 1;
    state_buffer.state = -1;
    state_buffer.level = -1;

    APP_LOG("WEMOHK", LOG_DEBUG, "wemo State Update (service %d): ", Service );

    /*
       Find all of the e:property tags in the document
     */
    properties = ixmlDocument_getElementsByTagName( ChangedVariables,
                                                    "e:property" );
    if( NULL != properties ) {
        length = ixmlNodeList_length( properties );
        for( i = 0; i < length; i++ ) { /* Loop through each property change found */
            property = ( IXML_Element * ) ixmlNodeList_item( properties, i );
            /*
               For each variable name in the state table, check if this
               is a corresponding property change
             */
            for( j = 0; j < wemoVarCount[Service]; j++ ) {
                variables = ixmlElement_getElementsByTagName( property,
                                                      wemoVarName[Service][j] );
                /*
                   If a match is found, extract the value, and update the state table
                 */
                if(variables) {
                    length1 = ixmlNodeList_length( variables );
                    if( length1 ) {
                        variable = (IXML_Element *)ixmlNodeList_item(variables, 0);
                        tmpstate = ctrlpt_util_GetElementValue(variable);
                        APP_LOG("WEMOHK", LOG_DEBUG, "variable->n.nodeName : %s, tmpstate = %s", variable->n.nodeName, tmpstate);
                        if(tmpstate) {
                            if((strcasestr(UDN, "uuid:insight") != NULL) &&
                               !strcmp(variable->n.nodeName, "BinaryState")) {
                                APP_LOG("WEMOHK", LOG_DEBUG,"==== insight BinaryState... ===");
                                struct we_name_value name_value;
                                strcpy(name_value.name, "InsightParams");
                                strcpy(name_value.value, tmpstate);
                                APP_LOG("WEMOHK", LOG_DEBUG, "call wemo_ipc_send_name_value: name: %s, value: %s",
                                        name_value.name, name_value.value);
                                wemo_ipc_send_name_value(wemo_id, &name_value);

                                char *token;
                                token = strtok(tmpstate, "|");
                                strcpy(State[j], token);
                            }
                            else {
                                if (WEMO_MAX_VAL_LEN < strlen(tmpstate) + 1) {
                                    APP_LOG("WEMOHK", LOG_DEBUG, "realloc State[%d](size %u) for name = %s, value = %s",
                                                      j, WEMO_MAX_VAL_LEN, variable->n.nodeName, tmpstate);
                                    State[j] = realloc(State[j], strlen(tmpstate) + 1);
                                    if (State[j] == NULL) {
                                        APP_LOG("WEMOHK", LOG_ERR, "#### BUG FIXME realloc failed! ####");
                                        free(tmpstate);
                                        ixmlNodeList_free(variables);
                                        ixmlNodeList_free(properties);
                                        return;
                                    }
                                }
                                strcpy( State[j], tmpstate );
                            }
                            /* update wemo_device DB */
                            if ((wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, UDN))) {
                                if (!strcmp(variable->n.nodeName, "BinaryState")) {
                                    if (wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_BINARY) != atoi(State[j])) {
                                        wemo_dev_db_update_capability(ctrlpt_state_db, wemo_id, CAP_BINARY, atoi(State[j]));
                                        state_buffer.state = atoi(State[j]);
                                        send_ack = 1;
                                    }
                                }
                                else if (!strcmp(variable->n.nodeName, "Brightness")) {
                                    if (wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL) != atoi(State[j])) {
                                        wemo_dev_db_update_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL, atoi(State[j]));
                                        state_buffer.level = atoi(State[j]);
                                        send_ack = 1;
                                    }
                                }
                                else if (!strcmp(variable->n.nodeName, "FriendlyName")) {
                                    struct we_name_change name_change;

                                    strcpy(name_change.name, State[j]);
                                    APP_LOG("WEMOHK", LOG_DEBUG, "call wemo_ipc_send_name_change: %s",
                                                      name_change.name);
                                    wemo_ipc_send_name_change(wemo_id, &name_change);
                                }
                                else {
                                    struct we_name_value name_value;

                                    if (!strcmp(variable->n.nodeName, "Fader")) {
                                        int fadeEnable = 0;
                                        int toBrightness = 0;
                                        unsigned int faderTimeSeconds = 0;
                                        long referenceTime = 0;
                                        float delta = 0.0;

                                        sscanf(State[j], "%u:%ld:%d:%f:%d", &faderTimeSeconds, &referenceTime, &fadeEnable, &delta, &toBrightness);
                                        if (delta == 0.0) {
                                            wemo_dev_db_update_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL, toBrightness);
                                        }
                                    }
                                    strcpy(name_value.name, variable->n.nodeName);
                                    strcpy(name_value.value, State[j]);
                                    APP_LOG("WEMOHK", LOG_DEBUG, "call wemo_ipc_send_name_value: name: %s, value: %s",
                                            name_value.name, name_value.value);
                                    wemo_ipc_send_name_value(wemo_id, &name_value);
                                }

                                APP_LOG("WEMOHK", LOG_DEBUG, "%s(wemo_id: %d): variable: %s value:'%s'",
                                                  UDN,
                                                  wemo_id,
                                                  wemoVarName[Service][j],
                                                  State[j]);
                            }
                            free( tmpstate );
                            tmpstate = NULL;
                        }
                    }

                    ixmlNodeList_free( variables );
                    variables = NULL;
                }
            }
        }
        if (send_ack) {
            state_buffer.is_online = 1;
            APP_LOG("WEMOHK", LOG_DEBUG, "sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                              wemo_id,
                              state_buffer.is_online,
                              state_buffer.state,
                              state_buffer.level);

            wemo_ipc_send_event(wemo_id, &state_buffer);
        }
        ixmlNodeList_free( properties );
    }
}

void
wemoNameValueState( char *UDN,
                   int Service,
                   IXML_Document * ChangedVariables,
                   char **State )
{
    IXML_NodeList *properties, *variables;
    IXML_Element *property, *variable;
    int length, length1;
    int i, j;
    char *tmpstate = NULL;
    int wemo_id = 0;

    /*
       Find all of the e:property tags in the document
     */
    properties = ixmlDocument_getElementsByTagName( ChangedVariables,
                                                    "e:property" );
    if( NULL != properties ) {
        length = ixmlNodeList_length( properties );
        for( i = 0; i < length; i++ ) { /* Loop through each property change found */
            property = ( IXML_Element * ) ixmlNodeList_item( properties, i );

            /*
               For each variable name in the state table, check if this
               is a corresponding property change
             */
            for( j = 0; j < wemoVarCount[Service]; j++ ) {
                variables = ixmlElement_getElementsByTagName( property,
                                                      wemoVarName[Service][j] );
                /*
                   If a match is found, extract the value, and update the state table
                 */
                if(variables) {
                    length1 = ixmlNodeList_length( variables );
                    if( length1 ) {
                        variable = (IXML_Element *)ixmlNodeList_item(variables, 0);
                        tmpstate = ctrlpt_util_GetElementValue(variable);
                        APP_LOG("WEMOHK", LOG_DEBUG, "variable->n.nodeName : %s, tmpstate = %s",
                                          variable->n.nodeName, tmpstate);
                        if(tmpstate) {
                            if (WEMO_MAX_VAL_LEN < strlen(tmpstate) + 1) {
                                APP_LOG("WEMOHK", LOG_DEBUG, "realloc State[%d](size %u) for name = %s, value = %s",
                                                  j, WEMO_MAX_VAL_LEN, variable->n.nodeName, tmpstate);
                                State[j] = realloc(State[j], strlen(tmpstate) + 1);
                                if (State[j] == NULL) {
                                    APP_LOG("WEMOHK", LOG_ERR,"#### BUG FIXME realloc failed! ####");
                                    free(tmpstate);
                                    ixmlNodeList_free(variables);
                                    ixmlNodeList_free(properties);
                                    return;
                                }
                            }
                            strcpy( State[j], tmpstate );

                            if ((wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, UDN))) {
                                if (strcmp(variable->n.nodeName, "NetworkStatus") == 0) {
                                    struct we_network_status net_state;
                                    APP_LOG("WEMOHK", LOG_DEBUG, "network status : %s", State[j]);
                                    net_state.connection_state = atoi(State[j]);
                                    APP_LOG("WEMOHK", LOG_DEBUG,"call wemo_ipc_send_netstate : %s", State[j]);
                                    wemo_ipc_send_netstate(wemo_id, &net_state);
                                }
                                else {
                                    struct we_name_value name_value;

                                    strcpy(name_value.name, variable->n.nodeName);
                                    strcpy(name_value.value, State[j]);
                                    APP_LOG("WEMOHK", LOG_DEBUG,"call wemo_ipc_send_name_value: %s",
                                            name_value.name);
                                    wemo_ipc_send_name_value(wemo_id, &name_value);

                                    APP_LOG("WEMOHK", LOG_DEBUG, "%s(wemo_id: %d): variable: %s value:'%s'",
                                            UDN,
                                            wemo_id,
                                            wemoVarName[Service][j],
                                            State[j]);
                                }
                                free(tmpstate);
                                tmpstate = NULL;
                            }
                        }
                    }
                    ixmlNodeList_free(variables);
                    variables = NULL;
                }
            }
        }
        ixmlNodeList_free(properties);
    }
}

void wemoCtrlPointHandleActionComplete(UpnpActionComplete *event)
{
    IXML_Document *action_result = NULL;
    char *binaryStateItem = NULL;
    char *brightnessItem = NULL;
    char *EndTimeItem = NULL;
    char *faderItem = NULL;
    const char *ctrlUrl = NULL;
	struct wemoDeviceNode *tmpdevnode;
    int service;
    int found = 0;
    int binaryState = -1;
    int brightness = -1;
    int wemo_id = 0;

    ctrlUrl = UpnpActionComplete_get_CtrlUrl_cstr(event);

    action_result = UpnpActionComplete_get_ActionResult(event);
    /* Disable below code to send notification back to homekit when actioncomplete happens */
    if (action_result) {
        APP_LOG("WEMOHK", LOG_DEBUG, "%s", ixmlNode_getLocalName(ixmlNode_getFirstChild(&action_result->n)));
        if (!strcmp(ixmlNode_getLocalName(ixmlNode_getFirstChild(&action_result->n)), "ConnectHomeNetworkResponse")) {
            system("nvram_set ProvisionedBy=APPLE_HOMEKIT");
        }
    }

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
        wemo_id = wemo_dev_db_retrieve_id(ctrlpt_dev_db, tmpdevnode->device.UDN);
        if (wemo_id == 0) {
            APP_LOG("WEMOHK", LOG_DEBUG, "Received action response for wrong UDN %s", tmpdevnode->device.UDN);
            break;
        }
        for (service = 0; service < WEMO_SERVICE_COUNT; service++) {
            if (strcmp(tmpdevnode->device.wemoService[service].ControlURL, ctrlUrl) == 0) {
                if (action_result) {
                    if ((binaryStateItem = ctrlpt_util_GetFirstDocumentItem(action_result, "BinaryState"))) {
                        if (!strcmp(binaryStateItem, "Error")) {
                            APP_LOG("WEMOHK", LOG_DEBUG, "ERROR binaryStateItem received.");
                            binaryState = atoi(tmpdevnode->device.wemoService[service].VariableStrVal[0]);
                        }
                        else {
                            binaryState = strtol(binaryStateItem, NULL, 10);
                            APP_LOG("WEMOHK", LOG_INFO, "Received BinaryState for UDN %s: value: %d",
                                    tmpdevnode->device.UDN, binaryState);
                            sprintf(tmpdevnode->device.wemoService[service].VariableStrVal[0],
                                    "%d",
                                    binaryState);
                        }
                        free(binaryStateItem);
                    }
                    if ((brightnessItem = ctrlpt_util_GetFirstDocumentItem(action_result, "brightness"))) {
                        brightness = atoi(brightnessItem);
                        APP_LOG("WEMOHK", LOG_INFO, "Received brightness for UDN %s: value: %d",
                                tmpdevnode->device.UDN, brightness);
                        strcpy(tmpdevnode->device.wemoService[service].VariableStrVal[1],
                                   brightnessItem);
                        free(brightnessItem);
                    }

                    if ((EndTimeItem =  ctrlpt_util_GetFirstDocumentItem(action_result, "CountdownEndTime"))) {
                        APP_LOG("WEMOHK", LOG_INFO, "Received CountdownEndTime for UDN %s: value: %s",
                                tmpdevnode->device.UDN, EndTimeItem);
                        struct we_name_value name_value;

                        strcpy(name_value.name, "CountdownEndTime");
                        strcpy(name_value.value, EndTimeItem);
                        APP_LOG("WEMOHK", LOG_DEBUG, "call wemo_ipc_send_name_value: name: %s value: %s",
                                name_value.name,
                                name_value.value);
                        wemo_ipc_send_name_value(wemo_id, &name_value);

                        free(EndTimeItem);
                    }

                    if ((faderItem =  ctrlpt_util_GetFirstDocumentItem(action_result, "fader"))
                        || (faderItem =  ctrlpt_util_GetFirstDocumentItem(action_result, "Fader"))) {
                        APP_LOG("WEMOHK", LOG_INFO, "Received fader for UDN %s: value: %s", tmpdevnode->device.UDN, faderItem);
                        struct we_name_value name_value;

                        strcpy(name_value.name, "Fader");
                        strcpy(name_value.value, faderItem);
                        APP_LOG("WEMOHK", LOG_DEBUG,"call wemo_ipc_send_name_value: name: %s value: %s",
                                name_value.name,
                                name_value.value);
                        wemo_ipc_send_name_value(wemo_id, &name_value);

                        free(faderItem);
                    }
                }

                if (binaryState != -1) {
                    wemo_dev_db_update_capability(ctrlpt_state_db, wemo_id, CAP_BINARY, binaryState);
                }
                if (brightness != -1) {
                    wemo_dev_db_update_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL, brightness);
                }
                struct we_state state_buffer;
                state_buffer.is_online = 1;
                if (strlen(tmpdevnode->device.wemoService[service].VariableStrVal[1])) {
                    brightness = atoi(tmpdevnode->device.wemoService[service].VariableStrVal[1]);
                }
                state_buffer.state = wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_BINARY);
                state_buffer.level = wemo_dev_db_get_capability(ctrlpt_state_db, wemo_id, CAP_LEVEL);

                APP_LOG("WEMOHK", LOG_DEBUG, "sending event wemo_id = %d, is_online = %d, state = %d, level = %d",
                                  wemo_id,
                                  state_buffer.is_online,
                                  state_buffer.state,
                                  state_buffer.level);
                wemo_ipc_send_event(wemo_id, &state_buffer);
                found = 1;
                break;
            }
        }

        if (found)
            break;
        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock(&DeviceListMutex);
}

/********************************************************************************
 * wemoCtrlPointHandleEvent
 *
 * Description:
 *       Handle a UPnP event that was received.  Process the event and update
 *       the appropriate service state table.
 *
 * Parameters:
 *   sid -- The subscription id for the event
 *   eventkey -- The eventkey number for the event
 *   changes -- The DOM document representing the changes
 *
 ********************************************************************************/
void wemoCtrlPointHandleEvent(const UpnpString *sid,
                              int evntkey,
                              IXML_Document *changes)
{
    struct wemoDeviceNode *tmpdevnode;
    int service;
    const char *aux_sid = NULL;
    int found = 0;

    ithread_mutex_lock(&DeviceListMutex);

    tmpdevnode = GlobalDeviceList;
    while (tmpdevnode) {
        for (service = 0; service < WEMO_SERVICE_COUNT; ++service) {
            aux_sid = UpnpString_get_String(sid);
            if (strcmp(tmpdevnode->device.wemoService[service].SID, aux_sid) ==  0) {
                APP_LOG("WEMOHK", LOG_DEBUG, "Received WEMO[service: %d] %s Event: %d for SID %s",
                        service,
                        wemoServiceName[service],
                        evntkey,
                        aux_sid);
                if (service == 2) {
                    wemoStateUpdate(tmpdevnode->device.UDN,
                                    service,
                                    changes,
                                    (char **)&tmpdevnode->device.wemoService[service].VariableStrVal);
                    found = 1;
                }
                else {
                    wemoNameValueState(tmpdevnode->device.UDN,
                                       service,
                                       changes,
                                       (char **)&tmpdevnode->device.wemoService[service].VariableStrVal);
                    found = 1;
                }
                break;
            }
        }
        if (found)
            break;
        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock(&DeviceListMutex);
}

/********************************************************************************
 * wemoCtrlPointHandleSubscribeUpdate
 *
 * Description:
 *       Handle a UPnP subscription update that was received.  Find the
 *       service the update belongs to, and update its subscription
 *       timeout.
 *
 * Parameters:
 *   eventURL -- The event URL for the subscription
 *   sid -- The subscription id for the subscription
 *   timeout  -- The new timeout for the subscription
 *
 ********************************************************************************/
void wemoCtrlPointHandleSubscribeUpdate(const char *eventURL, const Upnp_SID sid, int timeout)
{
    struct wemoDeviceNode *tmpdevnode;
    int service;

    ithread_mutex_lock( &DeviceListMutex );

    tmpdevnode = GlobalDeviceList;
    while( tmpdevnode ) {
        for( service = 0; service < WEMO_SERVICE_COUNT; service++ ) {

            if( strcmp(tmpdevnode->device.wemoService[service].EventURL,
                  eventURL ) == 0 ) {
                APP_LOG("WEMOHK", LOG_DEBUG, "Received WEMO %s Event Renewal for eventURL %s",
                        wemoServiceName[service], eventURL );
                strcpy( tmpdevnode->device.wemoService[service].SID, sid );
                break;
            }
        }

        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock( &DeviceListMutex );
}

void
wemoCtrlPointHandleGetVar( const char *controlURL,
                         const char *varName,
                         const DOMString varValue )
{

    struct wemoDeviceNode *tmpdevnode;
    int service;

    ithread_mutex_lock( &DeviceListMutex );

    tmpdevnode = GlobalDeviceList;
    while (tmpdevnode) {
        for (service = 0; service < WEMO_SERVICE_COUNT; service++) {
            if (strcmp(tmpdevnode->device.wemoService[service].ControlURL, controlURL ) == 0 ) {
                ctrlpt_util_StateUpdate(
                    varName, varValue, tmpdevnode->device.UDN, GET_VAR_COMPLETE );
                break;
            }
        }
        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock( &DeviceListMutex );
}

int wemoCtrlPointFindUDNfromCtrlUrl(const char *ctrlUrl, char *udn)
{
	struct wemoDeviceNode *tmpdevnode;
    int service;
    int found = 0;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
        for (service = 0; service < WEMO_SERVICE_COUNT; service++) {
            if (strcmp(tmpdevnode->device.wemoService[service].ControlURL, ctrlUrl) == 0) {
                if (udn != NULL) {
                    memcpy(udn, tmpdevnode->device.UDN, NAME_SIZE);
                }
                found = 1;
                break;
            }
        }
        if (found)
            break;
        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock(&DeviceListMutex);

    return found;
}

int wemoCtrlPointFindUDNfromEventUrl(const char *eventUrl, char *udn)
{
	struct wemoDeviceNode *tmpdevnode;
    int service;
    int found = 0;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
        for (service = 0; service < WEMO_SERVICE_COUNT; service++) {
            if (strcmp(tmpdevnode->device.wemoService[service].EventURL, eventUrl) == 0) {
                if (udn != NULL) {
                    memcpy(udn, tmpdevnode->device.UDN, NAME_SIZE);
                }
                found = 1;
                break;
            }
        }
        if (found)
            break;
        tmpdevnode = tmpdevnode->next;
    }

    ithread_mutex_unlock(&DeviceListMutex);

    return found;
}

/********************************************************************************
 * wemoCtrlPointCallbackEventHandler
 *
 * Description:
 *       The callback handler registered with the SDK while registering
 *       the control point.  Detects the type of callback, and passes the
 *       request on to the appropriate function.
 *
 * Parameters:
 *   EventType -- The type of callback event
 *   Event -- Data structure containing event data
 *   Cookie -- Optional data specified during callback registration
 *
 ********************************************************************************/
int wemoCtrlPointCallbackEventHandler(Upnp_EventType EventType, void *Event, void *Cookie)
{
	int errCode = 0;

	ctrlpt_util_PrintEvent(EventType, Event);
	switch ( EventType ) {
	/* SSDP Stuff */
	case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE: {
        const char *serviceType = UpnpDiscovery_get_ServiceType_cstr((UpnpDiscovery*)Event);
        APP_LOG("WEMOHK", LOG_DEBUG, "serviceType %s", serviceType);
        if (strcmp(serviceType, "urn:Belkin:service:basicevent:1") != 0) {
            APP_LOG("WEMOHK", LOG_DEBUG, "ignore serviceType %s for alive", serviceType);
            break;
        }
    }
	case UPNP_DISCOVERY_SEARCH_RESULT: {
		UpnpDiscovery *d_event = (UpnpDiscovery *)Event;
		IXML_Document *DescDoc = NULL;
		const char *location = NULL;
		const char *deviceId = UpnpDiscovery_get_DeviceID_cstr(d_event);

        /* break early if the deviceId doesn't match */
        if (strlen(DeviceUDN) > 0) {
            if (strcasecmp(deviceId, DeviceUDN) != 0) {
                APP_LOG("WEMOHK", LOG_DEBUG, "DeviceID (%s) received doesn't match with %s", deviceId, DeviceUDN);
                break;
            }
        }

        int errCode = UpnpDiscovery_get_ErrCode(d_event);
		if (errCode != UPNP_E_SUCCESS) {
			APP_LOG("WEMOHK", LOG_DEBUG, "Error in Discovery Callback -- %d", errCode);
		}

		location = UpnpDiscovery_get_Location_cstr(d_event);
		errCode = UpnpDownloadXmlDoc(location, &DescDoc);
		if (errCode != UPNP_E_SUCCESS) {
			APP_LOG("WEMOHK", LOG_DEBUG, "Error obtaining device description from %s -- error = %d",
                    location, errCode);
		} else {
			wemoCtrlPointAddDevice(DescDoc, location, UpnpDiscovery_get_Expires(d_event));
		}

		if( DescDoc ) {
			ixmlDocument_free(DescDoc);
		}

		break;
	}

	case UPNP_DISCOVERY_SEARCH_TIMEOUT:
            if (GlobalDeviceList == NULL) {
                wemoCtrlPointRefresh();
            }
            else {
                wemoCtrlPointPrintList();
            }
	    break;

	case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE: {
		UpnpDiscovery *d_event = (UpnpDiscovery *)Event;
		int errCode = UpnpDiscovery_get_ErrCode(d_event);
		const char *deviceId = UpnpDiscovery_get_DeviceID_cstr(d_event);
        const char *deviceLocation = UpnpDiscovery_get_Location_cstr(d_event);

        /* break early if the deviceId doesn't match */
        if (strlen(DeviceUDN) > 0) {
            if (strcasecmp(deviceId, DeviceUDN) != 0) {
                APP_LOG("WEMOHK", LOG_DEBUG, "BYEBYE DeviceID (%s) received doesn't match with %s", deviceId, DeviceUDN);
                break;
            }
        }

		if (errCode != UPNP_E_SUCCESS) {
			APP_LOG("WEMOHK", LOG_DEBUG, "Error in Discovery ByeBye Callback -- %d", errCode);
		}

        if (strlen(deviceId)) {
            APP_LOG("WEMOHK", LOG_DEBUG, "Received ByeBye for Device: %s", deviceId);
            wemoCtrlPointRefresh();
        }
        else if (strlen(deviceLocation)) {
            APP_LOG("WEMOHK", LOG_DEBUG, "Received ByeBye for Device location: %s", deviceLocation);
            wemoCtrlPointRemoveDevicebyLocation(deviceLocation);
        }

		APP_LOG("WEMOHK", LOG_DEBUG, "After byebye:");
		wemoCtrlPointPrintList();

		break;
	}

	/* SOAP Stuff */
	case UPNP_CONTROL_ACTION_COMPLETE: {
	    UpnpActionComplete *a_event = (UpnpActionComplete *)Event;
	    int errCode = UpnpActionComplete_get_ErrCode(a_event);
	    if (errCode != UPNP_E_SUCCESS) {
                const char *ctrlUrl = UpnpActionComplete_get_CtrlUrl_cstr(a_event);
                char udn[NAME_SIZE];

                APP_LOG("WEMOHK", LOG_DEBUG, "Error in  Action Complete Callback -- %d", errCode);
            if (wemoCtrlPointFindUDNfromCtrlUrl(ctrlUrl, udn) != -1) {
                APP_LOG("WEMOHK", LOG_DEBUG, "Found problem on device UDN -- %s", udn);
                wemoCtrlPointRefresh();
            }
        }
        else {
            wemoCtrlPointHandleActionComplete(a_event);
        }
		/* No need for any processing here, just print out results.
		 * Service state table updates are handled by events. */

		break;
	}

	case UPNP_CONTROL_GET_VAR_COMPLETE: {
		UpnpStateVarComplete *sv_event = (UpnpStateVarComplete *)Event;
		int errCode = UpnpStateVarComplete_get_ErrCode(sv_event);
		if (errCode != UPNP_E_SUCCESS) {
			APP_LOG("WEMOHK", LOG_DEBUG, "Error in Get Var Complete Callback -- %d", errCode );
		} else {
			wemoCtrlPointHandleGetVar(
				UpnpStateVarComplete_get_CtrlUrl_cstr(sv_event),
				UpnpStateVarComplete_get_StateVarName_cstr(sv_event),
				UpnpStateVarComplete_get_CurrentVal(sv_event) );
		}
		break;
	}

	/* GENA Stuff */
	case UPNP_EVENT_RECEIVED: {
		UpnpEvent *e_event = (UpnpEvent *)Event;
		wemoCtrlPointHandleEvent(
			UpnpEvent_get_SID(e_event),
			UpnpEvent_get_EventKey(e_event),
			UpnpEvent_get_ChangedVariables(e_event));
		break;
	}

	case UPNP_EVENT_SUBSCRIBE_COMPLETE:
	case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
	case UPNP_EVENT_RENEWAL_COMPLETE: {
		UpnpEventSubscribe *es_event = (UpnpEventSubscribe *)Event;
		errCode = UpnpEventSubscribe_get_ErrCode(es_event);
		if (errCode != UPNP_E_SUCCESS) {
			APP_LOG("WEMOHK", LOG_DEBUG, "Error in Event Subscribe Callback -- %d", errCode);
		} else {
			wemoCtrlPointHandleSubscribeUpdate(
				UpnpEventSubscribe_get_PublisherUrl_cstr(es_event),
				UpnpEventSubscribe_get_SID_cstr(es_event),
				UpnpEventSubscribe_get_TimeOut(es_event));
		}

		break;
	}

	case UPNP_EVENT_AUTORENEWAL_FAILED:
	case UPNP_EVENT_SUBSCRIPTION_EXPIRED: {
		UpnpEventSubscribe *es_event = (UpnpEventSubscribe *)Event;
		int TimeOut = default_timeout;
		Upnp_SID newSID;

        memset(newSID, 0, sizeof(Upnp_SID));
		errCode = UpnpSubscribe(
			ctrlpt_handle,
			UpnpEventSubscribe_get_PublisherUrl_cstr(es_event),
			&TimeOut,
			newSID);

		if (errCode == UPNP_E_SUCCESS) {
			wemoCtrlPointHandleSubscribeUpdate(
				UpnpEventSubscribe_get_PublisherUrl_cstr(es_event),
				newSID,
				TimeOut);
		}
		break;
	}

	/* ignore these cases, since this is not a device */
	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
	case UPNP_CONTROL_GET_VAR_REQUEST:
	case UPNP_CONTROL_ACTION_REQUEST:
		break;
	}

	return 0;
}

/********************************************************************************
 * wemoCtrlPointVerifyTimeouts
 *
 * Description:
 *       Checks the advertisement  each device
 *        in the global device list.  If an advertisement expires,
 *       the device is removed from the list.  If an advertisement is about to
 *       expire, a search request is sent for that device.
 *
 * Parameters:
 *    incr -- The increment to subtract from the timeouts each time the
 *            function is called.
 *
 ********************************************************************************/
void
wemoCtrlPointVerifyTimeouts( int incr )
{
    struct wemoDeviceNode *prevdevnode,
     *curdevnode;
    int ret;

    ithread_mutex_lock( &DeviceListMutex );

    prevdevnode = NULL;
    curdevnode = GlobalDeviceList;

    while( curdevnode ) {
        curdevnode->device.AdvrTimeOut -= incr;
        // APP_LOG("WEMOHK", LOG_DEBUG, "Advertisement Timeout: %d", curdevnode->device.AdvrTimeOut);

        if( curdevnode->device.AdvrTimeOut <= 0 ) {
            /*
               This advertisement has expired, so we should remove the device
               from the list
             */

            if( GlobalDeviceList == curdevnode )
                GlobalDeviceList = curdevnode->next;
            else
                prevdevnode->next = curdevnode->next;
            wemoCtrlPointDeleteNode( curdevnode );
            if( prevdevnode )
                curdevnode = prevdevnode->next;
            else
                curdevnode = GlobalDeviceList;
        } else {

            if( curdevnode->device.AdvrTimeOut < 2 * incr ) {
                /*
                   This advertisement is about to expire, so send
                   out a search request for this device UDN to
                   try to renew
                 */
                ret = UpnpSearchAsync( ctrlpt_handle, incr, targetServiceType, NULL );
                if( ret != UPNP_E_SUCCESS )
                      APP_LOG("WEMOHK", LOG_DEBUG, "Error sending search request -- err = %d", ret);
            }
            prevdevnode = curdevnode;
            curdevnode = curdevnode->next;
        }

    }
    ithread_mutex_unlock( &DeviceListMutex );

}

static int in_cksum(unsigned short *buf, int sz)
{
        int nleft = sz;
        int sum = 0;
        unsigned short *w = buf;
        unsigned short ans = 0;

        while (nleft > 1) {
                sum += *w++;
                nleft -= 2;
        }

        if (nleft == 1) {
                *(unsigned char *) (&ans) = *(unsigned char *) w;
                sum += ans;
        }

        sum = (sum >> 16) + (sum & 0xFFFF);
        sum += (sum >> 16);
        ans = ~sum;
        return (ans);
}

#define DEFDATALEN      56
#define MAXIPLEN        60
#define MAXICMPLEN      76

int is_dev_alive(char *ipaddr)
{
    struct sockaddr_in pingaddr;
    struct in_addr inaddr;
    struct icmp *pkt;
    int pingsock, c;
    char packet[DEFDATALEN + MAXIPLEN + MAXICMPLEN];
    struct timeval tv;

    if ((pingsock = socket(AF_INET, SOCK_RAW, 1)) < 0) {       /* 1 == ICMP */
        APP_LOG("WEMOHK", LOG_DEBUG, "ping: creating a raw socket");
        return 0;
    }

    /* drop root privs if running setuid */
    setuid(getuid());

    memset(&pingaddr, 0, sizeof(struct sockaddr_in));

    pingaddr.sin_family = AF_INET;
    if (!(inet_aton(ipaddr, &inaddr))) {
        APP_LOG("WEMOHK", LOG_DEBUG, "ping: formed ip: %s", ipaddr);
        close(pingsock);
        return 0;
    }
    memcpy(&pingaddr.sin_addr, &inaddr, sizeof(pingaddr.sin_addr));

    pkt = (struct icmp *) packet;
    memset(pkt, 0, sizeof(packet));
    pkt->icmp_type = ICMP_ECHO;
    pkt->icmp_cksum = in_cksum((unsigned short *) pkt, sizeof(packet));

    c = sendto(pingsock, packet, sizeof(packet), 0,
               (struct sockaddr *) &pingaddr, sizeof(struct sockaddr_in));

    if (c < 0 || c != sizeof(packet)) {
        if (c < 0)
            APP_LOG("WEMOHK", LOG_DEBUG, "ping: sendto");
        APP_LOG("WEMOHK", LOG_DEBUG, "ping: write incomplete");
        close(pingsock);
        return 0;
    }

    /* Setting waiting time for receive function to 5s */
    tv.tv_sec = 2; // 2s timeout
    tv.tv_usec = 0;
    setsockopt(pingsock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));

    /* listen for replies */
    while (1) {
        struct sockaddr_in from;
        size_t fromlen = sizeof(from);
        if ((c = recvfrom(pingsock, packet, sizeof(packet), 0,
                          (struct sockaddr *) &from, (socklen_t *)&fromlen)) < 0) {
            if (errno == EINTR)
                continue;
            // APP_LOG("WEMOHK", LOG_DEBUG, "ERROR: in receive ICMP from %s", host);
            close(pingsock);
            return 0;
        }
        if (c >= 76) {                   /* ip + icmp */
            struct iphdr *iphdr = (struct iphdr *) packet;
            pkt = (struct icmp *) (packet + (iphdr->ihl << 2));      /* skip ip hdr */
            if ((pkt->icmp_type == ICMP_ECHOREPLY) && (pingaddr.sin_addr.s_addr == iphdr->saddr)) {
                // APP_LOG("WEMOHK", LOG_DEBUG,"ping: got reply from %s........", host);
                break;
            } else {
                // APP_LOG("WEMOHK", LOG_DEBUG,"still waiting for ICMP ECHOREPLY from %s", host);
                continue;
            }
        }
    }
    close(pingsock);
    return 1;
}

void *wemoCtrlPointCheckDev(void *args)
{
    struct wemoDevice device;
    int ret;
    int tries = 3;
    int i = 0;

    memcpy(&device, (struct wemoDevice *) args, sizeof(struct wemoDevice));

    if (strlen(device.ipaddr) != 0) {
        for (i = 0; i < tries; i++) {
            if (is_dev_alive(device.ipaddr)) {
                return NULL;
            }
        }

        /* if we're here then it means ping failed,
           try to renew the subscription again */
        /* giving second chance */
        int TimeOut = default_timeout;
        ret = UpnpRenewSubscription(ctrlpt_handle,
                                    &TimeOut,
                                    device.wemoService[WEMO_SERVICE_BASICEVENT].SID);

        if( ret != UPNP_E_SUCCESS ) {
            APP_LOG("WEMOHK", LOG_DEBUG, "Error sending renew subscription for Device UDN: %s -- err = %d",
                              device.UDN, ret);
            wemoCtrlPointRefresh();
        }
    }
    return NULL;
}
/********************************************************************************
 * wemoCtrlPointTimerLoop
 *
 * Description:
 *       Function that runs in its own thread and monitors advertisement
 *       and subscription timeouts for devices in the global device list.
 *
 * Parameters:
 *    None
 *
 ********************************************************************************/
static int wemoCtrlPointTimerLoopRun = 1;
void *wemoCtrlPointTimerLoop(void *args)
{
    int incr = 30;              // how often to verify the timeouts, in seconds

    while (wemoCtrlPointTimerLoopRun) {
        isleep( incr );
        wemoCtrlPointVerifyTimeouts( incr );
    }

    return NULL;
}

static int wemoCtrlRetrieveSerialNumber(char *sn)
{
    FILE *pipe = NULL;
    char cmd_buffer[128];
    int value_length;
    char buffer[256];

    memset(cmd_buffer, 0, 128);
    sprintf(cmd_buffer, "nvram_get SerialNumber");
    pipe = popen(cmd_buffer, "r");
    if (pipe == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "popen error %s", strerror(errno));
        return 0;
    }
    memset(buffer, 0, 256);
    if (!fread(buffer, sizeof(char), 256, pipe)) {
        APP_LOG("WEMOHK", LOG_ERR, "nvram key - SerialNumber not set");
        pclose(pipe);
        return 0;
    }

    strcpy(sn, buffer);
    value_length = strlen(sn);
    if (sn[value_length - 1] == '\n') {
        sn[value_length - 1] = 0;
    }
    pclose(pipe);
    return value_length;
}

static int wemoCtrlRetrieveDeviceUDN(char *udn)
{
    FILE *pipe = NULL;
    char cmd_buffer[128];
    int value_length;
    char buffer[256];

    memset(cmd_buffer, 0, 128);
    sprintf(cmd_buffer, "nvram_get DeviceUDN");
    pipe = popen(cmd_buffer, "r");
    if (pipe == NULL) {
        APP_LOG("WEMOHK", LOG_ERR, "popen error %s", strerror(errno));
        return 0;
    }
    memset(buffer, 0, 256);
    if (!fread(buffer, sizeof(char), 256, pipe)) {
        APP_LOG("WEMOHK", LOG_ERR, "nvram key - SerialNumber not set.");
        pclose(pipe);
        return 0;
    }

    strcpy(udn, buffer);
    value_length = strlen(udn);
    if (udn[value_length - 1] == '\n') {
        udn[value_length - 1] = 0;
    }
    pclose(pipe);
    return value_length;
}

/********************************************************************************
 * wemoCtrlPointStart
 *
 * Description:
 *		Call this function to initialize the UPnP library and start the wemo Control
 *		Point.  This function creates a timer thread and provides a callback
 *		handler to process any UPnP events that are received.
 *
 * Parameters:
 *		None
 *
 * Returns:
 *		CTRLPT_SUCCESS if everything went well, else CTRLPT_ERROR
 *
 ********************************************************************************/
int wemoCtrlPointStart(char *ifname, print_string printFunctionPtr, state_update updateFunctionPtr)
{
	ithread_t timer_thread;
	int rc;
	unsigned short port = 49155;
    const char *ip_address = "127.0.0.1";

    if (wemoCtrlRetrieveSerialNumber(serial_number) <= 0) {
        return CTRLPT_ERROR;
    }

    if (wemoCtrlRetrieveDeviceUDN(DeviceUDN) <= 0) {
        APP_LOG("WEMOHK", LOG_DEBUG, "Device UDN not in nvram...");
    }

	ctrlpt_util_Initialize(printFunctionPtr);
	ctrlpt_util_RegisterUpdateFunction(updateFunctionPtr);

	ithread_mutex_init(&DeviceListMutex, 0);
    APP_LOG("WEMOHK", LOG_INFO, "Initializing UPnP Sdk with ip_address = %s port = %u",
                      ip_address, port);

    rc = UpnpInit2("lo", port, NULL);
	if (rc != UPNP_E_SUCCESS) {
		APP_LOG("WEMOHK", LOG_DEBUG, "UpnpInit() Error: %d", rc);
		UpnpFinish();
		return CTRLPT_ERROR;
	}
	if (!ip_address) {
		ip_address = UpnpGetServerIpAddress();
	}
	if (!port) {
		port = UpnpGetServerPort();
	}

	APP_LOG("WEMOHK", LOG_INFO, "UPnP Initialized ipaddress= %s port = %u", ip_address, port);

	APP_LOG("WEMOHK", LOG_DEBUG, "Registering Control Point");
	rc = UpnpRegisterClient((Upnp_FunPtr) wemoCtrlPointCallbackEventHandler,
		&ctrlpt_handle, &ctrlpt_handle);
	if (rc != UPNP_E_SUCCESS) {
		APP_LOG("WEMOHK", LOG_DEBUG, "Error registering CP: %d", rc );
		UpnpFinish();

		return CTRLPT_ERROR;
	}

	APP_LOG("WEMOHK", LOG_INFO, "Control Point Registered");

	wemoCtrlPointRefresh();

	/* start a timer thread */
	ithread_create(&timer_thread, NULL, wemoCtrlPointTimerLoop, NULL);
	ithread_detach(timer_thread);

	return CTRLPT_SUCCESS;
}

int wemoCtrlPointStop(void)
{
	wemoCtrlPointTimerLoopRun = 0;
	wemoCtrlPointRemoveAll();
	UpnpUnRegisterClient( ctrlpt_handle );
	UpnpFinish();
	ctrlpt_util_Finish();

	return CTRLPT_SUCCESS;
}
