"""The console's ground segment server, end to end against obsw_sim."""

import time

import pytest
from fastapi.testclient import TestClient

from conftest import APID, needs_sim
from tinyground.api import create_app


def wait_for(client, predicate, timeout=3.0):
    """Poll /api/events until predicate(events) is true."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        events = client.get("/api/events").json()
        if predicate(events):
            return events
        time.sleep(0.05)
    pytest.fail(f"condition not met; last events: {[e.get('type') for e in events]}")


def verifications_for(events, seq):
    return [e["verification"] for e in events
            if e["kind"] == "tm" and "verification" in e and e["verification"]["request_seq"] == seq]


def test_api_without_simulator_reports_silent_link():
    with TestClient(create_app(port=1, serve_console=False)) as client:
        assert client.get("/api/health").json() == {"status": "ok"}
        status = client.get("/api/status").json()
        assert status["link"] == "never"
        # the keep-alive ping is still logged as an automatic TC
        events = client.get("/api/events").json()
        assert events and events[0]["kind"] == "tc" and events[0]["auto"] is True


def test_request_validation():
    with TestClient(create_app(port=1, serve_console=False)) as client:
        assert client.post("/api/tc", json={"command": "launch"}).status_code == 422
        assert client.post("/api/tc", json={"command": "set_mode"}).status_code == 422
        assert client.post("/api/tc", json={"command": "set_mode", "mode": "WARP"}).status_code == 422
        assert client.post("/api/tc", json={"command": "hk_interval", "seconds": 0}).status_code == 422
        assert client.post("/api/fault", json={"fault": "nope"}).status_code == 422


@needs_sim
def test_ping_shows_full_chain(sim_port):
    with TestClient(create_app(port=sim_port, apid=APID, serve_console=False)) as client:
        wait_for(client, lambda ev: client.get("/api/status").json()["link"] == "live")
        tc = client.post("/api/tc", json={"command": "ping"}).json()
        assert tc["type"] == "TC[17,1]" and tc["label"] == "Are you alive?"
        events = wait_for(client, lambda ev: len(verifications_for(ev, tc["seq"])) == 3)
        stages = [v["stage"] for v in verifications_for(events, tc["seq"])]
        assert stages == ["acceptance OK", "start OK", "completion OK"]
        assert any(e.get("type") == "TM[17,2]" for e in events)


@needs_sim
def test_mode_change_and_housekeeping(sim_port):
    with TestClient(create_app(port=sim_port, apid=APID, serve_console=False)) as client:
        tc = client.post("/api/tc", json={"command": "set_mode", "mode": "STANDBY"}).json()
        wait_for(client, lambda ev: any(v["stage"] == "completion OK"
                                        for v in verifications_for(ev, tc["seq"])))
        client.post("/api/tc", json={"command": "hk_one_shot", "ack": 0})
        wait_for(client, lambda ev: (client.get("/api/status").json()["last_hk"] or {})
                 .get("mode") == "STANDBY")


@needs_sim
def test_faults_are_rejected_with_reasons(sim_port):
    with TestClient(create_app(port=sim_port, apid=APID, serve_console=False)) as client:
        expected = {"bad_crc": "CRC (6)", "unknown_service": "ILLEGAL_SERVICE (12)",
                    "bad_mode_value": "BAD_APP_DATA (14)", "truncated": "LENGTH_MISMATCH (5)"}
        for fault, reason in expected.items():
            tc = client.post("/api/fault", json={"fault": fault}).json()
            assert tc["fault"] == fault
            events = wait_for(client, lambda ev: verifications_for(ev, tc["seq"]))
            v = verifications_for(events, tc["seq"])[0]
            assert (v["stage"], v["failure"]) == ("acceptance FAILED", reason)


@needs_sim
def test_websocket_streams_events(sim_port):
    with TestClient(create_app(port=sim_port, apid=APID, serve_console=False)) as client:
        with client.websocket_connect("/ws") as ws:
            first = ws.receive_json()
            assert first["type"] == "snapshot"
            client.post("/api/tc", json={"command": "ping"})
            seen = set()
            for _ in range(50):
                msg = ws.receive_json()
                assert msg["type"] == "event"
                seen.add(msg["event"].get("type"))
                if "TM[17,2]" in seen:
                    break
            assert {"TC[17,1]", "TM[17,2]"} <= seen
