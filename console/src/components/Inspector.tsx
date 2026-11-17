import { useState } from 'react'
import type { FieldGroup, GroundEvent } from '../types'

const GROUP_NAME: Record<FieldGroup, string> = {
  primary: 'CCSDS primary header',
  secondary: 'PUS secondary header',
  data: 'Application data',
  crc: 'CRC',
}

function bytesOf(hex: string): string[] {
  const out: string[] = []
  for (let i = 0; i < hex.length; i += 2) out.push(hex.slice(i, i + 2).toUpperCase())
  return out
}

/** One packet drawn as a protocol diagram: bytes grouped under labelled fields. */
export function Inspector({ event }: { event: GroundEvent | null }) {
  const [hovered, setHovered] = useState<number | null>(null)

  if (!event) {
    return (
      <section className="panel inspector" aria-labelledby="insp-title">
        <h2 id="insp-title">Packet inspector</h2>
        <p className="empty">Select a packet in the log or a command in the list to see its bytes.</p>
      </section>
    )
  }

  const bytes = bytesOf(event.hex)
  const groups = Array.from(new Set(event.fields.map((f) => f.group)))
  const direction = event.kind === 'tc' ? 'Telecommand, ground to instrument' : 'Telemetry, instrument to ground'

  return (
    <section className="panel inspector" aria-labelledby="insp-title">
      <div className="panel-head">
        <h2 id="insp-title">
          {event.type} <span className="insp-sub">{direction}, {bytes.length} bytes</span>
        </h2>
        <ul className="legend" aria-label="Field colours">
          {groups.map((g) => (
            <li key={g}>
              <span className={`swatch g-${g}`} aria-hidden="true" />
              {GROUP_NAME[g]}
            </li>
          ))}
        </ul>
      </div>

      <div className="fields">
        {event.fields.map((f, i) => (
          <div
            key={`${f.start}-${f.name}`}
            className={`field g-${f.group}${hovered === i ? ' is-hovered' : ''}`}
            onMouseEnter={() => setHovered(i)}
            onMouseLeave={() => setHovered(null)}
          >
            <div className="field-bytes">
              {bytes.slice(f.start, f.end).map((b, j) => (
                <span key={j}>{b}</span>
              ))}
            </div>
            <div className="field-bar" aria-hidden="true" />
            <div className="field-name">{f.name}</div>
            <div className="field-value">{f.value}</div>
          </div>
        ))}
      </div>

      <details className="raw">
        <summary>Raw bytes</summary>
        <code>{bytes.join(' ')}</code>
      </details>
    </section>
  )
}
