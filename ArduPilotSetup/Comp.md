# Nemo (PART Comp 26 plane) ArduPilot Setup

## Resources
- [ArduPilot Guis/Docs](https://ardupilot.org/plane/docs)
- [ArduPilot Parameters](https://ardupilot.org/plane/docs/parameters.html)

# Connector Assignments

## Rail

| Autopilot Port | Servo # | Description                 | Value |
| -------------- | ------- | --------------------------- | ----- |
| main 1         | 1       | Left Flap                   | 2     |
| main 2         | 2       |                             |       |
| main 3         | 3       |                             |       |
| main 4         | 4       |                             |       |
| main 5         | 5       | Right Aileron               | 4     |
| main 6         | 6       | Right Flap                  | 2     |
| main 7         | 7       | Motor 1 (Front Right Motor) | 33    |
| main 8         | 8       | TiltMotorFrontRight         | 76    |
| AUX 1          | 9       | Motor 2 (Front Left Motor)  | 34    |
| AUX 2          | 10      | Elevator                    | 19    |
| AUX 3          | 11      | Rudder                      | 21    |
| AUX 4          | 12      | Motor 4 (Rear Motor)        | 36    |
| AUX 5          | 13      | Left Aileron                | 4     |
| AUX 6          | 14      | TiltMotorFrontLeft          | 75    |

## UART

| UART Port | Serial # | Description | Value |
| --------- | -------- | ----------- | ----- |
| Telem 1   | 1        | RC Input    | 23    |
| Telem 2   | 2        | Mavlink 2   | 2     |
| GPS       | 3        | GPS         | 5     |

# Parameters
## Q Parameters
#### Basic
Q_ENABLE = 1
Q_FRAME_TYPE = 1 (ignored)
Q_FRAME_CLASS = 7 (Tricopter)

#### Tilt
Q_TILT_ENABLE = 1
Q_TILT_MASK = 3 (bitmask, motors 1 and 2 can tilt)
Q_TILT_TYPE = 2 (vectored yaw)

Q_TILT_RATE_DN (hover -> forward)
Q_TILT_RATE_UP (forward -> hover)
Q_TILT_YAW_ANGLE
- angle motors can tilt past vertical position using vectored yaw (how much they move with yaw stick input) ([Docs: Vectored Yaw](https://ardupilot.org/plane/docs/guide-tilt-rotor.html#vectored-yaw))

## RC Channels
FLTMODE1+2 = 17 (QSTABLIZE, switch position 1)
FLTMODE3+4 = 5 (FBWA, switch position 2)
FLTMODE5+6 = 0 (MANUAL, switch position 3)

### Receiver Parameters
SERIAL1_PROTOCOL = 23 (Telemetry Port 1 - RC Input)
SERIAL2_PROTOCOL = 2 (Telemetry Port 2 - Telemetry Radio (Mavlink 2))
RSSI_TYPE = 3 (reads protocol of receiver)
RC_OPTIONS = 13888 ([Parameter List: RC Input Options](https://ardupilot.org/plane/docs/parameters.html#rc-options))
- value = 6,9,10,12,13
FLTMODE_CH=12 (Sets RC channel 12 to flight mode) ([ELRS Docs: ArduPilot Flight Modes](https://www.expresslrs.org/quick-start/ardupilot-setup/#ardupilot-flight-modes))


# Parameters To Look Into

#### Q Parameters
- Q_ASSIST_SPEED
- Q_OPTIONS (see [Docs: Q_OPTIONS](https://ardupilot.org/plane/docs/quadplane-parameters.html#q-options))
- Q_TRANSITION_MS

#### Tilt Range and Reversal
SERVOn_REVERSED (may need to reverse tilt servos)
SERVOn_MIN
SERVOn_MAX
- adjust exact angle of motors for hover and forward flight