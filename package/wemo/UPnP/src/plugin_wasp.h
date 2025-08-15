/***************************************************************************
*
*
* plugin_wasp.h
*
* Copyright (c) 2012-2014 Belkin International, Inc. and/or its affiliates.
* All rights reserved.
*
* Permission to use, copy, modify, and/or distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
*
*
* THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO ALL IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT,
* INCIDENTAL, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
* RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
* NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH
* THE USE OR PERFORMANCE OF THIS SOFTWARE.
*
*
***************************************************************************/

#ifndef PLUGIN_WASP_H_
#define PLUGIN_WASP_H_

#ifdef PRODUCT_WeMo_Dimmer
#include <stdbool.h>
#include "wasp.h"
#include "wasp_vars_dimmer.h"

#define MAX_VARS 3

typedef enum {
    NONE,
    RESET_ANIMATION,
    CONTINUOUS_ANIMATION,
    MOMENTARY_ANIMATION
} AnimationType;

/* structure to store the modified
    WASP variables. */
typedef struct waspChangedVars {
    int  id;
    char value[SIZE_32B];
} waspChangedVars;

/* flag to check if the error animations
   have to be restored */
extern bool errorAnimRes;

/**
 * initWASPLock
 * - Function to initialize the lock on changed variables
     store for WASP
 ***************************************************/
void initWASPLock(void);

/**
 * lockWASP
 * - Function to acquire the lock on changed variables
     store for WASP
 ***************************************************/
void lockWASP(void);

/**
 * unlockWASP
 * - Function to unlock the lock acquired by lockWASP
 ***************************************************/
void unlockWASP(void);

/**
 * setAnimation
 * - This function sets the LED animation
 *   to the value passed.
 *
 * - return:
 *      0: success
 *    > 0 - standard Linux errno error code
 *    < 0 - WASP error code (see WASP_ERR_*)
 *************************************************/
int setAnimation(AnimationValue val);

/**
 * getWaspVariable
 * - This function fetches the value of WASP variable
 *   as id in buffer pointer to by val
 * - args:
 *     id: id of the WASP variable
 *     type: type of the WASP variable
 *     val: buffer to fetch the value of the variable in
 * - return:
 *      0: success
 *     -1: failure
 *************************************************/
int getWaspVariable(unsigned char id, unsigned char type, void *val);

/**
 * setWaspVariable
 * - This function sets the value of WASP variable
 *   as id.
 * - args:
 *     id: id of the WASP variable
 *     type: type of the WASP variable
 *     val: value of the WASP variable to be set
 * - return:
 *      0: success
 *     -1: failure
 *************************************************/
int setWaspVariable(unsigned char id, unsigned char type, void *val);
/**
 * waspPollTask
 * - This thread polls for the changes in
 *   WASP variables.
 *************************************************/
void* waspPollTask(void* args);

/**
 * waspChangeNotify
 * - This thread notifies the WASP changes
 *   to the basic event subscribers .
 *************************************************/
void* waspChangeNotify(void* arg);


void addVarToWaspList(int id, char *value);

void hushAnimationIfActive(void);
#endif

#endif /* PLUGIN_WASP_H_ */
