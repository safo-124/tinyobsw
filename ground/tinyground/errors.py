"""Failure codes reported by the flight software in TM[1,2] and TM[1,8].

Mirrors obsw/include/tinyobsw/status.h. A test checks the two stay in sync.
"""

from enum import IntEnum


class ObswStatus(IntEnum):
    OK = 0
    NULL = 1
    SHORT_BUFFER = 2
    BAD_VERSION = 3
    FIELD_RANGE = 4
    LENGTH_MISMATCH = 5
    CRC = 6
    WRONG_TYPE = 7
    NO_SEC_HDR = 8
    BAD_PUS_VERSION = 9
    UNSUPPORTED = 10
    WRONG_APID = 11
    ILLEGAL_SERVICE = 12
    ILLEGAL_SUBTYPE = 13
    BAD_APP_DATA = 14
    ILLEGAL_TRANSITION = 15
    UNKNOWN_FUNCTION = 16
    UNKNOWN_SID = 17
    UNKNOWN_PMON = 18

    @classmethod
    def describe(cls, code: int) -> str:
        try:
            return f"{cls(code).name} ({code})"
        except ValueError:
            return f"unknown code {code}"
