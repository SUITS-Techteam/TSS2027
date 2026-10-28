#include "server.h"

struct group_t             groups[NUM_GROUPS] = { { NO_TEAM }, { NO_TEAM } };
struct peripheral_device_t devices[MAX_PERIPHERAL_DEVICES];
int                        device_count = 0;

static const char* group_names[NUM_GROUPS] = { "A", "B" };

// maps command ids to peripheral
static enum peripheral_type_t peripheral_type_for_command(unsigned int command) {
    if (command >= 2001 && command <= 2010) return PERIPHERAL_UIA;
    if (command >= 2011 && command <= 2016) return PERIPHERAL_DCU;
    if (command >= 2017 && command <= 2019) return PERIPHERAL_IMU;
    if (command == 2020)                    return PERIPHERAL_SPEC;
    if (command == 3000)        			return PERIPHERAL_SSU;
    if (command == 3001)        			return PERIPHERAL_SPM;
    return PERIPHERAL_UNKNOWN;
}

// maps peripheral enum to peripheral name
static const char* peripheral_type_name(enum peripheral_type_t type) {
    switch (type) {
        case PERIPHERAL_UIA:  return "UIA";
        case PERIPHERAL_DCU:  return "DCU";
        case PERIPHERAL_IMU:  return "IMU";
        case PERIPHERAL_SPEC: return "SPEC";
        case PERIPHERAL_SSU:  return "SSU";
        case PERIPHERAL_SPM:  return "SPM";
        default:              return "UNKNOWN";
    }
}

// translates peripheral name to peripheral enum
static enum peripheral_type_t peripheral_type_from_name(const char* name) {
    if (!name) return PERIPHERAL_UNKNOWN;
    if (strcmp(name, "UIA") == 0)  return PERIPHERAL_UIA;
    if (strcmp(name, "DCU") == 0)  return PERIPHERAL_DCU;
    if (strcmp(name, "IMU") == 0)  return PERIPHERAL_IMU;
    if (strcmp(name, "SPEC") == 0) return PERIPHERAL_SPEC;
    if (strcmp(name, "SSU") == 0)  return PERIPHERAL_SSU;
    if (strcmp(name, "SPM") == 0)  return PERIPHERAL_SPM;
    return PERIPHERAL_UNKNOWN;
}

// add device to list of devices, if device already added, return device struct
static struct peripheral_device_t* add_device(struct in_addr address, enum peripheral_type_t type) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].address.s_addr == address.s_addr && devices[i].type == type) {
            return &devices[i];
        }
    }

    if (device_count == MAX_PERIPHERAL_DEVICES) {
        printf("Peripheral list full, ignoring %s\n", inet_ntoa(address));
        return NULL;
    }

    struct peripheral_device_t* device = &devices[device_count++];
    memset(device, 0, sizeof(*device));
    device->address     = address;
    device->type        = type;
    device->group_index = NO_GROUP;

    printf("New %s peripheral at %s, waiting for a group\n", peripheral_type_name(type), inet_ntoa(address));
    save_group_assignments();
    return device;
}

// finds "key=<number>" in a form body like "group=0&team=3"
static bool read_form_int(const char* body, const char* key, int* value_out) {
    size_t key_length = strlen(key);
    const char* cursor = body;

    while (cursor && *cursor) {
        if (strncmp(cursor, key, key_length) == 0 && cursor[key_length] == '=') {
            *value_out = atoi(cursor + key_length + 1);
            return true;
        }
        cursor = strchr(cursor, '&');
        if (cursor) cursor++;
    }
    return false;
}

static bool is_valid_group(int group_index) {
    return group_index >= 0 && group_index < NUM_GROUPS;
}

static bool is_device_online(const struct peripheral_device_t* device, time_t now){
	return device->last_seen != 0 && difftime(now, device->last_seen) <= LINK_TIMEOUT;
}

// returns the team this packet belongs to, or NO_TEAM to drop it
int route_peripheral_packet(const struct sockaddr_in* sender, unsigned int command) {
    enum peripheral_type_t type = peripheral_type_for_command(command);
    if (type == PERIPHERAL_UNKNOWN) return NO_TEAM;

    struct peripheral_device_t* device = add_device(sender->sin_addr, type);
    if (!device) return NO_TEAM;

    device->last_seen = time(NULL);

    if (device->group_index == NO_GROUP) return NO_TEAM;

    int team = groups[device->group_index].assigned_team;
    if (team == NO_TEAM) return NO_TEAM;


    return team;
}

// frontend functions

// maps teams to a group
static bool assign_group_team(int group_index, int team, struct backend_data_t** backends) {

    if (!is_valid_group(group_index) || team < 0 || team >= NUM_TEAMS) return false;

    for (int other = 0; other < NUM_GROUPS; other++) {
        if (other != group_index && groups[other].assigned_team == team) {
            printf("Team %d is already in group %s\n", team, group_names[other]);
            return false;
        }
    }

	// allow finished team to continue testing without restarting server
	int previous_team = groups[group_index].assigned_team;
    if (previous_team != NO_TEAM && previous_team != team) {
        release_peripherals(backends[previous_team]);
    }

    groups[group_index].assigned_team = team;
    printf("Group %s now feeds team %d\n", group_names[group_index], team);
    save_group_assignments();
    return true;
}

// assigns device to a group
static bool set_device_group(int device_index, int group_index) {
    if (device_index < 0 || device_index >= device_count) return false;
    if (group_index != NO_GROUP && !is_valid_group(group_index)) return false;

    devices[device_index].group_index = group_index;
    save_group_assignments();
    return true;
}

