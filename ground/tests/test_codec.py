"""Ground-side codec tests.

The reference packets are the same bytes as in obsw/tests/test_pus.c, so
the C flight code and this Python ground code are checked against one
shared truth.
"""

import re
from pathlib import Path

import pytest

from tinyground.ccsds import CcsdsError, PrimaryHeader, TYPE_TC, next_seq_count
from tinyground.errors import ObswStatus
from tinyground.pus import (
    ACK_ACCEPTANCE,
    ACK_COMPLETION,
    CucTime,
    PusError,
    Telecommand,
    Telemetry,
    crc16,
)
from tinyground.services import decode_verification, describe

KNOWN_TC = bytes([0x19, 0x23, 0xC0, 0x00, 0x00, 0x06,
                  0x29, 0x11, 0x01, 0x00, 0x01,
                  0xF4, 0x77])
KNOWN_TM = bytes([0x09, 0x23, 0xC0, 0x07, 0x00, 0x0E,
                  0x20, 0x11, 0x02, 0x00, 0x03, 0x00, 0x01,
                  0x01, 0x02, 0x03, 0x04, 0x80, 0x00,
                  0x79, 0x0E])

STATUS_H = Path(__file__).resolve().parents[2] / "obsw" / "include" / "tinyobsw" / "status.h"


def test_crc_check_value():
    assert crc16(b"123456789") == 0x29B1


def test_primary_header_matches_c_vector():
    hdr = PrimaryHeader(type=TYPE_TC, sec_hdr_flag=True, apid=0x123, seq_count=5, data_len=10)
    assert hdr.encode() == bytes([0x19, 0x23, 0xC0, 0x05, 0x00, 0x0A])
    assert PrimaryHeader.decode(hdr.encode()) == hdr
    assert hdr.packet_len == 17


def test_primary_header_rejects_bad_fields():
    with pytest.raises(CcsdsError):
        PrimaryHeader(type=TYPE_TC, sec_hdr_flag=True, apid=0x800, seq_count=0, data_len=0).encode()
    with pytest.raises(CcsdsError):
        PrimaryHeader.decode(bytes([0x39, 0x23, 0xC0, 0x05, 0x00, 0x0A]))  # version 1
    assert next_seq_count(0x3FFF) == 0


def test_telecommand_matches_c_vector():
    tc = Telecommand(apid=0x123, seq_count=0, service=17, subtype=1,
                     ack_flags=ACK_ACCEPTANCE | ACK_COMPLETION, source_id=1)
    assert tc.encode() == KNOWN_TC
    assert Telecommand.decode(KNOWN_TC) == tc
    assert tc.request_id == KNOWN_TC[:4]


def test_telemetry_matches_c_vector():
    tm = Telemetry(apid=0x123, seq_count=7, service=17, subtype=2, msg_counter=3,
                   dest_id=1, time=CucTime(0x01020304, 0x8000))
    assert tm.encode() == KNOWN_TM
    assert Telemetry.decode(KNOWN_TM) == tm
    assert tm.time.to_seconds() == pytest.approx(0x01020304 + 0.5)


def test_round_trip_with_data():
    tc = Telecommand(0x7FF, 0x3FFF, 8, 1, 0xF, 0xFFFF, bytes(range(40)))
    assert Telecommand.decode(tc.encode()) == tc


@pytest.mark.parametrize("index", range(len(KNOWN_TM)))
def test_every_single_byte_corruption_rejected(index):
    bad = bytearray(KNOWN_TM)
    bad[index] ^= 0x5A
    with pytest.raises(PusError):
        Telemetry.decode(bytes(bad))


def test_wrong_type_rejected():
    with pytest.raises(PusError, match="wrong packet type"):
        Telecommand.decode(KNOWN_TM)
    with pytest.raises(PusError, match="wrong packet type"):
        Telemetry.decode(KNOWN_TC)


def test_verification_report_decoding():
    tc = Telecommand(100, 42, 99, 1, source_id=10)
    failure = Telemetry(100, 0, 1, 2, dest_id=10,
                        data=tc.request_id + bytes([0x00, ObswStatus.ILLEGAL_SERVICE]))
    v = decode_verification(failure)
    assert (v.request_apid, v.request_seq_count, v.ok) == (100, 42, False)
    assert v.failure_code == ObswStatus.ILLEGAL_SERVICE
    assert "ILLEGAL_SERVICE" in describe(failure)


def test_status_codes_match_flight_software():
    """The Python enum must agree with the C header, value for value."""
    c_codes = {
        name: int(value)
        for name, value in re.findall(r"OBSW_(?:ERR_)?(\w+)\s*=\s*(\d+)", STATUS_H.read_text())
    }
    py_codes = {s.name: s.value for s in ObswStatus}
    assert c_codes == py_codes


def test_command_builders_application_data():
    from tinyground import commands
    from tinyground.commands import Mode

    assert commands.set_mode(100, 0, Mode.MEASURE).data == bytes([0x00, 0x01, 0x02])
    assert commands.reset_science(100, 0).data == bytes([0x00, 0x02])
    assert commands.hk_one_shot(100, 0).data == bytes([1, 1])
    assert commands.hk_interval(100, 0, 5).data == bytes([1, 1, 0x00, 0x05])
    tc = commands.set_mode(100, 3, Mode.STANDBY, source=10)
    assert (tc.service, tc.subtype, tc.source_id) == (8, 1, 10)
    assert Telecommand.decode(tc.encode()) == tc


def test_housekeeping_decoding():
    from tinyground.services import InstrumentHk

    data = bytes([1, 2]) + (-150).to_bytes(2, "big", signed=True) + bytes(
        [0x01, 0xC2, 0x6D, 0x60, 0x00, 0x00, 0x0B, 0xB8, 0x00, 0x03, 0x00, 0x07, 0x00, 0x01])
    hk = InstrumentHk.decode(Telemetry(100, 0, 3, 25, data=data))
    assert hk.mode_name == "MEASURE"
    assert hk.temperature_c == -1.5          # signed: below zero survives the trip
    assert hk.current_ma == 450
    assert hk.voltage_v == 28.0
    assert hk.science_count == 3000
    assert (hk.mode_changes, hk.tc_accepted, hk.tc_rejected) == (3, 7, 1)
