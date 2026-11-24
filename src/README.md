# TSS Development Documentation

This document is intended to provide an overview of the current telemetry stream server repository, allowing new developers or interested students to understand the structure and how to make changes/contribute to the telemetry stream server.

![](/frontend/images/tss-structure.png)
_NASA SUITS block diagram of all peripherals and software_

## Introduction

### Navigation

- <a href="#frontend-development">Frontend Development</a>
- <a href="#server-development">Server Development</a>
- <a href="#telemetry-simulation-development">Telemetry Simulation Development</a>
- <a href="#peripheral-devices">Peripheral Devices</a>

### Background

The NASA SUITS project has taken on several iterations since its start in 2018. This telemetry server marks a significant rewrite from the previous iteration of [TSS 2025](https://github.com/SUITS-Techteam/TSS-2025). We identified several key improvement points including a new interface, better simulated telemetry values, and other features that we wanted to focus on.

TSS was developed for the NASA SUITS challenge with the goal of providing a reliable stream of data to participating teams to use within their interfaces and hardware designs. This started out with several basic linear decay and growth algorithms to model basic resource consumption, but has since been expanded to accommodate complex relationships between multiple telemetry values (e.g. EVA location values calculating speed, increasing heart rate). These complex telemetry relationships help to create a more realistic scenario for students to develop with.

For the 2027 challenge, TSS gained three things: grouping system, a binary packet protocol that student devices use to subscribe to telemetry, and simulations for the new POI peripherals (SSU and SPM, with the CTS in progress).

### Structure

The project is separated into several folders:

- `/data`: contains the JSON files that are read/written to for telemetry storage
- `/documents`: contains supporting documentation, images, etc. for students to use during development
- `/frontend`: all frontend assets, primarily HTML, JS, and CSS
- `/scripts`: supporting Python scripts to allow users to test various parts of their implementation
- `/src`: all C code that runs the server infrastructure and simulation engine

## Frontend Development

![Image of the user interface of the main page of TSS](../frontend/images/tss-main-page.png)
_Primary interface for interacting with TSS_

The frontend for TSS was developed in an extremely simple stack using HTML, CSS, and JavaScript. Through several iterations of the server, we have opted to keep the majority of the backend written in C. This meant that using more modern languages or libraries (e.g. React) would significantly complicate the development of the frontend.

### UI elements

The frontend is built with a minimal interface and reusable styling. All of the style code is consolidated into a singular stylesheet file that is titled <a href="../frontend/index.css">index.css</a>. The styling has slowly morphed into a large file than was originally planned and could likely be due for refactoring and simplification.

The primary color scheme is:

- Black (background): `#000000ff`
- Blue (title card background): `#06212cff`
- Light Blue (borders): `#3889abff`
- Green (buttons): `#28ae5f`
- Dark Gray (buttons): `#2d2d2d`
- Light Gray (borders and switches): `#8e8e8eff`

### Data syncing

Throughout the infrastructure, you'll note that we primarily use UDP sockets to communicate back and forth to make data changes and issue commands. The frontend is the one section of the tech stack that we have opted to still use HTTPS for data fetching. The server still serves the JSON files over HTTPS, and can be fetched with a relative URL such as: `/data/EVA.json`.

The frontend JavaScript fetches `EVA.json` every second and `groups.json` every three seconds (groups change rarely). A snippet of that code is referenced here:
```js
// Fetch fresh data from the backend every one second
setInterval(() => {
  pollData();
  updateClock();
  updateTelemetryStatus();
}, 1000);

// groups fetch every 3 seconds
setInterval(() => {
  pollGroups();
}, 3000);
```

`pollData()` and `pollGroups()` skip a fetch if the previous one hasn't finished yet. Without this guard, a slow server response lets requests pile up in the browser, and each fetch takes longer the longer the page is open. Keep this pattern for any new polling you add.

After fetching the data from the backend, we still need to take the fresh data and display it on the interface. This is done with a nifty setup that allows us to add new telemetry values and elements to the HTML without creating repetitive code in JavaScript. You'll note that every value being updated has a HTML attribute labeled `data-path`, here is an example below:

```html
<div class="telemetry-value">
  <span
    class="telemetry-data"
    data-path="eva.telemetry.scrubber_a_co2_storage"
    data-units="%"
  >
    ------
  </span>
</div>
```

The data path, in this instance `eva.telemetry.scrubber_a_co2_storage`, directly corresponds to a field in a resulting JSON file. For example this data path would be for the `EVA.json` file and uses a period as a delimeter to denote a field name. As a result, we could expect to find the value to update this HTML element in a JSON file structured like below:

`EVA.json`

```json
{
  "telemetry": {
    "scrubber_a_co2_storage": "new value!"
  }
}
```

Here is a snippet from the <a href="../frontend/index.js">index.js</a> file that quickly identifies all of the frontend elements that need to be updated with new telemetry values.

```js
const elements = document.querySelectorAll("[data-path]");
elements.forEach((el) => {
  // Process fetching the correct field from the JSON file and updating the HTML element's value
});
```

## Server Development

### UDP sockets

The server was converted to communicate primarily over the UDP protocol for the 24-25 challenge, after having bad experiences with several teams sending a high number of HTTP requests that brought the small server hosted on the local network down during test week. Moving to the UDP protocol helped remedy those issues and created an additional unique challenge for teams to develop with a networking protocol that they don't typically encounter.

The user datagram protocol is connectionless, which means that it can be much faster than TCP but can also be less reliable since there is no confirmation of delivery. The diagram below illustrates how the UDP protocol is used in C. If you are interested in learning more, I highly recommend this article by Cloudflare: [Everything you ever wanted to know about UDP sockets but were afraid to ask](https://blog.cloudflare.com/everything-you-ever-wanted-to-know-about-udp-sockets-but-were-afraid-to-ask-part-1/).

![](https://www.cs.dartmouth.edu/~campbell/cs60/UDPsockets.jpg)

_Image Credit: <a href="https://www.cs.dartmouth.edu/~campbell/cs60/socketprogramming.html">Dartmouth Computer Science</a>_

### Server structure

- `data.c`: Majority of the data handling, processing UDP requests, routing to JSON files, other data helpers, etc
- `groups.c`: Peripheral and team device registration, Group A/B assignments, subscriptions, and sending telemetry to student devices. Reads and writes `data/groups.json`
- `network.c`: Core networking functionality, creating socket connections, etc
- `server.c`: Sets up the frontend HTTP server, UDP sockets, sim engine, and other helper functions to communicate with peripherals.

### Data handling

Requests to change a value can be done over HTTP (from the frontend) or via UDP (peripherals, student devices, etc). In both cases, they are eventually converted into a string format that represents a file name and field path to update the resulting JSON field with a new value. For example, if someone flips the EVA 1 power switch on the physical UIA, it will send a UDP packet to the server with the command number `2003`, this command number will be converted to a data path based on the hard coded table found in <a href="/src/data.h">data.h: udp_command_mappings</a>, in this case that would be `eva.uia.eva1_power`. This is a very similar mechanism done in reverse to the frontend data update code highlighted above.

### Groups

`groups.c` writes `data/groups.json` for the frontend. It's only rewritten when something changes: an assignment, a subscription, or a device going online/offline.

- `groups`: each group's `name`, `assigned_team` (`-1` = none), and subscriptions (`dcu`, `uia`, `poi`)
- `devices`: each device's `address`, `type`, `group`, `online`, and `last_seen`

A device counts as online if it has sent something in the last 3 seconds. This is only relevant for devices that stream constantly (SSU, SPM, CTS). The UIA, DCU and team devices only send when something happens, so the TSS might not show its actual connection status.

Assignments are constantly saved in case of a shutdown (`save_group_assignments`) and they are loaded on startup (`load_group_assignments`). Subscriptions are not saved so student devices need to subscribe again after a restart.


### Frontend requests

The Groups card sends `POST /groups` with a form body, handled by `handle_group_request`:

| Body | Effect |
| ---- | ------ |
| `group=0&team=3` | Assign Group A to team 3 (`team=-1` clears it) |
| `device=2&group=1` | Put device 2 in Group B (`group=-1` removes it) |
| `device=2&forget=1` | Remove device 2 from the list |
| `group=0&dcu=1&uia=0&poi=4` | Set Group A's subscriptions (`poi=0` clears the POI) |

## Packet Protocol

TSS sends and receives telemetry as binary packets instead of JSON. The student-facing description (packet layout, device IDs, message codes, payload tables, example packets) is in the <a href="../README.md#development">main README</a>. This section covers the server side.

### Design

- Peripherals and the backend keep using cJSON and `EVA.json`. TSS only translates to binary when it sends to students.
- Subscriptions belong to **groups**, not teams. A team device is put in a group, the group is assigned a team, and anything the device subscribes to is stored on the group (`struct group_t`: `dcu`, `uia`, `poi`).
- UIA and DCU subscriptions are on/off. There's one POI slot (device IDs 3 to 6), and subscribing to a new POI replaces the old one. Unsubscribe clears only the current POI. For now, UIA and DCU subscriptions can only be toggled through TSS. If it hasn't been implemented, add a way for students to unsubscribe from them (could be as simple as an unsubscribe packet with their device id).

### Packet flow

```
team device  --Subscribe/Unsubscribe-->  14141  -> read_header_packet (network.c)
                                                 -> handle_team_packet (groups.c): update the group, send an Ack
every second:  send_group_telemetry (groups.c)
                 -> reads each assigned group's team EVA.json once
                 -> build_telemetry_payload (data.c) for each subscribed device
                 -> build_header_packet (network.c) -> sendto the team device's reply address
```

A packet is only treated as the new format if `read_header_packet` accepts it (its length field and checksum match) and its device ID is `15`. 

Ack results: `0` success, `1` the device isn't in a group or its group has no team, `2` unknown device ID or message.

### Adding telemetry for a device

CTS and XRF don't have payloads yet (`build_telemetry_payload` returns `-1`, so nothing is sent). To add one:

1. Decide the byte layout. Only send what a team's UI or assistant needs: flags and states as 1-byte values, numbers as 4-byte floats.
2. Add a block for the device ID in `build_telemetry_payload` (`data.c`). Use the helpers: `payload_flag` (bool to 0/1), `payload_enum` (string state to its index in a name array, `255` if unknown) and `payload_float` (big-endian float). Return the exact number of bytes written.
3. Keep the payload under `TELEMETRY_PAYLOAD_MAX_SIZE` (`data.h`).
4. Add the byte table to the device's README (e.g. `src/lib/ssu_simulation/README.md`), and make sure the state names match the sim's strings.

You can test this implementation with any way of sending byte packets. You can use Python to create small scripts that automatically send built packets and decode received packets.

## Telemetry Simulation Development

### Overview

A simulation engine was developed in C to help create a realistic scenario that incorporated the live telemetry values from the EVA, and create complex relationships with various other biometrics and environmental values.

The code for the library can be found in <a href="/src/lib/simulation">src/lib/simulation</a>. It is divided into two main files, `sim_algorithms.c` which specifies the supported algorithms for the simulation, and `sim_engine.c` which includes all of the helper functions and main code to initiate a simulation (this is called upon within the backend server to create a new simulation instance).

The supported algorithms are:

- Linear Growth
- Linear Decay
- Sine Wave
- Dependent Value (custom algorithm)
- External Value

The first three are quite basic and used for extremely simple time based telemetry calculations. What makes this powerful is the use of the final two types of algorithms, which can be used to pull in external values (e.g. the location of an EVA which is a real world value and not simulated), and custom equations that are dependent on both simuilated values and external values.

### Configuration

Instead of hardcoding every field that we wanted to simulate, we opted to create a format that was easily configurable. This took the form of JSON files that live within a <a href="/src/lib/simulation/config">config folder</a> in the root of the simulation library folder. Each file is representative of a single "component" that you want to simulate; in our case that would be `eva`. Below is an example of a config file with some of the supported algorithms mentioned above:

NOTE: For custom algorithms, to support order of operations with parentheses, the parser was implemented with the expectation that everything would have a space in between it including parentheses. For example, you would write an equation like so: `( external_temp * 2 ) + 16

```json
{
  "component_name": "eva",
  "fields": {
    "primary_battery_level": {
      "type": "float",
      "algorithm": "linear_decay",
      "min_value": 0.0,
      "max_value": 100.0,
      "rate": 0.05,
      "start_value": 100.0
      
    },
    "secondary_battery_level": {
      "type": "float",
      "algorithm": "linear_decay",
      "min_value": 0.0,
      "max_value": 100.0,
      "rate": 0.05,
      "start_value": 100.0
    },
    "oxy_pri_storage": {
      "type": "float",
      "algorithm": "linear_decay",
      "min_value": 0.0,
      "max_value": 100.0,
      "rate": 0.013,
      "start_value": 100.0
    },
    "oxy_sec_storage": {
      "type": "float",
      "algorithm": "linear_decay",
      "min_value": 0.0,
      "max_value": 100.0,
      "rate": 0.013,
      "start_value": 100.0
    }
  }
}
```

## Peripheral Devices

The peripheral devices used during test week communicate with TSS over the UDP protocol. The code for these devices are not available publicly.
