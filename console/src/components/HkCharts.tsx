import { CartesianGrid, Line, LineChart, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts'
import type { GroundEvent, Hk } from '../types'

interface Series {
  key: keyof Hk
  title: string
  unit: string
  digits: number
  tickDigits: number
  domain: [number | 'auto' | 'dataMin', number | 'auto' | 'dataMax']
}

// One chart per quantity: different units never share an axis.
// All four share the same margins so their time axes line up.
const SERIES: Series[] = [
  { key: 'temperature_c', title: 'Detector temperature', unit: '°C', digits: 2, tickDigits: 1, domain: ['auto', 'auto'] },
  { key: 'current_ma', title: 'Current', unit: 'mA', digits: 0, tickDigits: 0, domain: [0, 'auto'] },
  { key: 'voltage_v', title: 'Bus voltage', unit: 'V', digits: 3, tickDigits: 2, domain: [27.95, 28.05] },
  { key: 'science_count', title: 'Science count', unit: 'events', digits: 0, tickDigits: 0, domain: [0, 'auto'] },
]

const fmt = (v: number, digits: number) =>
  v.toLocaleString('en-GB', { minimumFractionDigits: digits, maximumFractionDigits: digits })

interface Marker {
  obt: number
  label: string
}

function Chart({ data, s, markers }: { data: Hk[]; s: Series; markers: Marker[] }) {
  const last = data.length ? data[data.length - 1] : null
  const latest = last ? (last[s.key] as number) : null
  // The temperature chart shows the monitoring limits, and always has room for them.
  const limits = s.key === 'temperature_c' && last ? last : null
  const domain: Series['domain'] | [(v: number) => number, (v: number) => number] = limits
    ? [(min: number) => Math.floor(Math.min(min, 18)), (max: number) => Math.ceil(Math.max(max, limits.danger_limit_c) + 6)]
    : s.domain
  const t0 = data.length ? data[0].obt : 0
  return (
    <figure className="chart">
      <figcaption>
        <span className="chart-title">{s.title}</span>
        <span className="chart-value">
          {latest === null ? '–' : fmt(latest, s.digits)} <small>{s.unit}</small>
        </span>
      </figcaption>
      <div className="chart-plot">
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={data} margin={{ top: 6, right: 76, bottom: 0, left: 0 }}>
            <CartesianGrid vertical={false} stroke="var(--grid)" />
            <XAxis
              dataKey="obt"
              type="number"
              domain={['dataMin', 'dataMax']}
              tickFormatter={(t: number) => `${Math.round(t)} s`}
              tick={{ fontSize: 11, fill: 'var(--ink-3)' }}
              tickLine={false}
              axisLine={{ stroke: 'var(--rule)' }}
              minTickGap={40}
            />
            <YAxis
              domain={domain}
              width={48}
              tick={{ fontSize: 11, fill: 'var(--ink-3)' }}
              tickLine={false}
              axisLine={false}
              tickFormatter={(v: number) => fmt(v, s.tickDigits)}
            />
            <Tooltip
              cursor={{ stroke: 'var(--ink-3)', strokeWidth: 1 }}
              isAnimationActive={false}
              labelFormatter={(t) => `On-board time ${fmt(Number(t), 1)} s`}
              formatter={(v) => [`${fmt(Number(v), s.digits)} ${s.unit}`, s.title]}
              contentStyle={{
                border: '1px solid var(--rule)',
                borderRadius: 4,
                fontFamily: 'var(--font-ui)',
                fontSize: 13,
              }}
            />
            {limits && (
              <ReferenceLine
                y={limits.warning_limit_c}
                stroke="var(--wait)"
                strokeDasharray="4 3"
                label={{ value: `Warning ${limits.warning_limit_c}°`, position: 'right', fontSize: 11, fill: 'var(--ink-2)' }}
              />
            )}
            {limits && (
              <ReferenceLine
                y={limits.danger_limit_c}
                stroke="var(--bad)"
                strokeDasharray="4 3"
                label={{ value: `Danger ${limits.danger_limit_c}°`, position: 'right', fontSize: 11, fill: 'var(--ink-2)' }}
              />
            )}
            {markers
              .filter((m) => m.obt >= t0)
              .map((m) => (
                <ReferenceLine
                  key={m.obt}
                  x={m.obt}
                  stroke="var(--ink-2)"
                  label={{
                    value: m.label,
                    // Below the curve on the temperature chart, where the limit labels sit at the top.
                    position: limits ? 'insideBottomRight' : 'insideTopRight',
                    fontSize: 11,
                    fill: 'var(--ink)',
                  }}
                />
              ))}
            <Line
              type="linear"
              dataKey={s.key}
              stroke="var(--series)"
              strokeWidth={2}
              dot={false}
              activeDot={{ r: 4, stroke: 'var(--panel)', strokeWidth: 2 }}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </figure>
  )
}

export function HkCharts({ data, events }: { data: Hk[]; events: GroundEvent[] }) {
  // Mark the moments the instrument put itself in SAFE.
  const markers: Marker[] = events.flatMap((e) =>
    e.kind === 'tm' && e.event?.id === 4 && e.obt !== undefined ? [{ obt: e.obt, label: 'Safe mode' }] : [],
  )
  return (
    <section className="panel" aria-labelledby="hk-title">
      <h2 id="hk-title">Housekeeping</h2>
      {data.length === 0 ? (
        <p className="empty">Waiting for the first housekeeping report. The instrument sends one every second.</p>
      ) : (
        <div className="chart-grid">
          {SERIES.map((s) => (
            <Chart key={s.key} data={data} s={s} markers={markers} />
          ))}
        </div>
      )}
    </section>
  )
}
