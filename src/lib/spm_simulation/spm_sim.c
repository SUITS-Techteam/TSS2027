#include "spm_sim.h"
#include <math.h>
#include <string.h>

#define SPM_PI 				3.14159265f
#define BOOT_STEP           5
#define BOOT_SECONDS       	1.0f
#define JOY_DEADZONE        0.05f

// starting degrees, y-axis disabled for now
#define START_TILT_X        5.0f
#if SPM_USE_Y_AXIS
#define START_TILT_Y       -4.0f
#else
#define START_TILT_Y        0.0f
#endif

// levelling control
#define MAX_TILT_DEG            5.0f
#define LEVEL_ACCEL        		20.0f
#define LEVEL_DRAG        		0.45f // lower = platform coasts further
#define LEVEL_MAX_SPEED         6.0f
#define LEVEL_TOL_DEG           1.0f // tolerable range for level (horizontal) platform
#define LEVEL_TOL_SPEED      	0.3f // must be moving at or slower than this speed to be able to lock

// bubble is like a spring that follows the tilt
// values can be adjusted to change the feel of the bubble
#define BUBBLE_RANGE   			5.0f   // tilt that pins the bubble to the end
#define BUBBLE_FREQ          	0.6f   // how fast the bubble reacts
#define BUBBLE_DAMPING_RATIO    0.35f   // how much the bubble wobbles
#define BUBBLE_WALL_BOUNCE		0.6f   // how much the bubble bounces off the ends
#define BUBBLE_SLOSH            2.5f // how "sloshy" the bubble is, makes it more liquid like

#define TRUSS_SECONDS           8.0f

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
#define SUN_PEAK_WIDTH_DEG      4.0f     /* std dev of the peak */
#define VOLTAGE_NOISE_V         1.2f     /* std dev of each reading */


#define LED_GAMMA               2.2f

typedef enum {
    OFF, BOOTING, LEVELING, DEPLOYING, ORIENTING, COMPLETE
} status_t;

static const char *status_names[] = {
    "off", "booting", "leveling", "deploying", "orienting", "complete"
};

