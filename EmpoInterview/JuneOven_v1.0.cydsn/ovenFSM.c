
#include "ovenFSM.h"
#include <stdbool.h>

/****** Global variables ******/

/* Struct for storing state information */
ovenState_s ovenState;

/** Event flags **/
/* Flag indicating the user hit ok/next/ignore */
bool flag_userHandled = false;
bool flag_manualCook = false;

/****** Function definitions ******/

void ovenFSM(void) {

    /* Handle state transitions */
    switch (ovenState.state) {
        case STATE_POR: {
            ovenState.state = STATE_INIT;
            break;
        }
        default {
            if (ovenState.stateComplete) {
                ovenState.stateComplete = false;
                ovenState.subStateCounter = 0;
                ovenState.state = ovenState.nextState;        
            }
            break;
        }
    }

    /* Execute state */
    switch (ovenState.state)
    {
        case STATE_INIT: {
            
            /* Initialize hardware and software */
            hardwareInit();
            softwareInit();

            ovenState.stateComplete = true;

            /* Determine next state */
            if ((!ovenState.connectionState.isConnected) ||
                (!ovenState.connectionState.isUserConfigured)) {
                ovenState.nextState = STATE_SETUP;
            } else {
                ovenState.nextState = STATE_IDLE;
            }

            break;
        }

        case STATE_SETUP: {

            if (!ovenState.connectionState.isConnected) {
                /* Setup network first */
                setupNetworkConnection();
            } else if (!ovenState.connectionState.isUserConfigured) {
                /* Allow user setup once connected */
                setupAppUser();
            } else {
                /* Setup complete */
                ovenState.stateComplete = true;
                ovenState.nextState = STATE_IDLE;
            }
            
            if (flag_userHandled) {
                /* Allow user to bypass setup and use reduced feature set */
                flag_userHandled = false;
                ovenState.stateComplete = true;
                ovenState.nextState = STATE_IDLE;
            } 

            break;
        }

        case STATE_IDLE: {

            if (0 == ovenState.subStateCounter) {
                /* First entry */
                ovenState.subStateCounter++;
                if (ovenState.connectionState.isConnected) {
                    startVisualCamera();
                    startInfraredCamera();
                }
            }

            /* Wait for instruction or food detection */
            if (ovenState.foodState.isFoodPresent) {
                ovenState.stateComplete = true;
                ovenState.nextState = STATE_COOK_AUTO;
            } else if (flag_manualCook) {
                ovenState.stateComplete = true;
                ovenState.nextState = STATE_COOK_MANUAL;
            }

            break;
        }

        case STATE_COOK_AUTO: {
            if (0 == ovenState.subStateCounter) {
                /* First entry */
                ovenState.subStateCounter++;
                
            }

            controlHeaters();
            
            /* Exit state on error */
            if (0 != ovenState.foodState.error) {
                ovenState.stateComplete = true;
                ovenState.nextState = STATE_LOCK;
            }
            /* Exit state if cooking completes */
            else if (1.0 == ovenState.foodState.completionLevel) {
                ovenState.stateComplete = true;
                ovenState.nextState = STATE_DONE;
            }

            break;
        }

        case STATE_DONE: {
            
            if (0 == ovenState.subStateCounter) {
                /* First entry */
                ovenState.subStateCounter++;
                
            }

            controlHeaters();
            
            /* Exit state if food is removed or user handles */
            if ((!ovenState.foodState.isFoodPresent) || flag_userHandled) {
                flag_userHandled = false;
                ovenState.stateComplete = true;
                ovenState.nextState = STATE_IDLE;
            }

            break;

        }

        default: {
            /* Would not reach in practice */
            break;
        }
    }
}


/**
 * @brief Add this frame to the running average for visual images
 * 
 * @param pixelCount
 *          Number of pixels in the image (3-bytes per pixel)
 * @param data 
 *          Pointer to the array of image data
 */
void visualImageDataAvailable(int pixelCount, uint8_t *data) {

    /* Lock */
    CyEnterCriticalSection;

    if (0 == ovenState.nImages_visual) {
        /* Allocate data to store the average image */
        /* Assume pixelCount won't change between power cycles */
        if (ovenState.activeA) {
            ovenState.visual_avgImgA = malloc(3 * pixelCount);
        } else {
            ovenState.visual_avgImgB = malloc(3 * pixelCount);
        }
    }

    /* Determine which memory space to use */
    uint8_t *activeBaseAddr;
    if (ovenState.activeA) {
        activeBaseAddr = ovenState.visual_avgImgA;
    } else {
        activeBaseAddr = ovenState.visual_avgImgB;
    }

    /* Factor this frame into the running average */
    for (uint32_t i = 0; i < (pixelCount * 3); i++) {
        activeBaseAddr[i] = ((activeBaseAddr[i] * ovenState.nImages_visual) + data[i]) / (ovenState.nImages_visual + 1);
    }

    /* Increment image count */
    ovenState.nImages_visual++;

    /* Unlock */
    CyExitCriticalSection;
}

