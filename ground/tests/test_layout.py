"""Packet inspector field layout."""

import pytest

from tinyground import commands
from tinyground.commands import Mode
from tinyground.layout import segments
from tinyground.pus import CucTime, Telemetry
from test_codec import KNOWN_TC, KNOWN_TM


def assert_tiles(raw: bytes, fields) -> None:
    """Fields must cover every byte exactly once, in order."""
    pos = 0
    for f in fields:
        assert f["start"] == pos, f
        assert f["end"] > f["start"], f
        pos = f["end"]
    assert pos == len(raw)


def names(fields):
    return [f["name"] for f in fields]


def test_known_tc_layout():
    fields = segments(KNOWN_TC)
    assert_tiles(KNOWN_TC, fields)
    assert names(fields) == ["Version, type, flag, APID", "Sequence", "Data length",
                             "PUS version, ack flags", "Service", "Subtype", "Source ID", "CRC"]
    assert fields[0]["value"] == "v0, TC, sec. header 1, APID 291"
    assert fields[3]["value"] == "v2, acks AC"
    assert fields[-1]["value"] == "0xF477 valid"
    assert {f["group"] for f in fields} == {"primary", "secondary", "crc"}


def test_known_tm_layout():
    fields = segments(KNOWN_TM)
    assert_tiles(KNOWN_TM, fields)
    assert "On-board time" in names(fields)
    time_field = next(f for f in fields if f["name"] == "On-board time")
    assert time_field["value"] == f"{0x01020304 + 0.5:.3f} s"


def test_housekeeping_split_into_values():
    data = bytes([1, 1, 0x09, 0xC4, 0, 120, 0x6D, 0x60, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0])
    raw = Telemetry(100, 5, 3, 25, data=data, time=CucTime(10)).encode()
    fields = segments(raw)
    assert_tiles(raw, fields)
    values = {f["name"]: f["value"] for f in fields if f["group"] == "data"}
    assert values["Mode"] == "STANDBY"
    assert values["Temperature"] == "25.00 °C"
    assert values["Current"] == "120 mA"
    assert values["Bus voltage"] == "28.000 V"


def test_verification_failure_shows_reason():
    tc = commands.ping(100, 7)
    raw = Telemetry(100, 0, 1, 2, data=tc.request_id + bytes([0, 6])).encode()
    values = {f["name"]: f["value"] for f in segments(raw)}
    assert values["Request ID"] == "TC APID 100, count 7"
    assert values["Failure code"] == "CRC (6)"


def test_mode_command_layout():
    raw = commands.set_mode(100, 0, Mode.MEASURE).encode()
    values = {f["name"]: f["value"] for f in segments(raw)}
    assert values["Function ID"] == "1 (set mode)"
    assert values["Mode"] == "MEASURE"


@pytest.mark.parametrize("cut", [0, 1, 3, 6, 9, 12])
def test_short_or_broken_packets_still_tile(cut):
    raw = KNOWN_TC[:cut]
    assert_tiles(raw, segments(raw))


def test_bad_crc_is_flagged():
    raw = bytearray(KNOWN_TC)
    raw[-1] ^= 0xFF
    assert segments(bytes(raw))[-1]["value"].endswith("INVALID")
