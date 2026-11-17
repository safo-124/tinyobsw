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

    FORMAT = ">BBhHHIHHH"  # 18 bytes, same order as obsw/src/services/st03.c

    @property
    def mode_name(self) -> str:
        return MODE_NAMES.get(self.mode, f"?{self.mode}")

    @classmethod
    def decode(cls, tm: Telemetry) -> "InstrumentHk":
        if (tm.service, tm.subtype) != (3, 25):
            raise ValueError(f"{tm.type_str} is not a housekeeping report")
        if len(tm.data) != struct.calcsize(cls.FORMAT):
            raise ValueError(f"housekeeping report has {len(tm.data)} bytes, expected 18")
        sid, mode, temp, cur, volt, sci, changes, acc, rej = struct.unpack(cls.FORMAT, tm.data)
        return cls(sid, mode, temp / 100.0, cur, volt / 1000.0, sci, changes, acc, rej)


MODE_NAMES = {0: "OFF", 1: "STANDBY", 2: "MEASURE", 3: "SAFE"}


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
        return (f"HK {hk.mode_name:<7} {hk.temperature_c:6.2f} C "
                f"{hk.current_ma:4d} mA {hk.voltage_v:6.3f} V science={hk.science_count} "
                f"TCs ok/rej={hk.tc_accepted}/{hk.tc_rejected}")
    return f"{len(tm.data)} data bytes"


def describe(tm: Telemetry) -> str:
    """One line per packet, for logs and the command line."""
    prefix = f"{tm.type_str:<9} t={tm.time.to_seconds():9.3f}s seq={tm.seq_count:<5}"
    return f"{prefix} {summary(tm)}"
