# HYPERSAT Interface Control Document

*Generated from `dictionary/mission.yaml` by `tools/gen.py`. Do not edit.*

All fields are big-endian. Packets follow CCSDS 133.0-B Space Packet Protocol
with ECSS-E-ST-70-41C (PUS-C) secondary headers.

## Application process identifiers

| Application | APID |
|---|---|
| TTC | `0x001` (1) |
| ADCS | `0x002` (2) |
| EPS | `0x003` (3) |
| GND | `0x00A` (10) |

## Space link

See [LINK.md](LINK.md) for the frame and coding layers in full.

| Constant | Value |
|---|---|
| `scid` | 421 |
| `tm_frame_bytes` | 223 |
| `tc_max_frame_bytes` | 1024 |
| `vc_realtime` | 0 |
| `vc_playback` | 1 |
| `vc_idle` | 7 |
| `tc_vc` | 0 |
| `farm_window` | 10 |

## Packet headers

### CCSDS primary header (6 bytes, all packets)

| Field | Bits | Value |
|---|---|---|
| Packet version number | 3 | 0 |
| Packet type | 1 | 0 = TM, 1 = TC |
| Secondary header flag | 1 | 1 |
| APID | 11 | see table above |
| Sequence flags | 2 | 3 (unsegmented) |
| Packet sequence count | 14 | increments per APID |
| Packet data length | 16 | octets after the header, minus one |

### PUS TM secondary header (13 bytes)

| Field | Bytes |
|---|---|
| TM packet PUS version (4 b) + time reference status (4 b) | 1 |
| Service type | 1 |
| Message subtype | 1 |
| Message type counter | 2 |
| Destination identifier | 2 |
| Time, CUC 4 + 2 | 6 |

Time is CCSDS Unsegmented Code referenced to **2000-01-01T00:00:00Z**: 4 octets of coarse seconds followed by 2 octets of fine time in units of 1/65536 s.

### PUS TC secondary header (5 bytes)

| Field | Bytes |
|---|---|
| TC packet PUS version (4 b) + acknowledgement flags (4 b) | 1 |
| Service type | 1 |
| Message subtype | 1 |
| Source identifier | 2 |

Every packet ends with a 2-byte packet error control field: CCSDS CRC-16, polynomial `0x1021`, seed `0xFFFF`, no reflection, no final XOR, computed over all preceding octets of the packet.

## Housekeeping telemetry (PUS ST[3,25])

### SYS_HK — structure id 1, APID `0x001`

Core system health, scheduler timing and link statistics. Nominal generation rate 1 Hz. Total packet size 82 bytes.

| Offset | Field | Type | Units | Description |
|---:|---|---|---|---|
| 0 | `uptime_s` | uint32 | s | Seconds since boot |
| 4 | `tick_count` | uint32 | ticks | Scheduler base ticks executed |
| 8 | `mode` | uint8 |  | Current spacecraft mode (BOOT=0, SAFE=1, DETUMBLE=2, STANDBY=3, POINTING=4) |
| 9 | `boot_count` | uint16 | count | Power-on / reset counter |
| 11 | `cpu_load_pct` | uint8 | % | Measured scheduler occupancy |
| 12 | `sched_overruns` | uint16 | count | Rate-group deadline misses |
| 14 | `tc_received` | uint32 | count | Telecommands accepted |
| 18 | `tc_rejected` | uint32 | count | Telecommands rejected |
| 22 | `tm_sent` | uint32 | count | Telemetry packets downlinked |
| 26 | `link_up` | uint8 | bool | Ground link connected |
| 27 | `events_logged` | uint32 | count | Events raised since boot |
| 31 | `last_event_id` | uint16 | id | Identifier of most recent event |
| 33 | `tm_frames_sent` | uint32 | count | TM transfer frames transmitted |
| 37 | `tc_frames_ok` | uint32 | count | TC transfer frames accepted |
| 41 | `tc_frames_bad` | uint32 | count | TC transfer frames rejected (frame checks or FARM-1) |
| 45 | `cltu_corrected` | uint32 | count | Uplink bit errors corrected by BCH |
| 49 | `farm_vr` | uint8 | - | FARM-1 V(R) |
| 50 | `farm_lockout` | uint8 | bool | FARM-1 is in lockout and needs an Unlock |
| 51 | `time_status` | uint8 | - | PUS time reference status |
| 52 | `sched_pending` | uint16 | count | Time-tagged telecommands waiting for release |
| 54 | `sched_enabled` | uint8 | bool | Time-based release is enabled |
| 55 | `store_packets` | uint32 | count | Packets held in the packet store |
| 59 | `store_used_pct` | uint8 | % | Packet store fill level |

