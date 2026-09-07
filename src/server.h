#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "lib/cjson/cJSON.h"
#include "network.h"
#include "data.h"

#define NUM_TEAMS 10
extern struct profile_context_t profile_context;

#endif // SERVER_H