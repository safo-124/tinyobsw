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
    data = bytes([1, 1, 0x09, 0xC4, 0, 120, 0x6D, 0x60, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0,
                  0, 0x02, 0x0F, 0xA0, 0x11, 0x94])
    raw = Telemetry(100, 5, 3, 25, data=data, time=CucTime(10)).encode()
    fields = segments(raw)
    assert_tiles(raw, fields)
    values = {f["name"]: f["value"] for f in fields if f["group"] == "data"}
    assert values["Mode"] == "STANDBY"
    assert values["Temperature"] == "25.00 °C"
    assert values["Current"] == "120 mA"
    assert values["Bus voltage"] == "28.000 V"
    assert values["Temperature check"] == "within limits"
    assert values["Flags"] == "monitoring on"
    assert values["Danger limit"] == "45.00 °C"


def test_event_and_transition_layouts():
    ev = Telemetry(100, 0, 5, 4, data=bytes([0, 2, 0x11, 0xF8])).encode()
    tr = Telemetry(100, 0, 12, 12, data=bytes([0, 1, 0, 1, 0x11, 0xF8, 0x11, 0x94, 1, 2])).encode()
    for raw in (ev, tr):
        assert_tiles(raw, segments(raw))
    ev_values = {f["name"]: f["value"] for f in segments(ev)}
    assert ev_values["Event ID"] == "2 (Temperature above danger limit, high severity)"
    assert ev_values["Temperature"] == "46.00 °C"
    tr_values = {f["name"]: f["value"] for f in segments(tr)}
    assert tr_values["New status"] == "above danger"
    assert tr_values["Limit crossed"] == "45.00 °C"


def test_monitoring_tc_layouts():
    limits = commands.monitoring_limits(100, 0, 30, 35).encode()
    fault = commands.sim_heater_fault(100, 0).encode()
    assert_tiles(limits, segments(limits))
    values = {f["name"]: f["value"] for f in segments(limits)}
    assert (values["Warning limit"], values["Danger limit"]) == ("30.00 °C", "35.00 °C")
    assert {f["name"]: f["value"] for f in segments(fault)}["Function ID"] == "100 (simulate heater fault)"


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


def test_time_report_layouts():
    tm = Telemetry(100, 0, 9, 2, data=bytes([0, 0, 0, 0, 42, 0x80, 0])).encode()
    tc = commands.time_report_rate(100, 0, 2).encode()
    assert_tiles(tm, segments(tm))
    tm_values = {f["name"]: f["value"] for f in segments(tm)}
    assert tm_values["Rate exponent"] == "0 (every 1 s)"
    assert tm_values["On-board time sample"] == "42.5000 s"
    assert {f["name"]: f["value"] for f in segments(tc)}["Rate exponent"] == "2 (every 4 s)"
