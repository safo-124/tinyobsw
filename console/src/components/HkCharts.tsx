import { CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts'
import type { Hk } from '../types'

interface Series {
  key: keyof Hk
  title: string
  unit: string
  digits: number
  tickDigits: number
  domain: [number | 'auto' | 'dataMin', number | 'auto' | 'dataMax']
}

// One chart per quantity: different units never share an axis.
const SERIES: Series[] = [
  { key: 'temperature_c', title: 'Detector temperature', unit: '°C', digits: 2, tickDigits: 1, domain: ['auto', 'auto'] },
  { key: 'current_ma', title: 'Current', unit: 'mA', digits: 0, tickDigits: 0, domain: [0, 'auto'] },
  { key: 'voltage_v', title: 'Bus voltage', unit: 'V', digits: 3, tickDigits: 2, domain: [27.95, 28.05] },
  { key: 'science_count', title: 'Science count', unit: 'events', digits: 0, tickDigits: 0, domain: [0, 'auto'] },
]

const fmt = (v: number, digits: number) =>
  v.toLocaleString('en-GB', { minimumFractionDigits: digits, maximumFractionDigits: digits })

function Chart({ data, s }: { data: Hk[]; s: Series }) {
  const latest = data.length ? (data[data.length - 1][s.key] as number) : null
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
          <LineChart data={data} margin={{ top: 6, right: 8, bottom: 0, left: 0 }}>
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
              domain={s.domain}
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

export function HkCharts({ data }: { data: Hk[] }) {
  return (
    <section className="panel" aria-labelledby="hk-title">
      <h2 id="hk-title">Housekeeping</h2>
      {data.length === 0 ? (
        <p className="empty">Waiting for the first housekeeping report. The instrument sends one every second.</p>
      ) : (
        <div className="chart-grid">
          {SERIES.map((s) => (
            <Chart key={s.key} data={data} s={s} />
          ))}
        </div>
      )}
    </section>
  )
}
