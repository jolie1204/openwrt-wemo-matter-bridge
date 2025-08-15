/***************************************************************************
*
*
* remote_event_rule.h
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
#ifndef _REMOTE_EVENT_RULE_H_
#define _REMOTE_EVENT_RULE_H_

#include <stdio.h>
#include "LinkedList.h"
#include "wemodefs.h"
#include "rule.h"

#define FOUND 1
#define NOT_FOUND 0

extern char g_szWiFiMacAddress[SIZE_64B];

extern void performSubsForEventrule(LinkedList *pSubsList,int count);
void addNewSubscription(LinkedList *pSubsList, char *producer,int type, char *value, ESubsStatus status);
int updateSubscription(LinkedList *pSubsList, char *producer,int type, char *value,int status);

/* creates lock for event subscription list */
void createSubsListLock();

/* takes lock for event subscription list */
void lockSubsList();

/* releases lock for event subscription list */
void unlockSubsList();

/* destroys lock for event subscription list */
void destroySubsListLock();

/*
 * This function manages event subscriptions
 * when rule engine reloads this function is called to check if any new subscription is required to trigger rule
 * pSubsList : List of existing subscriptions
*/
void ManageEventSubscriptionRequest(LinkedList *pSubsList);

/*
* update all node in subscribing state to subscribed state
* pSubsList : subscription List
*/
void updateAllSubscribedNodeInEventList(LinkedList *pSubsList);

/*
* deletes all node in unsubscribing state
* pSubsList : subscription List
*/
void deleteAllUnsubscribedNodeInEventList(LinkedList *pSubsList);

/*
* generates xml for subscription or unsubscription
* node with status1 or status2 in pSubsList will be added in XML and update these node's status as status2 in list
* buffer : buffer for storing XML
* length : length of buffer
* pSubsList : list of subscription
* status1 : SUBSCRIBE/UNSUBSCRIBE
* status2 : SUBSCRIBING/UNSUBSCRIBING
*/
int generateXMLRequestForEventSubscription(char * buffer,int length,LinkedList *pSubsList,int status1,int status2);

/*
 * This Function adds new subscription in List with given status
 * pSubslist : List of subscriptions
 * producer : producer of event
 * type : Type of event (home/away, rush hour etc..)
 * value : value of event (home, away, temperature, etc..)
 * status : status of new node in list
*/
void addNewSubscription(LinkedList *pSubsList, char *producer,int type, char *value, ESubsStatus status);

/*
 * This Function remove all subscription from list
*/
void emptySubscriptionList(LinkedList *pSubsList);

/*
 * This Function updates subscription in List for given event to status (not in case event is availble and status to be set is SUBSCIBE ); if event unavailable add it
 * pSubslist : List of subscriptions
 * producer : producer of event
 * type : Type of event (home/away, rush hour etc..)
 * value : value of event (home, away, temperature, etc..)
 * status : update node with given status to this or add event with this status
*/
int updateSubscription(LinkedList *pSubsList, char *producer,int type, char *value, int status);

#endif
