import { useEffect, useRef, useState } from 'react'
import type { Clock, GroundEvent, Hk, LinkState, Status } from './types'

const MAX_EVENTS = 600
const MAX_HK_POINTS = 180 // 3 minutes at 1 Hz
const MAX_CLOCK_POINTS = 300

/** One time report as plotted: estimate with its 2-sigma band, model truth, residual. */
export interface ClockPoint {
  obt: number
  drift: number | null
  band: [number, number] | null
  model: number | null
  residual_ms: number
  accepted: boolean
}
const SILENT_AFTER_MS = 3000

type ServerMessage =
  | { type: 'snapshot'; status: Status; events: GroundEvent[] }
  | { type: 'event'; event: GroundEvent; link: LinkState }

export interface GroundLink {
  events: GroundEvent[]
  hk: Hk[]
  clock: Clock | null
  clockPoints: ClockPoint[]
  link: LinkState
  apid: number | null
}

function clockPoint(e: GroundEvent): ClockPoint | null {
  if (e.kind !== 'tm' || !e.timecorr) return null
  const t = e.timecorr
  const c = t.clock
  return {
    obt: t.obt,
    drift: c ? c.drift_ppm : null,
    band: c ? [c.drift_ppm - 2 * c.drift_sigma_ppm, c.drift_ppm + 2 * c.drift_sigma_ppm] : null,
    model: c ? c.model_drift_ppm : null,
    residual_ms: t.residual_ms,
    accepted: t.accepted,
  }
}

function hkFrom(events: GroundEvent[]): Hk[] {
  const out: Hk[] = []
  for (const e of events) if (e.kind === 'tm' && e.hk) out.push(e.hk)
  return out.slice(-MAX_HK_POINTS)
}

/** Live connection to the ground server: every TC and TM as it happens. */
export function useGroundLink(): GroundLink {
  const [events, setEvents] = useState<GroundEvent[]>([])
  const [hk, setHk] = useState<Hk[]>([])
  const [link, setLink] = useState<LinkState>('connecting')
  const [apid, setApid] = useState<number | null>(null)
  const [clock, setClock] = useState<Clock | null>(null)
  const [clockPoints, setClockPoints] = useState<ClockPoint[]>([])
  const lastTmAt = useRef<number>(0)

  useEffect(() => {
    let socket: WebSocket | null = null
    let retry: ReturnType<typeof setTimeout> | undefined
    let backoff = 1000
    let closed = false

    const connect = () => {
      const scheme = location.protocol === 'https:' ? 'wss' : 'ws'
      socket = new WebSocket(`${scheme}://${location.host}/ws`)

      socket.onopen = () => {
        backoff = 1000
      }
      socket.onmessage = (msg) => {
        const data = JSON.parse(msg.data as string) as ServerMessage
        if (data.type === 'snapshot') {
          setEvents(data.events.slice(-MAX_EVENTS))
          setHk(hkFrom(data.events))
          setApid(data.status.apid)
          setLink(data.status.link)
          setClock(data.status.clock)
          setClockPoints(
            data.events.map(clockPoint).filter((p): p is ClockPoint => p !== null).slice(-MAX_CLOCK_POINTS),
          )
          if (data.status.link === 'live') lastTmAt.current = Date.now()
          return
        }
        const e = data.event
        setEvents((prev) => {
          const next = prev.length >= MAX_EVENTS ? prev.slice(1) : prev.slice()
          next.push(e)
          return next
        })
        if (e.kind === 'tm') {
          lastTmAt.current = Date.now()
          setLink('live')
          const point = e.hk
          if (point) setHk((prev) => [...prev.slice(-(MAX_HK_POINTS - 1)), point])
          const cp = clockPoint(e)
          if (cp) {
            setClockPoints((prev) => [...prev.slice(-(MAX_CLOCK_POINTS - 1)), cp])
            if (e.timecorr?.clock) setClock(e.timecorr.clock)
          }
        }
      }
      socket.onclose = () => {
        if (closed) return
        setLink('offline')
        retry = setTimeout(connect, backoff)
        backoff = Math.min(backoff * 2, 5000)
      }
    }

    connect()

    // Telemetry stopping is itself information: mark the link silent.
    const watchdog = setInterval(() => {
      setLink((current) =>
        current === 'live' && Date.now() - lastTmAt.current > SILENT_AFTER_MS ? 'silent' : current,
      )
    }, 500)

    return () => {
      closed = true
      clearTimeout(retry)
      clearInterval(watchdog)
      socket?.close()
    }
  }, [])

  return { events, hk, link, apid, clock, clockPoints }
}
