/***************************************************************************
 *
 *
 * plugin_wasp.c
 *
 * Created by Belkin International, Software Engineering on APR 06, 2016
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

#include "utils.h"
#include "osUtils.h"
#include "plugin_wasp.h"
#include "gpio.h"
#include "rule.h"
#include "simulatedOccupancy.h"
#include "dimmer_attr.h"
#include "belkin_api.h"
#include "belkin_diag.h"

#ifdef PRODUCT_WeMo_Dimmer
extern int gLongPressTriggered;

static bool g_notifyFlag;
static waspChangedVars g_vars[MAX_VARS];
static int g_numberOfWaspVars;
static pthread_mutex_t g_waspLock;
static pthread_mutex_t g_animationLock;
static bool onoffSetByApp = false;
static bool brightnessSetByApp = false;
int gMinLevel=-1;
int gMaxLevel=-1;
bool errorAnimRes = false;
static AnimationValue lastAnimationPlayed = 0;

void initWASPLock(void)
{
    osUtilsCreateLock(&g_waspLock);
    osUtilsCreateLock(&g_animationLock);
}

void lockWASP(void)
{
    osUtilsGetLock(&g_waspLock);
}

void unlockWASP(void)
{
    osUtilsReleaseLock(&g_waspLock);
}

int
restoreAnimation(AnimationValue animationToRestore)
{
    bool toSet = false;
    int ret = 0;
    if(LED_STATE_AP_MODE == animationToRestore) {
        /* check if the device is still in AP mode, before
           restoring the animation */
        char *pSSID = GetBelkinParameter (WIFI_CLIENT_SSID);
        if(!(pSSID && strlen (pSSID) > 0)) {
            toSet = true;
        }
    } else if(LED_STATE_ERR_1_DETECTED == animationToRestore ||
              LED_STATE_ERR_2_DETECTED == animationToRestore) {
        /* let the checkInetConnectivity thread restore the animation */
        errorAnimRes = true;

    } else if(LED_STATE_OVER_HEAT == animationToRestore) {
        /* check if the device is still in overHeat state, before
           restoring the animation */
        if(g_overTemp) {
            toSet = true;
        }
    } else if(LED_STATE_INCORRECT_WIRING == animationToRestore) {
        /* TBD */
    } else if(LED_STATE_AWAY_OPENING == animationToRestore) {
        /* check if the away/LP away rule is still running before
           restoring the animation */
        if(gRuleHandle[e_AWAY_RULE].ruleCnt || LONG_PRESS_AWAY_ACTIVE) {
            toSet = true;
        }
    }

    if(toSet) {
        WaspVariable anim;
        memset(&anim, 0, sizeof(anim));

        anim.ID = WASP_VAR_ANIMATION;
        anim.Type = WASP_VARTYPE_UINT8;
        /* set animation */
        anim.Val.U8 = animationToRestore;
        if(WASP_OK != (ret=WASP_SetVariable(&anim))) {
            APP_LOG("WiFiApp", LOG_ERR, "Restoring of WASP_VAR_ANIMATION to value %u failed. Return code:%s", animationToRestore,
                    WASP_strerror(ret));
        } else {
            lastAnimationPlayed = animationToRestore;
            APP_LOG("WiFiApp", LOG_DEBUG, "Restoring of WASP_VAR_ANIMATION to %u is successful.", animationToRestore);
        }
    }
    return ret;
}

