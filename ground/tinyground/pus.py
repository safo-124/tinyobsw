"""ECSS PUS-C telecommands and telemetry, ground side.

Same packet layout as obsw/include/tinyobsw/pus.h:

    TC: [primary 6][version|ack 1][service 1][subtype 1][source ID 2][data][CRC 2]
    TM: [primary 6][version|time ref 1][service 1][subtype 1][msg counter 2]
        [dest ID 2][CUC time 4+2][data][CRC 2]
"""

from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass

from .ccsds import (
    PRIMARY_HDR_LEN,
    SEQ_UNSEGMENTED,
    TYPE_TC,
    TYPE_TM,
    CcsdsError,
    PrimaryHeader,
)

PUS_VERSION_C = 2
TC_SEC_HDR_LEN = 5
TM_SEC_HDR_LEN = 13
CRC_LEN = 2

ACK_ACCEPTANCE = 0x1
ACK_START = 0x2
ACK_PROGRESS = 0x4
ACK_COMPLETION = 0x8
ACK_ALL = 0xF


class PusError(ValueError):
    """A PUS packet failed validation."""


def crc16(data: bytes) -> int:
    """CRC-16-CCITT, poly 0x1021, init 0xFFFF: PUS packet error control."""
    return binascii.crc_hqx(data, 0xFFFF)


@dataclass(frozen=True)
class CucTime:
    coarse: int  # whole seconds
    fine: int = 0  # 1/65536 s

    def to_seconds(self) -> float:
        return self.coarse + self.fine / 65536.0

    @classmethod
    def from_seconds(cls, t: float) -> "CucTime":
        coarse = int(t)
        return cls(coarse, int(round((t - coarse) * 65536)) & 0xFFFF)


def _finish(primary_type: int, apid: int, seq_count: int, body: bytes) -> bytes:
    header = PrimaryHeader(
        type=primary_type,
        sec_hdr_flag=True,
        apid=apid,
        seq_count=seq_count,
        data_len=len(body) + CRC_LEN - 1,
    ).encode()
    packet = header + body
    return packet + struct.pack(">H", crc16(packet))


def _check(buf: bytes, want_type: int, sec_len: int) -> PrimaryHeader:
    """Same checks, in the same order, as check_packet() in obsw/src/pus.c."""
    try:
        hdr = PrimaryHeader.decode(buf)
    except CcsdsError as e:
        raise PusError(str(e)) from e
    if hdr.packet_len != len(buf):
        raise PusError(f"length field says {hdr.packet_len} bytes, got {len(buf)}")
    if len(buf) < PRIMARY_HDR_LEN + 1 + CRC_LEN:
        raise PusError("packet too short")
    if crc16(buf[:-CRC_LEN]) != struct.unpack(">H", buf[-CRC_LEN:])[0]:
        raise PusError("CRC mismatch")
    if hdr.type != want_type:
        raise PusError("wrong packet type")
    if not hdr.sec_hdr_flag:
        raise PusError("no secondary header")
    if hdr.seq_flags != SEQ_UNSEGMENTED:
        raise PusError("segmented packets are not supported")
    if len(buf) < PRIMARY_HDR_LEN + sec_len + CRC_LEN:
        raise PusError("packet too short for its secondary header")
    if buf[PRIMARY_HDR_LEN] >> 4 != PUS_VERSION_C:
        raise PusError(f"PUS version {buf[PRIMARY_HDR_LEN] >> 4}, expected 2")
    return hdr


@dataclass(frozen=True)
class Telecommand:
    apid: int
    seq_count: int
    service: int
    subtype: int
    ack_flags: int = ACK_ALL
    source_id: int = 0
    data: bytes = b""

    def encode(self) -> bytes:
        if not 0 <= self.ack_flags <= 0xF:
            raise PusError("ack flags are 4 bits")
        body = struct.pack(
            ">BBBH",
            (PUS_VERSION_C << 4) | self.ack_flags,
            self.service,
            self.subtype,
            self.source_id,
        )
        return _finish(TYPE_TC, self.apid, self.seq_count, body + self.data)

    @property
    def request_id(self) -> bytes:
        """What the flight software quotes back in ST[1] reports."""
        return self.encode()[:4]

    @classmethod
    def decode(cls, buf: bytes) -> "Telecommand":
        hdr = _check(buf, TYPE_TC, TC_SEC_HDR_LEN)
        b0, service, subtype, source = struct.unpack(">BBBH", buf[6:11])
        return cls(
            apid=hdr.apid,
            seq_count=hdr.seq_count,
            service=service,
            subtype=subtype,
            ack_flags=b0 & 0xF,
            source_id=source,
            data=bytes(buf[11:-CRC_LEN]),
        )


@dataclass(frozen=True)
class Telemetry:
    apid: int
    seq_count: int
    service: int
    subtype: int
    msg_counter: int = 0
    dest_id: int = 0
    time: CucTime = CucTime(0, 0)
    data: bytes = b""
    time_ref_status: int = 0

    def encode(self) -> bytes:
        body = struct.pack(
            ">BBBHHIH",
            (PUS_VERSION_C << 4) | self.time_ref_status,
            self.service,
            self.subtype,
            self.msg_counter,
            self.dest_id,
            self.time.coarse,
            self.time.fine,
        )
        return _finish(TYPE_TM, self.apid, self.seq_count, body + self.data)

    @classmethod
    def decode(cls, buf: bytes) -> "Telemetry":
        hdr = _check(buf, TYPE_TM, TM_SEC_HDR_LEN)
        b0, service, subtype, counter, dest, coarse, fine = struct.unpack(
            ">BBBHHIH", buf[6:19]
        )
        return cls(
            apid=hdr.apid,
            seq_count=hdr.seq_count,
            service=service,
            subtype=subtype,
            msg_counter=counter,
            dest_id=dest,
            time=CucTime(coarse, fine),
            data=bytes(buf[19:-CRC_LEN]),
            time_ref_status=b0 & 0xF,
        )

    @property
    def type_str(self) -> str:
        return f"TM[{self.service},{self.subtype}]"
