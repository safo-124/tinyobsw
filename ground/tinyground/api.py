"""Ground segment server for the web console.

Keeps one UDP link to obsw_sim, decodes every packet in both directions
and streams them to browsers over a WebSocket.

    uvicorn tinyground.api:app --reload

Environment: OBSW_HOST (127.0.0.1), OBSW_PORT (10025), OBSW_APID (100),
OBSW_CLOCK_BASE_PPM (250) and OBSW_CLOCK_TEMPCO (10): the simulator's
oscillator model, used only to show the "true" drift next to the estimate.

    GET  /api/health          liveness
    GET  /api/status          link state and the latest housekeeping
    GET  /api/events?limit=N  recent TC/TM events, oldest first
    POST /api/tc              send a telecommand        {"command": "set_mode", "mode": "STANDBY"}
    POST /api/fault           send a deliberately bad TC {"fault": "bad_crc"}
    GET  /api/l1?limit=N      Level 1 housekeeping (UTC-stamped), oldest first
    GET  /api/l1.csv          the same, as a CSV download
    WS   /ws                  snapshot on connect, then one message per event
"""

from __future__ import annotations

import asyncio
import os
import struct
import time
from collections import deque
from contextlib import asynccontextmanager, suppress
from dataclasses import asdict
from pathlib import Path
from typing import Deque, Dict, List, Literal, Optional, Set

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import PlainTextResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from . import commands
from .ccsds import next_seq_count
from .errors import ObswStatus
from .l0_l1 import L1Housekeeping, iso_utc, to_csv, to_l1
from .layout import segments
from .pus import ACK_ALL, PusError, Telecommand, Telemetry
from .services import (ST01_NAMES, InstrumentHk, decode_event, decode_transition,
                       decode_verification, summary)
from .timecorr import TimeCorrelator

GROUND_ID = 10
LINK_SILENT_AFTER_S = 3.0
EVENT_HISTORY = 1000
L1_HISTORY = 3600
CONSOLE_DIST = Path(__file__).resolve().parents[2] / "console" / "dist"

Event = Dict[str, object]


class TcRequest(BaseModel):
    command: Literal["ping", "set_mode", "reset_science", "hk_one_shot",
                     "hk_enable", "hk_disable", "hk_interval",
                     "heater_fault", "repair_heater", "monitoring_on", "monitoring_off",
                     "set_limits"]
    mode: Optional[Literal["OFF", "STANDBY", "MEASURE", "SAFE"]] = None
    seconds: Optional[int] = Field(default=None, ge=1, le=3600)
    warning_c: Optional[float] = Field(default=None, ge=-50, le=150)
    danger_c: Optional[float] = Field(default=None, ge=-50, le=150)
    ack: int = Field(default=ACK_ALL, ge=0, le=15)


class FaultRequest(BaseModel):
    fault: Literal["bad_crc", "truncated", "unknown_service", "bad_mode_value"]


FAULT_LABELS = {
    "bad_crc": "Ping with a corrupted CRC",
    "truncated": "Ping missing its last byte",
    "unknown_service": "Command for unknown service 99",
    "bad_mode_value": "Set mode 9, which does not exist",
}


class _Protocol(asyncio.DatagramProtocol):
    def __init__(self, station: "GroundStation"):
        self.station = station

    def datagram_received(self, data: bytes, addr) -> None:
        self.station.on_tm(data)

    def error_received(self, exc: Exception) -> None:
        pass  # nothing listening yet: the link shows as silent


