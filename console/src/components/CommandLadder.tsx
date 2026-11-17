import type { GroundEvent, TcEvent, Verification } from '../types'
import { ACK } from '../types'

type StageState = 'ok' | 'failed' | 'waiting' | 'not-requested' | 'skipped'

interface Stage {
  name: string
  state: StageState
  detail?: string
}

interface Row {
  tc: TcEvent
  stages: Stage[]
  failure: string | null
}

/** Verification reports that answer this TC: same sequence count, after it, before the next TC with that count. */
function reportsFor(tc: TcEvent, events: GroundEvent[]): Verification[] {
  const out: Verification[] = []
  for (const e of events) {
    if (e.id <= tc.id) continue
    if (e.kind === 'tc' && e.seq === tc.seq) break
    if (e.kind === 'tm' && e.verification && e.verification.request_seq === tc.seq) out.push(e.verification)
  }
  return out
}

function stage(name: string, requested: boolean, okName: string, failName: string,
               reports: Verification[], blocked: boolean): Stage {
  const ok = reports.find((r) => r.stage === okName)
  const fail = reports.find((r) => r.stage === failName)
  if (fail) return { name, state: 'failed', detail: fail.failure ?? undefined }
  if (ok) return { name, state: 'ok' }
  if (blocked) return { name, state: 'skipped' }
  return { name, state: requested ? 'waiting' : 'not-requested' }
}

function buildRows(events: GroundEvent[]): Row[] {
  const rows: Row[] = []
  for (const e of events) {
    if (e.kind !== 'tc' || e.auto) continue
    const reports = reportsFor(e, events)
    const accept = stage('Accepted', (e.ack & ACK.accept) !== 0, 'acceptance OK', 'acceptance FAILED', reports, false)
    const rejected = accept.state === 'failed'
    const start = stage('Started', (e.ack & ACK.start) !== 0, 'start OK', 'start FAILED', reports, rejected)
    const done = stage('Completed', (e.ack & ACK.complete) !== 0, 'completion OK', 'completion FAILED', reports,
                       rejected || start.state === 'failed')
    const failed = [accept, start, done].find((s) => s.state === 'failed')
    const verb = failed?.name === 'Accepted' ? 'Refused' : 'Failed'
    rows.push({ tc: e, stages: [accept, start, done], failure: failed ? `${verb}: ${failed.detail}` : null })
  }
  return rows.reverse()
}

const ICON: Record<StageState, string> = {
  ok: '✓',
  failed: '✕',
  waiting: '…',
  'not-requested': '–',
  skipped: '–',
}

const STATE_TEXT: Record<StageState, string> = {
  ok: 'done',
  failed: 'failed',
  waiting: 'waiting',
  'not-requested': 'not requested',
  skipped: 'skipped',
}

interface Props {
  events: GroundEvent[]
  selectedId: number | null
  onSelect: (id: number) => void
}

export function CommandLadder({ events, selectedId, onSelect }: Props) {
  const rows = buildRows(events).slice(0, 40)
  return (
    <section className="panel ladder" aria-labelledby="ladder-title">
      <h2 id="ladder-title">Commands sent</h2>
      {rows.length === 0 ? (
        <p className="empty">Send a command and its verification reports appear here, stage by stage.</p>
      ) : (
        <ol className="ladder-list">
          {rows.map(({ tc, stages, failure }) => (
            <li key={tc.id}>
              <button
                className={`ladder-row${tc.id === selectedId ? ' is-selected' : ''}`}
                onClick={() => onSelect(tc.id)}
              >
                <span className="ladder-head">
                  <span className="ladder-label">{tc.label}</span>
                  <span className="ladder-meta">
                    {tc.type} seq {tc.seq}
                  </span>
                </span>
                <span className="stages">
                  {stages.map((s) => (
                    <span key={s.name} className={`stage stage-${s.state}`} title={`${s.name}: ${STATE_TEXT[s.state]}`}>
                      <span className="stage-icon" aria-hidden="true">
                        {ICON[s.state]}
                      </span>
                      {s.name}
                    </span>
                  ))}
                </span>
                {failure && <span className="ladder-failure">{failure}</span>}
              </button>
            </li>
          ))}
        </ol>
      )}
    </section>
  )
}
