import type { GroundEvent, Severity, TmEvent } from '../types'

const ICON: Record<Severity, string> = { high: '✕', medium: '!', low: '!', info: 'i' }
const WORD: Record<Severity, string> = { high: 'High', medium: 'Medium', low: 'Low', info: 'Info' }

function detail(e: TmEvent): string {
  const ev = e.event
  if (!ev) return ''
  if (ev.temperature_c !== null) return `${ev.temperature_c.toFixed(2)} °C`
  if (ev.previous_mode) return `was ${ev.previous_mode.charAt(0)}${ev.previous_mode.slice(1).toLowerCase()}`
  return ''
}

/** What the instrument decided on its own: ST[5] events, newest first. */
export function EventsPanel({ events }: { events: GroundEvent[] }) {
  const list = events
    .filter((e): e is TmEvent => e.kind === 'tm' && e.event !== undefined)
    .slice(-8)
    .reverse()

  return (
    <section className="panel" aria-labelledby="events-title">
      <h2 id="events-title">On-board events</h2>
      {list.length === 0 ? (
        <p className="empty">
          No events yet. Simulate a heater fault and the instrument's own monitoring reports what it sees and does.
        </p>
      ) : (
        <ol className="event-list">
          {list.map((e) => {
            const ev = e.event!
            return (
              <li key={e.id} className="event-row">
                <span className={`sev sev-${ev.severity}`}>
                  <span aria-hidden="true">{ICON[ev.severity]}</span> {WORD[ev.severity]}
                </span>
                <span className="event-name">{ev.name}</span>
                <span className="event-detail">{detail(e)}</span>
                <span className="event-time">{e.obt !== undefined ? `${e.obt.toFixed(1)} s` : ''}</span>
              </li>
            )
          })}
        </ol>
      )}
    </section>
  )
}