### ADCS_HK — structure id 2, APID `0x002`

Attitude determination and control state. Nominal generation rate 1 Hz. Total packet size 140 bytes.

| Offset | Field | Type | Units | Description |
|---:|---|---|---|---|
| 0 | `est_state` | uint8 |  | Estimator convergence state (INVALID=0, INITIALISING=1, CONVERGING=2, CONVERGED=3) |
| 1 | `ctrl_mode` | uint8 |  | Attitude controller mode (IDLE=0, DETUMBLE=1, STANDBY=2, POINTING=3) |
| 2 | `q_est_0` | float32 | - | Estimated attitude quaternion scalar part |
| 6 | `q_est_1` | float32 | - | Estimated attitude quaternion x |
| 10 | `q_est_2` | float32 | - | Estimated attitude quaternion y |
| 14 | `q_est_3` | float32 | - | Estimated attitude quaternion z |
| 18 | `omega_x` | float32 | rad/s | Bias-corrected body rate about X |
| 22 | `omega_y` | float32 | rad/s | Bias-corrected body rate about Y |
| 26 | `omega_z` | float32 | rad/s | Bias-corrected body rate about Z |
| 30 | `gyro_bias_x` | float32 | rad/s | Estimated gyro bias X |
| 34 | `gyro_bias_y` | float32 | rad/s | Estimated gyro bias Y |
| 38 | `gyro_bias_z` | float32 | rad/s | Estimated gyro bias Z |
| 42 | `pointing_err_deg` | float32 | deg | Angle between body and reference pointing axis |
| 46 | `rate_norm` | float32 | deg/s | Magnitude of the body rate vector |
| 50 | `sun_valid` | uint8 | bool | Sun sensor reference is usable |
| 51 | `mag_valid` | uint8 | bool | Magnetometer reference is usable |
| 52 | `eclipse` | uint8 | bool | Spacecraft is in Earth shadow |
| 53 | `torque_cmd_x` | float32 | N*m | Commanded control torque X |
| 57 | `torque_cmd_y` | float32 | N*m | Commanded control torque Y |
| 61 | `torque_cmd_z` | float32 | N*m | Commanded control torque Z |
| 65 | `pos_eci_x` | float64 | m | On-board estimated position ECI X |
| 73 | `pos_eci_y` | float64 | m | On-board estimated position ECI Y |
| 81 | `pos_eci_z` | float64 | m | On-board estimated position ECI Z |
| 89 | `att_sigma_deg` | float32 | deg | Estimator one-sigma attitude uncertainty |
| 93 | `wheel_h_x` | float32 | N*m*s | Reaction wheel momentum X |
| 97 | `wheel_h_y` | float32 | N*m*s | Reaction wheel momentum Y |
| 101 | `wheel_h_z` | float32 | N*m*s | Reaction wheel momentum Z |
| 105 | `dipole_cmd_x` | float32 | A*m^2 | Commanded magnetorquer dipole X |
| 109 | `dipole_cmd_y` | float32 | A*m^2 | Commanded magnetorquer dipole Y |
| 113 | `dipole_cmd_z` | float32 | A*m^2 | Commanded magnetorquer dipole Z |
| 117 | `gps_valid` | uint8 | bool | GPS fix used in the last second |

### EPS_HK — structure id 3, APID `0x003`

Power subsystem state. Nominal generation rate 1 Hz. Total packet size 50 bytes.

