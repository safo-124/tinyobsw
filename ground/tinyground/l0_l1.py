"""Level 0 -> Level 1 processing for instrument housekeeping.

Level 0 is the packet as received: values tagged with on-board time.
Level 1 adds what science and operations need: a UTC time stamp from
time correlation, with its uncertainty, and the values in engineering
units. Records are kept in memory and can be exported as CSV.
"""

from __future__ import annotations

import csv
import io
from dataclasses import asdict, dataclass, fields
from datetime import datetime, timezone
from typing import Iterable, Optional

from .services import InstrumentHk
from .timecorr import TimeCorrelator


@dataclass(frozen=True)
class L1Housekeeping:
    utc: str              # ISO 8601, millisecond resolution
    utc_sigma_ms: float   # 1-sigma uncertainty of the UTC stamp
    obt_s: float          # on-board time the packet carried
    mode: str
    temperature_c: float
    current_ma: int
    voltage_v: float
    science_count: int
    temperature_check: str
    heater_fault: bool


def iso_utc(epoch_s: float) -> str:
    return datetime.fromtimestamp(epoch_s, tz=timezone.utc).isoformat(timespec="milliseconds").replace(
        "+00:00", "Z")


def to_l1(hk: InstrumentHk, obt_s: float, corr: TimeCorrelator) -> Optional[L1Housekeeping]:
    """Stamp one housekeeping report with UTC; None until time correlation is ready."""
    if not corr.ready:
        return None
    utc, sigma = corr.obt_to_utc(obt_s)
    return L1Housekeeping(
        utc=iso_utc(utc),
        utc_sigma_ms=round(sigma * 1e3, 3),
        obt_s=round(obt_s, 6),
        mode=hk.mode_name,
        temperature_c=hk.temperature_c,
        current_ma=hk.current_ma,
        voltage_v=hk.voltage_v,
        science_count=hk.science_count,
        temperature_check=hk.pmon_status_name,
        heater_fault=hk.heater_fault,
    )


def to_csv(records: Iterable[L1Housekeeping]) -> str:
    out = io.StringIO()
    writer = csv.DictWriter(out, fieldnames=[f.name for f in fields(L1Housekeeping)])
    writer.writeheader()
    for r in records:
        writer.writerow(asdict(r))
    return out.getvalue()
