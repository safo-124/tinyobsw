import type { KeyboardEvent } from 'react'
import type { Mode } from '../types'

// Same transition table as obsw/src/instrument.c.
const ALLOWED: Record<Mode, Mode[]> = {
  OFF: ['STANDBY', 'SAFE'],
  STANDBY: ['OFF', 'MEASURE', 'SAFE'],
  MEASURE: ['STANDBY', 'SAFE'],
  SAFE: ['STANDBY'],
}

const NODES: Record<Mode, { x: number; y: number }> = {
  OFF: { x: 44, y: 34 },
  STANDBY: { x: 150, y: 34 },
  MEASURE: { x: 256, y: 34 },
  SAFE: { x: 150, y: 118 },
}

const W = 76
const H = 30

const LABEL: Record<Mode, string> = {
  OFF: 'Off',
  STANDBY: 'Standby',
  MEASURE: 'Measure',
  SAFE: 'Safe',
}

interface Props {
  current: Mode | null
  onCommand: (mode: Mode) => void
}

/** The instrument's mode machine. Click a mode to command it. */
export function ModeDiagram({ current, onCommand }: Props) {
  const reachable = current ? ALLOWED[current] : []

  const onKey = (mode: Mode) => (e: KeyboardEvent) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault()
      onCommand(mode)
    }
  }

  return (
    <svg className="mode-diagram" viewBox="0 0 300 150" role="group" aria-label="Instrument modes">
      <defs>
        <marker id="arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto">
          <path d="M0,0 L8,4 L0,8 z" fill="var(--ink-3)" />
        </marker>
      </defs>

      {/* Two-way links on the top row, one-way links into SAFE, one back out to STANDBY. */}
      <g className="mode-edges" markerEnd="url(#arrow)">
        <line x1="84" y1="29" x2="110" y2="29" />
        <line x1="110" y1="39" x2="84" y2="39" />
        <line x1="190" y1="29" x2="216" y2="29" />
        <line x1="216" y1="39" x2="190" y2="39" />
        <path d="M44,51 Q44,118 110,118" fill="none" />
        <path d="M256,51 Q256,118 190,118" fill="none" />
        <line x1="144" y1="102" x2="144" y2="51" />
        <line x1="156" y1="51" x2="156" y2="102" />
      </g>

      {(Object.keys(NODES) as Mode[]).map((mode) => {
        const { x, y } = NODES[mode]
        const isCurrent = mode === current
        const isReachable = reachable.includes(mode)
        const cls = ['mode-node', isCurrent && 'is-current', isReachable && 'is-reachable']
          .filter(Boolean)
          .join(' ')
        return (
          <g
            key={mode}
            className={cls}
            role="button"
            tabIndex={0}
            aria-pressed={isCurrent}
            aria-label={`Command ${LABEL[mode]} mode${isCurrent ? ' (current)' : ''}`}
            onClick={() => onCommand(mode)}
            onKeyDown={onKey(mode)}
          >
            <rect x={x - W / 2} y={y - H / 2} width={W} height={H} rx={4} />
            <text x={x} y={y + 5} textAnchor="middle">
              {LABEL[mode]}
            </text>
          </g>
        )
      })}
    </svg>
  )
}
