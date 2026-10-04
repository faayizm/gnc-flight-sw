# ============================================================================
#  GENERATED FILE -- DO NOT EDIT.
#  Source:    dictionary/mission.yaml
#  Generator: tools/gen.py
#  Edit the dictionary and run `make gen` instead.
# ============================================================================

"""Machine-generated mirror of the mission dictionary."""

APIDS = {
    'TTC': 0x001,
    'ADCS': 0x002,
    'EPS': 0x003,
    'FDIR': 0x004,
    'GND': 0x00A,
}

LINK = {'scid': 421, 'tm_frame_bytes': 223, 'tc_max_frame_bytes': 1024, 'vc_realtime': 0, 'vc_playback': 1, 'vc_idle': 7, 'tc_vc': 0, 'farm_window': 10}

ENUMS = {
    'SystemMode': {'BOOT': 0, 'SAFE': 1, 'DETUMBLE': 2, 'STANDBY': 3, 'POINTING': 4},
    'AdcsEstState': {'INVALID': 0, 'INITIALISING': 1, 'CONVERGING': 2, 'CONVERGED': 3},
    'AdcsCtrlMode': {'IDLE': 0, 'DETUMBLE': 1, 'STANDBY': 2, 'POINTING': 3},
    'PowerState': {'UNKNOWN': 0, 'NOMINAL': 1, 'LOW': 2, 'CRITICAL': 3},
    'PowerRail': {'OBC': 0, 'RX': 1, 'TX': 2, 'ADCS': 3, 'WHEELS': 4, 'PAYLOAD': 5, 'OPS_HEATERS': 6, 'SURVIVAL_HEATERS': 7},
    'ModeRefusal': {'NONE': 0, 'RATES_HIGH': 1, 'ATTITUDE_UNKNOWN': 2, 'POWER': 3, 'NOT_FROM_MODE': 4, 'INVALID': 5, 'WHEELS': 6},
    'SafeReason': {'GROUND': 0, 'POWER_CRITICAL': 1, 'NO_CONTACT': 2, 'ACTUATORS': 3},
    'SensorId': {'MAG': 0, 'GYRO': 1, 'SUN': 2, 'STAR': 3, 'GPS': 4},
    'SensorFault': {'NONE': 0, 'RANGE': 1, 'FROZEN': 2},
    'FdirWheelState': {'MONITOR': 0, 'CYCLE_OFF': 1, 'VERIFY': 2},
    'ResetCause': {'POWER_ON': 0, 'WATCHDOG': 1},
    'MonitorStatus': {'UNCHECKED': 0, 'WITHIN': 1, 'BELOW': 2, 'ABOVE': 3},
    'Severity': {'INFO': 1, 'LOW': 2, 'MEDIUM': 3, 'HIGH': 4},
}

