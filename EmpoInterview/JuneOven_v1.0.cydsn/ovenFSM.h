
/** Enums **/
typedef enum {
    STATE_POR,
    STATE_INIT,
    STATE_SETUP,
    STATE_IDLE,
    STATE_COOK_AUTO,
    STATE_COOK_MANUAL,
    STATE_LOCK,
    STATE_DONE
} OVEN_FSM_STATE;

/** Structs **/

/**
 * @brief Struct for storing network connection parameters.
 * 
 */
typedef struct {
    bool isConnected;
    bool isUserConfigured;
    /* TODO Specific network parameters */
} iotState_s;


/**
 * @brief Struct for heater commands. Corresponds to the 4 heaters
 * in the oven.
 * 
 */
typedef struct {
    float h1;
    float h2;
    float h3;
    float h4;
} heaterCommand_t;

/**
 * @brief Struct for storing information about food in the oven
 * 
 */
typedef struct {
    bool isFoodPresent;
    float completionLevel;
    heaterCommand_t heaterCommand;
    uint32_t error;
} foodState_s;

/**
 * @brief Struct for storing complete state information for the
 * oven.
 * 
 */
typedef struct {
    OVEN_FSM_STATE state;
    iotState_s connectionState;
    foodState_s foodState;
    
    uint8_t *visual_avgImgA;
    uint8_t *visual_avgImgB;
    uint32_t visual_imageSize;
    
    uint8_t *infrared_avgImgA;
    uint8_t *infrared_avgImgB;
    uint32_t infrared_imageSize;

    bool activeA; /* Tracks which buffer is active */
    /* Images collected so far within a group; used in averaging */
    uint8_t nImages_visual;
    uint8_t nImages_infrared;

    bool stateComplete;
    uint32_t subStateCounter;
    OVEN_FSM_STATE nextState;

} ovenState_s;


/** Function declarations **/
void ovenFSM(void);
uint32_t hardwareInit(void);
uint32_t softwareInit(void);
void setupNetworkConnection(void);
void setupAppUser(void);
void controlHeaters(void);
void processedImageDataAvailable(float completionLevel, heaterCommand_t heaterCommand, uint32_t errorCode);

/* Provided */
void startVisualCamera(void);
void stopVisualCamera(void);
void startInfraredCamera(void);
void stopInfraredCamera(void);
void visualImageDataAvailable(int pixelCount, uint8_t *data);
void infraredImageDataAvailable(int pixelCount, uint8_t *data);

