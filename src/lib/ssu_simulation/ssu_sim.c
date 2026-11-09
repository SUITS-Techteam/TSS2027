#include "ssu_sim.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BOOT_TICKS     5
#define TEMP_MIN      -25.0f
#define TEMP_WARNING   15.0f
#define TEMP_CRITICAL  35.0f
#define TEMP_UNLOCK    25.0f

#define AMBIENT_MIN   -25
#define AMBIENT_MAX	  -10


static const char* SSU_STATES[] = { "off", "booting", "ready" };
static const char* SENSORS[]    = { "stowed", "primed", "deployed" };
static const char* STATES[]     = { "idle", "drilling", "overheated", "retracting", "retracted", "deployed" };
static const char* THERMALS[]   = { "nominal", "warning", "critical" };

typedef struct {
    const char* name;
    const char *depth_key, *temp_key, *rpm_key, *state_key, *thermal_key, *sensor_key;
    float target_depth;
	float hard_depth;
	float hard_heat;
} drill_cfg_t;

static const drill_cfg_t SP = { "Short Period", "sp_depth", "sp_temp", "sp_rpm", "sp_state", "sp_thermal", "sp_sensor",  50.0f, 0.0f, 1.0f};
static const drill_cfg_t BB = { "Broadband",    "bb_depth", "bb_temp", "bb_rpm", "bb_state", "bb_thermal", "bb_sensor", 80.0f, 50.0f, 2.0f};