# name -> (sid, apid, [(field, type, units, enum_or_None), ...])
TELEMETRY = {
    'SYS_HK': (1, 0x001, [('uptime_s', 'uint32', 's', None), ('tick_count', 'uint32', 'ticks', None), ('mode', 'uint8', '', 'SystemMode'), ('boot_count', 'uint16', 'count', None), ('cpu_load_pct', 'uint8', '%', None), ('sched_overruns', 'uint16', 'count', None), ('tc_received', 'uint32', 'count', None), ('tc_rejected', 'uint32', 'count', None), ('tm_sent', 'uint32', 'count', None), ('link_up', 'uint8', 'bool', None), ('events_logged', 'uint32', 'count', None), ('last_event_id', 'uint16', 'id', None), ('tm_frames_sent', 'uint32', 'count', None), ('tc_frames_ok', 'uint32', 'count', None), ('tc_frames_bad', 'uint32', 'count', None), ('cltu_corrected', 'uint32', 'count', None), ('farm_vr', 'uint8', '-', None), ('farm_lockout', 'uint8', 'bool', None), ('time_status', 'uint8', '-', None), ('sched_pending', 'uint16', 'count', None), ('sched_enabled', 'uint8', 'bool', None), ('store_packets', 'uint32', 'count', None), ('store_used_pct', 'uint8', '%', None), ('last_reset', 'uint8', '', 'ResetCause')]),
    'ADCS_HK': (2, 0x002, [('est_state', 'uint8', '', 'AdcsEstState'), ('ctrl_mode', 'uint8', '', 'AdcsCtrlMode'), ('q_est_0', 'float32', '-', None), ('q_est_1', 'float32', '-', None), ('q_est_2', 'float32', '-', None), ('q_est_3', 'float32', '-', None), ('omega_x', 'float32', 'rad/s', None), ('omega_y', 'float32', 'rad/s', None), ('omega_z', 'float32', 'rad/s', None), ('gyro_bias_x', 'float32', 'rad/s', None), ('gyro_bias_y', 'float32', 'rad/s', None), ('gyro_bias_z', 'float32', 'rad/s', None), ('pointing_err_deg', 'float32', 'deg', None), ('rate_norm', 'float32', 'deg/s', None), ('sun_valid', 'uint8', 'bool', None), ('mag_valid', 'uint8', 'bool', None), ('eclipse', 'uint8', 'bool', None), ('torque_cmd_x', 'float32', 'N*m', None), ('torque_cmd_y', 'float32', 'N*m', None), ('torque_cmd_z', 'float32', 'N*m', None), ('pos_eci_x', 'float64', 'm', None), ('pos_eci_y', 'float64', 'm', None), ('pos_eci_z', 'float64', 'm', None), ('att_sigma_deg', 'float32', 'deg', None), ('wheel_h_x', 'float32', 'N*m*s', None), ('wheel_h_y', 'float32', 'N*m*s', None), ('wheel_h_z', 'float32', 'N*m*s', None), ('dipole_cmd_x', 'float32', 'A*m^2', None), ('dipole_cmd_y', 'float32', 'A*m^2', None), ('dipole_cmd_z', 'float32', 'A*m^2', None), ('gps_valid', 'uint8', 'bool', None), ('att_err_deg', 'float32', 'deg', None)]),
    'EPS_HK': (3, 0x003, [('power_state', 'uint8', '', 'PowerState'), ('batt_voltage', 'float32', 'V', None), ('batt_current', 'float32', 'A', None), ('batt_soc_pct', 'float32', '%', None), ('batt_temp_c', 'float32', 'degC', None), ('solar_power_w', 'float32', 'W', None), ('load_power_w', 'float32', 'W', None), ('rails_enabled', 'uint16', 'mask', None), ('shed_level', 'uint8', 'level', None)]),
    'FDIR_HK': (4, 0x004, [('wheel_state', 'uint8', '', 'FdirWheelState'), ('wheels_usable', 'uint8', 'mask', None), ('wheel_retries', 'uint8', 'count', None), ('wheel_resid_x', 'float32', 'N*m*s', None), ('wheel_resid_y', 'float32', 'N*m*s', None), ('wheel_resid_z', 'float32', 'N*m*s', None), ('edac_corrected', 'uint32', 'count', None), ('edac_uncorrectable', 'uint16', 'count', None), ('tmr_repairs', 'uint16', 'count', None), ('monitors_enabled', 'uint8', 'count', None), ('monitors_alarm', 'uint8', 'count', None)]),
}