| Offset | Field | Type | Units | Description |
|---:|---|---|---|---|
| 0 | `power_state` | uint8 |  | Coarse battery state (UNKNOWN=0, NOMINAL=1, LOW=2, CRITICAL=3) |
| 1 | `batt_voltage` | float32 | V | Battery bus voltage |
| 5 | `batt_current` | float32 | A | Battery current |
| 9 | `batt_soc_pct` | float32 | % | State of charge |
| 13 | `batt_temp_c` | float32 | degC | Battery temperature |
| 17 | `solar_power_w` | float32 | W | Total array power generation |
| 21 | `load_power_w` | float32 | W | Total bus load |
| 25 | `rails_enabled` | uint16 | mask | Bitmask of enabled power rails |
| 27 | `shed_level` | uint8 | level | Active load-shedding level |

## Telecommands

| Service | Subtype | Name | Args | Description |
|---:|---:|---|---:|---|
| 17 | 1 | `TEST_CONNECTION` | 0 B | ST[17,1] connection test. Flight software answers with ST[17,2]. |
| 3 | 5 | `ENABLE_HK` | 1 B | ST[3,5] enable periodic generation of a housekeeping structure. |
| 3 | 6 | `DISABLE_HK` | 1 B | ST[3,6] disable periodic generation of a housekeeping structure. |
| 20 | 1 | `REPORT_PARAM` | 2 B | ST[20,1] request the value of one on-board parameter, answered by ST[20,2]. |
| 20 | 3 | `SET_PARAM` | 10 B | ST[20,3] set one on-board parameter. Value is interpreted per the parameter type. |
| 8 | 1 | `SET_MODE` | 1 B | ST[8,1] request a spacecraft mode transition. The mode manager may refuse. |
| 8 | 3 | `SWITCH_RAIL` | 2 B | ST[8,3] (mission-specific) enable or disable a power rail. The rail is on only if the ground enables it AND the power and mode policy allow it. |
| 8 | 2 | `RESET_COUNTERS` | 0 B | ST[8,2] clear the housekeeping statistics counters. |
| 9 | 1 | `SET_TIME_REPORT_RATE` | 1 B | ST[9,1] generate a CUC time report every 2^rate_exp seconds; 255 stops them. |
| 9 | 128 | `ADJUST_TIME` | 8 B | ST[9,128] (mission-specific) shift the on-board clock by delta_s and mark time as correlated. The ground computes delta from a time report. |
| 11 | 1 | `ENABLE_SCHEDULE` | 0 B | ST[11,1] enable the release of time-tagged telecommands. |
| 11 | 2 | `DISABLE_SCHEDULE` | 0 B | ST[11,2] disable release; activities stay stored. |
| 11 | 3 | `RESET_SCHEDULE` | 0 B | ST[11,3] delete every scheduled activity. |
| 11 | 4 | `INSERT_ACTIVITIES` | 0 B | ST[11,4] insert time-tagged telecommands. Data: count (u8), then per activity release time (CUC coarse u32, fine u16) and a complete telecommand packet. All-or-nothing: one bad activity rejects the request. |
| 15 | 1 | `ENABLE_STORAGE` | 1 B | ST[15,1] start recording telemetry into a packet store. |
| 15 | 2 | `DISABLE_STORAGE` | 1 B | ST[15,2] stop recording into a packet store. |
| 15 | 9 | `RETRIEVE_BY_TIME` | 9 B | ST[15,9] replay stored packets with on-board time in [from_s, to_s] on the playback virtual channel. |
| 15 | 11 | `DELETE_STORE_UP_TO` | 5 B | ST[15,11] delete stored packets older than to_s. |
| 15 | 12 | `REPORT_STORE_SUMMARY` | 1 B | ST[15,12] request a packet store summary, answered by ST[15,13]. |

### `ENABLE_HK` — ST[3,5]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `sid` | uint8 | Housekeeping structure identifier |

### `DISABLE_HK` — ST[3,6]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `sid` | uint8 | Housekeeping structure identifier |

### `REPORT_PARAM` — ST[20,1]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `param_id` | uint16 | Parameter identifier |

### `SET_PARAM` — ST[20,3]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `param_id` | uint16 | Parameter identifier |
| 2 | `value` | float64 | New value |

