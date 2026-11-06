#ifndef SPM_SIM_H
#define SPM_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

#define SPM_TICK_HZ       20
#define SPM_DT            (1.0f / SPM_TICK_HZ)
#define SPM_SNAPSHOT_CMD  3

typedef enum { SPM_OFF, SPM_BOOTING, SPM_LEVELING, SPM_DEPLOYING, SPM_ORIENTING, SPM_COMPLETE } spm_state_t;
typedef enum { TRUSS_STOWED, TRUSS_UNLOCKED, TRUSS_RAISING, TRUSS_DEPLOYED, TRUSS_LOCKED } truss_t;

typedef struct {
    bool power;            // all power switches closed
    int mode;              // 0 = level, 1 = orient
    float joy_x, joy_y, joy_z;
    bool unlock_pressed;
    bool raise_pressed;
    bool lock_pressed;
    bool verify_pressed;
} spm_inputs_t;

// simulation only values
typedef struct {
    int boot_ticks;
    float tilt_speed_x, tilt_speed_y;
    float last_tilt_speed_x, last_tilt_speed_y;
    float bubble_x, bubble_y;
    float bubble_speed_x, bubble_speed_y;
    float azimuth_speed;
    float sun_azimuth;     // hidden from students
    float voltage_noise;
    int noise_ticks;
} spm_ctx_t;

cJSON* spm_sim_create(void);
void spm_sim_reset(cJSON* spm, spm_ctx_t* ctx);
void spm_sim_tick(cJSON* spm, spm_ctx_t* ctx, const spm_inputs_t* in);

// backup tick for TSS, runs one second of ticks
void spm_sim_tick_second(cJSON* spm, spm_ctx_t* ctx, const spm_inputs_t* in);

// crossfades the bubble (-1 to 1) across the LEDs like a bubble level
void spm_bubble_led_levels(float bubble, int led_count, uint8_t* out);

#endif
