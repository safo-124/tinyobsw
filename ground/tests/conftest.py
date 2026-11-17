"""Shared fixtures: start the real obsw_sim binary on a free UDP port."""

import os
import socket
import subprocess
import time
from pathlib import Path

import pytest

from tinyground.link import UdpLink
from tinyground.pus import Telecommand

APID = 100
GROUND_ID = 10
REPO = Path(__file__).resolve().parents[2]
SIM = Path(os.environ.get("OBSW_SIM", REPO / "build" / "obsw_sim"))

needs_sim = pytest.mark.skipif(not SIM.exists(), reason=f"simulator not built: {SIM}")


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def quiet_ping(seq: int = 0) -> bytes:
    return Telecommand(APID, seq, 17, 1, ack_flags=0, source_id=GROUND_ID).encode()


@pytest.fixture
def sim_port():
    """Start obsw_sim, wait until it answers, yield its port, stop it cleanly."""
    port = free_udp_port()
    proc = subprocess.Popen([str(SIM), "-p", str(port), "-a", str(APID), "-q"])
    try:
        with UdpLink("127.0.0.1", port) as probe:
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                probe.send(quiet_ping())
                if probe.collect(quiet=0.2):
                    break
            else:
                pytest.fail("obsw_sim did not answer within 5 s")
        yield port
    finally:
        proc.terminate()
        assert proc.wait(timeout=5) == 0, "obsw_sim did not shut down cleanly"