class GroundStation:
    def __init__(self, host: str, port: int, apid: int,
                 clock_base_ppm: float = 250.0, clock_tempco: float = 10.0):
        self.host, self.port, self.apid = host, port, apid
        self.clock_base_ppm, self.clock_tempco = clock_base_ppm, clock_tempco
        self.timecorr = TimeCorrelator()
        self.l1: Deque[L1Housekeeping] = deque(maxlen=L1_HISTORY)
        self.events: Deque[Event] = deque(maxlen=EVENT_HISTORY)
        self.subscribers: Set[asyncio.Queue] = set()
        self.seq = 0
        self.next_id = 1
        self.tc_sent = 0
        self.tm_received = 0
        self.last_tm_at: Optional[float] = None
        self.last_hk: Optional[Dict[str, object]] = None
        self.transport: Optional[asyncio.DatagramTransport] = None
        self._keepalive: Optional[asyncio.Task] = None

    # ---- lifecycle -------------------------------------------------------

    async def start(self) -> None:
        loop = asyncio.get_running_loop()
        self.transport, _ = await loop.create_datagram_endpoint(
            lambda: _Protocol(self), remote_addr=(self.host, self.port))
        self._keepalive = asyncio.create_task(self._keep_link_alive())

    async def stop(self) -> None:
        if self._keepalive:
            self._keepalive.cancel()
            with suppress(asyncio.CancelledError):
                await self._keepalive
        if self.transport:
            self.transport.close()

    async def _keep_link_alive(self) -> None:
        # The simulator sends telemetry to whoever sent the last TC. When no
        # TM has arrived for a while (start-up, simulator restarted), send a
        # quiet ping so it knows where we are.
        while True:
            if self.link_state() != "live":
                self.send(commands.ping(self.apid, self.seq, GROUND_ID, ack=0),
                          label="Link check", auto=True)
            await asyncio.sleep(2.0)

    # ---- state -----------------------------------------------------------

    def link_state(self) -> str:
        if self.last_tm_at is None:
            return "never"
        return "live" if time.monotonic() - self.last_tm_at < LINK_SILENT_AFTER_S else "silent"

    def model_drift_ppm(self) -> Optional[float]:
        """The simulator's true drift, from its oscillator model and the latest temperature."""
        if self.last_hk is None:
            return None
        return self.clock_base_ppm + self.clock_tempco * (float(self.last_hk["temperature_c"]) - 25.0)

    def clock(self) -> Optional[Dict[str, object]]:
        est = self.timecorr.estimate()
        if est is None:
            return None
        return {
            "drift_ppm": est.drift_ppm,
            "drift_sigma_ppm": est.drift_sigma_ppm,
            "base_ppm": est.base_ppm,
            "tempco_ppm_per_c": est.tempco_ppm_per_c,
            "tempco_sigma": est.tempco_sigma,
            "model_drift_ppm": self.model_drift_ppm(),
            "obt_zero_utc": iso_utc(est.utc_of_obt_zero),
            "offset_sigma_ms": est.offset_sigma_s * 1e3,
            "samples": est.samples,
            "rejected": est.rejected,
        }

    def status(self) -> Dict[str, object]:
        return {
            "clock": self.clock(),
            "link": self.link_state(),
            "sim": f"{self.host}:{self.port}",
            "apid": self.apid,
            "tc_sent": self.tc_sent,
            "tm_received": self.tm_received,
            "last_hk": self.last_hk,
        }

    def _publish(self, event: Event) -> Event:
        event["id"] = self.next_id
        self.next_id += 1
        self.events.append(event)
        for q in list(self.subscribers):
            with suppress(asyncio.QueueFull):
                q.put_nowait(event)
        return event

    # ---- uplink ----------------------------------------------------------

    def send(self, tc: Telecommand, label: str, raw: Optional[bytes] = None,
             fault: Optional[str] = None, auto: bool = False) -> Event:
        packet = raw if raw is not None else tc.encode()
        if self.transport is not None:
            self.transport.sendto(packet)
        self.tc_sent += 1
        self.seq = next_seq_count(self.seq)
        return self._publish({
            "kind": "tc",
            "time": time.time(),
            "seq": tc.seq_count,
            "service": tc.service,
            "subtype": tc.subtype,
            "type": f"TC[{tc.service},{tc.subtype}]",
            "ack": tc.ack_flags,
            "label": label,
            "fault": fault,
            "auto": auto,
            "hex": packet.hex(),
            "fields": segments(packet),
        })

    # ---- downlink --------------------------------------------------------

    def on_tm(self, raw: bytes) -> None:
        received_utc = time.time()  # first thing: this is the time correlation measurement
        self.tm_received += 1
        self.last_tm_at = time.monotonic()
        event: Event = {"kind": "tm", "time": time.time(), "hex": raw.hex(),
                        "fields": segments(raw)}
        try:
            tm = Telemetry.decode(raw)
        except PusError as e:
            event.update(valid=False, type="invalid TM", summary=str(e))
            self._publish(event)
            return

        event.update(valid=True, type=tm.type_str, service=tm.service, subtype=tm.subtype,
                     seq=tm.seq_count, dest=tm.dest_id, obt=tm.time.to_seconds(),
                     summary=summary(tm))
        if (tm.service, tm.subtype) == (9, 2) and len(tm.data) == 7:
            obt = struct.unpack(">I", tm.data[1:5])[0] + struct.unpack(">H", tm.data[5:7])[0] / 65536.0
            temp = float(self.last_hk["temperature_c"]) if self.last_hk else None
            upd = self.timecorr.add(obt, received_utc, temp)
            event["timecorr"] = {
                "obt": obt,
                "accepted": upd.accepted,
                "residual_ms": upd.residual_s * 1e3,
                "note": upd.reason,
                "clock": self.clock(),
            }
        elif tm.service == 1:
            v = decode_verification(tm)
            event["verification"] = {
                "request_seq": v.request_seq_count,
                "stage": ST01_NAMES.get(tm.subtype, "unknown"),
                "ok": v.ok,
                "failure": None if v.ok else ObswStatus.describe(v.failure_code),
            }
        elif (tm.service, tm.subtype) == (3, 25):
            hk = InstrumentHk.decode(tm)
            event["hk"] = {
                "obt": tm.time.to_seconds(),
                "mode": hk.mode_name,
                "temperature_c": hk.temperature_c,
                "current_ma": hk.current_ma,
                "voltage_v": hk.voltage_v,
                "science_count": hk.science_count,
                "mode_changes": hk.mode_changes,
                "tc_accepted": hk.tc_accepted,
                "tc_rejected": hk.tc_rejected,
                "pmon_status": hk.pmon_status_name,
                "pmon_enabled": hk.pmon_enabled,
                "heater_fault": hk.heater_fault,
                "warning_limit_c": hk.warning_limit_c,
                "danger_limit_c": hk.danger_limit_c,
            }
            l1 = to_l1(hk, tm.time.to_seconds(), self.timecorr)
            if l1 is not None:
                self.l1.append(l1)
                event["hk"]["utc"] = l1.utc
                event["hk"]["utc_sigma_ms"] = l1.utc_sigma_ms
            self.last_hk = event["hk"]
        elif tm.service == 5:
            ev = decode_event(tm)
            event["event"] = {"id": ev.event_id, "severity": ev.severity, "name": ev.name,
                              "temperature_c": ev.temperature_c, "previous_mode": ev.previous_mode}
        elif (tm.service, tm.subtype) == (12, 12):
            tr = decode_transition(tm)
            event["transition"] = {"value_c": tr.value_c, "limit_c": tr.limit_c,
                                   "from": tr.previous, "to": tr.current}
        self._publish(event)

    # ---- commands --------------------------------------------------------

    def command(self, req: TcRequest) -> Event:
        a, s, src, ack = self.apid, self.seq, GROUND_ID, req.ack
        if req.command == "ping":
            return self.send(commands.ping(a, s, src, ack), "Are you alive?")
        if req.command == "set_mode":
            if req.mode is None:
                raise HTTPException(422, "set_mode needs a mode")
            return self.send(commands.set_mode(a, s, commands.Mode[req.mode], src, ack),
                             f"Set mode {req.mode}")
        if req.command == "reset_science":
            return self.send(commands.reset_science(a, s, src, ack), "Reset science counter")
        if req.command == "hk_one_shot":
            return self.send(commands.hk_one_shot(a, s, src, ack), "Housekeeping report now")
        if req.command == "hk_enable":
            return self.send(commands.hk_enable(a, s, src, ack), "Periodic housekeeping on")
        if req.command == "hk_disable":
            return self.send(commands.hk_disable(a, s, src, ack), "Periodic housekeeping off")
        if req.command == "heater_fault":
            return self.send(commands.sim_heater_fault(a, s, src, ack), "Simulate heater stuck on")
        if req.command == "repair_heater":
            return self.send(commands.sim_clear_fault(a, s, src, ack), "Repair heater")
        if req.command == "monitoring_on":
            return self.send(commands.monitoring_enable(a, s, src, ack), "Temperature monitoring on")
        if req.command == "monitoring_off":
            return self.send(commands.monitoring_disable(a, s, src, ack), "Temperature monitoring off")
        if req.command == "set_limits":
            if req.warning_c is None or req.danger_c is None:
                raise HTTPException(422, "set_limits needs warning_c and danger_c")
            return self.send(commands.monitoring_limits(a, s, req.warning_c, req.danger_c, src, ack),
                             f"Limits {req.warning_c:g} / {req.danger_c:g} °C")
        if req.seconds is None:
            raise HTTPException(422, "hk_interval needs seconds")
        return self.send(commands.hk_interval(a, s, req.seconds, src, ack),
                         f"Housekeeping every {req.seconds} s")

    def inject(self, req: FaultRequest) -> Event:
        a, s, label = self.apid, self.seq, FAULT_LABELS[req.fault]
        if req.fault in ("bad_crc", "truncated"):
            tc = commands.ping(a, s, GROUND_ID)
            raw = bytearray(tc.encode())
            if req.fault == "bad_crc":
                raw[-1] ^= 0xFF
            else:
                del raw[-1]
            return self.send(tc, label, raw=bytes(raw), fault=req.fault)
        if req.fault == "unknown_service":
            return self.send(Telecommand(a, s, 99, 1, ACK_ALL, GROUND_ID), label, fault=req.fault)
        tc = Telecommand(a, s, 8, 1, ACK_ALL, GROUND_ID, bytes([0x00, 0x01, 9]))
        return self.send(tc, label, fault=req.fault)