AnimationType
fetchAnimationType(AnimationValue val)
{
    AnimationType type = NONE;
    switch(val) {
    case LED_STATE_BAR_PRESSED:
    case LED_STATE_BAR_RELEASED:
    case LED_STATE_RESTART:
    case LED_STATE_WIFI_RESET:
    case LED_STATE_FACTORY_RESTORE:
        type = RESET_ANIMATION;
        break;
    case LED_STATE_PWR_BOOT:
    case LED_STATE_AP_MODE:
    case LED_STATE_KNOWN_CONNECTION:
    case LED_STATE_ERR_1_DETECTED:
    case LED_STATE_ERR_2_DETECTED:
    case LED_STATE_OVER_HEAT:
    case LED_STATE_INCORRECT_WIRING:
    case LED_STATE_AWAY_OPENING:
        /* AWAY_CLOSING and CANCEL_ERR has been kept here
           to reflect an association with the other
           continuous animations */
    case LED_STATE_AWAY_CLOSING:
    case LED_STATE_CANCEL_ERR:
        type = CONTINUOUS_ANIMATION;
        break;
    case LED_STATE_CONNECTION_ESTABLISHED:
    case LED_STATE_CONNECTION_RESTABLISHED:
    case LED_STATE_TURN_ON:
    case LED_STATE_TURN_OFF:
    case LED_STATE_RULE_OPEN:
    case LED_STATE_RULE_CLOSE:
    case LED_STATE_RULE_3RD_PARTY_OPEN:
    case LED_STATE_RULE_3RD_PARTY_CLOSE:
    case LED_STATE_LONG_PRESS_RULE:
    case LED_STATE_SLEEP_TIMER:
    case LED_STATE_AUTO_OFF:
    case LED_STATE_AUTO_OFF_RESET:
        type = MOMENTARY_ANIMATION;
        break;
    default:
        APP_LOG("WASP", LOG_ERR, "Animation value:%d is not one of the types in the list.", val);
        break;
    }
    return type;
}

bool
checkIfAnimationToHush(AnimationValue val)
{
    bool ret = false;
    if(g_bHushAnimation && (val == LED_STATE_ERR_1_DETECTED || val == LED_STATE_ERR_2_DETECTED ||
                            val == LED_STATE_OVER_HEAT || val == LED_STATE_INCORRECT_WIRING)) {
        ret = true;
    }
    return ret;

}

void hushAnimationIfActive(void)
{
    if(lastAnimationPlayed == LED_STATE_ERR_1_DETECTED ||
       lastAnimationPlayed == LED_STATE_ERR_2_DETECTED ||
       lastAnimationPlayed == LED_STATE_OVER_HEAT ||
       lastAnimationPlayed == LED_STATE_INCORRECT_WIRING) {
        setAnimation(LED_STATE_CANCEL_ERR);
    }
}

