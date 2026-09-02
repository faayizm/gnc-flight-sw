"""
pyground -- a minimal, dependency-free ground segment for HYPERSAT.

This exists alongside the OpenC3 COSMOS configuration, and the two have
different jobs:

    COSMOS      the operator's tool. Telemetry screens, limits monitoring,
                packet logging, scripting. What you actually watch.

    pyground    the engineer's tool and the test harness. No Docker, no Ruby,
                no browser -- just Python. It is the reference implementation
                of this spacecraft's protocol, it is what the automated
                software-in-the-loop tests drive, and it is what you reach for
                when the question is "is the spacecraft or the ground tool
                wrong?"

Everything here is generated from or checked against dictionary/mission.yaml,
so the two ground segments cannot drift apart from the flight software.
"""

from .packets import (
    CCSDS_HEADER_BYTES,
    PUS_TC_HEADER_BYTES,
    PUS_TM_HEADER_BYTES,
    Telecommand,
    Telemetry,
    build_tc,
    crc16,
    parse_tm,
)
from .client import GroundClient

__all__ = [
    "CCSDS_HEADER_BYTES",
    "PUS_TC_HEADER_BYTES",
    "PUS_TM_HEADER_BYTES",
    "Telecommand",
    "Telemetry",
    "build_tc",
    "crc16",
    "parse_tm",
    "GroundClient",
]
