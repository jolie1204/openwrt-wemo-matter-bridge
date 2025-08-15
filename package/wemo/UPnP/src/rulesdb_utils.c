/***************************************************************************
 *
 *
 * rulesdb_utils.c
 *
 * Created by Belkin International, Software Engineering on May 27, 2011
 * Copyright (c) 2012-2014 Belkin International, Inc. and/or its affiliates.
 * All rights reserved.
 *
 * Belkin International, Inc. retains all right, title and interest (including
 * all intellectual property rights) in and to this computer program, which
 * is protected by applicable intellectual property laws.  Unless you have
 * obtained a separate written license from Belkin International, Inc., you are
 * not authorized to utilize all or a part of this computer program for any
 * purpose (including reproduction, distribution, modification, and compilation
 * into object code) and you must immediately destroy or return to Belkin
 * International, Inc all copies of this computer program.  If you are licensed
 * by Belkin International, Inc., your rights to utilize this computer program
 * are limited by the terms of that license.
 *
 * To obtain a license, please contact Belkin International, Inc.
 *
 * This computer program contains trade secrets owned by Belkin International,
 * Inc. and, unless unauthorized by Belkin International, Inc. in writing, you
 * agree to maintain the confidentiality of this computer program and related
 * information and to not disclose this computer program and related information
 * to any other person or entity.
 *
 * THIS COMPUTER PROGRAM IS PROVIDED AS IS WITHOUT ANY WARRANTIES, AND BELKIN
 * INTERNATIONAL, INC. EXPRESSLY DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED,
 * INCLUDING THE WARRANTIES OF MERCHANTIBILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, TITLE, AND NON-INFRINGEMENT.
 *
 *
 ***************************************************************************/
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "sqlite3.h"
#include "WemoDB.h"

#include "logger.h"
#include "rulesdb_utils.h"
#include "wemodefs.h"
#include "mxml.h"
#include "rule.h"
#include "controlledevice.h"
#ifdef _OPENWRT_
#include "belkin_api.h"
#else
#include "gemtek_api.h"
#endif
#include "wifiHndlr.h"
#include <belkin_diag.h>  /* Run-time diagnostics.  Keep as last include. */

#define RULE_TMPDB_FILE_PATH "/tmp/tmprule.db"

static int validate_ruledb(char *filename)
{
    struct stat file_info;
    char command[128] = {0};

    sqlite3 *ruleDB = NULL;
    char query[SIZE_256B];

    int rows = 0, cols = 0;
    char **ruleArray = NULL;

    if (stat(filename, &file_info) == -1) {
        APP_LOG ("RulesDBUtils", LOG_ERR, "Temporary Rule DB (%s) doesn't exist.", filename);
        return FAILURE;
    }

    sprintf(command, "unzip -p %s > /tmp/tmprules.db", filename);
    system(command);

    if (stat("/tmp/tmprules.db", &file_info) == -1) {
        APP_LOG ("RulesDBUtils", LOG_ERR, "Temporary Rule DB unzipping failed.");
        return FAILURE;
    }

    if (InitDB("/tmp/tmprules.db", &ruleDB)) {
        APP_LOG ("RulesDBUtils", LOG_ERR, "Opening Temporary Rule DB failed.");
        return FAILURE;
    }

    memset(query, 0, sizeof(query));
    snprintf(query, sizeof(query), "SELECT Type, RuleID FROM RULES WHERE STATE='1';");

    if (!WeMoDBGetTableData(&ruleDB, query, &ruleArray, &rows, &cols)) {
        if((rows == 0) || (cols == 0)) {
            WeMoDBTableFreeResult(&ruleArray, &rows, &cols);
            APP_LOG ("RulesDBUtils", LOG_INFO, "Temporary Rule DB empty (rows = %d, cols = %d)?", rows, cols);
        }
    }
    else {
        CloseDB(ruleDB);
        APP_LOG ("RulesDBUtils", LOG_ERR, "Temporary Rule DB has no Table data.");
        return FAILURE;
    }

    APP_LOG ("RulesDBUtils", LOG_INFO, "Temporary Rule DB validation succeeded.");
    CloseDB(ruleDB);
    return SUCCESS;
}

/**
 * @brief  DecodeRuleDbData: Decodes the XML received as part of the StoreRules
 *                           action and carries out the following:
 *                           a) Saves the rules db to its relevant location.
 *                           b) Sets the rule db version.
 *                           c) Processes the db based on the processDB field.
 * @param  sRuleDbData [IN] - RuleDbData XML string.
 *
 * @return retVal: 0: On Success.
 *                -1: On Failure
 *
 * @author Christopher A F
 */
