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
    'GND': 0x00A,
}

ENUMS = {
    'SystemMode': {'BOOT': 0, 'SAFE': 1, 'DETUMBLE': 2, 'STANDBY': 3, 'POINTING': 4},
    'AdcsEstState': {'INVALID': 0, 'INITIALISING': 1, 'CONVERGING': 2, 'CONVERGED': 3},
    'PowerState': {'UNKNOWN': 0, 'NOMINAL': 1, 'LOW': 2, 'CRITICAL': 3},
    'Severity': {'INFO': 1, 'LOW': 2, 'MEDIUM': 3, 'HIGH': 4},
}

# name -> (sid, apid, [(field, type, units, enum_or_None), ...])
TELEMETRY = {
    'SYS_HK': (1, 0x001, [('uptime_s', 'uint32', 's', None), ('tick_count', 'uint32', 'ticks', None), ('mode', 'uint8', '', 'SystemMode'), ('boot_count', 'uint16', 'count', None), ('cpu_load_pct', 'uint8', '%', None), ('sched_overruns', 'uint16', 'count', None), ('tc_received', 'uint32', 'count', None), ('tc_rejected', 'uint32', 'count', None), ('tm_sent', 'uint32', 'count', None), ('link_up', 'uint8', 'bool', None), ('events_logged', 'uint32', 'count', None), ('last_event_id', 'uint16', 'id', None)]),
    'ADCS_HK': (2, 0x002, [('est_state', 'uint8', '', 'AdcsEstState'), ('q_est_0', 'float32', '-', None), ('q_est_1', 'float32', '-', None), ('q_est_2', 'float32', '-', None), ('q_est_3', 'float32', '-', None), ('omega_x', 'float32', 'rad/s', None), ('omega_y', 'float32', 'rad/s', None), ('omega_z', 'float32', 'rad/s', None), ('gyro_bias_x', 'float32', 'rad/s', None), ('gyro_bias_y', 'float32', 'rad/s', None), ('gyro_bias_z', 'float32', 'rad/s', None), ('pointing_err_deg', 'float32', 'deg', None), ('rate_norm', 'float32', 'deg/s', None), ('sun_valid', 'uint8', 'bool', None), ('mag_valid', 'uint8', 'bool', None), ('eclipse', 'uint8', 'bool', None), ('torque_cmd_x', 'float32', 'N*m', None), ('torque_cmd_y', 'float32', 'N*m', None), ('torque_cmd_z', 'float32', 'N*m', None), ('pos_eci_x', 'float64', 'm', None), ('pos_eci_y', 'float64', 'm', None), ('pos_eci_z', 'float64', 'm', None)]),
    'EPS_HK': (3, 0x003, [('power_state', 'uint8', '', 'PowerState'), ('batt_voltage', 'float32', 'V', None), ('batt_current', 'float32', 'A', None), ('batt_soc_pct', 'float32', '%', None), ('batt_temp_c', 'float32', 'degC', None), ('solar_power_w', 'float32', 'W', None), ('load_power_w', 'float32', 'W', None), ('rails_enabled', 'uint16', 'mask', None), ('shed_level', 'uint8', 'level', None)]),
}

# name -> (service, subtype, [(arg, type, enum_or_None), ...])
COMMANDS = {
    'TEST_CONNECTION': (17, 1, []),
    'ENABLE_HK': (3, 5, [('sid', 'uint8', None)]),
    'DISABLE_HK': (3, 6, [('sid', 'uint8', None)]),
    'REPORT_PARAM': (20, 1, [('param_id', 'uint16', None)]),
    'SET_PARAM': (20, 3, [('param_id', 'uint16', None), ('value', 'float64', None)]),
    'SET_MODE': (8, 1, [('mode', 'uint8', 'SystemMode')]),
    'RESET_COUNTERS': (8, 2, []),
}

# id -> (name, severity, description)
EVENTS = {
    1: ('BOOT_COMPLETE', 'INFO', 'Flight software finished initialisation'),
    2: ('MODE_CHANGED', 'INFO', 'Spacecraft mode transition executed'),
    3: ('LINK_CONNECTED', 'INFO', 'Ground link established'),
    4: ('LINK_LOST', 'LOW', 'Ground link dropped'),
    5: ('TC_REJECTED', 'LOW', 'Telecommand failed acceptance checks'),
    6: ('HK_ENABLED', 'INFO', 'Housekeeping structure generation enabled'),
    7: ('HK_DISABLED', 'INFO', 'Housekeeping structure generation disabled'),
    8: ('PARAM_SET', 'INFO', 'On-board parameter modified from ground'),
    9: ('SCHED_OVERRUN', 'MEDIUM', 'A rate group missed its deadline'),
    10: ('MODE_REFUSED', 'LOW', 'Requested mode transition was refused'),
    11: ('SAFE_MODE_ENTERED', 'HIGH', 'Spacecraft autonomously entered safe mode'),
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
    8: ('LINK_TIMEOUT_S', 'uint32', 300, 10, 86400, 's', 'Ground contact loss timeout before autonomy reacts'),
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