# name -> (service, subtype, [(arg, type, enum_or_None), ...])
COMMANDS = {
    'TEST_CONNECTION': (17, 1, []),
    'TEST_WATCHDOG': (17, 128, []),
    'ENABLE_HK': (3, 5, [('sid', 'uint8', None)]),
    'DISABLE_HK': (3, 6, [('sid', 'uint8', None)]),
    'REPORT_PARAM': (20, 1, [('param_id', 'uint16', None)]),
    'SET_PARAM': (20, 3, [('param_id', 'uint16', None), ('value', 'float64', None)]),
    'SET_MODE': (8, 1, [('mode', 'uint8', 'SystemMode')]),
    'SWITCH_RAIL': (8, 3, [('rail', 'uint8', 'PowerRail'), ('on', 'uint8', None)]),
    'RESTORE_WHEELS': (8, 4, [('mask', 'uint8', None)]),
    'ENABLE_MONITOR': (12, 1, [('monitor_id', 'uint8', None)]),
    'DISABLE_MONITOR': (12, 2, [('monitor_id', 'uint8', None)]),
    'ENABLE_EVENT_ACTION': (19, 4, [('event_id', 'uint16', None)]),
    'DISABLE_EVENT_ACTION': (19, 5, [('event_id', 'uint16', None)]),
    'RESET_COUNTERS': (8, 2, []),
    'SET_TIME_REPORT_RATE': (9, 1, [('rate_exp', 'uint8', None)]),
    'ADJUST_TIME': (9, 128, [('delta_s', 'float64', None)]),
    'ENABLE_SCHEDULE': (11, 1, []),
    'DISABLE_SCHEDULE': (11, 2, []),
    'RESET_SCHEDULE': (11, 3, []),
    'INSERT_ACTIVITIES': (11, 4, []),
    'ENABLE_STORAGE': (15, 1, [('store_id', 'uint8', None)]),
    'DISABLE_STORAGE': (15, 2, [('store_id', 'uint8', None)]),
    'RETRIEVE_BY_TIME': (15, 9, [('store_id', 'uint8', None), ('from_s', 'uint32', None), ('to_s', 'uint32', None)]),
    'DELETE_STORE_UP_TO': (15, 11, [('store_id', 'uint8', None), ('to_s', 'uint32', None)]),
    'REPORT_STORE_SUMMARY': (15, 12, [('store_id', 'uint8', None)]),
}

# commands whose argument block is free-form: build_tc(name, data=bytes)
VARIABLE_COMMANDS = ['INSERT_ACTIVITIES']

