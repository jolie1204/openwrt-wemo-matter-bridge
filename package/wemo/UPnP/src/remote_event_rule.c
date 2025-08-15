/***************************************************************************
*
*
* remote_event_rule.c
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
#include "remote_event_rule.h"
#include <sys/time.h>
#include "utils.h"
#include "osUtils.h"
#include "WemoDB.h"
#include "logger.h"
#include "controlledevice.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */


static pthread_mutex_t   s_event_mutex;
extern sqlite3 *g_RulesDB;

/* creates lock for event subscription list */
void createSubsListLock()
{
    osUtilsCreateLock(&s_event_mutex);
}

/* takes lock for event subscription list */
void lockSubsList()
{
    osUtilsGetLock(&s_event_mutex);
}

/* releases lock for event subscription list */
void unlockSubsList()
{
    osUtilsReleaseLock(&s_event_mutex);
}

/* destroys lock for event subscription list */
void destroySubsListLock()
{
    osUtilsDestroyLock(&s_event_mutex);
}

/*
 * This function manages event subscriptions
 * when rule engine reloads this function is called to check if any new subscription is required to trigger rule
 * pSubsList : List of existing subscriptions
*/
void ManageEventSubscriptionRequest(LinkedList *pSubsList)
{
    int rowsRules=0,colsRules=0;
    char **ppsRulesArray=NULL;
    char query[SIZE_256B+SIZE_64B];
    int i=0,pendingSubs=0;
    if(!pSubsList) {
        APP_LOG("DEVICE:rule",LOG_DEBUG,"oops no list for subscription.");
        return;
    }
    /*get all event information for all rule ids which are enabled*/
    memset(query, 0, sizeof(query));
    sqlite3_snprintf(sizeof(query), query, "select DISTINCT rd1.DeviceID,rd1.Type,rd1.ZBCapabilityStart from ruledevices rd1,rules r,ruledevices rd where rd1.RuleId=r.RuleId and r.ruleid=rd.ruleid and r.Type='Event Rule' and r.State=1 and rd.DeviceID='%q' and rd1.DayID=-1",g_szUDN_1);
    APP_LOG("DEVICE:rule", LOG_DEBUG, "query:%s", query);
    /*execute database query to get all events (nest id,type,value) for active rules */
    if(!WeMoDBGetTableData(&g_RulesDB, query, &ppsRulesArray,&rowsRules,&colsRules)) {
        /*check if we got the data*/
        if(!rowsRules || !colsRules) {
            APP_LOG("DEVICE:rule", LOG_DEBUG, "No Event Rule");
            /*free database buffer*/
            WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
            /* WEMO-49374 No rules for Events, so remove all subscription; no cloud unsubscription is sent*/
            emptySubscriptionList(pSubsList);
            return ;
        } else {

            for(i=colsRules; i<((rowsRules +1)*colsRules); i+=colsRules) {
                APP_LOG("DEVICE:rule",LOG_DEBUG,"Producer:%s,EventType:%s,EventValue:%s \n",ppsRulesArray[i],ppsRulesArray[i+1],ppsRulesArray[i+2]);

                if(ppsRulesArray[i] && ppsRulesArray[i+1] && ppsRulesArray[i+2]) {
                    if(updateSubscription(pSubsList,ppsRulesArray[i],atoi(ppsRulesArray[i+1]),ppsRulesArray[i+2],SUBSCRIBE)==NOT_FOUND) {
                        APP_LOG("DEVICE:rule",LOG_DEBUG,"Adding New Subscription");
                        pendingSubs++;
                    } else {
                        APP_LOG("DEVICE:rule",LOG_DEBUG,"Subscription Found");
                    }
                }
            }
        }
    } else {
        APP_LOG("DEVICE:rule", LOG_ERR, "No target devices");
        return ;
    }

    /* free database table result */
    WeMoDBTableFreeResult(&ppsRulesArray,&rowsRules,&colsRules);
}

/*
* update all node in subscribing state to subscribed state
* pSubsList : subscription List
*/
void updateAllSubscribedNodeInEventList(LinkedList *pSubsList)
{
    ListNode *pNode = NULL;
    SEventSubsNode *event_list = NULL;
    lockSubsList();
    pNode = ListHead( pSubsList);
    while( pNode != NULL) {
        event_list = (SEventSubsNode *)(pNode->item);

        if(event_list->status == SUBSCRIBING) {
            event_list->status = SUBSCRIBED;
        }
        pNode = ListNext( pSubsList, pNode);
    }
    unlockSubsList();
}

/*
* deletes all node in unsubscribing state
* pSubsList : subscription List
*/
void deleteAllUnsubscribedNodeInEventList(LinkedList *pSubsList)
{
    ListNode *pNode = NULL,*pNodePrev = NULL;
    SEventSubsNode *event_list = NULL;
    lockSubsList();
    pNode = ListHead( pSubsList);
    while( pNode != NULL) {
        event_list = (SEventSubsNode *)(pNode->item);

        if(event_list->status == UNSUBSCRIBING) {
            ListDelNode(pSubsList,pNode,1);
            pNode= pNodePrev;
        }
        pNodePrev = pNode;
        pNode = ListNext( pSubsList, pNode);
    }
    unlockSubsList();
}

