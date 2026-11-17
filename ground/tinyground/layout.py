"""Break a raw packet into labelled byte fields, for the console's packet inspector.

Each field is a dict: name, group, start, end (byte offsets, end exclusive), value.
group is one of "primary", "secondary", "data", "crc" and decides its colour.
The fields always tile the packet exactly, even when the packet is invalid.
"""

from __future__ import annotations

import struct
from typing import Dict, List

from .ccsds import PRIMARY_HDR_LEN
from .errors import ObswStatus
from .pus import CRC_LEN, crc16
from .services import MODE_NAMES

Field = Dict[str, object]

SEQ_FLAG_NAMES = {0: "continuation", 1: "first", 2: "last", 3: "unsegmented"}


def _field(name: str, group: str, start: int, end: int, value: str) -> Field:
    return {"name": name, "group": group, "start": start, "end": end, "value": value}


def _u16(b: bytes, i: int) -> int:
    return struct.unpack_from(">H", b, i)[0]


def _primary(raw: bytes) -> List[Field]:
    out: List[Field] = []
    if len(raw) >= 2:
        w0 = _u16(raw, 0)
        kind = "TC" if (w0 >> 12) & 1 else "TM"
        out.append(_field("Version, type, flag, APID", "primary", 0, 2,
                          f"v{w0 >> 13}, {kind}, sec. header {(w0 >> 11) & 1}, APID {w0 & 0x7FF}"))
    if len(raw) >= 4:
        w1 = _u16(raw, 2)
        out.append(_field("Sequence", "primary", 2, 4,
                          f"{SEQ_FLAG_NAMES[w1 >> 14]}, count {w1 & 0x3FFF}"))
    if len(raw) >= 6:
        dl = _u16(raw, 4)
        out.append(_field("Data length", "primary", 4, 6, f"{dl} (= {dl + 1} bytes follow)"))
    return out


def _tc_secondary(raw: bytes) -> List[Field]:
    b0 = raw[6]
    acks = "".join(c for bit, c in ((1, "A"), (2, "S"), (4, "P"), (8, "C")) if b0 & bit) or "none"
    return [
        _field("PUS version, ack flags", "secondary", 6, 7, f"v{b0 >> 4}, acks {acks}"),
        _field("Service", "secondary", 7, 8, str(raw[7])),
        _field("Subtype", "secondary", 8, 9, str(raw[8])),
        _field("Source ID", "secondary", 9, 11, str(_u16(raw, 9))),
    ]


def _tm_secondary(raw: bytes) -> List[Field]:
    coarse, fine = struct.unpack_from(">IH", raw, 13)
    return [
        _field("PUS version, time ref.", "secondary", 6, 7, f"v{raw[6] >> 4}, ref {raw[6] & 0xF}"),
        _field("Service", "secondary", 7, 8, str(raw[7])),
        _field("Subtype", "secondary", 8, 9, str(raw[8])),
        _field("Message counter", "secondary", 9, 11, str(_u16(raw, 9))),
        _field("Destination ID", "secondary", 11, 13, str(_u16(raw, 11))),
        _field("On-board time", "secondary", 13, 19, f"{coarse + fine / 65536:.3f} s"),
    ]


def _data_fields(raw: bytes, is_tc: bool, start: int, end: int) -> List[Field]:
    """Known application data layouts get one field per value."""
    service, subtype = raw[7], raw[8]
    d = raw[start:end]
    n = len(d)

    if not is_tc and service == 1 and n in (4, 6):
        out = [_field("Request ID", "data", start, start + 4,
                      f"TC APID {_u16(d, 0) & 0x7FF}, count {_u16(d, 2) & 0x3FFF}")]
        if n == 6:
            out.append(_field("Failure code", "data", start + 4, end,
                              ObswStatus.describe(_u16(d, 4))))
        return out

    if not is_tc and (service, subtype) == (3, 25) and n == 18:
        sid, mode, temp, cur, volt, sci, chg, acc, rej = struct.unpack(">BBhHHIHHH", d)
        layout = [
            ("SID", 1, str(sid)),
            ("Mode", 1, MODE_NAMES.get(mode, str(mode))),
            ("Temperature", 2, f"{temp / 100:.2f} °C"),
            ("Current", 2, f"{cur} mA"),
            ("Bus voltage", 2, f"{volt / 1000:.3f} V"),
            ("Science count", 4, str(sci)),
            ("Mode changes", 2, str(chg)),
            ("TCs accepted", 2, str(acc)),
            ("TCs rejected", 2, str(rej)),
        ]
        out, pos = [], start
        for name, size, value in layout:
            out.append(_field(name, "data", pos, pos + size, value))
            pos += size
        return out

    if is_tc and (service, subtype) == (8, 1) and n >= 2:
        func = _u16(d, 0)
        out = [_field("Function ID", "data", start, start + 2,
                      {1: "1 (set mode)", 2: "2 (reset science)"}.get(func, str(func)))]
        if func == 1 and n == 3:
            out.append(_field("Mode", "data", start + 2, end, MODE_NAMES.get(d[2], str(d[2]))))
        elif n > 2:
            out.append(_field("Arguments", "data", start + 2, end, f"{n - 2} bytes"))
        return out

    if n == 0:
        return []
    return [_field("Application data", "data", start, end, f"{n} bytes")]


def segments(raw: bytes) -> List[Field]:
    fields = _primary(raw)
    covered = fields[-1]["end"] if fields else 0
    is_tc = len(raw) >= 1 and bool(raw[0] & 0x10)
    sec_len = 5 if is_tc else 13
    body_end = len(raw) - CRC_LEN

    if len(raw) >= PRIMARY_HDR_LEN + sec_len + CRC_LEN:
        fields += _tc_secondary(raw) if is_tc else _tm_secondary(raw)
        fields += _data_fields(raw, is_tc, PRIMARY_HDR_LEN + sec_len, body_end)
        ok = crc16(raw[:-CRC_LEN]) == _u16(raw, body_end)
        fields.append(_field("CRC", "crc", body_end, len(raw),
                             f"0x{_u16(raw, body_end):04X} {'valid' if ok else 'INVALID'}"))
    elif len(raw) > covered:
        fields.append(_field("Unparsed", "data", int(covered), len(raw),
                             f"{len(raw) - int(covered)} bytes (packet too short)"))
    return fields
