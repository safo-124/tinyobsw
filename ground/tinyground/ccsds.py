"""CCSDS Space Packet primary header (CCSDS 133.0-B-2), ground side.

Same bit layout as obsw/include/tinyobsw/ccsds.h:

    word 0: version(3) | type(1) | sec hdr flag(1) | APID(11)
    word 1: sequence flags(2) | sequence count(14)
    word 2: packet data length - 1
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

PRIMARY_HDR_LEN = 6
APID_MAX = 0x7FF
APID_IDLE = 0x7FF
SEQ_COUNT_MAX = 0x3FFF

TYPE_TM = 0
TYPE_TC = 1

SEQ_CONTINUATION = 0
SEQ_FIRST = 1
SEQ_LAST = 2
SEQ_UNSEGMENTED = 3


class CcsdsError(ValueError):
    """A packet or header field is invalid."""


@dataclass(frozen=True)
class PrimaryHeader:
    type: int
    sec_hdr_flag: bool
    apid: int
    seq_count: int
    data_len: int  # packet data field length minus one
    seq_flags: int = SEQ_UNSEGMENTED
    version: int = 0

    @property
    def packet_len(self) -> int:
        return PRIMARY_HDR_LEN + self.data_len + 1

    def encode(self) -> bytes:
        if self.version != 0:
            raise CcsdsError("CCSDS version must be 0")
        if self.type not in (TYPE_TM, TYPE_TC):
            raise CcsdsError(f"bad packet type {self.type}")
        if not 0 <= self.apid <= APID_MAX:
            raise CcsdsError(f"APID {self.apid} out of range")
        if not 0 <= self.seq_flags <= 3:
            raise CcsdsError(f"bad sequence flags {self.seq_flags}")
        if not 0 <= self.seq_count <= SEQ_COUNT_MAX:
            raise CcsdsError(f"sequence count {self.seq_count} out of range")
        if not 0 <= self.data_len <= 0xFFFF:
            raise CcsdsError(f"data length {self.data_len} out of range")
        word0 = (self.type << 12) | (int(self.sec_hdr_flag) << 11) | self.apid
        word1 = (self.seq_flags << 14) | self.seq_count
        return struct.pack(">HHH", word0, word1, self.data_len)

    @classmethod
    def decode(cls, buf: bytes) -> "PrimaryHeader":
        if len(buf) < PRIMARY_HDR_LEN:
            raise CcsdsError("buffer shorter than a primary header")
        word0, word1, data_len = struct.unpack(">HHH", buf[:PRIMARY_HDR_LEN])
        version = word0 >> 13
        if version != 0:
            raise CcsdsError(f"CCSDS version {version}, expected 0")
        return cls(
            type=(word0 >> 12) & 1,
            sec_hdr_flag=bool((word0 >> 11) & 1),
            apid=word0 & APID_MAX,
            seq_count=word1 & SEQ_COUNT_MAX,
            data_len=data_len,
            seq_flags=word1 >> 14,
        )


def next_seq_count(count: int) -> int:
    return (count + 1) & SEQ_COUNT_MAX
