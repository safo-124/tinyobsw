"""Command-line ground station for obsw_sim.

    python -m tinyground.cli ping
    python -m tinyground.cli mode standby
    python -m tinyground.cli mode measure
    python -m tinyground.cli hk                      # one housekeeping report now
    python -m tinyground.cli hk-interval 5
    python -m tinyground.cli monitor --seconds 10    # watch live telemetry
    python -m tinyground.cli send 99 1               # any TC, e.g. an unknown service
"""

from __future__ import annotations

import argparse
import sys

from . import commands
from .link import DEFAULT_HOST, DEFAULT_PORT, UdpLink
from .pus import ACK_ALL, PusError, Telecommand, Telemetry
from .services import describe


def _print_tm(raw: bytes) -> None:
    try:
        print("  " + describe(Telemetry.decode(raw)))
    except (PusError, ValueError) as e:
        print(f"  invalid TM ({e}): {raw.hex()}")


def _send_and_print(args: argparse.Namespace, tc: Telecommand) -> int:
    packet = tc.encode()
    print(f"TC[{tc.service},{tc.subtype}]  seq={tc.seq_count} ack=0x{tc.ack_flags:X} "
          f"{len(packet)} bytes: {packet.hex()}")
    with UdpLink(args.host, args.port) as link:
        link.send(packet)
        replies = link.collect(quiet=args.wait)
    if not replies:
        print("no reply (is obsw_sim running?)", file=sys.stderr)
        return 1
    for raw in replies:
        _print_tm(raw)
    return 0


def _monitor(args: argparse.Namespace) -> int:
    # The simulator sends telemetry to whoever sent the last TC,
    # so announce ourselves with a quiet ping first.
    hello = commands.ping(args.apid, args.seq, args.source, ack=0).encode()
    print(f"monitoring for {args.seconds:g} s (Ctrl+C to stop)")
    with UdpLink(args.host, args.port) as link:
        link.send(hello)
        try:
            for raw in link.listen(args.seconds):
                _print_tm(raw)
        except KeyboardInterrupt:
            pass
    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="tinyground", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default=DEFAULT_HOST)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--apid", type=lambda s: int(s, 0), default=100)
    p.add_argument("--source", type=int, default=10, help="our ground source ID")
    p.add_argument("--seq", type=int, default=0, help="TC sequence count")
    p.add_argument("--ack", type=lambda s: int(s, 0), default=ACK_ALL,
                   help="acknowledgement flags, 0x0-0xF (default 0xF: all reports)")
    p.add_argument("--wait", type=float, default=0.3, help="seconds of silence before giving up")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("ping", help="TC[17,1] are you alive?")
    m = sub.add_parser("mode", help="TC[8,1] change instrument mode")
    m.add_argument("mode", type=str.upper, choices=[x.name for x in commands.Mode])
    sub.add_parser("reset-science", help="TC[8,1] zero the science counter")
    sub.add_parser("hk", help="TC[3,27] one housekeeping report now")
    sub.add_parser("hk-enable", help="TC[3,5] periodic housekeeping on")
    sub.add_parser("hk-disable", help="TC[3,6] periodic housekeeping off")
    hi = sub.add_parser("hk-interval", help="TC[3,31] housekeeping period")
    hi.add_argument("seconds", type=int)
    mon = sub.add_parser("monitor", help="print all telemetry for a while")
    mon.add_argument("--seconds", type=float, default=10.0)
    send = sub.add_parser("send", help="any TC")
    send.add_argument("service", type=int)
    send.add_argument("subtype", type=int)
    send.add_argument("--data", default="", help="application data as hex")

    args = p.parse_args(argv)
    a, s, src, ack = args.apid, args.seq, args.source, args.ack

    if args.cmd == "monitor":
        return _monitor(args)
    builders = {
        "ping": lambda: commands.ping(a, s, src, ack),
        "mode": lambda: commands.set_mode(a, s, commands.Mode[args.mode], src, ack),
        "reset-science": lambda: commands.reset_science(a, s, src, ack),
        "hk": lambda: commands.hk_one_shot(a, s, src, ack),
        "hk-enable": lambda: commands.hk_enable(a, s, src, ack),
        "hk-disable": lambda: commands.hk_disable(a, s, src, ack),
        "hk-interval": lambda: commands.hk_interval(a, s, args.seconds, src, ack),
        "send": lambda: Telecommand(a, s, args.service, args.subtype, ack, src,
                                    bytes.fromhex(args.data)),
    }
    return _send_and_print(args, builders[args.cmd]())


if __name__ == "__main__":
    sys.exit(main())