/*
* generates xml for subscription or unsubscription
* node with status1 or status2 in pSubsList will be added in XML and update these node's status as status2 in list
* buffer : buffer for storing XML
* length : length of buffer
* pSubsList : list of subscription
* status1 : SUBSCRIBE/UNSUBSCRIBE
* status2 : SUBSCRIBING/UNSUBSCRIBING
*/
int generateXMLRequestForEventSubscription(char * buffer,int length,LinkedList *pSubsList,int status1,int status2)
{
    ListNode *pNode = NULL;
    SEventSubsNode *event_list = NULL;
    int len_used = 0,ret=0,end_xml_len = strlen("</deviceEventSubsrciptions>");
    int new_sub=0;
    len_used = snprintf(buffer,length,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><deviceEventSubsrciptions>");
    lockSubsList();
    pNode = ListHead( pSubsList);
    while( pNode != NULL) {
        event_list = (SEventSubsNode *)(pNode->item);
        if(event_list->status == status1 || event_list->status == status2) {
            /* add next (un)subscription */
            ret= snprintf(buffer+len_used,length-len_used,"<deviceEventSubsrciption><producerId>%s</producerId><consumerId>%s</consumerId><eventType>%d</eventType><eventValue>%s</eventValue></deviceEventSubsrciption>",event_list->eventProducer,g_szWiFiMacAddress,event_list->eventType,event_list->eventValue);
            /* did we able to add this subscription also in our string limit */
            if((len_used + ret+end_xml_len+1) >= length) {
                /* can't accomadate this (un)subscription, so don't update length and count */
                break;
            } else {
                /* can accomadate this (un)subscription, so update length and count */
                len_used += ret;
                new_sub++;
                event_list->status = status2;
            }
        }
        pNode = ListNext( pSubsList, pNode);
    }
    unlockSubsList();
    snprintf(buffer+len_used,length-len_used,"</deviceEventSubsrciptions>");
    /* no node with status1 or status2 */
    if(!new_sub)
        return NO_XML;
    /* able to accomodate only few (un)subscription in buffer*/
    if(pNode)
        return PARTIAL_XML;
    /* all (un)subscriptions added in buffer */
    else
        return FULL_XML;
}

/*
 * This Function adds new subscription in List with given status
 * pSubslist : List of subscriptions
 * producer : producer of event
 * type : Type of event (home/away, rush hour etc..)
 * value : value of event (home, away, temperature, etc..)
 * status : status of new node in list
*/
void addNewSubscription(LinkedList *pSubsList, char *producer,int type, char *value, ESubsStatus status)
{
    SEventSubsNode *newSub = NULL;
    newSub = MALLOC(sizeof(SEventSubsNode));
    strncpy(newSub->eventProducer,producer,sizeof(newSub->eventProducer));
    newSub->eventType = type;
    strncpy(newSub->eventValue,value,sizeof(newSub->eventValue));
    newSub->status = status;
    if (ListAddHead(pSubsList,newSub) == NULL) {
        APP_LOG("DEVICE:rule",LOG_DEBUG,"Not able to add new event in subscription List");
        resetSystem();
    }
}

/*
 * This Function remove all subscription from list
*/
void emptySubscriptionList(LinkedList *pSubsList)
{
    ListNode *pNode,*pNodeTmp;
    SEventSubsNode *event = NULL;
    lockSubsList();
    pNode = ListHead( pSubsList);
    while( pNode != NULL) {
        event = (SEventSubsNode *)(pNode->item);
        APP_LOG("DEVICE:rule",LOG_DEBUG,"Deleting %s %d %s event",event->eventProducer,event->eventType,event->eventValue);
        pNodeTmp = pNode;

        pNode = ListNext( pSubsList, pNode);
        ListDelNode(pSubsList,pNodeTmp,1);
    }
    unlockSubsList();
}

/*
 * This Function updates subscription in List for given event to status (not in case event is availble and status to be set is SUBSCIBE ); if event unavailable add it
 * pSubslist : List of subscriptions
 * producer : producer of event
 * type : Type of event (home/away, rush hour etc..)
 * value : value of event (home, away, temperature, etc..)
 * status : update node with given status to this or add event with this status
*/
int updateSubscription(LinkedList *pSubsList, char *producer,int type, char *value, int status)
{
    ListNode *pNode;
    SEventSubsNode *event = NULL;
    lockSubsList();
    int retVal = NOT_FOUND;
    pNode = ListHead( pSubsList);
    while( pNode != NULL) {
        event = (SEventSubsNode *)(pNode->item);
        /* check if we have already subscribed to this event */
        if((!strncmp(producer,event->eventProducer,SIZE_64B)) && (type==event->eventType) && (!strncmp(value,event->eventValue,MAX_EVENT_VALUE))) {
            break;
        }
        pNode = ListNext( pSubsList, pNode);
    }
    if(pNode ==NULL) {
        /* add new node with given status in list */
        addNewSubscription(pSubsList,producer,type,value,status);
    } else {
        retVal = FOUND;
        /* status to be set is SUBSCRIBE no need to do anything, if node exist */
        if(status == UNSUBSCRIBE) {
            if(event->status == SUBSCRIBED)
                event->status = UNSUBSCRIBE;
            /*should not reach to below condition as unsubcription is only when an event comes for which rule doesn't exist */
            else if (event->status == SUBSCRIBE)
                ListDelNode(pSubsList,pNode,1);
        }
    }
    unlockSubsList();
    return retVal;
}
