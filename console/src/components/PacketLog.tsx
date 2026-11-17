import { useState } from 'react'
import type { GroundEvent } from '../types'

interface Props {
  events: GroundEvent[]
  selectedId: number | null
  onSelect: (id: number) => void
}

const isPeriodicHk = (e: GroundEvent) => e.kind === 'tm' && e.service === 3 && e.subtype === 25 && e.dest === 0

function when(e: GroundEvent): string {
  if (e.kind === 'tm' && e.obt !== undefined) return `${e.obt.toFixed(1)} s`
  return new Date(e.time * 1000).toLocaleTimeString('en-GB', { hour12: false })
}

function text(e: GroundEvent): string {
  if (e.kind === 'tc') return e.label
  return e.summary
}

function failed(e: GroundEvent): boolean {
  return e.kind === 'tm' && (!e.valid || e.verification?.ok === false || e.event?.severity === 'high')
}

function warning(e: GroundEvent): boolean {
  return e.kind === 'tm' && (e.event?.severity === 'medium' || e.event?.severity === 'low')
}

export function PacketLog({ events, selectedId, onSelect }: Props) {
  const [showHk, setShowHk] = useState(false)
  const [showAuto, setShowAuto] = useState(false)

  const visible = events
    .filter((e) => (showHk || !isPeriodicHk(e)) && (showAuto || e.kind !== 'tc' || !e.auto))
    .slice(-200)
    .reverse()

  return (
    <section className="panel log" aria-labelledby="log-title">
      <div className="panel-head">
        <h2 id="log-title">Packets</h2>
        <div className="log-filters">
          <label className="check">
            <input type="checkbox" checked={showHk} onChange={(e) => setShowHk(e.target.checked)} />
            Periodic housekeeping
          </label>
          <label className="check">
            <input type="checkbox" checked={showAuto} onChange={(e) => setShowAuto(e.target.checked)} />
            Link checks
          </label>
        </div>
      </div>
      {visible.length === 0 ? (
        <p className="empty">No packets yet. Commands you send and everything the instrument answers is listed here.</p>
      ) : (
        <ol className="log-list">
          {visible.map((e) => (
            <li key={e.id}>
              <button
                className={`log-row log-${e.kind}${failed(e) ? ' is-failed' : ''}${warning(e) ? ' is-warning' : ''}${
                  e.id === selectedId ? ' is-selected' : ''
                }`}
                onClick={() => onSelect(e.id)}
              >
                <span className="log-dir" aria-label={e.kind === 'tc' ? 'uplink' : 'downlink'}>
                  {e.kind === 'tc' ? '↑' : '↓'}
                </span>
                <span className="log-type">{e.type}</span>
                <span className="log-text">
                  {failed(e) && <span aria-hidden="true">✕ </span>}
                  {warning(e) && <span aria-hidden="true">! </span>}
                  {text(e)}
                </span>
                <span className="log-time">{when(e)}</span>
              </button>
            </li>
          ))}
        </ol>
      )}
    </section>
  )
}