def create_app(host: str = "127.0.0.1", port: int = 10025, apid: int = 100,
               serve_console: bool = True, clock_base_ppm: float = 250.0,
               clock_tempco: float = 10.0) -> FastAPI:
    station = GroundStation(host, port, apid, clock_base_ppm, clock_tempco)

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        await station.start()
        yield
        await station.stop()

    app = FastAPI(title="tinyobsw ground segment", lifespan=lifespan)
    app.state.station = station

    @app.get("/api/health")
    def health():
        return {"status": "ok"}

    @app.get("/api/status")
    def status():
        return station.status()

    @app.get("/api/events")
    def events(limit: int = 300) -> List[Event]:
        return list(station.events)[-max(1, min(limit, EVENT_HISTORY)):]

    @app.get("/api/l1")
    def level1(limit: int = 600) -> List[Dict[str, object]]:
        return [asdict(r) for r in list(station.l1)[-max(1, min(limit, L1_HISTORY)):]]

    @app.get("/api/l1.csv")
    def level1_csv() -> PlainTextResponse:
        return PlainTextResponse(
            to_csv(station.l1), media_type="text/csv",
            headers={"Content-Disposition": 'attachment; filename="tinyobsw_hk_level1.csv"'})

    @app.post("/api/tc")
    def send_tc(req: TcRequest) -> Event:
        return station.command(req)

    @app.post("/api/fault")
    def send_fault(req: FaultRequest) -> Event:
        return station.inject(req)

    @app.websocket("/ws")
    async def ws(socket: WebSocket):
        await socket.accept()
        queue: asyncio.Queue = asyncio.Queue(maxsize=500)
        station.subscribers.add(queue)
        try:
            await socket.send_json({"type": "snapshot", "status": station.status(),
                                    "events": list(station.events)[-300:]})
            while True:
                event = await queue.get()
                await socket.send_json({"type": "event", "event": event,
                                        "link": station.link_state()})
        except WebSocketDisconnect:
            pass
        finally:
            station.subscribers.discard(queue)

    # Serve the built console (npm run build) from the same server, if present.
    if serve_console and CONSOLE_DIST.is_dir():
        app.mount("/", StaticFiles(directory=CONSOLE_DIST, html=True), name="console")
    return app


app = create_app(
    host=os.environ.get("OBSW_HOST", "127.0.0.1"),
    port=int(os.environ.get("OBSW_PORT", "10025")),
    apid=int(os.environ.get("OBSW_APID", "100"), 0),
    clock_base_ppm=float(os.environ.get("OBSW_CLOCK_BASE_PPM", "250")),
    clock_tempco=float(os.environ.get("OBSW_CLOCK_TEMPCO", "10")),
)
