import { useState } from 'react'
import type { Fault, TcCommand } from '../api'
import { ACK, type Mode } from '../types'
import { ModeDiagram } from './ModeDiagram'

interface Monitoring {
  heaterFault: boolean
  enabled: boolean
}

interface Props {
  mode: Mode | null
  monitoring: Monitoring | null
  onCommand: (cmd: TcCommand, ack: number) => void
  onFault: (fault: Fault) => void
  error: string | null
}

const ACK_OPTIONS: { bit: number; label: string }[] = [
  { bit: ACK.accept, label: 'Accepted' },
  { bit: ACK.start, label: 'Started' },
  { bit: ACK.complete, label: 'Completed' },
]

const FAULTS: { fault: Fault; label: string; hint: string }[] = [
  { fault: 'bad_crc', label: 'Corrupt the CRC', hint: 'Ping with its last byte flipped' },
  { fault: 'truncated', label: 'Cut the last byte', hint: 'Length field no longer matches' },
  { fault: 'unknown_service', label: 'Unknown service', hint: 'TC[99,1]' },
  { fault: 'bad_mode_value', label: 'Mode that does not exist', hint: 'Set mode 9' },
]

export function CommandPanel({ mode, monitoring, onCommand, onFault, error }: Props) {
  const [ack, setAck] = useState(ACK.accept | ACK.start | ACK.complete)
  const [period, setPeriod] = useState(1)
  const [warning, setWarning] = useState(40)
  const [danger, setDanger] = useState(45)

  const send = (cmd: TcCommand) => onCommand(cmd, ack)
  const toggle = (bit: number) => setAck((a) => a ^ bit)

  return (
    <div className="stack">
      <section className="panel" aria-labelledby="mode-title">
        <h2 id="mode-title">Instrument mode</h2>
        <p className="hint">Click a mode to command it. Outlined modes can be reached from here.</p>
        <ModeDiagram current={mode} onCommand={(m) => send({ command: 'set_mode', mode: m })} />
      </section>

      <section className="panel" aria-labelledby="cmd-title">
        <h2 id="cmd-title">Commands</h2>
        <fieldset className="acks">
          <legend>Report back when</legend>
          {ACK_OPTIONS.map(({ bit, label }) => (
            <label key={bit} className="check">
              <input type="checkbox" checked={(ack & bit) !== 0} onChange={() => toggle(bit)} />
              {label}
            </label>
          ))}
        </fieldset>
        <div className="button-row">
          <button onClick={() => send({ command: 'ping' })}>Are you alive?</button>
          <button onClick={() => send({ command: 'hk_one_shot' })}>Report now</button>
          <button onClick={() => send({ command: 'reset_science' })}>Reset science</button>
        </div>

        <h3>Housekeeping</h3>
        <div className="button-row">
          <button onClick={() => send({ command: 'hk_enable' })}>Periodic on</button>
          <button onClick={() => send({ command: 'hk_disable' })}>Periodic off</button>
        </div>
        <form
          className="inline-form"
          onSubmit={(e) => {
            e.preventDefault()
            send({ command: 'hk_interval', seconds: period })
          }}
        >
          <label htmlFor="period">Every</label>
          <input
            id="period"
            type="number"
            min={1}
            max={3600}
            value={period}
            onChange={(e) => setPeriod(Math.max(1, Math.min(3600, Number(e.target.value) || 1)))}
          />
          <span>s</span>
          <button type="submit">Set period</button>
        </form>
        {error && (
          <p className="error" role="alert">
            {error}
          </p>
        )}
      </section>

      <section className="panel" aria-labelledby="sim-title">
        <h2 id="sim-title">Instrument faults</h2>
        <p className="hint">
          A stuck heater overheats the instrument while it is powered. Its own monitoring should switch it to Safe,
          which cuts the heater.
        </p>
        <div className="button-row">
          <button className="danger-btn" onClick={() => send({ command: 'heater_fault' })}>
            Simulate heater stuck on
          </button>
          <button onClick={() => send({ command: 'repair_heater' })}>Repair heater</button>
        </div>
        <p className="state-line">
          Heater:{' '}
          <strong>{monitoring === null ? 'unknown' : monitoring.heaterFault ? 'stuck on (fault)' : 'working'}</strong>
        </p>

        <h3>Temperature monitoring</h3>
        <div className="button-row">
          <button onClick={() => send({ command: 'monitoring_on' })}>Monitoring on</button>
          <button onClick={() => send({ command: 'monitoring_off' })}>Monitoring off</button>
        </div>
        <p className="state-line">
          Monitoring: <strong>{monitoring === null ? 'unknown' : monitoring.enabled ? 'on' : 'off'}</strong>
        </p>
        <form
          className="limits-form"
          onSubmit={(e) => {
            e.preventDefault()
            send({ command: 'set_limits', warning_c: warning, danger_c: danger })
          }}
        >
          <label>
            Warning
            <input type="number" step="0.5" value={warning} onChange={(e) => setWarning(Number(e.target.value))} />
          </label>
          <label>
            Danger
            <input type="number" step="0.5" value={danger} onChange={(e) => setDanger(Number(e.target.value))} />
          </label>
          <span>°C</span>
          <button type="submit">Set limits</button>
        </form>
      </section>

      <section className="panel" aria-labelledby="fault-title">
        <h2 id="fault-title">Send a bad command</h2>
        <p className="hint">Each should be refused with a reason. Watch the command list.</p>
        <div className="fault-list">
          {FAULTS.map(({ fault, label, hint }) => (
            <button key={fault} className="fault" onClick={() => onFault(fault)}>
              <span>{label}</span>
              <small>{hint}</small>
            </button>
          ))}
        </div>
      </section>
    </div>
  )
}
