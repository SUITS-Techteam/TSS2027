The SSU deploys two seismic sensors into the ground:

- a **short period (SP)** sensor
- a **broadband (BB)** sensor

For each sensor, the operator drills a hole to a target depth, retracts the drill, and then deploys the sensor. The drill heats up while it runs, and if it gets too hot it locks itself until it cools down. Managing drill temperature is the main challenge of the SSU, and it's where good telemetry displays and assistant callouts make the biggest difference.

During the challenge, the SSU is a physical device with switches, buttons, and indicator lights. Before then, TSS simulates the SSU so you can test without it. The telemetry is the same in both cases.

#### Where the data lives

All SSU telemetry is in the `ssu` section of `EVA.json`, alongside the rest of the EVA telemetry. **It updates once per second.**

| Field | Values | Meaning |
|---|---|---|
| `power` | true / false | Whether the SSU's power switch is on |
| `status` | `"off"`, `"booting"`, `"ready"` | Overall SSU state |
| `mode` | 0 or 1 | Which sensor is selected: 0 = short period, 1 = broadband |
| `link` | `"none"`, `"connected"`, `"lost"` | Connection to the physical SSU (see below) |
| `sp_sensor` / `bb_sensor` | `"not ready"`, `"primed"`, `"deployed"` | Status of each sensor |
| `sp_state` / `bb_state` | see Drill states | What each drill is doing |
| `sp_depth` / `bb_depth` | number (cm) | Current drill depth |
| `sp_temp` / `bb_temp` | number (°C) | Current drill temperature |
| `sp_thermal` / `bb_thermal` | `"nominal"`, `"warning"`, `"critical"` | Drill temperature level |
| `sp_rpm` / `bb_rpm` | number | Current drill speed. This is 0 whenever the drill can't spin, even if the operator has set a speed. |

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

- **Retracting** only works when the drill is **stopped** (`idle`) **and** has reached the target depth. A retract press while the drill is spinning, locked, or short of the target depth is ignored. Once started, retracting finishes on its own at 10 cm per second.
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