/**
 * @brief Add this frame to the running average for infrared images
 * 
 * @param pixelCount
 *          Number of pixels in the image (2-bytes per pixel)
 * @param data 
 *          Pointer to the array of image data
 */
void infraredImageDataAvailable(int pixelCount, uint8_t *data) {
    
    /* Lock */
    CyEnterCriticalSection;
    
    if (0 == ovenState.nImages_infrared) {
        /* Allocate data to store the average image */
        /* Assume pixelCount won't change between power cycles */
        if (ovenState.activeA) {
            ovenState.infrared_avgImgA = malloc(2 * pixelCount);
        } else {
            ovenState.infrared_avgImgB = malloc(2 * pixelCount);
        }
    }

    /* Determine which memory space to use */
    uint8_t *activeBaseAddr;
    if (ovenState.activeA) {
        activeBaseAddr = ovenState.infrared_avgImgA;
    } else {
        activeBaseAddr = ovenState.infrared_avgImgB;
    }

    /* Factor this frame into the running average */
    for (uint32_t i = 0; i < (pixelCount * 2); i++) {
        activeBaseAddr[i] = ((activeBaseAddr[i] * ovenState.nImages_infrared) + data[i]) / (ovenState.nImages_infrared + 1);
    }

    /* Increment image count */
    ovenState.nImages_infrared++;

    /* Unlock */
    CyExitCriticalSection;
}

/**
 * @brief Callback function to handle processed image data.
 * 1. Updates the state struct so it may be handled in the FSM and
 * enacted in hardware.
 * 2. Sends the next image data for processing
 * 3. Switches which buffers are active (collecting average vs. sending for processing)
 * 
 * 
 * @param completionLevel
 *          The 0.0-1.0 cooking completion level of the food
 * @param heaterCommand 
 *          The 0.0-1.0 heat level commands for the four heaters in the oven.
*           ASSUME algorithm returns 0.05 for all heaters when completion = 1.0 (keep warm)
 * @param errorCode 
 *          Code indicating if there's an error in the process (burning, etc)
 */
void processedImageDataAvailable(float completionLevel, heaterCommand_t heaterCommand, uint32_t errorCode) {

    /* Update state structs */
    ovenState.foodState.completionLevel = completionLevel;
    ovenState.foodState.heaterCommand.h1 = heaterCommand.h1;
    ovenState.foodState.heaterCommand.h2 = heaterCommand.h2;
    ovenState.foodState.heaterCommand.h3 = heaterCommand.h3;
    ovenState.foodState.heaterCommand.h4 = heaterCommand.h4;
    ovenState.foodState.errorCode = errorCode;

    /* Lock */
    CyEnterCriticalSection;

    if (ovenState.activeA) {
        sendImages(ovenState.infrared_avgImgA, ovenState.infrared_imageSize, ovenState.visual_avgImgA, ovenState.visual_imageSize);
        ovenState.nImages_visual = 0;
        ovenState.nImages_infrared = 0;
        free(ovenState.visual_avgImgA);
        free(ovenState.infrared_avgImgA);
    } else {
        sendImages(ovenState.infrared_avgImgB, ovenState.infrared_imageSize, ovenState.visual_avgImgB, ovenState.visual_imageSize);
        ovenState.nImages_visual = 0;
        ovenState.nImages_infrared = 0;
        free(ovenState.visual_avgImgB);
        free(ovenState.infrared_avgImgB);
    }

    /* Switch active buffer */
    ovenState.activeA = !ovenState.activeA;

    /* Unlock */
    CyExitCriticalSection;
}

/**
 * @brief Function for controlling the oven's heaters.
 * 
 * Utilizes the commands in ovenState.foodState.heaterCommand, which is
 * updated through automatic and manual cooking.
 */
void controlHeaters(void) {
    void dummy_updateSlowPWM(uint8_t heaterID, float duty);

    dummy_updateSlowPWM(1, ovenState.foodState.heaterCommand.h1);
    dummy_updateSlowPWM(2, ovenState.foodState.heaterCommand.h2);
    dummy_updateSlowPWM(3, ovenState.foodState.heaterCommand.h3);
    dummy_updateSlowPWM(4, ovenState.foodState.heaterCommand.h4);
}