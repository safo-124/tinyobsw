import { useMemo, useState } from 'react'
import { sendFault, sendTc, type Fault, type TcCommand } from './api'
import { CommandLadder } from './components/CommandLadder'
import { CommandPanel } from './components/CommandPanel'
import { EventsPanel } from './components/EventsPanel'
import { HkCharts } from './components/HkCharts'
import { Inspector } from './components/Inspector'
import { PacketLog } from './components/PacketLog'
import type { LinkState, Mode } from './types'
import { useGroundLink } from './useGroundLink'

const LINK_TEXT: Record<LinkState, string> = {
  connecting: 'Connecting',
  offline: 'Ground server offline',
  never: 'Waiting for instrument',
  live: 'Telemetry live',
  silent: 'Telemetry stopped',
}

const CHECK: Record<string, { icon: string; text: string; cls: string }> = {
  'within limits': { icon: '✓', text: 'Within limits', cls: 'ok' },
  'above warning': { icon: '!', text: 'Above warning', cls: 'warn' },
  'above danger': { icon: '✕', text: 'Above danger', cls: 'bad' },
}

const LINK_HELP: Partial<Record<LinkState, string>> = {
  offline: 'The console cannot reach the ground server. Start it with: uvicorn tinyground.api:app',
  never: 'The ground server is running, but the instrument is not answering. Start it with: ./build/obsw_sim',
  silent: 'No telemetry for 3 seconds. The simulator may have stopped, or housekeeping is switched off.',
}

export default function App() {
  const { events, hk, link, apid } = useGroundLink()
  const [selectedId, setSelectedId] = useState<number | null>(null)
  const [error, setError] = useState<string | null>(null)

  const latest = hk.length ? hk[hk.length - 1] : null
  const mode = (latest?.mode ?? null) as Mode | null
  // Until something is picked, show the most recent command the operator sent.
  const selected = useMemo(() => {
    if (selectedId !== null) return events.find((e) => e.id === selectedId) ?? null
    for (let i = events.length - 1; i >= 0; i--) {
      const e = events[i]
      if (e.kind === 'tc' && !e.auto) return e
    }
    return null
  }, [events, selectedId])

  const run = (p: Promise<{ id: number }>) =>
    p.then(
      (tc) => {
        setError(null)
        setSelectedId(tc.id)
      },
      (e: Error) => setError(e.message),
    )

  const onCommand = (cmd: TcCommand, ack: number) => run(sendTc(cmd, ack))
  const onFault = (fault: Fault) => run(sendFault(fault))

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <span className="wordmark">tinyOBSW</span>
          <span className="brand-sub">Instrument console{apid !== null && `, APID ${apid}`}</span>
        </div>
        <dl className="readouts">
          <div>
            <dt>On-board time</dt>
            <dd>{latest ? `${latest.obt.toFixed(1)} s` : '–'}</dd>
          </div>
          <div>
            <dt>Mode</dt>
            <dd>{mode ? mode.charAt(0) + mode.slice(1).toLowerCase() : '–'}</dd>
          </div>
          <div>
            <dt>Temperature</dt>
            <dd>
              {latest ? (
                <span className={`check check-${latest.pmon_enabled ? CHECK[latest.pmon_status].cls : 'off'}`}>
                  {latest.temperature_c.toFixed(1)} °C{' '}
                  <span aria-hidden="true">{latest.pmon_enabled ? CHECK[latest.pmon_status].icon : '–'}</span>{' '}
                  <small>{latest.pmon_enabled ? CHECK[latest.pmon_status].text : 'Not monitored'}</small>
                </span>
              ) : (
                '–'
              )}
            </dd>
          </div>
          <div>
            <dt>Accepted / refused</dt>
            <dd>{latest ? `${latest.tc_accepted} / ${latest.tc_rejected}` : '–'}</dd>
          </div>
        </dl>
        <p className={`link link-${link}`} role="status">
          <span className="link-dot" aria-hidden="true" />
          {LINK_TEXT[link]}
        </p>
      </header>

      {LINK_HELP[link] && <p className="banner">{LINK_HELP[link]}</p>}

      <main className="layout">
        <div className="col-left">
          <CommandPanel
            mode={mode}
            monitoring={latest ? { heaterFault: latest.heater_fault, enabled: latest.pmon_enabled } : null}
            onCommand={onCommand}
            onFault={onFault}
            error={error}
          />
        </div>
        <div className="col-center">
          <HkCharts data={hk} events={events} />
          <EventsPanel events={events} />
          <Inspector event={selected} />
        </div>
        <div className="col-right">
          <CommandLadder events={events} selectedId={selected?.id ?? null} onSelect={setSelectedId} />
          <PacketLog events={events} selectedId={selected?.id ?? null} onSelect={setSelectedId} />
        </div>
      </main>
    </div>
  )
}