### `SET_MODE` — ST[8,1]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `mode` | uint8 | Requested mode (BOOT=0, SAFE=1, DETUMBLE=2, STANDBY=3, POINTING=4) |

### `SWITCH_RAIL` — ST[8,3]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `rail` | uint8 | Rail to switch (OBC=0, RX=1, TX=2, ADCS=3, WHEELS=4, PAYLOAD=5, OPS_HEATERS=6, SURVIVAL_HEATERS=7) |
| 1 | `on` | uint8 | 1 to enable |

### `SET_TIME_REPORT_RATE` — ST[9,1]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `rate_exp` | uint8 | Report period exponent (0 = every second) |

### `ADJUST_TIME` — ST[9,128]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `delta_s` | float64 | Correction to add to on-board time |

### `ENABLE_STORAGE` — ST[15,1]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `store_id` | uint8 | Packet store identifier |

### `DISABLE_STORAGE` — ST[15,2]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `store_id` | uint8 | Packet store identifier |

### `RETRIEVE_BY_TIME` — ST[15,9]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `store_id` | uint8 | Packet store identifier |
| 1 | `from_s` | uint32 | Start of the time range (CUC coarse seconds) |
| 5 | `to_s` | uint32 | End of the time range (CUC coarse seconds) |

### `DELETE_STORE_UP_TO` — ST[15,11]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `store_id` | uint8 | Packet store identifier |
| 1 | `to_s` | uint32 | Delete everything before this time (CUC coarse seconds) |

### `REPORT_STORE_SUMMARY` — ST[15,12]

| Offset | Argument | Type | Description |
|---:|---|---|---|
| 0 | `store_id` | uint8 | Packet store identifier |

## Request verification (PUS ST[01])

| Subtype | Meaning |
|---:|---|
| 1 | Successful acceptance |
| 2 | Failed acceptance, carries a 16-bit failure code |
| 7 | Successful completion of execution |
| 8 | Failed completion, carries a 16-bit failure code |

Each report carries the APID and packet sequence count of the telecommand it refers to, so the ground can correlate it unambiguously.

| Failure code | Meaning |
|---:|---|
| 0 | OK |
| 1 | BAD_CRC |
| 2 | BAD_LENGTH |
| 3 | UNKNOWN_SERVICE |
| 4 | ILLEGAL_ARG |
| 5 | UNAVAILABLE |
| 6 | REFUSED |

## Events (PUS ST[05])

The message subtype carries the severity: 1 informative, 2 low, 3 medium, 4 high. The source data is a 16-bit event identifier followed by 32 bits of auxiliary data whose meaning depends on the event.

| ID | Name | Severity | Description |
|---:|---|---|---|
| 1 | `BOOT_COMPLETE` | INFO | Flight software finished initialisation |
| 2 | `MODE_CHANGED` | INFO | Spacecraft mode transition executed; aux = old mode << 8 | new mode |
| 3 | `LINK_CONNECTED` | INFO | Ground link established |
| 4 | `LINK_LOST` | LOW | Ground link dropped |
| 5 | `TC_REJECTED` | LOW | Telecommand failed acceptance checks |
| 6 | `HK_ENABLED` | INFO | Housekeeping structure generation enabled |
| 7 | `HK_DISABLED` | INFO | Housekeeping structure generation disabled |
| 8 | `PARAM_SET` | INFO | On-board parameter modified from ground |
| 9 | `SCHED_OVERRUN` | MEDIUM | A rate group missed its deadline |
| 10 | `MODE_REFUSED` | LOW | Requested mode transition was refused; aux = requested mode << 8 | ModeRefusal |
| 11 | `SAFE_MODE_ENTERED` | HIGH | Spacecraft entered safe mode; aux = SafeReason |
| 14 | `SENSOR_TIMEOUT` | MEDIUM | No sensor data from the simulator bridge; actuators commanded to zero |
| 16 | `ESTIMATOR_INIT` | INFO | Attitude estimator initialised; aux 1 = from TRIAD, 2 = from the star tracker |
| 17 | `ESTIMATOR_CONVERGED` | INFO | Attitude estimator uncertainty fell below the pointing threshold |
| 19 | `ESTIMATOR_RESET` | MEDIUM | Attitude estimator discarded after persistent large innovations |
| 20 | `TIME_ADJUSTED` | INFO | On-board time corrected from the ground; aux = correction in ms, two's complement |
| 21 | `SCHED_RELEASED` | INFO | A time-tagged telecommand was released; aux = its packet sequence count |
| 22 | `PLAYBACK_STARTED` | INFO | Packet store retrieval began; aux = packets selected |
| 23 | `PLAYBACK_DONE` | INFO | Packet store retrieval finished; aux = packets replayed |
| 24 | `STORE_WRAPPED` | LOW | The packet store filled and began overwriting its oldest packets |
| 25 | `POWER_STATE_CHANGED` | MEDIUM | Battery power state changed; aux = old << 8 | new (PowerState) |
| 26 | `LOAD_SHED` | MEDIUM | Load-shedding level changed; aux = new level (0 = everything restored) |
| 27 | `RAIL_SWITCHED` | INFO | A power rail was switched by ground command; aux = rail << 8 | on |
| 15 | `SENSOR_RESTORED` | INFO | Sensor data resumed after a timeout |

