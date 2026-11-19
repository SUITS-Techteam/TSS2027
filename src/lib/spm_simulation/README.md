The SPM sets up a solar array and points it at the sun. The operator:

- **levels** the platform
- **raises and locks** the truss that holds the array
- **orients** the array toward the sun

The sun's exact direction is not given. TSS only reports a rough heading and the array's **voltage**, which peaks when the array faces the sun. Finding the exact peak from voltage readings is the one of challenges of the SPM and teams' assistants are intended to help find it.

During the challenge, the SPM is a physical device with switches, buttons, a joystick, and indicator lights. Before then, TSS simulates the SPM so you can test without it. The telemetry is the same in both cases.

#### Where the data lives

All SPM telemetry is received in the packet structure outlined on the TSS repository. Below is how each byte is structured in the data field in the payload. **It updates once per second.**

| Byte(s) | Field              | Value |
| ------- | ------------------ | ----- |
| 0       | Power              | 0 = OFF, 1 = ON |
| 1       | Status             | 0 = OFF, 1 = BOOTING, 2 = LEVELING, 3 = DEPLOYING, 4 = ORIENTING, 5 = COMPLETE |
| 2       | Mode               | 0 = LEVEL, 1 = ORIENT |
| 3       | Level              | 0 = NO, 1 = YES |
| 4       | Level Locked       | 0 = NO, 1 = YES |
| 5       | Truss              | 0 = STOWED, 1 = UNLOCKED, 2 = RAISING, 3 = DEPLOYED, 4 = LOCKED |
| 6       | Aligned            | 0 = NO, 1 = YES |
| 7       | Orientation Locked | 0 = NO, 1 = YES |
| 8 - 11  | Tilt X             | float, degrees (0 = level) |
| 12 - 15 | Tilt Y             | float, degrees (0 = level) |
| 16 - 19 | Truss Height       | float, 0.0 (stowed) to 1.0 (fully raised) |
| 20 - 23 | Azimuth            | float, degrees (0 to 360) |
| 24 - 27 | Voltage            | float, volts |
| 28 - 31 | Sun Azimuth Approx | float, degrees, rounded to the nearest 10 |

#### Status

The SPM is always in one of these statuses, and moves through them in order:

| Status | Meaning |
|---|---|
| `off` | One or more power switches are off. |
| `booting` | All power switches are on and the SPM is starting up. This takes 5 seconds. |
| `leveling` | The platform can be tilted with the joystick and locked once level. |
| `deploying` | The platform is level. The truss can be unlocked, raised, and locked. |
| `orienting` | The truss is up. The array can be turned with the joystick, locked, and verified. |
| `complete` | The array is pointed at the sun. The SPM is finished. |

Turning **any** power switch off resets the SPM completely. When it powers back on, the sun's position is randomized again.

#### Leveling

- The platform starts tilted. The joystick tilts it left or right, up to **15°** either way.
- **The platform has momentum.** It keeps moving after the joystick is released and slows down gradually, so let go early.
- `level` is `true` while the tilt is within **±1°**.
- **Locking the level** only works while the platform is level **and** nearly still (moving slower than **0.3° per second**). A lock press at any other time is ignored. Once locked, status moves to `deploying`.

#### Deploying the truss

The truss has to go through these steps in order:

| Truss | Meaning |
|---|---|
| `stowed` | The truss is down and locked in place. |
| `unlocked` | The truss is free to raise. |
| `raising` | The truss is rising on its own. This takes 8 seconds. |
| `deployed` | The truss is fully up but not locked. |
| `locked` | The truss is locked up. Status moves to `orienting`. |

An unlock, raise, or lock press out of this order is ignored.

#### Orienting

- The sun is somewhere between **60° and 300°**. `sun_azimuth_approx` gives its direction rounded to the nearest **10°**, so the real sun can be up to 5° either way.
- The joystick turns the array, up to **15° per second**. **The array has momentum** and coasts after the joystick is released.
- **Voltage** shows how well the array faces the sun:
  - It peaks at about **30 V** when the array points straight at the sun.
  - It falls off quickly on either side, to about half at **5°** away and close to zero past about **12°**.
  - **Every reading is noisy**, by about **±1 V**, and a single reading can be higher or lower than the true value. The highest reading you see is not always the peak. Using several readings from both sides of the peak gives a far better answer than any single one.
- **Locking the orientation** only works while the array is nearly still (turning slower than **0.5° per second**). Once locked, the array can't move until it's unlocked again.
- **Verifying** only works while the orientation is locked. If the array is within **±1°** of the sun, status moves to `complete`, `azimuth_aligned` becomes `true`. If it isn't, nothing changes. Unlock, adjust, lock, and verify again.

When a press is ignored, TSS does not report why. Your interface or assistant can work it out from the current state and the rules above. For example, a verify that does nothing while `orientation_locked` is `true` means the array is not within 1° of the sun.

#### Limits and rates

| | |
|---|---|
| Boot time | 5 s |
| Max tilt | ±15° |
| Level tolerance | ±1°, moving slower than 0.3°/s |
| Truss raise time | 8 s |
| Sun range | 60° to 300° |
| Sun hint | nearest 10° |
| Max turn speed | 15°/s |
| Lock speed (orientation) | slower than 0.5°/s |
| Peak voltage | about 30 V |
| Voltage noise | about ±1 V per reading |
| Verify tolerance | ±1° |
