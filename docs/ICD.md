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

Core system health, scheduler timing and link statistics. Nominal generation rate 1 Hz. Total packet size 55 bytes.

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

### ADCS_HK — structure id 2, APID `0x002`

Attitude determination and control state. Populated from Phase 2 onward. Nominal generation rate 1 Hz. Total packet size 110 bytes.

| Offset | Field | Type | Units | Description |
|---:|---|---|---|---|
| 0 | `est_state` | uint8 |  | Estimator convergence state (INVALID=0, INITIALISING=1, CONVERGING=2, CONVERGED=3) |
| 1 | `q_est_0` | float32 | - | Estimated attitude quaternion scalar part |
| 5 | `q_est_1` | float32 | - | Estimated attitude quaternion x |
| 9 | `q_est_2` | float32 | - | Estimated attitude quaternion y |
| 13 | `q_est_3` | float32 | - | Estimated attitude quaternion z |
| 17 | `omega_x` | float32 | rad/s | Bias-corrected body rate about X |
| 21 | `omega_y` | float32 | rad/s | Bias-corrected body rate about Y |
| 25 | `omega_z` | float32 | rad/s | Bias-corrected body rate about Z |
| 29 | `gyro_bias_x` | float32 | rad/s | Estimated gyro bias X |
| 33 | `gyro_bias_y` | float32 | rad/s | Estimated gyro bias Y |
| 37 | `gyro_bias_z` | float32 | rad/s | Estimated gyro bias Z |
| 41 | `pointing_err_deg` | float32 | deg | Angle between body and reference pointing axis |
| 45 | `rate_norm` | float32 | deg/s | Magnitude of the body rate vector |
| 49 | `sun_valid` | uint8 | bool | Sun sensor reference is usable |
| 50 | `mag_valid` | uint8 | bool | Magnetometer reference is usable |
| 51 | `eclipse` | uint8 | bool | Spacecraft is in Earth shadow |
| 52 | `torque_cmd_x` | float32 | N*m | Commanded control torque X |
| 56 | `torque_cmd_y` | float32 | N*m | Commanded control torque Y |
| 60 | `torque_cmd_z` | float32 | N*m | Commanded control torque Z |
| 64 | `pos_eci_x` | float64 | m | On-board estimated position ECI X |
| 72 | `pos_eci_y` | float64 | m | On-board estimated position ECI Y |
| 80 | `pos_eci_z` | float64 | m | On-board estimated position ECI Z |

### EPS_HK — structure id 3, APID `0x003`

Power subsystem state. Populated from Phase 5 onward. Nominal generation rate 1 Hz. Total packet size 50 bytes.

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
| 8 | 2 | `RESET_COUNTERS` | 0 B | ST[8,2] clear the housekeeping statistics counters. |

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
| 2 | `MODE_CHANGED` | INFO | Spacecraft mode transition executed |
| 3 | `LINK_CONNECTED` | INFO | Ground link established |
| 4 | `LINK_LOST` | LOW | Ground link dropped |
| 5 | `TC_REJECTED` | LOW | Telecommand failed acceptance checks |
| 6 | `HK_ENABLED` | INFO | Housekeeping structure generation enabled |
| 7 | `HK_DISABLED` | INFO | Housekeeping structure generation disabled |
| 8 | `PARAM_SET` | INFO | On-board parameter modified from ground |
| 9 | `SCHED_OVERRUN` | MEDIUM | A rate group missed its deadline |
| 10 | `MODE_REFUSED` | LOW | Requested mode transition was refused |
| 11 | `SAFE_MODE_ENTERED` | HIGH | Spacecraft autonomously entered safe mode |

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
| 8 | `LINK_TIMEOUT_S` | uint32 | 300 | 10 | 86400 | s | Ground contact loss timeout before autonomy reacts |

`ST[20,1]` requests one parameter and is answered by `ST[20,2]`, which reports the identifier followed by the value widened to a 64-bit float. `ST[20,3]` sets a parameter; the value is sent as a 64-bit float and converted to the parameter's declared type, and is rejected with `ILLEGAL_ARG` if it falls outside the declared range.