// json helpers
static void cjson_set(cJSON* obj, const char* key, cJSON* item) {
    if (cJSON_GetObjectItemCaseSensitive(obj, key)) {
        cJSON_ReplaceItemInObjectCaseSensitive(obj, key, item);
    } else {
        printf("Warning: tried to set missing field '%s'\n", key);
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

static ssu_state_t get_ssu_state(cJSON* ssu) {
    const char* s = cJSON_GetStringValue(cjson_get(ssu, "status"));
    for (int i = SSU_OFF; i <= SSU_READY; ++i) {
        if (s && strcmp(s, SSU_STATES[i]) == 0) return i;
    }
    return SSU_OFF;
}

static state_t get_state(cJSON* ssu, const char* key) {
    const char* s = cJSON_GetStringValue(cjson_get(ssu, key));
    for (int i = IDLE; i <= DEPLOYED; ++i) {
        if (s && strcmp(s, STATES[i]) == 0) return i;
    }
    return IDLE;
}

static thermal_t get_thermal(float t) {
    if (t >= TEMP_CRITICAL) return THERMAL_CRITICAL;
    if (t >= TEMP_WARNING)  return THERMAL_WARNING;
    return THERMAL_NOMINAL;
}

static float get_cool_rate(thermal_t thermal) {
    switch (thermal) {
        case THERMAL_CRITICAL: return 1.5f;
        case THERMAL_WARNING:  return 2.0f;
        default:               return 3.0f;
    }
}

static void set_state(cJSON* ssu, const drill_cfg_t* d, state_t s) {
    cjson_set(ssu, d->state_key, cJSON_CreateString(STATES[s]));
}

static float random_ambient(void) {
	return AMBIENT_MIN + rand() % (AMBIENT_MAX - AMBIENT_MIN + 1);
}


// drill tick by mode
static void drill_tick(cJSON* ssu, const drill_cfg_t* d, const ssu_inputs_t* in) {
    float depth = get_num(ssu, d->depth_key);
    float temp  = get_num(ssu, d->temp_key);
    state_t state = get_state(ssu, d->state_key);
    thermal_t thermal = get_thermal(temp);

    // drill can only spin while idle or drilling (not overheated or during deployment phases)
    bool can_drill = (state == IDLE || state == DRILLING);
    float rpm = can_drill ? in->rpm : 0.0f;
    bool heated = false;

    // retract only once the drill is stopped (idle) at the target depth
    if (in->retract_pressed && state == IDLE && depth >= d->target_depth) {
        set_state(ssu, d, RETRACTING);
        rpm = 0.0f;
    }
    else switch (state) {

        // idle: drill is stopped, starts drilling once rpm is set
        case IDLE:
            if (rpm > 0) set_state(ssu, d, DRILLING);
            break;

        // drilling: depth and heat increase, stops at the thermal limit
        case DRILLING: {
            if (rpm <= 0) {
                set_state(ssu, d, IDLE);
                break;
            }
            heated = true;
            float r = rpm / 600.0f;
			float heat = 6.0f * r * r * r;
			float new_temp = temp;

			if(d->hard_depth > 0 && depth >= d->hard_depth){
				heat = heat * d->hard_heat;
			}
			new_temp += heat;

            cjson_set(ssu, d->temp_key,  cJSON_CreateNumber(new_temp));
            cjson_set(ssu, d->depth_key, cJSON_CreateNumber(depth + (rpm / 300.0f) * 2.0f));
            if (new_temp >= TEMP_CRITICAL) {
                printf("CRITICAL: %s drill thermal limit reached, disengaging\n", d->name);
                set_state(ssu, d, OVERHEATED);
                rpm = 0.0f;
            }
            break;
        }

        // overheated: drill locked until it cools to the unlock temperature
        case OVERHEATED:
            if (temp <= TEMP_UNLOCK) set_state(ssu, d, IDLE);
            break;

        // retracting: depth decreases until back to 0
        case RETRACTING: {
            float new_depth = depth - 10.0f;
            if (new_depth <= 0.0f) {
                new_depth = 0.0f;
                set_state(ssu, d, RETRACTED);
            }
            cjson_set(ssu, d->depth_key, cJSON_CreateNumber(new_depth));
            break;
        }

        // retracted: sensor can be deployed
        case RETRACTED:
            if (in->deploy_pressed) {
                printf("Deploying %s Sensor\n", d->name);
                set_state(ssu, d, DEPLOYED);
                cjson_set(ssu, d->sensor_key, cJSON_CreateString(SENSORS[SENSOR_DEPLOYED]));
            }
            break;

        // deployed: final state, no more interaction
        case DEPLOYED:
            break;
    }

    // passive cooling whenever the drill didn't heat this tick
	float ambient = get_num(ssu,"ambient_temp");
    if (!heated && temp > ambient) {
        float new_temp = temp - get_cool_rate(thermal);
        if (new_temp < ambient) new_temp = ambient;
        cjson_set(ssu, d->temp_key, cJSON_CreateNumber(new_temp));
    }

    cjson_set(ssu, d->rpm_key, cJSON_CreateNumber(rpm));

    // publish the thermal level from the updated temperature
    thermal_t new_thermal = get_thermal(get_num(ssu, d->temp_key));
    if (thermal == THERMAL_NOMINAL && new_thermal == THERMAL_WARNING) {
        printf("WARNING: %s drill approaching thermal limit\n", d->name);
    }
    cjson_set(ssu, d->thermal_key, cJSON_CreateString(THERMALS[new_thermal]));
}

void ssu_sim_reset(cJSON* ssu, ssu_ctx_t* ctx) {
    cjson_set(ssu, "status",     cJSON_CreateString(SSU_STATES[SSU_OFF]));
    cjson_set(ssu, "sp_sensor",  cJSON_CreateString(SENSORS[STOWED]));
    cjson_set(ssu, "sp_depth",   cJSON_CreateNumber(0.0));
    cjson_set(ssu, "sp_temp",    cJSON_CreateNumber(-25.0));
    cjson_set(ssu, "sp_rpm",     cJSON_CreateNumber(0));
    cjson_set(ssu, "sp_state",   cJSON_CreateString(STATES[IDLE]));
    cjson_set(ssu, "sp_thermal", cJSON_CreateString(THERMALS[THERMAL_NOMINAL]));
    cjson_set(ssu, "bb_sensor",  cJSON_CreateString(SENSORS[STOWED]));
    cjson_set(ssu, "bb_depth",   cJSON_CreateNumber(0.0));
    cjson_set(ssu, "bb_temp",    cJSON_CreateNumber(-25.0));
    cjson_set(ssu, "bb_rpm",     cJSON_CreateNumber(0));
    cjson_set(ssu, "bb_state",   cJSON_CreateString(STATES[IDLE]));
    cjson_set(ssu, "bb_thermal", cJSON_CreateString(THERMALS[THERMAL_NOMINAL]));
    cjson_set(ssu, "launch_lock", cJSON_CreateString("locked"));
    cjson_set(ssu, "keepout", cJSON_CreateString("inactive"));
    cjson_set(ssu, "keepout_timer", cJSON_CreateNumber(0));
    ctx->boot_ticks = 0;
    ctx->last_mode = -1;
}

cJSON* ssu_sim_create(void) {
    cJSON* ssu = cJSON_CreateObject();
    cJSON_AddBoolToObject(ssu,   "power", false);
    cJSON_AddStringToObject(ssu, "status", "off");
    cJSON_AddNumberToObject(ssu, "mode", 0);
    cJSON_AddNumberToObject(ssu, "ambient_temp", -25);
    cJSON_AddStringToObject(ssu, "keepout", "inactive");
    cJSON_AddNumberToObject(ssu, "keepout_timer", 0);
    cJSON_AddStringToObject(ssu, "sp_sensor", "stowed");
    cJSON_AddNumberToObject(ssu, "sp_depth", 0);
    cJSON_AddNumberToObject(ssu, "sp_temp", -25);
    cJSON_AddNumberToObject(ssu, "sp_rpm", 0);
    cJSON_AddStringToObject(ssu, "sp_state", "idle");
    cJSON_AddStringToObject(ssu, "sp_thermal", "nominal");
    cJSON_AddStringToObject(ssu, "bb_sensor", "stowed");
    cJSON_AddNumberToObject(ssu, "bb_depth", 0);
    cJSON_AddNumberToObject(ssu, "bb_temp", -25);
    cJSON_AddNumberToObject(ssu, "bb_rpm", 0);
    cJSON_AddStringToObject(ssu, "bb_state", "idle");
    cJSON_AddStringToObject(ssu, "bb_thermal", "nominal");
    cJSON_AddStringToObject(ssu, "launch_lock", "locked");
    return ssu;
}

static void update_keepout(cJSON* ssu, const ssu_inputs_t* in, int team) {
    const char* keepout = cJSON_GetStringValue(cjson_get(ssu, "keepout"));
    if (keepout == NULL || strcmp(keepout, "inactive") == 0) {
        return;   // the zone isn't watched until the launch lock is released
    }

    bool was_breached = strcmp(keepout, "breached") == 0;

    if (strcmp(keepout, "arming") == 0) {
        int seconds_left = (int)get_num(ssu, "keepout_timer") - 1;
        if (seconds_left > 0) {
            cjson_set(ssu, "keepout_timer", cJSON_CreateNumber(seconds_left));
            return;   // still counting down
        }
        cjson_set(ssu, "keepout_timer", cJSON_CreateNumber(0));
        printf("Keep-out zone armed for Team %d\n", team);
    }

    if (in->keepout_clear) {
        cjson_set(ssu, "keepout", cJSON_CreateString("active"));
    } else {
        if (!was_breached) {
            printf("KEEP-OUT ZONE BREACHED for Team %d\n", team);
        }
        cjson_set(ssu, "keepout", cJSON_CreateString("breached"));
    }
}

void ssu_sim_tick(cJSON* ssu, ssu_ctx_t* ctx, const ssu_inputs_t* in) {
    ssu_state_t status = get_ssu_state(ssu);

    // power and mode always reflect the switches
    cjson_set(ssu, "power", cJSON_CreateBool(in->power));
    cjson_set(ssu, "mode",  cJSON_CreateNumber(in->mode));

    if (!in->power) {
        ssu_sim_reset(ssu, ctx);
        return;
    }

    switch (status) {
        case SSU_OFF:
            float ambient = random_ambient();
			cjson_set(ssu, "ambient_temp", cJSON_CreateNumber(ambient));
			cjson_set(ssu, "sp_temp", cJSON_CreateNumber(ambient));
			cjson_set(ssu, "bb_temp", cJSON_CreateNumber(ambient));
			cjson_set(ssu, "status", cJSON_CreateString(SSU_STATES[SSU_BOOTING]));
            ctx->boot_ticks = 0;
            printf("Starting SSU boot sequence\n");
            break;

        case SSU_BOOTING:
            // >= so a skipped second can't leave the SSU stuck booting
            if (++ctx->boot_ticks >= BOOT_TICKS) {
                cjson_set(ssu, "status",    cJSON_CreateString(SSU_STATES[SSU_READY]));
                cjson_set(ssu, "sp_sensor", cJSON_CreateString(SENSORS[SENSOR_PRIMED]));
                cjson_set(ssu, "bb_sensor", cJSON_CreateString(SENSORS[SENSOR_PRIMED]));
                printf("SSU ready\n");
            }
            break;

        case SSU_READY:
            if (in->mode != ctx->last_mode) {
                if (ctx->last_mode != -1) {
                    printf("SSU in %s mode\n", in->mode == 0 ? "Short Period" : "Broadband");
                }
                ctx->last_mode = in->mode;
            }
            drill_tick(ssu, in->mode == 0 ? &SP : &BB, in);

            // keep-out runs before the lock check, so the release tick still shows the full countdown
            update_keepout(ssu, in, ctx->team);

            // launch lock: a held press releases it once both sensors are deployed, which starts the keep-out countdown
            if (in->launch_lock_pressed) {
                bool both_deployed = get_state(ssu, "sp_state") == DEPLOYED &&
                                     get_state(ssu, "bb_state") == DEPLOYED;
                const char* launch_lock = cJSON_GetStringValue(cjson_get(ssu, "launch_lock"));
                bool still_locked = launch_lock != NULL && strcmp(launch_lock, "locked") == 0;

                if (both_deployed && still_locked) {
                    cjson_set(ssu, "launch_lock", cJSON_CreateString("released"));
                    cjson_set(ssu, "keepout", cJSON_CreateString("arming"));
                    cjson_set(ssu, "keepout_timer", cJSON_CreateNumber(5));
                    printf("Launch lock released for Team %d, keep-out zone arms in %d s\n", ctx->team, 5);
                }
            }
            break;
    }
}
