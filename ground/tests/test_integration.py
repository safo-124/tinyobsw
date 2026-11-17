"""End-to-end: start the real obsw_sim binary and talk to it over UDP.

Skipped when the simulator has not been built. Point OBSW_SIM at the
binary to use a different build (the default is build/obsw_sim).
"""

import pytest

from conftest import APID, GROUND_ID, needs_sim, quiet_ping
from tinyground import commands
from tinyground.commands import Mode
from tinyground.errors import ObswStatus
from tinyground.link import UdpLink
from tinyground.pus import ACK_ALL, Telecommand, Telemetry
from tinyground.services import InstrumentHk, decode_event, decode_transition, decode_verification

pytestmark = needs_sim


@pytest.fixture
def link(sim_port):
    with UdpLink("127.0.0.1", sim_port) as lnk:
        lnk.send(quiet_ping())  # make this socket the simulator's ground address
        lnk.collect(quiet=0.2)
        yield lnk


def is_periodic_hk(tm: Telemetry) -> bool:
    return (tm.service, tm.subtype) == (3, 25) and tm.dest_id == 0


def exchange(link: UdpLink, tc: Telecommand) -> list:
    """Send a TC and return the replies, ignoring periodic housekeeping
    (it arrives every second and could land in the middle of a reply)."""
    link.send(tc.encode())
    return [t for t in (Telemetry.decode(p) for p in link.collect()) if not is_periodic_hk(t)]


def types(tms: list) -> list:
    return [(t.service, t.subtype) for t in tms]


def test_ping_full_verification_chain(link):
    tms = exchange(link, Telecommand(APID, 7, 17, 1, ACK_ALL, GROUND_ID))
    assert [(t.service, t.subtype) for t in tms] == [(1, 1), (1, 3), (17, 2), (1, 7)]
    for t in tms:
        assert t.apid == APID
        assert t.dest_id == GROUND_ID
    for t in (tms[0], tms[1], tms[3]):
        v = decode_verification(t)
        assert (v.request_apid, v.request_seq_count, v.ok) == (APID, 7, True)
    # sequence counts are consecutive and on-board time never runs backwards
    seqs = [t.seq_count for t in tms]
    assert seqs == list(range(seqs[0], seqs[0] + 4))
    times = [t.time.to_seconds() for t in tms]
    assert times == sorted(times)


def test_unknown_service_rejected(link):
    tms = exchange(link, Telecommand(APID, 8, 99, 1, ACK_ALL, GROUND_ID))
    assert len(tms) == 1
    v = decode_verification(tms[0])
    assert (tms[0].subtype, v.failure_code) == (2, ObswStatus.ILLEGAL_SERVICE)


def test_corrupted_crc_rejected(link):
    raw = bytearray(Telecommand(APID, 9, 17, 1, ACK_ALL, GROUND_ID).encode())
    raw[-1] ^= 0xFF
    link.send(bytes(raw))
    tms = [t for t in (Telemetry.decode(p) for p in link.collect()) if not is_periodic_hk(t)]
    assert len(tms) == 1
    assert decode_verification(tms[0]).failure_code == ObswStatus.CRC


def test_other_apid_gets_no_reply(link):
    assert exchange(link, Telecommand(APID + 1, 0, 17, 1, ACK_ALL, GROUND_ID)) == []


# ---- instrument, ST[8] and ST[3] ------------------------------------------------


def test_periodic_housekeeping_arrives_every_second(link):
    tms = [Telemetry.decode(p) for p in link.listen(3.5)]
    hks = [InstrumentHk.decode(t) for t in tms if is_periodic_hk(t)]
    assert 2 <= len(hks) <= 4
    for hk in hks:
        assert hk.sid == 1
        assert hk.mode_name == "OFF"
        assert hk.temperature_c == pytest.approx(20.0, abs=0.05)
        assert hk.current_ma == 0
        assert hk.voltage_v == pytest.approx(28.0, abs=0.03)


def test_mode_change_shows_in_housekeeping(link):
    tms = exchange(link, commands.set_mode(APID, 20, Mode.STANDBY, GROUND_ID))
    assert types(tms) == [(1, 1), (1, 3), (1, 7)]
    tms = exchange(link, commands.hk_one_shot(APID, 21, GROUND_ID, ack=0))
    assert types(tms) == [(3, 25)]
    assert tms[0].dest_id == GROUND_ID  # one-shot goes to whoever asked
    hk = InstrumentHk.decode(tms[0])
    assert (hk.mode_name, hk.mode_changes) == ("STANDBY", 1)
    assert hk.current_ma == pytest.approx(120, abs=4)


def test_illegal_transition_reported_as_completion_failure(link):
    tms = exchange(link, commands.set_mode(APID, 30, Mode.MEASURE, GROUND_ID))
    assert types(tms) == [(1, 1), (1, 3), (1, 8)]
    assert decode_verification(tms[2]).failure_code == ObswStatus.ILLEGAL_TRANSITION


def test_housekeeping_can_be_switched_off(link):
    assert types(exchange(link, commands.hk_disable(APID, 40, GROUND_ID))) == [(1, 1), (1, 3), (1, 7)]
    tms = [Telemetry.decode(p) for p in link.listen(2.5)]
    assert [t for t in tms if is_periodic_hk(t)] == []


def test_heater_fault_puts_instrument_in_safe_mode(link):
    """The fault scenario end to end: inject, watch the on-board reaction, recover."""
    for seq, tc in enumerate([commands.set_mode(APID, 50, Mode.STANDBY, GROUND_ID, ack=0),
                              commands.set_mode(APID, 51, Mode.MEASURE, GROUND_ID, ack=0),
                              commands.sim_heater_fault(APID, 52, GROUND_ID, ack=0)]):
        link.send(tc.encode())
    tms = [Telemetry.decode(p) for p in link.listen(14.0)]

    events = [decode_event(t) for t in tms if t.service == 5]
    transitions = [decode_transition(t) for t in tms if (t.service, t.subtype) == (12, 12)]
    assert [e.event_id for e in events][:3] == [1, 2, 4]    # warning, danger, put in SAFE
    assert events[2].previous_mode == "MEASURE"
    assert [(tr.previous, tr.current) for tr in transitions][:2] == [
        ("within limits", "above warning"), ("above warning", "above danger")]
    hks = [InstrumentHk.decode(t) for t in tms if (t.service, t.subtype) == (3, 25)]
    assert hks[-1].mode_name == "SAFE" and hks[-1].heater_fault

    # repair, recover to STANDBY: accepted and completed
    assert types(exchange(link, commands.sim_clear_fault(APID, 53, GROUND_ID))) == [(1, 1), (1, 3), (1, 7)]
    assert types(exchange(link, commands.set_mode(APID, 54, Mode.STANDBY, GROUND_ID))) == [(1, 1), (1, 3), (1, 7)]