int DecodeRuleDbData (char *sRuleDbData)
{
    int              retVal                   = 0;
    int              processDb                = -1;

    unsigned int     len                      = 0;
    unsigned int     ruleDbVersion            = 0;
    unsigned int     temp                     = 0;

    char             aRuleDbVersion[SIZE_32B] = {0};
    char            *pStr                     = NULL;
    char            *pBody                    = NULL;
    char            *pDec                     = NULL;

    mxml_node_t     *pTree                    = NULL;
    mxml_node_t     *pChildNode               = NULL;
    mxml_node_t     *pTempNode                = NULL;

    FILE            *pFile                    = NULL;

    char command[128] = {0};

    UTL_LOG ("RulesDBUtils", LOG_DEBUG, "***** Entered *****");

    /* Input parameter validation */
    if ((NULL == sRuleDbData) || (0 == (len = strlen (sRuleDbData)))) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "Invalid arguments");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    UTL_LOG ("RulesDBUtils", LOG_DEBUG, "len: %d, sRuleDbData = %s", len, sRuleDbData);

    pTree = mxmlLoadString (NULL, sRuleDbData, MXML_OPAQUE_CALLBACK);
    if (NULL == pTree) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "mxmlLoadString Error: sRuleDbData");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    /* BEGIN: Extract the rule db version */
    pChildNode = mxmlFindElement (pTree, pTree, "ruleDbVersion", NULL,
                                  NULL, MXML_DESCEND);
    if (NULL == pChildNode) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "ruleDbVersion: mxmlFindElement Error");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    pStr = (char *) mxmlGetOpaque (pChildNode);
    if ((NULL == pStr) || (0 == strlen (pStr))) {
        APP_LOG ("RulesDBUtils", LOG_ERR, "mxmlGetOpaque Error: ruleDbVersion");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    /* Update the Rules DB Version */
    ruleDbVersion= atoi (pStr);

#ifdef RULESDB_UTILS_DBG
    UTL_LOG ("RulesDBUtils", LOG_DEBUG, "%d: <MXML> ruleDbVersion: %d"
             " <MXML>", __LINE__, ruleDbVersion);
#endif
    /* END: Extract the rule db version */

    /* BEGIN: Extract the process db value */
    pChildNode = mxmlFindElement (pTree, pTree, "processDb", NULL,
                                  NULL, MXML_DESCEND);
    if (NULL == pChildNode) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "mxmlFindElement Error processDb");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    pStr = (char *) mxmlGetOpaque (pChildNode);
    if ((NULL == pStr) || (0 == strlen (pStr))) {
        APP_LOG ("RulesDBUtils", LOG_ERR, "mxmlGetOpaque Error processDb");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    /* Update the process db value */
    processDb = atoi (pStr);

#ifdef RULESDB_UTILS_DBG
    UTL_LOG ("RulesDBUtils", LOG_DEBUG, "%d: <MXML> processDb: %d"
             " <MXML>", __LINE__, processDb);
#endif
    /* END: Extract the process db value */

    /* BEGIN: Extract the body */
    pChildNode = mxmlFindElement (pTree, pTree, "ruleDbBody",
                                  NULL, NULL, MXML_DESCEND);
    if (NULL == pChildNode) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "mxmlFindElement Error ruleDbBody");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    pTempNode = mxmlWalkNext (pChildNode, pTree, MXML_DESCEND);
    if ((NULL == pTempNode) || (0 == (len  = strlen (pTempNode->value.opaque)))) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "mxmlWalkNext Error ruleDbBody");
        retVal = -1;
        goto CLEAN_RETURN;
    } else
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "len: %d, ruleDbBody: %s", len, pTempNode->value.opaque);

    /* extract data within CDATA */
    pStr = strstr (pTempNode->value.opaque, CDATA_PATTERN);
    if (NULL == pStr) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "body doesn't include CDATA");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    len  -= (CDATA_PATTERN_PREFIX_LEN + CDATA_PATTERN_SUFFIX_LEN);
    pBody = (char *) MALLOC (len+1);

    strncpy (pBody, ((char *)pTempNode->value.opaque + CDATA_PATTERN_PREFIX_LEN), len);

#ifdef RULESDB_UTILS_DBG
    UTL_LOG ("RulesDBUtils", LOG_DEBUG, "len: %d, pBody: %s", strlen(pBody), pBody);
#endif

    pDec = base64Decode (pBody, &len);
    if (NULL == pDec) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "base64Decode Error");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    /* Write the contents to a file namely rules.db */
    pFile = fopen (RULE_TMPDB_FILE_PATH, "wb");
    if (NULL == pFile) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "fopen() Error");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    temp = fwrite (pDec, sizeof (char), len, pFile);

    UTL_LOG ("RulesDBUtils", LOG_DEBUG, "%d: temp = %d, len = %d",
             __LINE__, temp, len);

    if (len != temp) {
        APP_LOG ("UPNPDevice", LOG_DEBUG, "%d: fwrite() Error", __LINE__);
        retVal = -1;
        fclose (pFile);
        goto CLEAN_RETURN;
    }

    fclose (pFile);

    if (validate_ruledb(RULE_TMPDB_FILE_PATH) == SUCCESS) {
        sprintf(command, "cp -f %s %s", RULE_TMPDB_FILE_PATH, RULE_DB_FILE_PATH);
        system(command);
    }
    else {
        unlink(RULE_TMPDB_FILE_PATH);
        APP_LOG("UPNPDevice", LOG_DEBUG, "RULE DB validation failed!");
        retVal = -1;
        goto CLEAN_RETURN;
    }

    /* Update the Rules DB Version */
    snprintf (aRuleDbVersion, sizeof (aRuleDbVersion), "%u", ruleDbVersion);
    UTL_LOG ("RulesDBUtils", LOG_DEBUG, "%d: aRuleDbVersion: %s",
             __LINE__, aRuleDbVersion);
    SetBelkinParameter (RULE_DB_VERSION_KEY, aRuleDbVersion);
    AsyncSaveData();
    RuleDBVersionNotify();

    if (processDb) {
        gRestartRuleEngine = RULE_ENGINE_RELOAD;
    }

CLEAN_RETURN:
    if (NULL != pTree) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "free pTree = [%p]", pTree);
        mxmlDelete (pTree);
        pTree = NULL;
    }

    if (NULL != pBody) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "free pBody = [%p]", pBody);
        free (pBody);
        pBody = NULL;
    }

    if (NULL != pDec) {
        UTL_LOG ("RulesDBUtils", LOG_DEBUG, "free pDec = [%p]", pDec);
        free (pDec);
        pDec = NULL;
    }

    return retVal;
}