int setAnimation(AnimationValue val)
{
    /* check if the animation is hushed and not to be played. */
    if(checkIfAnimationToHush(val))
        return 0;

    osUtilsGetLock(&g_animationLock);
    bool playAnimation = true;
    static AnimationValue lastAnimationUsed = 0;
    AnimationType type = fetchAnimationType(val);
    AnimationType lastAnimationType = fetchAnimationType(lastAnimationUsed);
    bool b_restoreAnimation = false;
    static AnimationValue animationToRestore = 0;

    /* reset animation is of hightest priority.
       Play it always. */
    if(RESET_ANIMATION == type) {
        playAnimation = true;
        if(LED_STATE_BAR_RELEASED == val)
            b_restoreAnimation = true;
        if(CONTINUOUS_ANIMATION == lastAnimationType && LED_STATE_CANCEL_ERR != lastAnimationUsed &&
           LED_STATE_AWAY_CLOSING != lastAnimationUsed)
            animationToRestore = lastAnimationUsed;
    } else if(CONTINUOUS_ANIMATION == type) {
        /* if it is a continuous animation, check if reset animation is already being
           played. i.e If the reset bar is still pressed, dont play anyother animation. */
        if(lastAnimationType == RESET_ANIMATION && LED_STATE_BAR_PRESSED == lastAnimationUsed)
            playAnimation = false;
        /* if it LED_STATE_CANCEL_ERR, play only if last continuous animation played was one of
           LED_STATE_ERR_1_DETECTED/LED_STATE_ERR_2_DETECTED/LED_STATE_OVER_HEAT. */
        else if(LED_STATE_CANCEL_ERR == val && CONTINUOUS_ANIMATION == lastAnimationType &&
                LED_STATE_ERR_1_DETECTED != lastAnimationUsed &&
                LED_STATE_ERR_2_DETECTED != lastAnimationUsed &&
                LED_STATE_OVER_HEAT != lastAnimationUsed)
            playAnimation = false;
        /* if it LED_STATE_AWAY_CLOSING, play only if last continuous animation played was
           LED_STATE_AWAY_OPENING */
        else if(LED_STATE_AWAY_CLOSING == val && CONTINUOUS_ANIMATION == lastAnimationType &&
                LED_STATE_AWAY_OPENING != lastAnimationUsed)
            playAnimation = false;
        else
            playAnimation = true;

        if(LED_STATE_CANCEL_ERR == val || LED_STATE_AWAY_CLOSING == val)
            b_restoreAnimation = true;
        else if(CONTINUOUS_ANIMATION == lastAnimationType && LED_STATE_CANCEL_ERR != lastAnimationUsed &&
                LED_STATE_AWAY_CLOSING  != lastAnimationUsed)
            animationToRestore = lastAnimationUsed;
    } else if(MOMENTARY_ANIMATION == type) {
        if(RESET_ANIMATION == lastAnimationType && LED_STATE_BAR_PRESSED == lastAnimationUsed)
            playAnimation = false;
        /* if it is a momentary animation and last animation played was LED_STATE_CANCEL_ERR/LED_STATE_AWAY_CLOSING
           let the momentary animation play. */
        else if(CONTINUOUS_ANIMATION == lastAnimationType &&
                (LED_STATE_CANCEL_ERR == lastAnimationUsed || LED_STATE_AWAY_CLOSING == lastAnimationUsed))
            playAnimation = true;
        /* if it is a momentary animation, check if there is some continuous animation being played.
           If the momentary animation is not one of LED_STATE_CONNECTION_ESTABLISHED or
           LED_STATE_CONNECTION_RESTABLISHED, dont play. */
        else if(CONTINUOUS_ANIMATION == lastAnimationType && LED_STATE_CONNECTION_ESTABLISHED != val &&
                LED_STATE_CONNECTION_RESTABLISHED != val)
            playAnimation = false;
        else
            playAnimation = true;
    }

    if(false == playAnimation) {
        APP_LOG("WiFiApp", LOG_DEBUG, "Animation with higher precedence is in progress. Returning!!");
        osUtilsReleaseLock(&g_animationLock);
        return FAILURE;
    }

    int ret = 0;
    WaspVariable anim;
    memset(&anim, 0, sizeof(anim));

    anim.ID = WASP_VAR_ANIMATION;
    anim.Type = WASP_VARTYPE_UINT8;
    /* set animation */
    anim.Val.U8 = val;

    if(WASP_OK != (ret=WASP_SetVariable(&anim))) {
        APP_LOG("WiFiApp", LOG_ERR, "Setting of WASP_VAR_ANIMATION to value %u failed. Return code:%s", val,
                WASP_strerror(ret));
    } else {
        lastAnimationUsed = val;
        lastAnimationPlayed = val;
        APP_LOG("WiFiApp", LOG_DEBUG, "Setting of WASP_VAR_ANIMATION to %u is successful.", val);
    }
    if(b_restoreAnimation && animationToRestore) {
        restoreAnimation(animationToRestore);
        b_restoreAnimation = false;
        animationToRestore = 0;
    }

    osUtilsReleaseLock(&g_animationLock);
    return ret;
}

