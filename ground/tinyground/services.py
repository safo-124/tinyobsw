"""Human-readable interpretation of the TM the flight software sends."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Optional

from .errors import ObswStatus
from .pus import Telemetry

ST01_NAMES = {
    1: "acceptance OK",
    2: "acceptance FAILED",
    3: "start OK",
    4: "start FAILED",
    5: "progress OK",
    6: "progress FAILED",
    7: "completion OK",
    8: "completion FAILED",
}


MODE_NAMES = {0: "OFF", 1: "STANDBY", 2: "MEASURE", 3: "SAFE"}
PMON_STATUS_NAMES = {0: "within limits", 1: "above warning", 2: "above danger"}
SEVERITY_NAMES = {1: "info", 2: "low", 3: "medium", 4: "high"}
EVENT_NAMES = {
    1: "Temperature above warning limit",
    2: "Temperature above danger limit",
    3: "Temperature back within limits",
    4: "Instrument put in SAFE on board",
}


@dataclass(frozen=True)
class InstrumentHk:
    """TM[3,25] for SID 1, decoded into engineering units."""

    sid: int
    mode: int
    temperature_c: float
    current_ma: int
    voltage_v: float
    science_count: int
    mode_changes: int
    tc_accepted: int
    tc_rejected: int
    pmon_status: int
    heater_fault: bool
    pmon_enabled: bool
    warning_limit_c: float
    danger_limit_c: float

    FORMAT = ">BBhHHIHHHBBhh"  # 24 bytes, same order as obsw/src/services/st03.c

    @property
    def pmon_status_name(self) -> str:
        return PMON_STATUS_NAMES.get(self.pmon_status, f"?{self.pmon_status}")

    @property
    def mode_name(self) -> str:
        return MODE_NAMES.get(self.mode, f"?{self.mode}")

    @classmethod
    def decode(cls, tm: Telemetry) -> "InstrumentHk":
        if (tm.service, tm.subtype) != (3, 25):
            raise ValueError(f"{tm.type_str} is not a housekeeping report")
        size = struct.calcsize(cls.FORMAT)
        if len(tm.data) != size:
            raise ValueError(f"housekeeping report has {len(tm.data)} bytes, expected {size}")
        (sid, mode, temp, cur, volt, sci, changes, acc, rej,
         pmon, flags, warn, danger) = struct.unpack(cls.FORMAT, tm.data)
        return cls(sid, mode, temp / 100.0, cur, volt / 1000.0, sci, changes, acc, rej,
                   pmon, bool(flags & 1), bool(flags & 2), warn / 100.0, danger / 100.0)


@dataclass(frozen=True)
class EventReport:
    """A decoded ST[5] event."""

    event_id: int
    severity: str
    name: str
    temperature_c: Optional[float] = None
    previous_mode: Optional[str] = None


def decode_event(tm: Telemetry) -> EventReport:
    if tm.service != 5 or len(tm.data) < 2:
        raise ValueError(f"{tm.type_str} is not an event report")
    event_id = struct.unpack(">H", tm.data[:2])[0]
    aux = tm.data[2:]
    temp = struct.unpack(">h", aux[:2])[0] / 100.0 if event_id in (1, 2, 3) and len(aux) >= 2 else None
    prev = MODE_NAMES.get(aux[0]) if event_id == 4 and aux else None
    return EventReport(event_id, SEVERITY_NAMES.get(tm.subtype, "?"),
                       EVENT_NAMES.get(event_id, f"event {event_id}"), temp, prev)


@dataclass(frozen=True)
class CheckTransition:
    """A decoded TM[12,12] check transition report."""

    pmon_id: int
    parameter_id: int
    value_c: float
    limit_c: float
    previous: str
    current: str

    FORMAT = ">HHhhBB"  # 10 bytes, same order as obsw/src/services/st12.c


def decode_transition(tm: Telemetry) -> CheckTransition:
    if (tm.service, tm.subtype) != (12, 12) or len(tm.data) != 10:
        raise ValueError(f"{tm.type_str} is not a check transition report")
    pmon, param, value, limit, prev, cur = struct.unpack(CheckTransition.FORMAT, tm.data)
    return CheckTransition(pmon, param, value / 100.0, limit / 100.0,
                           PMON_STATUS_NAMES.get(prev, str(prev)), PMON_STATUS_NAMES.get(cur, str(cur)))


@dataclass(frozen=True)
class Verification:
    """A decoded ST[1] report."""

    subtype: int
    request_apid: int
    request_seq_count: int
    failure_code: Optional[int]

    @property
    def ok(self) -> bool:
        return self.failure_code is None


def decode_verification(tm: Telemetry) -> Verification:
    if tm.service != 1 or len(tm.data) < 4:
        raise ValueError(f"{tm.type_str} is not a verification report")
    word0, word1 = struct.unpack(">HH", tm.data[:4])
    code = struct.unpack(">H", tm.data[4:6])[0] if len(tm.data) >= 6 else None
    return Verification(
        subtype=tm.subtype,
        request_apid=word0 & 0x7FF,
        request_seq_count=word1 & 0x3FFF,
        failure_code=code,
    )


def summary(tm: Telemetry) -> str:
    """What the packet says, in words."""
    if tm.service == 1:
        v = decode_verification(tm)
        text = f"{ST01_NAMES.get(v.subtype, 'unknown')} for TC seq {v.request_seq_count}"
        if v.failure_code is not None:
            text += f": {ObswStatus.describe(v.failure_code)}"
        return text
    if (tm.service, tm.subtype) == (17, 2):
        return "alive"
    if (tm.service, tm.subtype) == (3, 25):
        hk = InstrumentHk.decode(tm)
        fault = " HEATER FAULT" if hk.heater_fault else ""
        return (f"HK {hk.mode_name:<7} {hk.temperature_c:6.2f} C "
                f"{hk.current_ma:4d} mA {hk.voltage_v:6.3f} V science={hk.science_count} "
                f"TCs ok/rej={hk.tc_accepted}/{hk.tc_rejected} temp {hk.pmon_status_name}{fault}")
    if tm.service == 5:
        ev = decode_event(tm)
        detail = ""
        if ev.temperature_c is not None:
            detail = f" ({ev.temperature_c:.2f} C)"
        elif ev.previous_mode:
            detail = f" (was {ev.previous_mode})"
        return f"{ev.severity} severity: {ev.name}{detail}"
    if (tm.service, tm.subtype) == (9, 2) and len(tm.data) == 7:
        coarse, fine = struct.unpack(">IH", tm.data[1:7])
        return f"time report: on-board time {coarse + fine / 65536:.4f} s (every {1 << tm.data[0]} s)"
    if (tm.service, tm.subtype) == (12, 12):
        tr = decode_transition(tm)
        return (f"temperature {tr.previous} -> {tr.current} at {tr.value_c:.2f} C "
                f"(limit {tr.limit_c:.2f} C)")
    return f"{len(tm.data)} data bytes"


def describe(tm: Telemetry) -> str:
    """One line per packet, for logs and the command line."""
    prefix = f"{tm.type_str:<9} t={tm.time.to_seconds():9.3f}s seq={tm.seq_count:<5}"
    return f"{prefix} {summary(tm)}"
