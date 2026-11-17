// Shapes of the JSON the ground server (ground/tinyground/api.py) sends.

export type FieldGroup = 'primary' | 'secondary' | 'data' | 'crc'

export interface Field {
  name: string
  group: FieldGroup
  start: number
  end: number
  value: string
}

export interface Verification {
  request_seq: number
  stage: string
  ok: boolean
  failure: string | null
}

export interface Hk {
  obt: number
  mode: string
  temperature_c: number
  current_ma: number
  voltage_v: number
  science_count: number
  mode_changes: number
  tc_accepted: number
  tc_rejected: number
  pmon_status: 'within limits' | 'above warning' | 'above danger'
  pmon_enabled: boolean
  heater_fault: boolean
  warning_limit_c: number
  danger_limit_c: number
}

export type Severity = 'info' | 'low' | 'medium' | 'high'

export interface EventInfo {
  id: number
  severity: Severity
  name: string
  temperature_c: number | null
  previous_mode: string | null
}

export interface Transition {
  value_c: number
  limit_c: number
  from: string
  to: string
}

export interface TcEvent {
  kind: 'tc'
  id: number
  time: number
  seq: number
  service: number
  subtype: number
  type: string
  ack: number
  label: string
  fault: string | null
  auto: boolean
  hex: string
  fields: Field[]
}

export interface TmEvent {
  kind: 'tm'
  id: number
  time: number
  hex: string
  fields: Field[]
  valid: boolean
  type: string
  summary: string
  service?: number
  subtype?: number
  seq?: number
  dest?: number
  obt?: number
  verification?: Verification
  hk?: Hk
  event?: EventInfo
  transition?: Transition
}

export type GroundEvent = TcEvent | TmEvent

export type LinkState = 'connecting' | 'offline' | 'never' | 'live' | 'silent'

export interface Status {
  link: LinkState
  sim: string
  apid: number
  tc_sent: number
  tm_received: number
  last_hk: Hk | null
}

export type Mode = 'OFF' | 'STANDBY' | 'MEASURE' | 'SAFE'

export const ACK = { accept: 1, start: 2, progress: 4, complete: 8 } as const