int getWaspVariable(unsigned char id, unsigned char type, void *val)
{
    if(NULL == val) {
        APP_LOG("WiFiApp", LOG_ERR, "Value passed is NULL. Returning...");
        return FAILURE;
    }
    int ret = SUCCESS;
    WaspVariable Var;
    memset(&Var, 0, sizeof(Var));
    Var.ID = id;
    Var.State = VAR_VALUE_LIVE;
    Var.Type = type;
    if(WASP_OK != (ret=WASP_GetVariable(&Var))) {
        APP_LOG("WiFiApp", LOG_ERR, "Fetching value of WASP variable with ID: %u failed. Return code:%s", id,
                WASP_strerror(ret));
        return FAILURE;
    }

    switch(type) {
    case WASP_VARTYPE_ENUM:
        *((unsigned char*)val) = Var.Val.Enum;
        break;
    case WASP_VARTYPE_PERCENT:
        *((unsigned short*)val) = Var.Val.Percent;
        break;
    case WASP_VARTYPE_TEMP:
        *((short*)val) = Var.Val.Temperature;
        break;
    case WASP_VARTYPE_TIME32:
        *((int*)val) = Var.Val.TimeTenths;
        break;
    case WASP_VARTYPE_TIME16:
        *((unsigned short*)val) = Var.Val.TimeSecs;
        break;
    case WASP_VARTYPE_TIMEBCD:
        memcpy(val, (void*)Var.Val.BcdTime, sizeof(Var.Val.BcdTime));
        break;
    case WASP_VARTYPE_BOOL:
        *((unsigned char*)val) = Var.Val.Boolean;
        break;
    case WASP_VARTYPE_BCD_DATE:
        memcpy(val, (void*)Var.Val.BcdDate, sizeof(Var.Val.BcdDate));
        break;
    case WASP_VARTYPE_DATETIME:
        memcpy(val, (void*)Var.Val.BcdDateTime, sizeof(Var.Val.BcdDateTime));
        break;
    case WASP_VARTYPE_STRING:
        val = (void*)Var.Val.String;
        break;
    case WASP_VARTYPE_BLOB:
        memcpy(val, (void*)&Var.Val.Blob, sizeof(Var.Val.Blob));
        break;
    case WASP_VARTYPE_UINT8:
        *((unsigned char*)val) = Var.Val.U8;
        break;
    case WASP_VARTYPE_INT8:
        *((signed char*)val) = Var.Val.I8;
        break;
    case WASP_VARTYPE_UINT16:
        *((unsigned short*)val) = Var.Val.U16;
        break;
    case WASP_VARTYPE_INT16:
        *((signed short*)val) = Var.Val.I16;
        break;
    case WASP_VARTYPE_UINT32:
        *((unsigned int*)val) = Var.Val.U32;
        break;
    case WASP_VARTYPE_INT32:
        *((int*)val) = Var.Val.I32;
        break;
    case WASP_VARTYPE_TIME_M16:
        *((unsigned short*)val) = Var.Val.TimeMins;
        break;
    default:
        APP_LOG("WiFiApp", LOG_ERR, "WASP variable type did not match to the types defined. Please check...");
        return FAILURE;
    }
    APP_LOG("WiFiApp", LOG_DEBUG, "Feching of WASP variable with ID:%u is successful.", id);
    return ret;
}