# id -> (name, severity, description)
EVENTS = {
    1: ('BOOT_COMPLETE', 'INFO', 'Flight software finished initialisation; aux = ResetCause'),
    2: ('MODE_CHANGED', 'INFO', 'Spacecraft mode transition executed; aux = old mode << 8 | new mode'),
    3: ('LINK_CONNECTED', 'INFO', 'Ground link established'),
    4: ('LINK_LOST', 'LOW', 'Ground link dropped'),
    5: ('TC_REJECTED', 'LOW', 'Telecommand failed acceptance checks'),
    6: ('HK_ENABLED', 'INFO', 'Housekeeping structure generation enabled'),
    7: ('HK_DISABLED', 'INFO', 'Housekeeping structure generation disabled'),
    8: ('PARAM_SET', 'INFO', 'On-board parameter modified from ground'),
    9: ('SCHED_OVERRUN', 'MEDIUM', 'A rate group missed its deadline'),
    10: ('MODE_REFUSED', 'LOW', 'Requested mode transition was refused; aux = requested mode << 8 | ModeRefusal'),
    11: ('SAFE_MODE_ENTERED', 'HIGH', 'Spacecraft entered safe mode; aux = SafeReason'),
    14: ('SENSOR_TIMEOUT', 'MEDIUM', 'No sensor data from the simulator bridge; actuators commanded to zero'),
    16: ('ESTIMATOR_INIT', 'INFO', 'Attitude estimator initialised; aux 1 = from TRIAD, 2 = from the star tracker'),
    17: ('ESTIMATOR_CONVERGED', 'INFO', 'Attitude estimator uncertainty fell below the pointing threshold'),
    19: ('ESTIMATOR_RESET', 'MEDIUM', 'Attitude estimator discarded after persistent large innovations'),
    20: ('TIME_ADJUSTED', 'INFO', "On-board time corrected from the ground; aux = correction in whole seconds, two's complement"),
    21: ('SCHED_RELEASED', 'INFO', 'A time-tagged telecommand was released; aux = its packet sequence count'),
    22: ('PLAYBACK_STARTED', 'INFO', 'Packet store retrieval began; aux = packets selected'),
    23: ('PLAYBACK_DONE', 'INFO', 'Packet store retrieval finished; aux = packets replayed'),
    24: ('STORE_WRAPPED', 'LOW', 'The packet store filled and began overwriting its oldest packets'),
    25: ('POWER_STATE_CHANGED', 'MEDIUM', 'Battery power state changed; aux = old << 8 | new (PowerState)'),
    26: ('LOAD_SHED', 'MEDIUM', 'Load-shedding level changed; aux = new level (0 = everything restored)'),
    27: ('RAIL_SWITCHED', 'INFO', 'A power rail was switched by ground command; aux = rail << 8 | on'),
    15: ('SENSOR_RESTORED', 'INFO', 'Sensor data resumed after a timeout'),
    12: ('SENSOR_REJECTED', 'MEDIUM', 'A sensor failed its range or frozen-value check and is ignored; aux = SensorId << 8 | SensorFault'),
    28: ('WHEEL_FAULT', 'MEDIUM', 'A reaction wheel is not delivering its commanded torque; aux = wheel mask, bit 0 = X'),
    29: ('WHEEL_POWER_CYCLE', 'INFO', 'Wheel drives switched off and on to clear a possible latch-up; aux = wheels under test'),
    30: ('WHEEL_RECOVERED', 'INFO', 'A wheel works again after its power cycle; aux = wheel mask'),
    31: ('WHEEL_ISOLATED', 'HIGH', 'A wheel still failed after its retry and is out of service; aux = wheel mask'),
    32: ('BATT_VOLTAGE_ALARM', 'MEDIUM', 'Battery voltage outside its monitoring limits; aux = monitor id << 8 | MonitorStatus'),
    33: ('BATT_TEMP_ALARM', 'MEDIUM', 'Battery temperature outside its monitoring limits; aux = monitor id << 8 | MonitorStatus'),
    34: ('RATE_ALARM', 'MEDIUM', 'Body rate above its monitoring limit; aux = monitor id << 8 | MonitorStatus'),
    35: ('POINTING_LOST', 'HIGH', 'Pointing error above its limit for five minutes; aux = monitor id << 8 | MonitorStatus'),
    36: ('WHEEL_MOMENTUM_HIGH', 'MEDIUM', 'A reaction wheel is storing more than two thirds of its capacity; aux = monitor id << 8 | MonitorStatus'),
    37: ('EVENT_ACTION', 'INFO', 'An on-board action ran in response to an event; aux = the triggering event id'),
    38: ('EDAC_UNCORRECTABLE', 'HIGH', 'A parameter word has two flipped bits and cannot be corrected; it reads as its default until reloaded; aux = parameter id'),
    39: ('PARAMS_RELOADED', 'MEDIUM', 'The parameter table was reloaded after an uncorrectable error; aux = 1 from non-volatile storage, 0 kept the defaults'),
    18: ('SENSOR_GAP', 'MEDIUM', 'Sensor samples resumed after a gap in their own timestamps; aux = gap in milliseconds'),
    13: ('SENSOR_READMITTED', 'INFO', 'A rejected sensor passed its checks again for long enough to be trusted; aux = SensorId'),
}

