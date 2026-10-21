#ifndef SPM_SIM_H
#define SPM_SIM_H

#include <stdint.h>
#include "cJSON.h"

#define SPM_TICK_HZ       20
#define SPM_DT            (1.0f / SPM_TICK_HZ)
#define SPM_SNAPSHOT_CMD  3001
// test with x-axis only for now
#define SPM_USE_Y_AXIS    0

typedef struct {
    int   switches_on;  	// all power switches closed
    int   mode;      		// 0 = level, 1 = orient
    float joy_x, joy_y;      	// joystick x and y axes
    int   unlock_pressed;   	// immediate button locks
    int   deploy_pressed;
    int   lock_pressed;
    int   verify_pressed;
} spm_inputs_t;

// sim only values not in JSON
typedef struct {
    float boot_timer;
	float last_tilt_vx, last_tilt_vy;
    float tilt_vx, tilt_vy;
	float bubble_x, bubble_y;
    float bubble_vx, bubble_vy;
    float az_velocity;
    float tilt_velocity;
	float sun_az;
	float voltage_noise;
	int noise_ticks;
	uint32_t rng;
} spm_ctx_t;

// creates the cJSON struct to send to TSS
cJSON *spm_create_state(void);

// spm sim ticks 20 times a second to have smooth physics
void spm_tick(cJSON *state, spm_ctx_t *ctx, const spm_inputs_t *in);

// backup tick for TSS
void spm_tick_second(cJSON *state, spm_ctx_t *ctx, const spm_inputs_t *in);

// controls led bubble with crossfading for smooth leveler-like bubble
void spm_bubble_led_levels(float pos, int led_count, uint8_t *out);

#endif
