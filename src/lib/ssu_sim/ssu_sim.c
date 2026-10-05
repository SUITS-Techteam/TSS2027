#include "ssu_sim.h"
#include <stdio.h>
#include <string.h>

#define BOOT_TICKS     5        // 5 ticks from power-on to ready
#define TEMP_MIN      -25.0f
#define TEMP_WARNING   15.0f
#define TEMP_CRITICAL  35.0f
#define TEMP_UNLOCK    25.0f

static const char* SSU_STATES[] = { "off", "booting", "ready" };
static const char* SENSORS[]    = { "not ready", "primed", "deployed" };
static const char* STATES[]     = { "idle", "drilling", "overheated", "retracting", "retracted", "deployed" };
static const char* THERMALS[]   = { "nominal", "warning", "critical" };

// Per-sensor settings so SP and BB share one drilling routine
typedef struct {
    const char* name;
    const char *depth_key, *temp_key, *rpm_key, *state_key, *thermal_key, *sensor_key;
    float target_depth;
        float hard_depth;
        float hard_heat;
} drill_cfg_t;

static const drill_cfg_t SP = { "Short Period", "sp_depth", "sp_temp", "sp_rpm", "sp_state", "sp_thermal", "sp_sensor",  50.0f, 0.0f, 1.0f};
static const drill_cfg_t BB = { "Broadband",    "bb_depth", "bb_temp", "bb_rpm", "bb_state", "bb_thermal", "bb_sensor", 100.0f, 50.0f,2.0f};

// Replace a field, or add it if missing (a plain replace leaks the item when the key doesn't exist)
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

// Missing or non-number fields read as 0 instead of crashing
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

// One tick of drilling for whichever sensor is selected
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
    if (!heated && temp > TEMP_MIN) {
        float new_temp = temp - get_cool_rate(thermal);
        if (new_temp < TEMP_MIN) new_temp = TEMP_MIN;
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
    cjson_set(ssu, "sp_sensor",  cJSON_CreateString(SENSORS[NOT_READY]));
    cjson_set(ssu, "sp_depth",   cJSON_CreateNumber(0.0));
    cjson_set(ssu, "sp_temp",    cJSON_CreateNumber(-25.0));
    cjson_set(ssu, "sp_rpm",     cJSON_CreateNumber(0));
    cjson_set(ssu, "sp_state",   cJSON_CreateString(STATES[IDLE]));
    cjson_set(ssu, "sp_thermal", cJSON_CreateString(THERMALS[THERMAL_NOMINAL]));
    cjson_set(ssu, "bb_sensor",  cJSON_CreateString(SENSORS[NOT_READY]));
    cjson_set(ssu, "bb_depth",   cJSON_CreateNumber(0.0));
    cjson_set(ssu, "bb_temp",    cJSON_CreateNumber(-25.0));
    cjson_set(ssu, "bb_rpm",     cJSON_CreateNumber(0));
    cjson_set(ssu, "bb_state",   cJSON_CreateString(STATES[IDLE]));
    cjson_set(ssu, "bb_thermal", cJSON_CreateString(THERMALS[THERMAL_NOMINAL]));
    ctx->boot_ticks = 0;
    ctx->last_mode = -1;
}

cJSON* ssu_sim_create(void) {
    cJSON* ssu = cJSON_CreateObject();
    cJSON_AddBoolToObject(ssu,   "power", false);
    cJSON_AddStringToObject(ssu, "status", "off");
    cJSON_AddNumberToObject(ssu, "mode", 0);
    cJSON_AddStringToObject(ssu, "sp_sensor", "not ready");
    cJSON_AddNumberToObject(ssu, "sp_depth", 0);
    cJSON_AddNumberToObject(ssu, "sp_temp", -25);
    cJSON_AddNumberToObject(ssu, "sp_rpm", 0);
    cJSON_AddStringToObject(ssu, "sp_state", "idle");
    cJSON_AddStringToObject(ssu, "sp_thermal", "nominal");
    cJSON_AddStringToObject(ssu, "bb_sensor", "not ready");
    cJSON_AddNumberToObject(ssu, "bb_depth", 0);
    cJSON_AddNumberToObject(ssu, "bb_temp", -25);
    cJSON_AddNumberToObject(ssu, "bb_rpm", 0);
    cJSON_AddStringToObject(ssu, "bb_state", "idle");
    cJSON_AddStringToObject(ssu, "bb_thermal", "nominal");
    return ssu;
}

void ssu_sim_tick(cJSON* ssu, ssu_ctx_t* ctx, const ssu_inputs_t* in) {
    ssu_state_t status = get_ssu_state(ssu);

    // power and mode always reflect the switches
    cjson_set(ssu, "power", cJSON_CreateBool(in->power));
    cjson_set(ssu, "mode",  cJSON_CreateNumber(in->mode));

    // everything resets while powered off
    if (!in->power) {
        ssu_sim_reset(ssu, ctx);
        return;
    }

    switch (status) {
        case SSU_OFF:
            cjson_set(ssu, "status", cJSON_CreateString(SSU_STATES[SSU_BOOTING]));
            ctx->boot_ticks = 0;
            printf("Starting SSU boot sequence for Team %d\n", ctx->team);
            break;

        case SSU_BOOTING:
            // >= so a skipped second can't leave the SSU stuck booting
            if (++ctx->boot_ticks >= BOOT_TICKS) {
                cjson_set(ssu, "status",    cJSON_CreateString(SSU_STATES[SSU_READY]));
                cjson_set(ssu, "sp_sensor", cJSON_CreateString(SENSORS[SENSOR_PRIMED]));
                cjson_set(ssu, "bb_sensor", cJSON_CreateString(SENSORS[SENSOR_PRIMED]));
                printf("SSU ready for Team %d\n", ctx->team);
            }
            break;

        case SSU_READY:
            if (in->mode != ctx->last_mode) {
                if (ctx->last_mode != -1) {
                    printf("SSU in %s mode for Team %d\n", in->mode == 0 ? "Short Period" : "Broadband", ctx->team);
                }
                ctx->last_mode = in->mode;
            }
            drill_tick(ssu, in->mode == 0 ? &SP : &BB, in);
            break;
    }
}