# id -> (name, type, default, min, max, units, description)
PARAMS = {
    1: ('SYS_HK_PERIOD_MS', 'uint32', 1000, 100, 60000, 'ms', 'Generation period of SYS_HK'),
    2: ('ADCS_HK_PERIOD_MS', 'uint32', 1000, 100, 60000, 'ms', 'Generation period of ADCS_HK'),
    3: ('EPS_HK_PERIOD_MS', 'uint32', 1000, 100, 60000, 'ms', 'Generation period of EPS_HK'),
    4: ('DETUMBLE_RATE_DPS', 'float32', 2.0, 0.1, 30.0, 'deg/s', 'Rate threshold above which detumble is commanded'),
    5: ('POINTING_RATE_DPS', 'float32', 0.5, 0.01, 10.0, 'deg/s', 'Rate threshold below which pointing is permitted'),
    6: ('BATT_LOW_SOC_PCT', 'float32', 40.0, 5.0, 90.0, '%', 'State of charge entering the LOW power state'),
    7: ('BATT_CRIT_SOC_PCT', 'float32', 20.0, 2.0, 80.0, '%', 'State of charge entering the CRITICAL power state'),
    8: ('LINK_TIMEOUT_S', 'uint32', 86400, 600, 604800, 's', 'Time without hearing the ground before the spacecraft enters SAFE'),
    9: ('BDOT_GAIN', 'float32', 300000.0, 0.0, 10000000.0, 'A*m^2/(T/s)', 'B-dot proportional gain'),
    10: ('MTQ_MAX_DIPOLE', 'float32', 0.2, 0.0, 10.0, 'A*m^2', 'Largest magnetic dipole commanded on any axis'),
    11: ('BDOT_FILTER_TAU_S', 'float32', 3.0, 0.1, 60.0, 's', 'Time constant of the filter applied to the field derivative'),
    12: ('POINT_BANDWIDTH_RADPS', 'float32', 0.1, 0.005, 1.0, 'rad/s', 'Natural frequency of the pointing control loop'),
    13: ('POINT_MAX_SLEW_DPS', 'float32', 1.0, 0.05, 5.0, 'deg/s', 'Largest body rate the pointing controller will command while acquiring'),
    14: ('MOMENTUM_DUMP_GAIN', 'float32', 0.0005, 0.0, 0.1, '1/s', 'Magnetic momentum-unloading gain'),
    15: ('BATT_CAPACITY_WH', 'float32', 30.0, 1.0, 1000.0, 'W*h', 'Usable battery energy at 100% state of charge'),
    16: ('FDIR_HK_PERIOD_MS', 'uint32', 1000, 100, 60000, 'ms', 'Generation period of FDIR_HK'),
}

# id -> (name, packet, field, low, high, repetitions, event)
MONITORS = {
    1: ('BATT_VOLTAGE', 'EPS_HK', 'batt_voltage', 6.6, 8.7, 50, 'BATT_VOLTAGE_ALARM'),
    2: ('BATT_TEMP', 'EPS_HK', 'batt_temp_c', -5.0, 45.0, 100, 'BATT_TEMP_ALARM'),
    3: ('BODY_RATE', 'ADCS_HK', 'rate_norm', -1.0, 5.0, 50, 'RATE_ALARM'),
    4: ('POINTING', 'ADCS_HK', 'pointing_err_deg', -1.0, 15.0, 3000, 'POINTING_LOST'),
    5: ('WHEEL_H_X', 'ADCS_HK', 'wheel_h_x', -0.02, 0.02, 50, 'WHEEL_MOMENTUM_HIGH'),
    6: ('WHEEL_H_Y', 'ADCS_HK', 'wheel_h_y', -0.02, 0.02, 50, 'WHEEL_MOMENTUM_HIGH'),
    7: ('WHEEL_H_Z', 'ADCS_HK', 'wheel_h_z', -0.02, 0.02, 50, 'WHEEL_MOMENTUM_HIGH'),
}

# event name -> (command, args, description)
EVENT_ACTIONS = {
    'POINTING_LOST': ('SET_MODE', {'mode': 'SAFE'}, 'Pointing has been lost for five minutes and nothing below has recovered it: stop trying, go SAFE, wait for the ground'),
}

STRUCT_CODES = {
    'uint8': 'B',
    'int8': 'b',
    'uint16': 'H',
    'int16': 'h',
    'uint32': 'I',
    'int32': 'i',
    'uint64': 'Q',
    'int64': 'q',
    'float32': 'f',
    'float64': 'd',
}