int setWaspVariable(unsigned char id, unsigned char type, void *val)
{
    if(NULL == val) {
        APP_LOG("WiFiApp", LOG_ERR, "Value passed is NULL. Returning...");
        return FAILURE;
    }
    int ret = SUCCESS;
    WaspVariable Var;
    memset(&Var, 0, sizeof(Var));
    Var.ID = id;
    Var.Type = type;
    switch(type) {
    case WASP_VARTYPE_ENUM:
        Var.Val.Enum = *((unsigned char*)val);
        break;
    case WASP_VARTYPE_PERCENT:
        Var.Val.Percent = *((unsigned short*)val);
        break;
    case WASP_VARTYPE_TEMP:
        Var.Val.Temperature = *((short*)val);
        break;
    case WASP_VARTYPE_TIME32:
        Var.Val.TimeTenths = *((int*)val);
        break;
    case WASP_VARTYPE_TIME16:
        Var.Val.TimeSecs = *((unsigned short*)val);
        break;
    case WASP_VARTYPE_TIMEBCD:
        memcpy((void*)Var.Val.BcdTime, val, sizeof(Var.Val.BcdTime));
        break;
    case WASP_VARTYPE_BOOL:
        Var.Val.Boolean = *((unsigned char*)val);
        break;
    case WASP_VARTYPE_BCD_DATE:
        memcpy((void*)Var.Val.BcdDate, val, sizeof(Var.Val.BcdDate));
        break;
    case WASP_VARTYPE_DATETIME:
        memcpy((void*)Var.Val.BcdDateTime, val, sizeof(Var.Val.BcdDateTime));
        break;
    case WASP_VARTYPE_STRING:
        Var.Val.String = (char*)val;
        break;
    case WASP_VARTYPE_BLOB:
        memcpy((void*)&Var.Val.Blob, val, sizeof(Var.Val.Blob));
        break;
    case WASP_VARTYPE_UINT8:
        Var.Val.U8 = *((unsigned char*)val);
        break;
    case WASP_VARTYPE_INT8:
        Var.Val.I8 = *((signed char*)val);
        break;
    case WASP_VARTYPE_UINT16:
        Var.Val.U16 = *((unsigned short*)val);
        break;
    case WASP_VARTYPE_INT16:
        Var.Val.I16 = *((signed short*)val);
        break;
    case WASP_VARTYPE_UINT32:
        Var.Val.U32 = *((unsigned int*)val);
        break;
    case WASP_VARTYPE_INT32:
        Var.Val.I32 = *((int*)val);
        break;
    case WASP_VARTYPE_TIME_M16:
        Var.Val.TimeMins = *((unsigned short*)val);
        break;
    default:
        APP_LOG("WiFiApp", LOG_ERR, "WASP variable type did not match to the types defined. Please check...");
        return FAILURE;
    }
    if(WASP_OK != (ret= WASP_SetVariable(&Var))) {
        APP_LOG("WiFiApp", LOG_ERR, "Setting of WASP variable with ID:%u failed. Return code:%s", id,
                WASP_strerror(ret));
        return FAILURE;
    }

    /* check if wasp var set was WASP_VAR_ON_OFF.
       This is to differentiate between the on/off from
       App or directly from HW. */
    if(WASP_VAR_ON_OFF == id) {
        onoffSetByApp = true;
        APP_LOG("waspPollTask", LOG_DEBUG, "onoffSetByApp set to true");
    } else if(WASP_VAR_CURRENT_BRIGHTNESS == id) {
        brightnessSetByApp = true;
    }
    /* WEMO-52378:because we were setting onoffSetByApp later which was leading to thread switch and away rule is getting suspended */
    APP_LOG("WiFiApp", LOG_DEBUG, "Setting of WASP variable with ID:%u is successful.", id);
    return ret;
}

void* waspChangeNotify(void* arg)
{
    char* parameters[MAX_VARS];
    char* value[MAX_VARS];
    int i = 0;
    for(;;) {
        lockWASP();
        if(g_notifyFlag) {
            for(i=0; i<g_numberOfWaspVars; i++) {
                value[i] = ZALLOC(SIZE_32B);
                if(g_vars[i].id == WASP_VAR_ON_OFF) {
                    /* add the BinaryState notification to send */
                    parameters[i] = "BinaryState";
                    strncpy(value[i], g_vars[i].value, SIZE_32B-1);
                    APP_LOG("UPNP", LOG_DEBUG, "Notification:BinaryState:state: %s", value[i]);
                    setAttrFlagDimmer(ATTR_STATE, 1, 0);
                }
                if(g_vars[i].id == WASP_VAR_CURRENT_BRIGHTNESS) {
                    if(true == brightnessSetByApp) {
                        brightnessSetByApp = false;
                    }
                    unsigned char brightness = atoi(g_vars[i].value);
                    /* add the Brightness notification to send */
                    parameters[i] = "Brightness";
                    snprintf(value[i], SIZE_32B, "%u", brightness);
                    APP_LOG("UPNP", LOG_DEBUG, "Notification:BinaryState:brightness: %s%%", value[i]);
                    setAttrFlagDimmer(ATTR_BRIGHTNESS, 1, 0);
                }
                if(g_vars[i].id == WASP_VAR_ERR_STATUS) {
                    int overheat = atoi(g_vars[i].value);

                    /* add the Overheat notification to send */
                    parameters[i] = "OverTemp";
                    snprintf(value[i], SIZE_32B, "%d", overheat);
                    setAttrFlagDimmer(ATTR_OVERHEAT, 1, 0);

                    APP_LOG("UPNP", LOG_DEBUG, "Notification:Overheat: %d", overheat);
                }
            }
            UpnpNotify(device_handle, SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].UDN,
                       SocketDevice.service_table[PLUGIN_E_EVENT_SERVICE].ServiceId, (const char **)parameters, (const char **)value, g_numberOfWaspVars);
            for(i=0; i<g_numberOfWaspVars; i++) {
                if(value[i])
                    free(value[i]);
            }
            /* reset the buffer and the counter */
            memset(g_vars, 0, sizeof(waspChangedVars));
            g_numberOfWaspVars = 0;
            g_notifyFlag = false;
        }
        unlockWASP();
        pluginUsleep(1000000);
    }
    return NULL;
}

