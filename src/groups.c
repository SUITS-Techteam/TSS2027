#include "server.h"

struct group_t             groups[NUM_GROUPS] = { { NO_TEAM }, { NO_TEAM } };
struct peripheral_device_t devices[MAX_PERIPHERAL_DEVICES];
int                        device_count = 0;

static const char* group_names[NUM_GROUPS] = { "A", "B" };

// maps command ids to peripheral
// 300x commands are snapshots only sent by the pis to the tss
static enum peripheral_type_t peripheral_command_mapping(unsigned int command) {
    if (command == 1) return PERIPHERAL_DCU;
    if (command == 2) return PERIPHERAL_UIA;
    if (command == 3 || command == 3003) return PERIPHERAL_SPM;
	if (command == 4 || command == 3004) return PERIPHERAL_SSU;
	if (command == 5) return PERIPHERAL_CTS;
    if (command == 6) return PERIPHERAL_SPEC;
	if (command == 15) return PERIPHERAL_TEAM_DEVICE;
    return PERIPHERAL_UNKNOWN;
}

// maps peripheral enum to peripheral name
static const char* peripheral_name_mapping(enum peripheral_type_t type) {
    switch (type) {
        case PERIPHERAL_DCU:  return "DCU";
        case PERIPHERAL_UIA:  return "UIA";
        case PERIPHERAL_SPM:  return "SPM";
        case PERIPHERAL_SSU:  return "SSU";
        case PERIPHERAL_CTS:  return "CTS";
        case PERIPHERAL_SPEC: return "SPEC";
        case PERIPHERAL_TEAM_DEVICE: return "TEAM";
        default:              return "UNKNOWN";
    }
}

// translates peripheral name to peripheral enum
static enum peripheral_type_t peripheral_type_from_name(const char* name) {
    if (!name) return PERIPHERAL_UNKNOWN;
    if (strcmp(name, "DCU") == 0)  return PERIPHERAL_DCU;
    if (strcmp(name, "UIA") == 0)  return PERIPHERAL_UIA;
    if (strcmp(name, "SPM") == 0)  return PERIPHERAL_SPM;
    if (strcmp(name, "SSU") == 0)  return PERIPHERAL_SSU;
    if (strcmp(name, "CTS") == 0)  return PERIPHERAL_CTS;
    if (strcmp(name, "SPEC") == 0) return PERIPHERAL_SPEC;
    if (strcmp(name, "TEAM") == 0) return PERIPHERAL_TEAM_DEVICE;
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

    printf("New %s peripheral at %s, waiting for a group\n", peripheral_name_mapping(type), inet_ntoa(address));
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
    enum peripheral_type_t type = peripheral_command_mapping(command);
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

    if (!is_valid_group(group_index) || (team != NO_TEAM && (team < 0 || team >= NUM_TEAMS)) ) return false;

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