## On-board parameters (PUS ST[20])

| ID | Name | Type | Default | Min | Max | Units | Description |
|---:|---|---|---:|---:|---:|---|---|
| 1 | `SYS_HK_PERIOD_MS` | uint32 | 1000 | 100 | 60000 | ms | Generation period of SYS_HK |
| 2 | `ADCS_HK_PERIOD_MS` | uint32 | 1000 | 100 | 60000 | ms | Generation period of ADCS_HK |
| 3 | `EPS_HK_PERIOD_MS` | uint32 | 1000 | 100 | 60000 | ms | Generation period of EPS_HK |
| 4 | `DETUMBLE_RATE_DPS` | float32 | 2.0 | 0.1 | 30.0 | deg/s | Rate threshold above which detumble is commanded |
| 5 | `POINTING_RATE_DPS` | float32 | 0.5 | 0.01 | 10.0 | deg/s | Rate threshold below which pointing is permitted |
| 6 | `BATT_LOW_SOC_PCT` | float32 | 40.0 | 5.0 | 90.0 | % | State of charge entering the LOW power state |
| 7 | `BATT_CRIT_SOC_PCT` | float32 | 20.0 | 2.0 | 80.0 | % | State of charge entering the CRITICAL power state |
| 8 | `LINK_TIMEOUT_S` | uint32 | 86400 | 600 | 604800 | s | Time without hearing the ground before the spacecraft enters SAFE |
| 9 | `BDOT_GAIN` | float32 | 300000.0 | 0.0 | 10000000.0 | A*m^2/(T/s) | B-dot proportional gain |
| 10 | `MTQ_MAX_DIPOLE` | float32 | 0.2 | 0.0 | 10.0 | A*m^2 | Largest magnetic dipole commanded on any axis |
| 11 | `BDOT_FILTER_TAU_S` | float32 | 3.0 | 0.1 | 60.0 | s | Time constant of the filter applied to the field derivative |
| 12 | `POINT_BANDWIDTH_RADPS` | float32 | 0.1 | 0.005 | 1.0 | rad/s | Natural frequency of the pointing control loop |
| 13 | `POINT_MAX_SLEW_DPS` | float32 | 1.0 | 0.05 | 5.0 | deg/s | Largest body rate the pointing controller will command while acquiring |
| 14 | `MOMENTUM_DUMP_GAIN` | float32 | 0.0005 | 0.0 | 0.1 | 1/s | Magnetic momentum-unloading gain |
| 15 | `BATT_CAPACITY_WH` | float32 | 30.0 | 1.0 | 1000.0 | W*h | Usable battery energy at 100% state of charge |

`ST[20,1]` requests one parameter and is answered by `ST[20,2]`, which reports the identifier followed by the value widened to a 64-bit float. `ST[20,3]` sets a parameter; the value is sent as a 64-bit float and converted to the parameter's declared type, and is rejected with `ILLEGAL_ARG` if it falls outside the declared range.