// cJSON helpers
static double get_num(const cJSON *obj, const char *name)
{
    const cJSON *item = cJSON_GetObjectItem(obj, name);
    if (cJSON_IsNumber(item)) return item->valuedouble;
    return 0.0;
}
// rounded to 4 decimals for readability
static void set_num(cJSON *obj, const char *name, double v)
{
    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (cJSON_IsNumber(item)) {
        cJSON_SetNumberValue(item, round(v * 10000.0) / 10000.0);
    }
}
static int get_bool(const cJSON *obj, const char *name)
{
    if (cJSON_IsTrue(cJSON_GetObjectItem(obj, name))) return 1;
    return 0;
}
static void set_bool(cJSON *obj, const char *name, int v)
{
    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!cJSON_IsBool(item)) return;
    if (get_bool(obj, name) == (v != 0)) return;
    cJSON_ReplaceItemInObject(obj, name, cJSON_CreateBool(v != 0));
}
static const char *get_str(const cJSON *obj, const char *name)
{
    const cJSON *item = cJSON_GetObjectItem(obj, name);
    if (cJSON_IsString(item)) return item->valuestring;
    return "";
}
static void set_str(cJSON *obj, const char *name, const char *v)
{
    cJSON *item = cJSON_GetObjectItem(obj, name);
    if (!cJSON_IsString(item)) return;
    if (strcmp(item->valuestring, v) == 0) return;
    cJSON_ReplaceItemInObject(obj, name, cJSON_CreateString(v));
}
static status_t get_status(const cJSON *state)
{
    const char *name = get_str(state, "status");
    for (int i = 0; i <= COMPLETE; i++) {
        if (strcmp(name, status_names[i]) == 0) return (status_t)i;
    }
    return OFF;
}
static void set_status(cJSON *state, status_t status)
{
    set_str(state, "status", status_names[status]);
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
static float angle_error(float to, float from)
{
    float err = fmodf(to - from, 360.0f);
    if (err > 180.0f)   err -= 360.0f;
    if (err <= -180.0f) err += 360.0f;
    return err;
}
static uint32_t rng_next(spm_ctx_t *ctx)
{
    uint32_t x = ctx->rng;
    if (x == 0) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ctx->rng = x;
    return x;
}
static float rng_uniform(spm_ctx_t *ctx)
{
    return (rng_next(ctx) >> 8) / 16777216.0f;
}
static float rng_gauss(spm_ctx_t *ctx)
{
    float sum = 0.0f;
    for (int i = 0; i < 4; i++) sum += rng_uniform(ctx);
    return (sum - 2.0f) / 0.57735f;
}


static float bubble_target(float tilt_deg)
{
    return clampf(tilt_deg / BUBBLE_RANGE, -1.0f, 1.0f);
}

// spm state related functions
static void spm_reset(cJSON *state, spm_ctx_t *ctx)
{
    cJSON *tilt   = cJSON_GetObjectItem(state, "tilt");

	uint32_t rng = ctx->rng;
    memset(ctx, 0, sizeof(*ctx));
    ctx->rng = rng;

    set_bool(state, "power", 0);
    set_status(state, OFF);
    set_num(state, "mode", 0);
    set_bool(state, "level_locked", 0);
    set_bool(state, "level", 0);
    set_str(state, "truss", "stowed");
    set_num(state, "truss_height", 0.0);
    set_num(state, "azimuth", 0.0);
    set_num(state, "sun_azimuth_approx", 0.0);
    set_num(state, "azimuth_target", 0.0);
    set_bool(state, "azimuth_aligned", 0);
    set_bool(state, "orientation_locked", 0);
    set_num(state, "voltage", 0.0);

    if (tilt) {
        set_num(tilt, "x", START_TILT_X);
        set_num(tilt, "y", START_TILT_Y);
    }
    ctx->bubble_x = bubble_target(START_TILT_X);
    ctx->bubble_y = bubble_target(START_TILT_Y);
}

static void pick_angle(cJSON *state, spm_ctx_t *ctx)
{
    ctx->sun_az = SUN_AZ_MIN_DEG
                + rng_uniform(ctx) * (SUN_AZ_MAX_DEG - SUN_AZ_MIN_DEG);

    float approx = roundf(ctx->sun_az / SUN_APPROX_STEP_DEG) * SUN_APPROX_STEP_DEG;
    set_num(state, "sun_azimuth_approx", approx);
}

// per-status functions
static void step_booting(cJSON *state, spm_ctx_t *ctx)
{
    ctx->boot_timer += SPM_DT;

    if (ctx->boot_timer >= BOOT_STEP * BOOT_SECONDS) {
        set_status(state, LEVELING);
    }
}

// adds the "tilt" effect so the platform keeps moving in the direction of tilt
static void step_tilt_axis(float *tilt, float *vel, float joy)
{
    *vel += deadzone(joy) * LEVEL_ACCEL * SPM_DT;
    *vel -= *vel * LEVEL_DRAG * SPM_DT;
    *vel  = clampf(*vel, -LEVEL_MAX_SPEED, LEVEL_MAX_SPEED);
    *tilt += *vel * SPM_DT;

    // stops the tilt from going past max tilt degree
    if (*tilt > MAX_TILT_DEG) {
    	*tilt = MAX_TILT_DEG;
    	*vel = 0.0f;
    }
    if (*tilt < -MAX_TILT_DEG) {
    	*tilt = -MAX_TILT_DEG;
    	*vel = 0.0f;
    }
}
static void update_tilt(cJSON *state, spm_ctx_t *ctx, const spm_inputs_t *in,
                        status_t status)
{
    cJSON *tilt = cJSON_GetObjectItem(state, "tilt");
    int can_adjust = (status == LEVELING) && !in->mode;
    if (!tilt) return;

    if (!can_adjust) {
        ctx->tilt_vx = 0.0f;
        ctx->tilt_vy = 0.0f;
        return;
    }

    float joy_y = 0.0f;
    if (SPM_USE_Y_AXIS) joy_y = in->joy_y;

    float x = get_num(tilt, "x");
    float y = get_num(tilt, "y");
    step_tilt_axis(&x, &ctx->tilt_vx, in->joy_x);
    step_tilt_axis(&y, &ctx->tilt_vy, joy_y);
    set_num(tilt, "x", x);
    set_num(tilt, "y", y);
}

// updates the led bubble
static void step_bubble_axis(float *pos, float *vel, float target)
{
    float omega     = 2.0f * SPM_PI * BUBBLE_FREQ;
    float stiffness = omega * omega;
    float damping   = 2.0f * BUBBLE_DAMPING_RATIO * omega;

    *vel += (stiffness * (target - *pos) - damping * *vel) * SPM_DT;
    *pos += *vel * SPM_DT;

    // bounces off the ends
    if (*pos > 1.0f) {
	*pos = 1.0f;
	*vel = -fabsf(*vel) * BUBBLE_WALL_BOUNCE;
    }
    if (*pos < -1.0f) {
    	*pos = -1.0f;
    	*vel =  fabsf(*vel) * BUBBLE_WALL_BOUNCE;
    }
}
static void update_bubble(cJSON *state, spm_ctx_t *ctx)
{
    cJSON *tilt   = cJSON_GetObjectItem(state, "tilt");
    if (!tilt) return;


	ctx->bubble_vx -= BUBBLE_SLOSH * (ctx->tilt_vx - ctx->last_tilt_vx) / BUBBLE_RANGE;
    ctx->bubble_vy -= BUBBLE_SLOSH * (ctx->tilt_vy - ctx->last_tilt_vy) / BUBBLE_RANGE;
    ctx->last_tilt_vx = ctx->tilt_vx;
    ctx->last_tilt_vy = ctx->tilt_vy;

    step_bubble_axis(&ctx->bubble_x, &ctx->bubble_vx, bubble_target(get_num(tilt, "x")));
    step_bubble_axis(&ctx->bubble_y, &ctx->bubble_vy, bubble_target(get_num(tilt, "y")));

}
static void update_level_flag(cJSON *state, const spm_inputs_t *in)
{
    cJSON *tilt = cJSON_GetObjectItem(state, "tilt");
    if (!tilt) return;
    int level = fabs(get_num(tilt, "x")) <= LEVEL_TOL_DEG && fabs(get_num(tilt, "y")) <= LEVEL_TOL_DEG;
    set_bool(state, "level", level);
}
// locks the platform once level is reached and lock is pressed
static void step_leveling(cJSON *state, const spm_ctx_t *ctx,
                          const spm_inputs_t *in)
{
    if (!in->lock_pressed || !get_bool(state, "level")) return;
    if (fabsf(ctx->tilt_vx) > LEVEL_TOL_SPEED) return;
    if (fabsf(ctx->tilt_vy) > LEVEL_TOL_SPEED) return;

    set_bool(state, "level_locked", 1);
    set_status(state, DEPLOYING);
}

// controls truss deploying status
static void step_deploying(cJSON *state, const spm_inputs_t *in)
{
    const char *truss = get_str(state, "truss");

    if (strcmp(truss, "stowed") == 0) {
        if (in->unlock_pressed) set_str(state, "truss", "unlocked");
    } else if (strcmp(truss, "unlocked") == 0) {
        if (in->deploy_pressed && get_bool(state, "level")) {
            set_str(state, "truss", "raising");
        }
    } else if (strcmp(truss, "raising") == 0) {
        float h = get_num(state, "truss_height") + SPM_DT / TRUSS_SECONDS;
        if (h >= 1.0f) {
            h = 1.0f;
            set_str(state, "truss", "deployed");
        }
        set_num(state, "truss_height", h);
    } else if (strcmp(truss, "deployed") == 0) {
        if (in->lock_pressed) {
            set_str(state, "truss", "locked");
            set_status(state, ORIENTING);
        }
    }
}

// controls orienting status
static void step_orienting(cJSON *state, spm_ctx_t *ctx, const spm_inputs_t *in)
{
    int locked = get_bool(state, "orientation_locked");
    float az = get_num(state, "azimuth");

    if (locked && in->unlock_pressed) {
        set_bool(state, "orientation_locked", 0);
        locked = 0;
    }

    if (locked) {
        ctx->az_velocity = 0.0f;
    } else {
        if (in->mode) {
            ctx->az_velocity += deadzone(in->joy_x) * AZ_ACCEL * SPM_DT;
        }
        ctx->az_velocity -= ctx->az_velocity * AZ_DRAG * SPM_DT;
        ctx->az_velocity = clampf(ctx->az_velocity, -AZ_MAX_SPEED, AZ_MAX_SPEED);

        az = fmodf(az + ctx->az_velocity * SPM_DT, 360.0f);
        if (az < 0.0f) az += 360.0f;
        set_num(state, "azimuth", az);
    }

    if (!locked && in->lock_pressed && fabsf(ctx->az_velocity) <= AZ_TOL_SPEED) {
        set_bool(state, "orientation_locked", 1);
        ctx->az_velocity = 0.0f;
        locked = 1;
    }

    if (locked && in->verify_pressed) {
        float err = angle_error(ctx->sun_az, az);
        if (get_bool(state, "level") && fabsf(err) <= AZ_TOL_DEG) {
            set_bool(state, "azimuth_aligned", 1);
            set_num(state, "azimuth_target", ctx->sun_az);
            set_status(state, COMPLETE);
        }
    }
}
static void update_voltage(cJSON *state, spm_ctx_t *ctx)
{
    const char *truss = get_str(state, "truss");
    int array_up = strcmp(truss, "deployed") == 0 || strcmp(truss, "locked") == 0;
    double volts = 0.0;

    if (array_up) {
        cJSON *tilt = cJSON_GetObjectItem(state, "tilt");
        float err = angle_error(ctx->sun_az, get_num(state, "azimuth")) / SUN_PEAK_WIDTH_DEG;
        float tilt_deg = 0.0f;
        if (tilt) {
            float tx = get_num(tilt, "x");
            float ty = get_num(tilt, "y");
            tilt_deg = sqrtf(tx * tx + ty * ty);
        }
        volts = VOLTAGE_MAX * expf(-0.5f * err * err) * cosf(tilt_deg * SPM_PI / 180.0f);
		if (ctx->noise_ticks == 0) {
	        ctx->voltage_noise = VOLTAGE_NOISE_V * rng_gauss(ctx);
	    }
	    ctx->noise_ticks = (ctx->noise_ticks + 1) % SPM_TICK_HZ;
	    volts += ctx->voltage_noise;
        if (volts < 0.0) {
        	volts = 0.0;
       	}
    }
    set_num(state, "voltage", volts);
}

static const char *SPM_STATE_TEMPLATE =
    "{"
    "\"power\": false,"
    "\"status\": \"off\","
    "\"mode\": 0,"
	"\"link\": \"none\","
    "\"tilt\": { \"x\": 0.0, \"y\": 0.0 },"
    "\"level\": false,"
    "\"level_locked\": false,"
    "\"truss\": \"stowed\","
    "\"truss_height\": 0.0,"
    "\"azimuth\": 0.0,"
    "\"sun_azimuth_approx\": 0.0,"
    "\"azimuth_target\": 0.0,"
    "\"azimuth_aligned\": false,"
    "\"orientation_locked\": false,"
    "\"voltage\": 0.0"
    "}";

cJSON *spm_create_state(void)
{
    return cJSON_Parse(SPM_STATE_TEMPLATE);
}

void spm_tick(cJSON *state, spm_ctx_t *ctx, const spm_inputs_t *in)
{
    if (!state || !ctx || !in) return;

    if (!in->switches_on) {
        spm_reset(state, ctx);
        return;
    }

    status_t status = get_status(state);

    if (status == OFF) {
		spm_reset(state, ctx);
		pick_angle(state, ctx);
        set_bool(state, "power", 1);
        set_status(state, BOOTING);
        return;
    }
    if (status == BOOTING) {
        step_booting(state, ctx);
        return;
    }
    if (in->mode) set_num(state, "mode", 1);
    else {
    	set_num(state, "mode", 0);
    }
    update_tilt(state, ctx, in, status);
    update_bubble(state, ctx);
    update_level_flag(state, in);

    if (status == LEVELING)       step_leveling(state, ctx, in);
    else if (status == DEPLOYING) step_deploying(state, in);
    else if (status == ORIENTING) step_orienting(state, ctx, in);

    update_voltage(state, ctx);
}

void spm_tick_second(cJSON *state, spm_ctx_t *ctx, const spm_inputs_t *in)
{
    if (!in) return;

    spm_inputs_t step_in = *in;
    for (int i = 0; i < SPM_TICK_HZ; i++) {
        spm_tick(state, ctx, &step_in);

        step_in.unlock_pressed = 0;
        step_in.deploy_pressed = 0;
        step_in.lock_pressed   = 0;
        step_in.verify_pressed = 0;
    }
}

// converts brightness (0.0 to 1.0) to PWM duty (0 - 255), corrected for gamma
static uint8_t gamma_duty(float perceived)
{
    perceived = clampf(perceived, 0.0f, 1.0f);
    return (uint8_t)(255.0f * powf(perceived, LED_GAMMA) + 0.5f);
}
// translates bubble position to led brightness
void spm_bubble_led_levels(float pos, int led_count, uint8_t *out)
{
    if (!out || led_count < 1) return;
    memset(out, 0, (size_t)led_count);

    if (led_count == 1) {
        out[0] = 255;
        return;
    }

    float spot = (clampf(pos, -1.0f, 1.0f) + 1.0f) * 0.5f * (led_count - 1);
    int lo = (int)spot;
    float frac = spot - lo;

    out[lo] = gamma_duty(1.0f - frac);
    if (lo + 1 < led_count) out[lo + 1] = gamma_duty(frac);
}