	if (previous_team != team) {
        groups[group_index].dcu = false;
        groups[group_index].uia = false;
        groups[group_index].poi = 0;
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

static bool set_group_subscriptions(int group_index, const char* request_content) {
    if (!is_valid_group(group_index) || groups[group_index].assigned_team == NO_TEAM) return false;
    struct group_t* group = &groups[group_index];
    int value;
    bool changed = false;

    if (read_form_int(request_content, "dcu", &value)) {
        group->dcu = value != 0;
        changed = true;
    }
    if (read_form_int(request_content, "uia", &value)) {
        group->uia = value != 0;
        changed = true;
    }
    if (read_form_int(request_content, "poi", &value)) {
        if (value != 0 && (value < 3 || value > 6)) return false;
        group->poi = value;   // 0 clears the POI slot
        changed = true;
    }
    if (!changed) return false;

    printf("Group %s subscriptions set from the frontend: DCU %d, UIA %d, POI %d\n", group_names[group_index],
           group->dcu, group->uia, group->poi);
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
        return set_group_subscriptions(group_index, request_content);
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
		cJSON_AddBoolToObject(group_json, "dcu", groups[i].dcu);
        cJSON_AddBoolToObject(group_json, "uia", groups[i].uia);
        cJSON_AddNumberToObject(group_json, "poi", groups[i].poi);
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
        cJSON_AddStringToObject(device_json, "type", peripheral_name_mapping(device->type));
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

// new packet stuff
static void send_ack(SOCKET udp_socket, const struct sockaddr_in* destination, uint8_t result) {
    uint8_t packet[12];
    int packet_size = build_header_packet((uint32_t)time(NULL), 15, 4,
                                       &result, 1, packet, sizeof(packet));
    sendto(udp_socket, (const char*)packet, packet_size, 0,
           (const struct sockaddr*)destination, sizeof(*destination));
}

/**
 * Handles a valid packet from a team device (device ID 15).
 * The device is registered by IP like a peripheral; its group's subscriptions change
 * once staff have put it in a group that has a team.
 */
void handle_team_packet(SOCKET udp_socket, const struct sockaddr_in* sender, uint16_t message_code,
                        const uint8_t* data, uint16_t data_length) {
	// 15 device id for team device
    struct peripheral_device_t* device = add_device(sender->sin_addr, PERIPHERAL_TEAM_DEVICE);

    if (!device) return;
    device->last_seen = time(NULL);
    device->reply_address = *sender;   // the port can change when a team restarts its app

	// not sub or unsub
    if (message_code != 1 && message_code != 2) {
        return;   // not a message teams send; ignore it
    }

	// unassigned teams cannot sub or unsub
    if (device->group_index == NO_GROUP || groups[device->group_index].assigned_team == NO_TEAM) {
        send_ack(udp_socket, sender, 1);
        return;
    }
    struct group_t* group = &groups[device->group_index];

	// msg code 2 = unsub
    if (message_code == 2) {
        group->poi = 0;   // DCU and UIA stay on
        printf("Group %s unsubscribed from its POI\n", group_names[device->group_index]);
        send_ack(udp_socket, sender, 0);
        save_group_assignments();
        return;
    }

    // 2-byte device id to subscribe to
    if (data_length != 2) {
        send_ack(udp_socket, sender, 2);
        return;
    }
    uint16_t subscribe_to = (data[0] << 8) | data[1];

	//
    if (subscribe_to == 1) {
        group->dcu = true;
    } else if (subscribe_to == 2) {
        group->uia = true;
    } else if (subscribe_to >= 3 && subscribe_to <= 6) {
        group->poi = subscribe_to;   // one POI at a time: replaces the previous one
    } else {
        send_ack(udp_socket, sender, 2);
        return;
    }
    printf("Group %s subscribed to device %u\n", group_names[device->group_index], subscribe_to);
    send_ack(udp_socket, sender, 0);
    save_group_assignments();
}

// sends one telemetry packet for a device to every team device in a group
static void send_device_telemetry(SOCKET udp_socket, int group_index, uint16_t device_id, cJSON* eva) {

    uint8_t payload[128];
    int payload_size = build_telemetry_payload(device_id, eva, payload);
    if (payload_size < 0) return;   // no payload layout for this device yet

    uint8_t packet[10 + sizeof(payload) + 1];
    int packet_size = build_header_packet((uint32_t)time(NULL), device_id, 3,
                                       payload, payload_size, packet, sizeof(packet));

    for (int i = 0; i < device_count; i++) {
        struct peripheral_device_t* device = &devices[i];
        if (device->type != PERIPHERAL_TEAM_DEVICE || device->group_index != group_index) continue;
        if (device->reply_address.sin_port == 0) continue;   // hasn't sent anything since the server started

        sendto(udp_socket, (const char*)packet, packet_size, 0,
               (const struct sockaddr*)&device->reply_address, sizeof(device->reply_address));
    }
}

/**
 * sends each group's subscribed devices to its team devices.
 */
void send_group_telemetry(SOCKET udp_socket, struct backend_data_t** backends) {
    static time_t last_sent = 0;
    time_t now = time(NULL);
    if (now == last_sent) return;
    last_sent = now;

    for (int group_index = 0; group_index < NUM_GROUPS; group_index++) {
        struct group_t* group = &groups[group_index];
        if (group->assigned_team == NO_TEAM) continue;
        if (!group->dcu && !group->uia && group->poi == 0) continue;

        cJSON* eva = get_json_file(backends[group->assigned_team], "EVA");
        if (!eva) continue;

        if (group->dcu) send_device_telemetry(udp_socket, group_index, 1, eva);
        if (group->uia) send_device_telemetry(udp_socket, group_index, 2, eva);
        if (group->poi) send_device_telemetry(udp_socket, group_index, group->poi, eva);

        cJSON_Delete(eva);
    }
}
