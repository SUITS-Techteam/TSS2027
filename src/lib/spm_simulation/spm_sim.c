#include "spm_sim.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define PI 					3.14159265f
#define JOY_DEADZONE        0.05f

// starting degrees, y-axis disabled for now
#define START_TILT_X        5.0f
#define START_TILT_Y        0.0f

// levelling control
#define MAX_TILT_DEG            5.0f
#define LEVEL_ACCEL        		20.0f
#define LEVEL_DRAG        		0.45f // lower = platform coasts further
#define LEVEL_MAX_SPEED         6.0f
#define LEVEL_TOL_DEG           1.0f // must be within 1 degree to lock
#define LEVEL_TOL_SPEED      	0.3f // must be <= this speed to lock

// bubble is like a spring that follows the tilt
// values can be adjusted to change the feel of the bubble
#define BUBBLE_RANGE   			5.0f   // tilt that pins the bubble to the end
#define BUBBLE_FREQ          	0.6f   // how fast the bubble reacts
#define BUBBLE_DAMPING_RATIO    0.35f   // how much the bubble wobbles
#define BUBBLE_WALL_BOUNCE		0.6f   // how much the bubble bounces off the ends
#define BUBBLE_SLOSH            2.5f // how "sloshy" the bubble is, makes it more liquid like

// orientation control, nearly the same as leveling
// randomized target degree between 60 and 300
// teams can get close within 10 degrees, then have to use
// the voltage to find the target
#define SUN_AZ_MIN_DEG          60.0f
#define SUN_AZ_MAX_DEG          300.0f
#define SUN_APPROX_STEP_DEG     10.0f
#define AZ_ACCEL                12.0f
#define AZ_DRAG                 0.5f
#define AZ_MAX_SPEED            15.0f
#define AZ_TOL_DEG              1.0f
#define AZ_TOL_SPEED         	0.5f     // must be nearly stopped to lock

#define VOLTAGE_MAX             30.0f
#define SUN_PEAK_WIDTH_DEG      4.0f     // std dev of the peak
#define VOLTAGE_NOISE_V         1.2f     // std dev of each reading

#define LED_GAMMA               2.2f

static const char* SPM_STATES[] = { "off", "booting", "leveling", "deploying", "orienting", "complete" };
static const char* TRUSS[]      = { "stowed", "unlocked", "raising", "deployed", "locked" };

// cJSON helpers
static void cjson_set(cJSON* obj, const char* key, cJSON* item) {
    if (cJSON_GetObjectItemCaseSensitive(obj, key)) {
        cJSON_ReplaceItemInObjectCaseSensitive(obj, key, item);
    } else {
        cJSON_Delete(item);
    }
}

