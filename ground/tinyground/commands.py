"""Builders for every telecommand the flight software accepts.

Each returns a Telecommand; call .encode() to get the bytes to send.
Application data formats match the comments in obsw/src/services/.
"""

from __future__ import annotations

import struct
from enum import IntEnum

from .pus import ACK_ALL, Telecommand

SID_INSTRUMENT = 1

FUNC_SET_MODE = 1
FUNC_RESET_SCIENCE = 2
FUNC_SIM_HEATER_FAULT = 100
FUNC_SIM_CLEAR_FAULT = 101
PMON_TEMPERATURE = 1


class Mode(IntEnum):
    OFF = 0
    STANDBY = 1
    MEASURE = 2
    SAFE = 3


def ping(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[17,1] are you alive?"""
    return Telecommand(apid, seq, 17, 1, ack, source)


def set_mode(apid: int, seq: int, mode: Mode, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[8,1] function 1: change instrument mode."""
    data = struct.pack(">HB", FUNC_SET_MODE, int(mode))
    return Telecommand(apid, seq, 8, 1, ack, source, data)


def reset_science(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[8,1] function 2: zero the science counter."""
    return Telecommand(apid, seq, 8, 1, ack, source, struct.pack(">H", FUNC_RESET_SCIENCE))


def _sids(sids) -> bytes:
    return bytes([len(sids), *sids])


def hk_enable(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL, sids=(SID_INSTRUMENT,)) -> Telecommand:
    """TC[3,5] enable periodic housekeeping."""
    return Telecommand(apid, seq, 3, 5, ack, source, _sids(sids))


def hk_disable(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL, sids=(SID_INSTRUMENT,)) -> Telecommand:
    """TC[3,6] disable periodic housekeeping."""
    return Telecommand(apid, seq, 3, 6, ack, source, _sids(sids))


def hk_one_shot(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL, sids=(SID_INSTRUMENT,)) -> Telecommand:
    """TC[3,27] send one housekeeping report now."""
    return Telecommand(apid, seq, 3, 27, ack, source, _sids(sids))


def hk_interval(apid: int, seq: int, seconds: int, source: int = 0, ack: int = ACK_ALL,
                sid: int = SID_INSTRUMENT) -> Telecommand:
    """TC[3,31] change the housekeeping period."""
    return Telecommand(apid, seq, 3, 31, ack, source, struct.pack(">BBH", 1, sid, seconds))


def sim_heater_fault(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[8,1] function 100 (simulator only): the heater sticks on."""
    return Telecommand(apid, seq, 8, 1, ack, source, struct.pack(">H", FUNC_SIM_HEATER_FAULT))


def sim_clear_fault(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[8,1] function 101 (simulator only): repair the heater."""
    return Telecommand(apid, seq, 8, 1, ack, source, struct.pack(">H", FUNC_SIM_CLEAR_FAULT))


def monitoring_enable(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[12,1] enable temperature monitoring."""
    return Telecommand(apid, seq, 12, 1, ack, source, struct.pack(">BH", 1, PMON_TEMPERATURE))


def monitoring_disable(apid: int, seq: int, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[12,2] disable temperature monitoring."""
    return Telecommand(apid, seq, 12, 2, ack, source, struct.pack(">BH", 1, PMON_TEMPERATURE))


def monitoring_limits(apid: int, seq: int, warning_c: float, danger_c: float,
                      source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[12,7] change the temperature warning and danger limits."""
    data = struct.pack(">BHhh", 1, PMON_TEMPERATURE, round(warning_c * 100), round(danger_c * 100))
    return Telecommand(apid, seq, 12, 7, ack, source, data)


def time_report_rate(apid: int, seq: int, exponent: int, source: int = 0, ack: int = ACK_ALL) -> Telecommand:
    """TC[9,1] send a time report every 2**exponent seconds (0..6)."""
    return Telecommand(apid, seq, 9, 1, ack, source, bytes([exponent]))
