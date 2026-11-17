import type { Mode, TcEvent } from './types'

export type TcCommand =
  | { command: 'ping' }
  | { command: 'set_mode'; mode: Mode }
  | { command: 'reset_science' }
  | { command: 'hk_one_shot' }
  | { command: 'hk_enable' }
  | { command: 'hk_disable' }
  | { command: 'hk_interval'; seconds: number }
  | { command: 'heater_fault' }
  | { command: 'repair_heater' }
  | { command: 'monitoring_on' }
  | { command: 'monitoring_off' }
  | { command: 'set_limits'; warning_c: number; danger_c: number }

export type Fault = 'bad_crc' | 'truncated' | 'unknown_service' | 'bad_mode_value'

async function post(path: string, body: object): Promise<TcEvent> {
  const res = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  if (!res.ok) {
    throw new Error(`The ground server refused the command (HTTP ${res.status}).`)
  }
  return res.json() as Promise<TcEvent>
}

export const sendTc = (cmd: TcCommand, ack: number) => post('/api/tc', { ...cmd, ack })
export const sendFault = (fault: Fault) => post('/api/fault', { fault })
