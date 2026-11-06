# 🚨 **Note:** This is not the final version of TSS 2027 but has most functional capabilities of the final version. More information can be found under [documents/updates/](./documents/updates/). 🚨


# TSS 2027

NASA Spacesuit User Interface Technologies for Students ([SUITS](https://www.nasa.gov/learning-resources/spacesuit-user-interface-technologies-for-students/)) is a design challenge in which college students from across the country help design user interface solutions for future spaceflight needs. The following is a web interface for the SUITS telemetry stream server designed and developed for the challenge.

<img src="frontend/images/suits-introduction.png"/>

## Introduction

TSS (telemetry stream server) is the centralized server for sending and receiving data for the challenge. The following document will detail how you can run your own instance of the server and begin developing your software and hardware.

### Navigation

- <a href="#getting-started">Getting Started</a>
- <a href="#peripheral-devices">Peripheral Devices</a>
- <a href="#development">Development</a>
- <a href="#testing">Testing</a>
- <a href="#jsc-test-week">JSC Test Week</a>
- <a href="#questions">Questions</a>

### Helpful Links

- [Mission Description](https://www.nasa.gov/wp-content/uploads/2026/08/fy27-nasa-suits-mission-description.pdf?emrc=d6c427?emr)
- <a href="/documents/mission_description/mission-description.pdf">Acronym List</a>
- <a href="/documents/maps/">Rock Yard maps</a>
- <a href="/documents/3d-scans/">Rock Yard 3D Scans</a>
- <a href="/documents/telemetry_ranges/eva-telemetry-ranges.pdf">EVA Telemetry Ranges</a>

## Getting Started

1. Clone the repository:
```
git clone https://github.com/SUITS-Techteam/TSS2027.git
```

2. Navigate into the root of the repository on your terminal of choice

3. Allow the build script to be ran as an executable:
```
chmod +x ./build.bat
```

4. To build the server, run:
```
./build.bat
``` 
**NOTE:** TSS only runs on UNIX based operating systems. If running on Windows, you will have to setup [WSL](https://learn.microsoft.com/en-us/windows/wsl/about) and **MUST** [install GCC](https://code.visualstudio.com/docs/cpp/config-wsl#:~:text=From%20the%20WSL%20command%20prompt%2C%20first%20run). If you don't, several errors will be displayed when trying to build the server.

5. To start the server, run:
```
./server.exe
```

You should see the following lines in your terminal appear:

```
Launching Server at IP: 172.20.182.43:14141
Configuring Local Address...
Creating HTTP Socket...
Binding HTTP Socket...
Listening to HTTP Socket...
```

6. Type the IP address printed in the first output for `Launching Server at IP: xxx.xx.xxx.xx:14141`. This will open the website for the server. From this website, you can interact with the server. This is where you can monitor the state of the simulation, verify the display of your system, and virtually interact with the EVA devices like you will be using during test week.

![Image of the user interface of the main page of TSS](frontend/images/tss-main-page.png)

## Peripheral Devices

The devices listed below are physical devices that will be used during test week to create a realistic scenario for both the EVA teams. The sensor data listed below will be synced with the telemetry server and can be fetched for use within your interface.

### UIA

The umbilical interface assembly (UIA) is a component used at the beginning of an EVA to transfer power and fluids to the suit.

<img src="documents/peripherals/uia.jpeg" style="height: 400px"/>

| Sensor       | Value False | Value True | Description                      |
| ------------ | ----------- | ---------- | ---------------------------------|
| POWER        | OFF         | ON         | Remotely powers the suit for EVA |
| OXYGEN       | CLOSED      | OPEN       | Fills suit oxygen tanks          |
| WATER SUPPLY | CLOSED      | OPEN       | Fills suit liquid coolant        |
| WATER WASTE  | CLOSED      | OPEN       | Flushes suit liquid coolant      |
| O2 Vent      | CLOSED      | OPEN       | Flushes suit oxygen tank         |
| DEPRESS PUMP | OFF         | ON         | Pressurizes EVA suit             |

### DCU

The display and control unit (DCU) used for this challenge is a component that allows the user to control various settings of their suit's operation during an EVA. For example, if scrubber A's CO2 storage fills up, you could flip a switch on the DCU to flush it while switching to scrubber B.

<img src="documents/peripherals/dcu_front_new.jpg" style="height: 200px"/> <img src="documents/peripherals/dcu_top_new.jpg" style="height: 200px"/>

| Sensor  | Value False | Value True     | Description                                                                                           |
| ------- | ----------- | --------------- | ----------------------------------------------------------------------------------------------------- |
| BATTERY | LOCAL BATT  | UMBILICAL POWER | Describes if the suit is running off its local battery or UIA power                                   |
| OXYGEN  | PRI TANK    | SEC TANK        | Describes if the suit is pulling from primary or secondary oxygen tanks                               |
| COMM    | A           | B               | Describes which communications channel the suit is currently connected to                             |
| FAN     | PRI FAN     | SEC FAN         | Describes if the suit is using the primary fan or secondary fan                                       |
| PUMP    | CLOSED      | OPEN            | Describes if the coolant pump for the suit is open or closed (allows water to be flushed or supplied) |
| CO2     | PRI SCRUB   | SEC SCRUB       | Describes which scrubber is currently filling with CO2 (other is venting)                             |

### SPM

The solar power module (SSU) is one of the planned peripheral hardware devices that the user will setup as one of the POIs during an EVA. The SPM will send telemetry data to the TSS indicating its operational state, deployment status, and any active readings returned from the device.

### SSU

See [SSU](src/lib/ssu_simulation/README.md)

### CTS

The comms tower station (SSU) is one of the planned peripheral hardware devices that the user will setup as one of the POIs during an EVA. The SPM will send telemetry data to the TSS indicating its operational state, deployment status, and any active readings returned from the device.

## Development

The telemetry server is an important part of the challenge as it will serve as the main way to fetch telemetry for the EVA. You'll take this telemetry data and use it within the respective interfaces that you are designing and developing ahead of test week. This section will outline how to connect to the data stream, data formats, and other helpful information for development.

To create a more realistic scenario, we require that you request and send commands over the [user datagram protocol](https://www.cloudflare.com/learning/ddos/glossary/user-datagram-protocol-udp/) (UDP) instead of a HTTPS connection. Please note that all requests should be formatted in [big endian](https://www.geeksforgeeks.org/dsa/little-and-big-endian-mystery/) format.

Instead of requesting data over and over, your device **subscribes** to the devices it needs. Once subscribed, TSS sends you that device's data every second until you unsubscribe.

### Packet structure

Every packet you send and every packet TSS sends you has the structure below

| Offset | Size    | Field         | Description                                                                 |
| ------ | ------- | ------------- | --------------------------------------------------------------------------- |
| 0      | 4 bytes | Timestamp     | UNIX timestamp (uint32)                                                     |
| 4      | 2 bytes | Packet length | Size of the whole packet in bytes, including the header and checksum (uint16) |
| 6      | 2 bytes | Device ID     | The device the packet is from or about, see <a href="#device-ids">Device IDs</a> (uint16) |
| 8      | 2 bytes | Message code  | What the packet is for, see <a href="#message-codes">Message Codes</a> (uint16) |
| 10     | x bytes | Data          | Depends on the message code, can be empty                                   |
| 10 + x | 1 byte  | Checksum      | XOR of every byte before it (uint8                                              |

The smallest possible packet is 11 bytes: a header and a checksum with no data. TSS ignores any packet whose length field doesn't match the number of bytes it received, or whose checksum is wrong. You should do the same with packets you receive.

To calculate the checksum, start at `0` and XOR it with each byte of the packet in order, stopping before the checksum byte. The result is the checksum.

### Device IDs

Every packet your device sends must use device ID `15`. 

| Device ID |               Device                |
| --------- | ----------------------------------- |
| 1         | DCU (Display and Control Unit)      | 
| 2         | UIA (Umbilical Interface Assembly)  | 
| 3         | SPM (Solar Power Module)            | 
| 4         | SSU (Seismometer Sensing Unit)      |
| 5         | CTS (Comm Tower Station)            |
| 6         | XRF (X-ray Frequency) Scanner       | 
| 15        | University Team Device              | 

### Message Codes

These are the message codes that can be in a header packet.

| Code | Message     | Sent by | Data |
| ---- | ----------- | ------- | ---- |
| 1    | Subscribe   | Team    | 2 bytes: the device ID to subscribe to (uint16) |
| 2    | Unsubscribe | Team    | None |
| 3    | Telemetry   | TSS     | The device's data. The header's device ID says which device it is. |
| 4    | Ack         | TSS     | 1 byte: `0` = success, `1` = your team isn't assigned to a group yet, `2` = unknown device ID |

#### Connecting your device

TSS recognizes your team by your device's IP address. The first time your device sends a packet, it shows up on the TSS website as an unassigned device. Until it's assigned to your team, every reply you get is an Ack with result `1`. During test week, staff assign your devices. On your own instance of TSS, you can assign your device yourself from the website.

TSS sends its replies back to the IP address and port your packets came from, so listen on the same socket you send from. **Teams can only have one device connected to TSS** 

#### Subscribing

- You can subscribe to the **UIA** and **DCU** at any time. They don't count toward your POI limit.
- You can be subscribed to **one POI device** at a time: SPM, SSU, CTS or XRF. Subscribing to a new POI device replaces the one you had.
- **Unsubscribe** removes your POI subscription. Your UIA and DCU subscriptions stay active.
- Every Subscribe and Unsubscribe gets an Ack back. If you don't receive one, resend the packet, since UDP packets can be lost.

#### Example Packets

Here is an example packet you can could send to subscribe to the SSU telemetry data feed. The TSS responds back with a packet with the Ack message code for if your packet was successful. It will respond with Device ID 15 when sending the Ack packet.
```
Timestamp: 1763414183 -> bytes: 69 1b 90 a7
Packet Length: 13 -> bytes: 00 0a
Device ID: University Device (15) -> bytes: 00 0f
Message Code: Subscribe (1) -> bytes: 00 01
Data: SSU (4) -> bytes: 00 04
Checksum: 42 -> bytes: 42

Full packet bytes: 691b90a7000a000f0001000442
Response bytes: 691b90a7000a000f0005000042
```

Then, every second, a Telemetry packet with the SSU's data:

```
Response bytes: 691b90a7001a0004000301020041480000c04000004316000087

Timestamp: 1763414183 -> bytes: 69 1b 90 a7
Packet Length: 13 -> bytes: 00 0a
Device ID: 4 (SSU) -> bytes: 00 04
Message Code 3 (Telemetry) -> bytes: 00 03
Data:
  Power: ON (1) -> byte: 01
  Status: READY (2) -> byte: 02
  Mode: 0 -> byte: 00
  Depth: 12.5 -> bytes: 41 48 00 00
  Drill Temp:-3.0 -> bytes: c0 40 00 00
  RPM: 150.0 -> bytes: 43 16 00 00 
Checksum: 97 -> bytes: 97
```

## Testing

It is incredibly important to test your hardware and software ahead of test week in May. The interface for TSS is intended to allow you to debug certain parts of your design in the absence of the physical <a href="#peripheral-devices">peripheral devices</a>. In the web interface, you'll note sections for both the UIA and DCU, with switches you can flip. These can be enabled and disabled to test your systems and note how they can impact telemetry values.

### Scripts

We have created various scripts to support testing and simulate real world values ahead of test week. Please run the following commands below in the <a href="/scripts/">scripts</a> folder.

- Simulate position values: `python simulate_position.py <tss_server_address>`
- Reset all telemetry data: `./reset_data.bat`

## JSC Test Week

![](https://www.nasa.gov/wp-content/uploads/2023/02/jsc2025e046380.jpg?w=1200)
_24-25 SUITS group photo at the rock yard located at JSC_

Test week at The Lyndon B. Johnson Space Center is the culmination of your teams effort and a chance to test and highlight your work to NASA team members. During the week, you should expect two sessions to showcase your work in a complete scenario. They will be spaced apart on separate days to allow additional time for fixing any issues that arise during your first session. A list of the procedures and expected timing (subject to change) that will be tested can be found <a href="/documents/procedures/">here</a>.

### What is the rock yard?

The rock yard is a physical location on-site at Johnson Space Center where your work will be tested. This is the same location used for several ongoing projects at NASA to validate equipment for lunar rovers and other devices. We have provided 3D scans of the rock yard which can be found [here](/documents/3d-scans), they might be useful for development, or just to see the environment that testing will take place in.

### What to expect for testing

<img src="frontend/images/suits-test-week.png">

During test week, we will be hosting an official instance of TSS. This will be deployed on a local network and you will connect to it via a network address. Provided that you are connected to the same Wi-Fi network as the server, you should be able to connect and issue commands in the exact same way. You should expect and plan that the network address for the server will differ from your development instances, so we suggest making it easy to change in your interface or code.

## Questions

If this README or the additional documents provided in the <a href="/documents/">documents folder</a> are not enough to answer a specific question, please reach the SUITS tech team at `nasa-suits@mail.nasa.gov`.

If there are any notable bugs with TSS blocking development, we welcome you to create a new issue within the GitHub repository.

If you have reached the very end of this document and still want to learn more about how TSS works, feel free to read the more technical documentation located in the `src` folder: <a href="/src/README.md">TSS Development Documentation</a>
