"""UDP link to obsw_sim: one CCSDS packet per datagram."""

from __future__ import annotations

import socket
import time
from typing import List, Optional

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 10025


class UdpLink:
    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # connect() on UDP just fixes the peer: send() goes there and
        # recv() only accepts datagrams from it.
        self._sock.connect((host, port))

    def send(self, packet: bytes) -> None:
        self._sock.send(packet)

    def recv(self, timeout: float) -> Optional[bytes]:
        self._sock.settimeout(timeout)
        try:
            return self._sock.recv(65542)
        except (socket.timeout, ConnectionRefusedError):
            return None

    def collect(self, quiet: float = 0.3, limit: float = 3.0) -> List[bytes]:
        """Receive packets until none arrive for `quiet` seconds (or `limit` passes)."""
        packets: List[bytes] = []
        deadline = time.monotonic() + limit
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            pkt = self.recv(min(quiet, remaining))
            if pkt is None:
                break
            packets.append(pkt)
        return packets

    def listen(self, seconds: float) -> List[bytes]:
        """Receive everything that arrives during the next `seconds`."""
        packets: List[bytes] = []
        deadline = time.monotonic() + seconds
        while (remaining := deadline - time.monotonic()) > 0:
            pkt = self.recv(remaining)
            if pkt is not None:
                packets.append(pkt)
        return packets

    def close(self) -> None:
        self._sock.close()

    def __enter__(self) -> "UdpLink":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
