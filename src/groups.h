#ifndef GROUPS_H
#define GROUPS_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "network.h"

// all peripherals send to main UDP port where the server will register each peripheral
// by its ip and peripheral type.
// staff can assign teams to group A or group B on challenge day, then assign peripherals to
// specific group.

#define NUM_GROUPS               2      // group A = 0, group B = 1
#define MAX_PERIPHERAL_DEVICES   32
#define NO_GROUP                -1
#define NO_TEAM                 -1
#define LINK_TIMEOUT 			 3
#define GROUPS_FILE_PATH         "data/groups.json"

enum peripheral_type_t {
    PERIPHERAL_UNKNOWN,
    PERIPHERAL_UIA,
    PERIPHERAL_DCU,
    PERIPHERAL_IMU,
    PERIPHERAL_SPEC,
    PERIPHERAL_SSU,
    PERIPHERAL_SPM,
};

struct peripheral_device_t {
    struct in_addr         address;             // source IP, identifies the device
    enum peripheral_type_t type;
    int                    group_index;         // NO_GROUP until set on the frontend
    time_t                 last_seen;
	bool				   was_online;
};

struct group_t {
    int assigned_team;                          // NO_TEAM between rotations
};

extern struct group_t             groups[NUM_GROUPS];
extern struct peripheral_device_t devices[MAX_PERIPHERAL_DEVICES];
extern int                        device_count;

// json file functions
void load_group_assignments(void);
void save_group_assignments(void);
void update_group_status_file(void);   // call every loop; writes when device status changes

// returns the team a peripheral packet belongs to, or NO_TEAM to drop it
int route_peripheral_packet(const struct sockaddr_in* sender, unsigned int command);

// frontend functions
void end_group_run(int group_index);
bool handle_group_request(const char* request_content);

#endif // GROUPS_H