void
addVarToWaspList(int id, char *value)
{
    int i = 0;
    lockWASP();
    for(i=0; i<g_numberOfWaspVars; i++) {
        if(id == g_vars[i].id) {
            strncpy(g_vars[i].value, value, sizeof(g_vars[i].value)-1);
            unlockWASP();
            return;
        }
    }
    g_vars[g_numberOfWaspVars].id = id;
    strncpy(g_vars[g_numberOfWaspVars].value, value, sizeof(g_vars[g_numberOfWaspVars].value)-1);
    g_notifyFlag = true;
    g_numberOfWaspVars++;
    unlockWASP();
}

void* waspPollTask(void* args)
{
    int Ret;
    DimmerStatus Old;
    DimmerStatus New;
    WaspVariable Var;
    memset(&Var,0,sizeof(Var));
    VarID Id = WASP_VAR_DIMMER_STATUS;

    memset(&Old, 0, sizeof(DimmerStatus));
    memset(&New, 0, sizeof(DimmerStatus));

    /* Initlize the variable Old, to avoid the pollTask from processing
       the state/brightness if the value has not changed which
       normally happens on wemoApp restart. */
    Old.CurrentBrightness = g_brightness;
    Old.Status = g_PowerStatus;

    do {
        // Set the changed variable mask so that WASP_GetChangedVar only
        // reports changes to WASP_VAR_DIMMER_STATUS
        if((Ret = WASP_SetChangedVarMask(1,&Id)) != WASP_OK) {
            APP_LOG("waspPollTask",LOG_ERR,"WASP_SetChangedVarMask failed: %s\n",
                    WASP_strerror(Ret));
            break;
        }
        if((Ret = WASP_SetAsyncDataQueueDepth(1)) != WASP_OK) {
            APP_LOG("waspPollTask",LOG_ERR,
                    "WASP_SetAsyncDataQueueDepth failed: %s\n",WASP_strerror(Ret));
            break;
        }
    } while(false);

    if(Ret == 0) while(true) {
            WASP_FreeValue(&Var);
            /* Wait for a variable to change value */
            if((Ret = WASP_GetChangedVar(&Var)) != WASP_OK) {
                APP_LOG("waspPollTask",LOG_ERR,"WASP_GetChangedVarfailed: %s",
                        WASP_strerror(Ret));
                pluginUsleep(1000000);;
                continue;
            }
            if(Var.ID != WASP_VAR_DIMMER_STATUS) {
                APP_LOG("waspPollTask",LOG_ERR,
                        "Error: WASP_GetChangedVar reported change in VarID %d",
                        Var.ID);
                continue;
            }

            if(Var.Type != WASP_VARTYPE_BLOB) {
                APP_LOG("waspPollTask",LOG_ERR,"Error: incorrect type %d returned",
                        Var.Type);
                continue;
            }

            if(Var.Val.Blob.Len != sizeof(New)) {
                APP_LOG("waspPollTask",LOG_ERR,"Error: Incorrect length %d returned",
                        Var.Val.Blob.Len);
                continue;
            }
            memcpy(&New,Var.Val.Blob.Data,sizeof(New));

            if(gMinLevel == -1)
                gMinLevel = New.MinLevel;

            if(gMaxLevel == -1)
                gMaxLevel = New.MaxLevel;

            /*
            Brightness change is handled before turn OFF as g_brightness must be updated before stop fader
            notification can be sent out with currrent brightness value.
                 */

            if(New.CurrentBrightness != Old.CurrentBrightness) {
                APP_LOG("waspPollTask", LOG_DEBUG, "Current Brightness set to WASP level: %d \n", New.CurrentBrightness);
                if(New.CurrentBrightness) {
                    g_brightness = New.CurrentBrightness;
#ifdef SIMULATED_OCCUPANCY
                    if(LONG_PRESS_AWAY_ACTIVE ||
                       (gRuleHandle[e_AWAY_RULE].ruleCnt && (gpSimulatedDevice && gpSimulatedDevice->ruleEndTime))) {
                        if(!brightnessSetByApp) {
                            APP_LOG("waspPollTask", LOG_DEBUG, "brightness change is considered as manual intervention. Notify devices part of Away Rule, if exists!!");
                            notifyManualToggle();
                        }
                    }
#endif
                    /* If brightness modified from hardware during on-going fade, send stop fader notification */
                    if(g_faderRunning) {
                        unsigned char curr = 0;
                        usleep(1000000);
                        /* read live value of on-off state to confirm if the device is actually ON */

                        if(SUCCESS != getWaspVariable(WASP_VAR_ON_OFF, WASP_VARTYPE_UINT8, &curr)) {
                            APP_LOG("WiFiApp", LOG_ERR, "Fetching of WASP_VAR_ON_OFF failed.");
                        } else if(curr != 0) {
                            /* BRIGHTNESS changed in ON state */
                            g_faderRunning=0;
                            sendFaderStopNotification(0);
                            g_faderToTimer = true;
                        }
                    } else {
                        char valueStr[SIZE_4B];
                        snprintf(valueStr, sizeof(valueStr), "%u", New.CurrentBrightness);
                        addVarToWaspList(WASP_VAR_CURRENT_BRIGHTNESS, valueStr);
                    }
                }
            }


            if((New.Status & DPR_STAT_ON) != (Old.Status & DPR_STAT_ON)) {

#ifdef SIMULATED_OCCUPANCY
                if(LONG_PRESS_AWAY_ACTIVE ||
                   (gRuleHandle[e_AWAY_RULE].ruleCnt && (gpSimulatedDevice && gpSimulatedDevice->ruleEndTime))) {
                    /* whenever overtemp is set, wasp turns off the dimmer and notifies. It is not needed to
                       consider this notification as manual toggle. */
                    if(!onoffSetByApp && !g_overTemp) {
                        APP_LOG("waspPollTask", LOG_DEBUG, "plugin state toggled. Notify devices part of Away Rule, if exists!!");
                        notifyManualToggle();
                    }
                }
#endif

                /* On/off state has changed */
                if(New.Status & DPR_STAT_ON) {
                    APP_LOG("waspPollTask", LOG_DEBUG, "Dimmer turned ON");
                    g_PowerStatus=1;
                    addVarToWaspList(WASP_VAR_ON_OFF,"1");
                    if(true == onoffSetByApp) {
                        onoffSetByApp = false;
                        APP_LOG("waspPollTask", LOG_DEBUG, "onoffSetByApp set to false");
                    } else {
                        SetLastUserActionOnState(g_PowerStatus);
                        if(isCountDownRuleActive())
                            /* handle countdown timer */
                            executeCountdownRule(POWER_ON);
                        else
                            setAnimation(LED_STATE_TURN_ON);
                    }
                } else {
                    APP_LOG("waspPollTask", LOG_DEBUG, "Dimmer turned OFF");
                    g_PowerStatus=0;
                    addVarToWaspList(WASP_VAR_ON_OFF,"0");
                    if(true == onoffSetByApp) {
                        onoffSetByApp = false;
                        APP_LOG("waspPollTask", LOG_DEBUG, "onoffSetByApp set to false");
                    } else {
                        SetLastUserActionOnState(g_PowerStatus);
                        setAnimation(LED_STATE_TURN_OFF);
                        /*If device is turned OFF from HW during active countdown rule not in last minute */
                        checkAndExecuteCountdownTimer(POWER_OFF);
                    }

                    if(checkIfFaderRunning()) {
                        g_faderRunning=0;
                        cancelSleepTimer();
                        /* Send the stop fader notification */
                        sendFaderStopNotification(1);
                        /* sleep to let the user see OFF as well before the
                           RULE_CLOSE animation. */
                        pluginUsleep(500000);
                        /* reset the animation active for the fader */
                        setAnimation(LED_STATE_RULE_CLOSE);
                    }
                    /* if LP away is running on this device, it will override the
                       night mode settings */
                    if(!g_longPressAwayRunning && gNightModeActive && gpsNightMode) {
                        /*
                        WEMO-48335: Rule activation: Smart brightness rule handling
                        Set the night mode brightness while going OFF. As per WEMO-48337,
                        brightness can be overridden when device is OFF and next ON operation should
                        pick the last configured brightness.
                        */
                        APP_LOG("UPNPDevice", LOG_DEBUG, "Smart brightness change to %d", gpsNightMode->brightness);
                        setBrightness(gpsNightMode->brightness, false);
                    }
                }
            }

            if(New.bLongPress & LPR_BUTTON_IGNORED) {
                /* clear the status bit */
                //unsigned char longPress = LPR_BUTTON_IGNORED;
                unsigned char longPress  = LPR_BUTTON_BIT_CLR | (New.bLongPress & LPR_BUTTON_IGNORED);
                unsigned char inhibitTimer = 0; /*stop the timer */

                APP_LOG("waspPollTask", LOG_DEBUG, "Button pressed during last minute of auto-off rule, input: %x, output: %x", New.bLongPress, longPress);
                if(SUCCESS != setWaspVariable(WASP_VAR_LONG_PRESS, WASP_VARTYPE_UINT8, (void*)&longPress)) {
                    APP_LOG("waspPollTask", LOG_DEBUG, "Resetting the Button Ignore WASP variable failed!!");
                }

                /* stop the inhibit timer by writing 0 to it */
                if(SUCCESS != setWaspVariable(WASP_VAR_BUTTON_INHIBIT_TIMER, WASP_VARTYPE_UINT8, (void*)&inhibitTimer)) {
                    APP_LOG("waspPollTask", LOG_DEBUG, "Setting the inhibit timer failed!!");
                }
                checkAndExecuteCountdownTimer(POWER_OFF);
            }

            if(New.bLongPress & LPR_BUTTON_LONG_PRESS) {
                /* dont process long press event if in over heated state */
                if(!g_overTemp) {
                    APP_LOG("waspPollTask", LOG_DEBUG, "Plugin Long Pressed.");
                    /* set animation on detecting LONG_PRESS */
                    setAnimation(LED_STATE_LONG_PRESS_RULE);

                    LockLongPress();
                    gLongPressTriggered = 1;
                    APP_LOG("waspPollTask", LOG_DEBUG, "Plugin Long Pressed.:%d", gLongPressRuleActive);
                    UnlockLongPress();
                    /* gLongPressTriggered is cleared after sending remote notification, handle long press rules here */
                    if(gLongPressRuleActive) {
                        APP_LOG("waspPollTask", LOG_DEBUG, "Plugin Long Pressed.");
                        handleLongPressRule();
                    }
                    APP_LOG("Button",LOG_DEBUG,"set gLongPressTriggered: %d", gLongPressTriggered);
                }
                unsigned char longPress  = LPR_BUTTON_BIT_CLR | (New.bLongPress & LPR_BUTTON_LONG_PRESS);
                APP_LOG("waspPollTask", LOG_DEBUG, "input: %x, output: %x", New.bLongPress, longPress);
                if(SUCCESS != setWaspVariable(WASP_VAR_LONG_PRESS, WASP_VARTYPE_UINT8, (void*)&longPress)) {
                    APP_LOG("waspPollTask", LOG_DEBUG, "Resetting the Long Press WASP variable failed!!");
                }
            }

            memcpy(&Old,&New,sizeof(Old));
        }

    return NULL;
}
#endif