// removes a device, e.g. if a device comes back on a new IP
static bool forget_device(int device_index) {
    if (device_index < 0 || device_index >= device_count) return false;

    for (int i = device_index; i < device_count - 1; i++) {
        devices[i] = devices[i + 1];
    }
    device_count--;
    save_group_assignments();
    return true;
}

// body formats (from the frontend):
//   "device=2&group=1"   put device 2 into group B (group=-1 removes it from its group)
//   "device=2&forget=1"  remove device 2 from the list
//   "group=0&team=3"     group A now feeds team 3
bool handle_group_request(const char* request_content, struct backend_data_t** backends) {
    int device_index, group_index, team, flag;

    if (read_form_int(request_content, "device", &device_index)) {
        if (read_form_int(request_content, "forget", &flag)) {
            return forget_device(device_index);
        }
        if (read_form_int(request_content, "group", &group_index)) {
            return set_device_group(device_index, group_index);
        }
        return false;
    }

    if (read_form_int(request_content, "group", &group_index)) {
        if (read_form_int(request_content, "team", &team)) {
            return assign_group_team(group_index, team, backends);
        }
    }

    return false;
}

// cjson functions

// writes assignments plus device status, the frontend polls this file
void save_group_assignments(void) {
    time_t now = time(NULL);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "server_time", (double)now);

    cJSON* group_array = cJSON_AddArrayToObject(root, "groups");
    for (int i = 0; i < NUM_GROUPS; i++) {
        cJSON* group_json = cJSON_CreateObject();
        cJSON_AddStringToObject(group_json, "name", group_names[i]);
        cJSON_AddNumberToObject(group_json, "assigned_team", groups[i].assigned_team);
        cJSON_AddItemToArray(group_array, group_json);
    }

    cJSON* device_array = cJSON_AddArrayToObject(root, "devices");
    for (int i = 0; i < device_count; i++) {
        struct peripheral_device_t* device = &devices[i];
		device->was_online = is_device_online(device, now);

		char last_seen_text[16] = "";
		if (device->last_seen != 0){
			strftime(last_seen_text, sizeof(last_seen_text), "%H:%M:%S", localtime(&device->last_seen));
		}

        cJSON* device_json = cJSON_CreateObject();
        cJSON_AddStringToObject(device_json, "address", inet_ntoa(device->address));
        cJSON_AddStringToObject(device_json, "type", peripheral_type_name(device->type));
        cJSON_AddNumberToObject(device_json, "group", device->group_index);
        cJSON_AddBoolToObject(device_json, "online", device->was_online);
        cJSON_AddStringToObject(device_json, "last_seen", last_seen_text);
        cJSON_AddItemToArray(device_array, device_json);
    }

    char* json_text = cJSON_Print(root);
    FILE* file = fopen(GROUPS_FILE_PATH, "w");
    if (file) {
        fputs(json_text, file);
        fclose(file);
    } else {
        printf("Could not write %s\n", GROUPS_FILE_PATH);
    }

    free(json_text);
    cJSON_Delete(root);
}

// restores assignments after a restart so devices don't need re-grouping
void load_group_assignments(void) {
    FILE* file = fopen(GROUPS_FILE_PATH, "rb");
    if (!file) {
        printf("No %s yet, starting with empty groups\n", GROUPS_FILE_PATH);
        save_group_assignments();
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* file_text = malloc(file_size + 1);
    if (!file_text) {
        fclose(file);
        return;
    }
    size_t bytes_read = fread(file_text, 1, file_size, file);
    file_text[bytes_read] = '\0';
    fclose(file);

    cJSON* root = cJSON_Parse(file_text);
    free(file_text);
    if (!root) {
        printf("Could not parse %s, starting with empty groups\n", GROUPS_FILE_PATH);
        return;
    }

    cJSON* group_array = cJSON_GetObjectItemCaseSensitive(root, "groups");
    for (int i = 0; i < NUM_GROUPS; i++) {
        cJSON* group_json = cJSON_GetArrayItem(group_array, i);
        cJSON* team_json = cJSON_GetObjectItemCaseSensitive(group_json, "assigned_team");
        groups[i].assigned_team = cJSON_IsNumber(team_json) ? team_json->valueint : NO_TEAM;
    }

    device_count = 0;
    cJSON* device_json;
    cJSON_ArrayForEach(device_json, cJSON_GetObjectItemCaseSensitive(root, "devices")) {
        if (device_count == MAX_PERIPHERAL_DEVICES) break;

        const char* address_text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(device_json, "address"));
        const char* type_text    = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(device_json, "type"));
        cJSON* group_json        = cJSON_GetObjectItemCaseSensitive(device_json, "group");
        if (!address_text) continue;

        struct peripheral_device_t* device = &devices[device_count++];
        memset(device, 0, sizeof(*device));
        device->address.s_addr     = inet_addr(address_text);
        device->type               = peripheral_type_from_name(type_text);
        device->group_index        = cJSON_IsNumber(group_json) ? group_json->valueint : NO_GROUP;
    }

    cJSON_Delete(root);
    printf("Loaded %d peripherals, Group A team %d, Group B team %d\n",
           device_count, groups[0].assigned_team, groups[1].assigned_team);
	save_group_assignments();
}

// updates group json file
void update_group_status_file(void) {
    static time_t last_checked = 0;
    time_t now = time(NULL);
    if (now == last_checked) return;
    last_checked = now;
	for(int i = 0; i < device_count; ++i) {
		if(is_device_online(&devices[i], now) != devices[i].was_online){
			save_group_assignments();
			return;
		}
	}
}
