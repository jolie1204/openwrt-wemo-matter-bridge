/***************************************************************************
*
*
* WemoDB.c
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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wemo_device_db.h"

#define WEMO_DEVICE_ENTRIES 9
#define STATE_ENTRIES 4

extern char wemo_device_db[];
extern char wemo_state_db[];

static int update_state_db_callback(void *data, int argc, char **argv, char **colName)
{
    sqlite3 *state_db;
    int i;
    state_db = (sqlite3 *) data;

    for (i = 0; i < argc; i++) {
        if(!strcmp("wemo_id", colName[i])) {
            wemo_dev_statedb_insert(state_db, atoi(argv[i]), 0, "1=0");
        }
    }
    return 0;
}

int wemo_dev_init_state_db(sqlite3 *dev_db, sqlite3 *state_db)
{
    char *sql = "SELECT wemo_id from wemo_device";
    char *errmsg = NULL;
    int rc;

    rc = sqlite3_exec(dev_db, sql, update_state_db_callback, state_db, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL: %s\n", errmsg);
        return DB_ERROR;
    }
    return DB_SUCCESS;
}

int wemo_dev_db_init(sqlite3 **dev_db, sqlite3 **state_db)
{
	struct stat db_file;

	char *device_table = "wemo_device";
	char *state_table = "state";

	TableDetails wemo_device_info[WEMO_DEVICE_ENTRIES] =
	{
			{"wemo_id", "INTEGER PRIMARY KEY AUTOINCREMENT"},
			{"UDN", "VARCHAR(256)"},
			{"device_type", "TINYINT"},
			{"friendly_name", "VARCHAR(256)"},
			{"firmware_version", "VARCHAR(256)"},
			{"serial_number", "CHAR(14)"},
			{"model_name", "VARCHAR(45)"},
			{"manufacturer", "VARCHAR(45)"},
			{"UNIQUE", "(UDN)"}
	};

	TableDetails wemo_state_info[STATE_ENTRIES] =
	{
			{"wemo_id", "INTEGER PRIMARY KEY"},
			{"is_online", "BOOL"},
			{"capability", "VARCHAR(256)"},
            {"UNIQUE", "(wemo_id)"}
	};

	if (stat(wemo_device_db, &db_file) != -1) {
		fprintf(stderr, "wemo device db already exists\n");
		if (InitDB(wemo_device_db, dev_db)) {
			fprintf(stderr, "device DB initialization failed\n");
			return DB_ERROR;
		}
	}
	else {
		if (!InitDB(wemo_device_db, dev_db)) {
			if (WeMoDBCreateTable(dev_db, device_table, wemo_device_info, 0, WEMO_DEVICE_ENTRIES)) {
				fprintf(stderr, "wemo_device table creation failed %s", device_table);
				return DB_ERROR;
			}
        }
    }

    if(stat(wemo_state_db, &db_file) != -1) {
        fprintf(stderr, "wemo state db already exists\n");
        if (InitDB(wemo_state_db, state_db)) {
            fprintf(stderr, "state DB initialization failed\n");
            return DB_ERROR;
        }
    }
    else {
        if (!InitDB(wemo_state_db, state_db)) {
			if (WeMoDBCreateTable(state_db, state_table, wemo_state_info, 0, STATE_ENTRIES)) {
				fprintf(stderr, "table create failed %s", state_table);
				return DB_ERROR;
			}
            else {
                if (wemo_dev_init_state_db(*dev_db, *state_db) != DB_SUCCESS) {
                    return DB_ERROR;
                }
            }
		}
	}
	fprintf(stderr, "DB init done\n");
	return DB_SUCCESS;
}

void wemo_dev_db_finish(sqlite3 *dev_db, sqlite3 *state_db)
{
	CloseDB(dev_db);
    CloseDB(state_db);
}

static int wemo_dev_parse_version(char *firmware, char *UDN, char *version, int *type)
{
    int major;
    int minor;
    int fix;

    char firmware_type[10];
    char os[10];
    char dev[10];
    char *substring = NULL;

    if (sscanf(firmware, "WeMo_WW_%d.%d.%d.%[^-]-%[^-]-%[^-]", &major, &minor, &fix, firmware_type, os, dev) != 6) {
        fprintf(stderr, "error parsing firmware version (%s)..\n", firmware);
        fprintf(stderr, "Trying to parse old for old firmware..\n");

        if (sscanf(firmware, "WeMo_US_%d.%d.%d.%[^-]", &major, &minor, &fix, firmware_type) != 4) {
        return 0;
    }
        else {
            if ((substring = strcasestr(UDN, "socket"))) {
                *type = WEMO_SWITCH;
            }
            else if ((substring = strcasestr(UDN, "lightswitch"))) {
                *type = WEMO_LIGHT;
            }
            else {
                *type = WEMO_UNKNOWN;
            }

            sprintf(version, "%d,%d,%d", major, minor, fix);
            return 1;
        }
    }

    sprintf(version, "%d.%d.%d", major, minor, fix);

    if ((substring = strcasestr(UDN, "uuid:socket"))) {
    if (!strcasecmp(dev, "SNS")) {
        *type = WEMO_SWITCH;
    }
        else {
        *type = WEMO_MINI;
    }
    }
    else if ((substring = strcasestr(UDN, "uuid:lightswitch"))) {
        *type = WEMO_LIGHT;
    }
    else if ((substring = strcasestr(UDN, "uuid:dimmer"))) {
        *type = WEMO_DIMMER;
    }
    else if ((substring = strcasestr(UDN, "uuid:insight"))) {
        *type = WEMO_INSIGHT;
    }
    else if ((substring = strcasestr(UDN, "uuid:sensor"))) {
            *type = WEMO_SENSOR;
    }
    else {
        *type = WEMO_UNKNOWN;
        return 0;
    }
    return 1;
}

void wemo_dev_db_insert(sqlite3 *db, struct wemoDevice *dev)
{
    // Add device to DB
    ColDetails devParams[7] ;
    int type = 0;
    char version[12];

    memset(version, 0, 12);

    if (wemo_dev_parse_version(dev->firmwareVersion, dev->UDN, version, &type)) {
        sprintf(devParams[0].ColName, "%s","UDN");
        sprintf(devParams[0].ColValue, "'%s'", dev->UDN);
        sprintf(devParams[1].ColName, "%s", "device_type");
        sprintf(devParams[1].ColValue, "%d", type);
        sprintf(devParams[2].ColName, "%s", "friendly_name");
        sprintf(devParams[2].ColValue, "\'%s\'", dev->FriendlyName);
        sprintf(devParams[3].ColName, "%s", "firmware_version");
        sprintf(devParams[3].ColValue, "\'%s\'", version);
        sprintf(devParams[4].ColName, "%s", "serial_number");
        sprintf(devParams[4].ColValue, "\'%s\'", dev->serialNumber);
        sprintf(devParams[5].ColName, "%s", "model_name");
        sprintf(devParams[5].ColValue, "\'%s\'", dev->modelName);
        sprintf(devParams[6].ColName, "%s", "manufacturer");
        sprintf(devParams[6].ColValue, "\'%s\'", dev->manufacturer);

        WeMoDBInsertInTable(&db, "wemo_device", devParams, 7);
    }
}

void wemo_dev_statedb_insert(sqlite3 *db, int id, int is_online, char *cap)
{
    ColDetails stateParams[4];
    sprintf(stateParams[0].ColName, "%s", "wemo_id");
    sprintf(stateParams[0].ColValue, "%d", id);
    sprintf(stateParams[1].ColName, "%s", "is_online");
    sprintf(stateParams[1].ColValue, "%d", is_online);
    sprintf(stateParams[2].ColName, "%s", "capability");
    sprintf(stateParams[2].ColValue, "%s", cap);

    if (WeMoDBInsertInTable(&db, "state", stateParams, 3) == -1) {
        char Condition[512];
        sprintf(Condition, "wemo_id=%d", id);
        WeMoDBUpdateTable(&db,
                          "state",
                          &stateParams[1],
                          2,
                          Condition);
    }
}

void wemo_dev_statedb_update_online(sqlite3 *db, int id, int is_online)
{
    ColDetails stateParams;
    char Condition[512];

    sprintf(stateParams.ColName, "%s", "is_online");
    sprintf(stateParams.ColValue, "%d", 0);
    sprintf(Condition, "wemo_id=%d", id);
    WeMoDBUpdateTable(&db,
                      "state",
                      &stateParams,
                      1,
                      Condition);
}

int wemo_dev_db_retrieve_id(sqlite3 *db, char *UDN)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql;
    int id_value = 0;

    sql = "SELECT wemo_id FROM wemo_device where UDN = ?";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc == SQLITE_OK) {
        if (sqlite3_bind_text(stmt, 1, UDN, strlen(UDN), SQLITE_STATIC) != SQLITE_OK) {
            fprintf(stderr, "Could not bind text\n");
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    else {
        fprintf(stderr, "Failed to execute statement %s\n",
                sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
    }

    int step = sqlite3_step(stmt);

    if (step == SQLITE_ROW) {
        id_value = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return id_value;
}

int wemo_dev_db_retrieve_udn(sqlite3 *db, int wemo_id, char *UDN)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql;

    sql = "SELECT UDN FROM wemo_device where wemo_id = ?";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc == SQLITE_OK) {
        if (sqlite3_bind_int(stmt, 1, wemo_id) != SQLITE_OK) {
            fprintf(stderr, "Could not bind int\n");
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    else {
        fprintf(stderr, "Failed to execute statement %s\n",
                sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
    }

    int step = sqlite3_step(stmt);

    if (step == SQLITE_ROW) {
        strcpy(UDN, (const char *)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return 1;
}

int wemo_dev_db_retrieve_cap(sqlite3 *db, int wemo_id, char *cap)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql;

    sql = "SELECT capability FROM state where wemo_id = ?";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc == SQLITE_OK) {
        if (sqlite3_bind_int(stmt, 1, wemo_id) != SQLITE_OK) {
            fprintf(stderr, "Could not bind int\n");
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    else {
        fprintf(stderr, "Failed to execute statement %s\n",
                sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return 0;
    }

    int step = sqlite3_step(stmt);

    if (step == SQLITE_ROW) {
        strcpy(cap, (const char *)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return 1;
}

struct cap_state {
    cap_t cap;
    int state;
};

int wemo_dev_db_update_capability(sqlite3 *db, int wemo_id, int cap, int value)
{
    char buffer[512];
    ColDetails stateParams;
    char Condition[512];
    
    sprintf(stateParams.ColName, "%s", "capability");
    sprintf(Condition, "wemo_id=%d", wemo_id);
    
    memset(buffer, 0, 512);

    if (wemo_dev_db_retrieve_cap(db, wemo_id, buffer)) {
        if (strlen(buffer) == 0) {
            sprintf(buffer, "%d=%d", cap, value);
            sprintf(stateParams.ColValue, "\'%s\'", buffer);
            WeMoDBUpdateTable(&db, "state",
                              &stateParams,
                              1,
                              Condition);
        }
        else {
            int i, j;
            char *str, *token;
            struct cap_state *capabilities = NULL;
            int found = 0;

            capabilities = (struct cap_state *) malloc(sizeof(struct cap_state) * (CAP_FUTURE - 1));
            memset(capabilities, 0, sizeof(struct cap_state) * (CAP_FUTURE - 1));
            for (i = 0, str = buffer;; i++, str = NULL) {
                token = strtok(str, ":");
                if (token == NULL)
                    break;
                
                sscanf(token, "%d=%d", (int *) &capabilities[i].cap, &capabilities[i].state);
                if (capabilities[i].cap == cap) {
                    capabilities[i].state = value;
                    found = 1;
                }
            }

            if (!found) {
                capabilities[i].cap = cap;
                capabilities[i].state = value;
                i++;
            }

            memset(buffer, 0, 512);
            for (j = 0; j < i; j++) {
                sprintf(buffer + strlen(buffer), "%d=%d:", capabilities[j].cap, capabilities[j].state);
            }
            buffer[strlen(buffer) - 1] = 0;

            sprintf(stateParams.ColValue, "\'%s\'", buffer);
            WeMoDBUpdateTable(&db, "state",
                              &stateParams,
                              1,
                              Condition);

            if (capabilities != NULL) {
                free(capabilities);
            }
        }
    }
    return 1;
}

int wemo_dev_db_get_capability(sqlite3 *db, int wemo_id, int cap)
{
    char buffer[512];
    ColDetails stateParams;
    char Condition[512];
    int value = -1;

    sprintf(stateParams.ColName, "%s", "capability");
    sprintf(Condition, "wemo_id=%d", wemo_id);
    
    memset(buffer, 0, 512);

    if (wemo_dev_db_retrieve_cap(db, wemo_id, buffer)) {
        if (strlen(buffer) != 0) {
            int i;
            char *str, *token;
            struct cap_state *capabilities = NULL;

            capabilities = (struct cap_state *) malloc(sizeof(struct cap_state) * (CAP_FUTURE - 1));
            memset(capabilities, 0, sizeof(struct cap_state) * (CAP_FUTURE - 1));
            for (i = 0, str = buffer;; i++, str = NULL) {
                token = strtok(str, ":");
                if (token == NULL)
                    break;
                
                sscanf(token, "%d=%d", (int *) &capabilities[i].cap, &capabilities[i].state);
                if (capabilities[i].cap == cap) {
                    value = capabilities[i].state;
                }
            }

            if (capabilities != NULL) {
                free(capabilities);
            }
        }
    }
    return value;
}

int wemo_dev_db_delete_row(sqlite3 *db, int wemo_id)
{
    char condition[512];

    sprintf(condition, "wemo_id=%d", wemo_id);

    if (WeMoDBDeleteEntry(&db, "wemo_device", condition)) {
        return 1;
    } else {
        return 0;
    }
}

int wemo_dev_statedb_delete_row(sqlite3 *db, int wemo_id)
{
    char condition[512];

    sprintf(condition, "wemo_id=%d", wemo_id);

    if (WeMoDBDeleteEntry(&db, "state", condition)) {
        return 1;
    } else {
        return 0;
    }
}
