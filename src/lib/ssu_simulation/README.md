The SSU deploys two seismic sensors into the ground:

- a **short period (SP)** sensor
- a **broadband (BB)** sensor

For each sensor, the operator drills a hole to a target depth, retracts the drill, and then deploys the sensor. The drill heats up while it runs, and if it gets too hot it locks itself until it cools down. Managing drill temperature is the main challenge of the SSU, and it's where good telemetry displays and assistant callouts make the biggest difference.

During the challenge, the SSU is a physical device with switches, buttons, and indicator lights. Before then, TSS simulates the SSU so you can test without it. The telemetry is the same in both cases.

#### Where the data lives

All SSU telemetry is received in the packet structure outlined on the TSS repository. Below is how each byte is structured in the data field in the payload. **It updates once per second.**

| Byte(s) | Field      | Value                         |
| ------- | ---------- | ----------------------------- |
| 0       | Power      | 0 = OFF, 1 = ON               |
| 1       | Status     | 0 = OFF, 1 = BOOT, 2 = READY  |
| 2       | Mode       | 0 = SP, 1 = BB                |
| 3 - 17  | SP Drill   | 15 bytes, see drill layout below |
| 18 - 32 | BB Drill   | 15 bytes, see drill layout below |
| 33      | Keepout    | 0 = INACTIVE, 1 = "ARMING", 2 = "ACTIVE", 3 = "BREACHED" |

Each drill (byte offsets within its 15 bytes):

| Byte    | Field      | Value |
| ------- | ---------- | ----- |
| 0       | Sensor     | 0 = NOT READY, 1 = PRIMED, 2 = DEPLOYED |
| 1       | State      | 0 = IDLE, 1 = DRILLING, 2 = OVERHEATED, 3 = RETRACTING, 4 = RETRACTED, 5 = DEPLOYED |
| 2       | Thermal    | 0 = NOMINAL, 1 = WARNING, 2 = CRITICAL |
| 3 - 6   | Depth      | float |
| 7 - 10  | Drill Temp | float |
| 11 - 14 | RPM        | float |

#### Drill states

Each sensor has its own drill, and each drill is always in one of these states:

| State | Meaning |
|---|---|
| `idle` | The drill is stopped. It starts drilling as soon as a speed is set. |
| `drilling` | The drill is spinning. Depth and temperature are increasing. |
| `overheated` | The drill hit its temperature limit and is locked until it cools down. |
| `retracting` | The drill is pulling itself out of the hole automatically. |
| `retracted` | The drill is fully out, and the sensor can be deployed. |
| `deployed` | The sensor is in place. This sensor is finished. |

Only the drill for the **selected mode** is active. Switching modes pauses the other drill exactly where it is. It does not continue cooling while paused.

#### Drilling and temperature

- **Faster drilling heats the drill much faster.** Heating grows with the cube of the speed: at half speed, the drill heats about one eighth as much per second. Running at full speed is not always the fastest way down.
- **The drill only cools while it is not spinning.** It cools faster when it's colder.
- **At 35 °C the drill locks** (`overheated`) and stops on its own. It unlocks once it has cooled to **25 °C**. If a speed is still set when it unlocks, the drill starts drilling again right away.

#### Retracting and deploying

- **Retracting** only works when the drill is **stopped** (`idle`) **and** has reached the target depth. A retract press while the drill is spinning, locked, or short of the target depth is ignored. Once started, retracting finishes on its own at 10 cm per second.,
- **Deploying** only works once the drill is fully `retracted`. A deploy press at any other time is ignored.

When a press is ignored, TSS does not report why. Your interface or assistant can work it out from the current state and the rules above. For example, a retract that does nothing while `sp_state` is `"drilling"` means the drill needs to be stopped first.

#### Limits and rates

| | Short Period | Broadband |
|---|---|---|
| Target depth | 50 cm | 80 cm |
| Starting temperature | −25 °C to -10 °C | −25 °C to -10 °C |
| Hard layer | none | past 50 cm, the drill heats 2x faster |

| Thermal level | Temperature | Cooling per second while stopped |
|---|---|---|
| `nominal` | below 15 °C | 3.0 °C |
| `warning` | 15 °C to 35 °C | 2.0 °C |
| `critical` | 35 °C and above | 1.5 °C |

- **Max drill speed** is 600 rpm.
- **Each second while drilling,** depth increases by rpm ÷ 150 cm, and temperature rises by 6 × (rpm ÷ 600)³ °C. Broadband heats 2× faster past 50 cm.
- Temperature never drops below the ambient temperature, which is **randomized to anywhere between -25 °C and -10 °C** each time the SSU is started up. Both drills will start at this temperature.
