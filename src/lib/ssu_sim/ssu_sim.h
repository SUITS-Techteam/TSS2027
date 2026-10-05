#ifndef SSU_SIM_H
#define SSU_SIM_H

#include <stdbool.h>
#include "cJSON.h"

typedef enum { SSU_OFF, SSU_BOOTING, SSU_READY } ssu_state_t;
typedef enum { NOT_READY, SENSOR_PRIMED, SENSOR_DEPLOYED } sensor_t;
typedef enum { IDLE, DRILLING, OVERHEATED, RETRACTING, RETRACTED, DEPLOYED } state_t;
typedef enum { THERMAL_NOMINAL, THERMAL_WARNING, THERMAL_CRITICAL } thermal_t;

// One tick of input, read from hardware (Pi) or from the JSON/web form (TSS)
typedef struct {
    bool power;
    int mode;              // 0 = short period, 1 = broadband
    float rpm;             // requested drill rpm
    bool deploy_pressed;   // true for one tick per press
    bool retract_pressed;  // true for one tick per press
} ssu_inputs_t;

// Simulation-only values, never published
typedef struct {
    int boot_ticks;
    int last_mode;
    int team;              // only used in log messages
} ssu_ctx_t;

cJSON* ssu_sim_create(void);                                          // ssu object with every field at its default
void ssu_sim_reset(cJSON* ssu, ssu_ctx_t* ctx);
void ssu_sim_tick(cJSON* ssu, ssu_ctx_t* ctx, const ssu_inputs_t* in);  // call once per second

#endif