static cJSON* cjson_get(cJSON* obj, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

static float get_num(cJSON* obj, const char* key) {
    cJSON* item = cjson_get(obj, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : 0.0f;
}

// rounded to 4 decimals so the 20 Hz floats stay readable
static void set_num(cJSON* obj, const char* key, float v) {
    cjson_set(obj, key, cJSON_CreateNumber(round(v * 10000.0) / 10000.0));
}

static bool get_bool(cJSON* obj, const char* key) {
    return cJSON_IsTrue(cjson_get(obj, key));
}

static spm_state_t get_status(cJSON* spm) {
    const char* s = cJSON_GetStringValue(cjson_get(spm, "status"));
    for (int i = SPM_OFF; i <= SPM_COMPLETE; ++i) {
        if (s && strcmp(s, SPM_STATES[i]) == 0) return i;
    }
    return SPM_OFF;
}

static truss_t get_truss(cJSON* spm) {
    const char* s = cJSON_GetStringValue(cjson_get(spm, "truss"));
    for (int i = TRUSS_STOWED; i <= TRUSS_LOCKED; ++i) {
        if (s && strcmp(s, TRUSS[i]) == 0) return i;
    }
    return TRUSS_STOWED;
}
static void set_status(cJSON* spm, spm_state_t s) {
    cjson_set(spm, "status", cJSON_CreateString(SPM_STATES[s]));
}

static void set_truss(cJSON* spm, truss_t t) {
    cjson_set(spm, "truss", cJSON_CreateString(TRUSS[t]));
}

// math helpers
static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
// keeps very small stick movements from registering
static float deadzone(float v)
{
    float mag = fabsf(v);
    if (mag < JOY_DEADZONE) return 0.0f;
    if (mag > 1.0f) mag = 1.0f;
    mag = (mag - JOY_DEADZONE) / (1.0f - JOY_DEADZONE);
    if (v < 0.0f) return -mag;
    return mag;
}
// signed shortest way from `from` to `to`, degrees, in (-180, 180]
static float angle_diff(float to, float from)
{
    float err = fmodf(to - from, 360.0f);
    if (err > 180.0f)   err -= 360.0f;
    if (err <= -180.0f) err += 360.0f;
    return err;
}
static float random_uniform(void) {
    return (float)rand() / RAND_MAX;
}

// roughly normal, mean 0 and std dev 1
static float random_gauss(void) {
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) sum += random_uniform();
    return (sum - 2.0f) / 0.57735f;
}

static float bubble_target(float tilt) {
    return clampf(tilt / BUBBLE_RANGE, -1.0f, 1.0f);
}

// joystick accelerates the platform, drag slows it, stops at max tilt
static void move_tilt(float* tilt, float* speed, float joy) {
    *speed += deadzone(joy) * LEVEL_ACCEL * SPM_DT;
    *speed -= *speed * LEVEL_DRAG * SPM_DT;
    *speed  = clampf(*speed, -LEVEL_MAX_SPEED, LEVEL_MAX_SPEED);
    *tilt  += *speed * SPM_DT;

    if (fabsf(*tilt) > MAX_TILT_DEG) {
        *tilt  = clampf(*tilt, -MAX_TILT_DEG, MAX_TILT_DEG);
        *speed = 0.0f;
    }
}

// bubble springs toward the tilt and sloshes when the platform speeds up or slows down
static void move_bubble(float* pos, float* speed, float tilt, float tilt_speed_change) {
    float omega     = 2.0f * PI * BUBBLE_FREQ;
    float stiffness = omega * omega;
    float damping   = 2.0f * BUBBLE_DAMPING_RATIO * omega;

    *speed -= BUBBLE_SLOSH * tilt_speed_change / BUBBLE_RANGE;
    *speed += (stiffness * (bubble_target(tilt) - *pos) - damping * *speed) * SPM_DT;
    *pos   += *speed * SPM_DT;

    if (*pos > 1.0f) {
        *pos   = 1.0f;
        *speed = -fabsf(*speed) * BUBBLE_WALL_BOUNCE;
    }
    if (*pos < -1.0f) {
        *pos   = -1.0f;
        *speed = fabsf(*speed) * BUBBLE_WALL_BOUNCE;
    }
}

static void update_bubble(cJSON* spm, spm_ctx_t* ctx) {
    move_bubble(&ctx->bubble_x, &ctx->bubble_speed_x, get_num(spm, "tilt_x"), ctx->tilt_speed_x - ctx->last_tilt_speed_x);
    move_bubble(&ctx->bubble_y, &ctx->bubble_speed_y, get_num(spm, "tilt_y"), ctx->tilt_speed_y - ctx->last_tilt_speed_y);
    ctx->last_tilt_speed_x = ctx->tilt_speed_x;
    ctx->last_tilt_speed_y = ctx->tilt_speed_y;
}

// voltage peaks facing the sun and drops with tilt, only once the array is raised
static void update_voltage(cJSON* spm, spm_ctx_t* ctx) {
    truss_t truss = get_truss(spm);
    float volts = 0.0f;

    if (truss == TRUSS_DEPLOYED || truss == TRUSS_LOCKED) {
        float x = get_num(spm, "tilt_x");
        float y = get_num(spm, "tilt_y");
        float tilt = sqrtf(x * x + y * y);
        float off_sun = angle_diff(ctx->sun_azimuth, get_num(spm, "azimuth")) / SUN_PEAK_WIDTH_DEG;

        // new noise once a second to match the student update rate
        if (ctx->noise_ticks == 0) ctx->voltage_noise = VOLTAGE_NOISE_V * random_gauss();
        ctx->noise_ticks = (ctx->noise_ticks + 1) % SPM_TICK_HZ;

        volts = VOLTAGE_MAX * expf(-0.5f * off_sun * off_sun) * cosf(tilt * PI / 180.0f) + ctx->voltage_noise;
        if (volts < 0.0f) volts = 0.0f;
    }
    set_num(spm, "voltage", volts);
}


// leveling: joystick tilts the platform in mode 0, locks once level and nearly still
static void level_tick(cJSON* spm, spm_ctx_t* ctx, const spm_inputs_t* in) {
    float x = get_num(spm, "tilt_x");
    float y = get_num(spm, "tilt_y");

    if (in->mode == 0) {
        move_tilt(&x, &ctx->tilt_speed_x, in->joy_x);
        move_tilt(&y, &ctx->tilt_speed_y, in->joy_y);
    } else {
        ctx->tilt_speed_x = 0.0f;
        ctx->tilt_speed_y = 0.0f;
    }
    set_num(spm, "tilt_x", x);
    set_num(spm, "tilt_y", y);

    bool level = fabsf(x) <= LEVEL_TOL_DEG && fabsf(y) <= LEVEL_TOL_DEG;
    bool still = fabsf(ctx->tilt_speed_x) <= LEVEL_TOL_SPEED && fabsf(ctx->tilt_speed_y) <= LEVEL_TOL_SPEED;
    cjson_set(spm, "level", cJSON_CreateBool(level));

    if (in->lock_pressed && level && still) {
        ctx->tilt_speed_x = 0.0f;
        ctx->tilt_speed_y = 0.0f;
        cjson_set(spm, "level_locked", cJSON_CreateBool(true));
        set_status(spm, SPM_DEPLOYING);
    }
}

// deploying: unlock, raise, then lock the truss
static void truss_tick(cJSON* spm, const spm_inputs_t* in) {
    switch (get_truss(spm)) {
        case TRUSS_STOWED:
            if (in->unlock_pressed) set_truss(spm, TRUSS_UNLOCKED);
            break;

        case TRUSS_UNLOCKED:
            if (in->raise_pressed) set_truss(spm, TRUSS_RAISING);
            break;

        case TRUSS_RAISING: {
            float height = get_num(spm, "truss_height") + SPM_DT / 8.0f;
            if (height >= 1.0f) {
                height = 1.0f;
                set_truss(spm, TRUSS_DEPLOYED);
            }
            set_num(spm, "truss_height", height);
            break;
        }

        case TRUSS_DEPLOYED:
            if (in->lock_pressed) {
                set_truss(spm, TRUSS_LOCKED);
                set_status(spm, SPM_ORIENTING);
            }
            break;

        case TRUSS_LOCKED:
            break;
    }
}

// orienting: joystick turns the array in mode 1, lock it, then verify against the sun
static void orient_tick(cJSON* spm, spm_ctx_t* ctx, const spm_inputs_t* in) {
    bool locked = get_bool(spm, "orientation_locked");
    float azimuth = get_num(spm, "azimuth");

    if (locked && in->unlock_pressed) locked = false;

    if (!locked) {
        if (in->mode == 1) ctx->azimuth_speed += deadzone(in->joy_z) * AZ_ACCEL * SPM_DT;
        ctx->azimuth_speed -= ctx->azimuth_speed * AZ_DRAG * SPM_DT;
        ctx->azimuth_speed  = clampf(ctx->azimuth_speed, -AZ_MAX_SPEED, AZ_MAX_SPEED);
        azimuth = fmodf(azimuth + ctx->azimuth_speed * SPM_DT + 360.0f, 360.0f);

        if (in->lock_pressed && fabsf(ctx->azimuth_speed) <= AZ_TOL_SPEED) {
            locked = true;
            ctx->azimuth_speed = 0.0f;
        }
    }

    if (locked && in->verify_pressed && fabsf(angle_diff(ctx->sun_azimuth, azimuth)) <= AZ_TOL_DEG) {
        cjson_set(spm, "azimuth_aligned", cJSON_CreateBool(true));
        set_num(spm, "azimuth_target", ctx->sun_azimuth);
        set_status(spm, SPM_COMPLETE);
    }

    set_num(spm, "azimuth", azimuth);
    cjson_set(spm, "orientation_locked", cJSON_CreateBool(locked));
}


void spm_sim_reset(cJSON* spm, spm_ctx_t* ctx) {
    cjson_set(spm, "status",             cJSON_CreateString(SPM_STATES[SPM_OFF]));
    cjson_set(spm, "tilt_x",             cJSON_CreateNumber(START_TILT_X));
    cjson_set(spm, "tilt_y",             cJSON_CreateNumber(START_TILT_Y));
    cjson_set(spm, "level",              cJSON_CreateBool(false));
    cjson_set(spm, "level_locked",       cJSON_CreateBool(false));
    cjson_set(spm, "truss",              cJSON_CreateString(TRUSS[TRUSS_STOWED]));
    cjson_set(spm, "truss_height",       cJSON_CreateNumber(0));
    cjson_set(spm, "azimuth",            cJSON_CreateNumber(0));
    cjson_set(spm, "sun_azimuth_approx", cJSON_CreateNumber(0));
    cjson_set(spm, "azimuth_target",     cJSON_CreateNumber(0));
    cjson_set(spm, "azimuth_aligned",    cJSON_CreateBool(false));
    cjson_set(spm, "orientation_locked", cJSON_CreateBool(false));
    cjson_set(spm, "voltage",            cJSON_CreateNumber(0));

    memset(ctx, 0, sizeof(*ctx));
    ctx->bubble_x = bubble_target(START_TILT_X);
    ctx->bubble_y = bubble_target(START_TILT_Y);
}

cJSON* spm_sim_create(void) {
    cJSON* spm = cJSON_CreateObject();
    cJSON_AddBoolToObject(spm,   "power", false);
    cJSON_AddStringToObject(spm, "status", "off");
    cJSON_AddNumberToObject(spm, "mode", 0);
    cJSON_AddStringToObject(spm, "link", "none");
    cJSON_AddNumberToObject(spm, "tilt_x", START_TILT_X);
    cJSON_AddNumberToObject(spm, "tilt_y", START_TILT_Y);
    cJSON_AddBoolToObject(spm,   "level", false);
    cJSON_AddBoolToObject(spm,   "level_locked", false);
    cJSON_AddStringToObject(spm, "truss", "stowed");
    cJSON_AddNumberToObject(spm, "truss_height", 0);
    cJSON_AddNumberToObject(spm, "azimuth", 0);
    cJSON_AddNumberToObject(spm, "sun_azimuth_approx", 0);
    cJSON_AddNumberToObject(spm, "azimuth_target", 0);
    cJSON_AddBoolToObject(spm,   "azimuth_aligned", false);
    cJSON_AddBoolToObject(spm,   "orientation_locked", false);
    cJSON_AddNumberToObject(spm, "voltage", 0);
    return spm;
}

void spm_sim_tick(cJSON* spm, spm_ctx_t* ctx, const spm_inputs_t* in) {
    spm_state_t status = get_status(spm);

    // power and mode always reflect the switches
    cjson_set(spm, "power", cJSON_CreateBool(in->power));
    cjson_set(spm, "mode",  cJSON_CreateNumber(in->mode));

    if (!in->power) {
        spm_sim_reset(spm, ctx);
        return;
    }

    switch (status) {
        case SPM_OFF:
            ctx->sun_azimuth = SUN_AZ_MIN_DEG + random_uniform() * (SUN_AZ_MAX_DEG - SUN_AZ_MIN_DEG);
            set_num(spm, "sun_azimuth_approx", roundf(ctx->sun_azimuth / SUN_APPROX_STEP_DEG) * SUN_APPROX_STEP_DEG);
            set_status(spm, SPM_BOOTING);
            ctx->boot_ticks = 0;
            break;

        case SPM_BOOTING:
            if (++ctx->boot_ticks >= 80) {
                set_status(spm, SPM_LEVELING);
            }
            break;

        case SPM_LEVELING:  level_tick(spm, ctx, in);  break;
        case SPM_DEPLOYING: truss_tick(spm, in);       break;
        case SPM_ORIENTING: orient_tick(spm, ctx, in); break;
        case SPM_COMPLETE:  break;
    }

    update_bubble(spm, ctx);
    update_voltage(spm, ctx);
}

// runs one second of ticks, button presses only count on the first
void spm_sim_tick_second(cJSON* spm, spm_ctx_t* ctx, const spm_inputs_t* in) {
    spm_inputs_t step = *in;
    for (int i = 0; i < SPM_TICK_HZ; ++i) {
        spm_sim_tick(spm, ctx, &step);
        step.unlock_pressed = false;
        step.raise_pressed  = false;
        step.lock_pressed   = false;
        step.verify_pressed = false;
    }
}

// converts brightness (0 to 1) to PWM duty (0-255), gamma corrected
static uint8_t gamma_duty(float brightness) {
    return (uint8_t)(255.0f * powf(clampf(brightness, 0.0f, 1.0f), LED_GAMMA) + 0.5f);
}

// spreads the bubble position (-1 to 1) across the two nearest LEDs
void spm_bubble_led_levels(float bubble, int led_count, uint8_t* out) {
    if (!out || led_count < 1) return;
    memset(out, 0, led_count);

    if (led_count == 1) {
        out[0] = 255;
        return;
    }

    float spot = (clampf(bubble, -1.0f, 1.0f) + 1.0f) * 0.5f * (led_count - 1);
    int left = (int)spot;
    float blend = spot - left;

    out[left] = gamma_duty(1.0f - blend);
    if (left + 1 < led_count) out[left + 1] = gamma_duty(blend);
}
