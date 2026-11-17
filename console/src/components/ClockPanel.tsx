import {
  Area,
  CartesianGrid,
  ComposedChart,
  Legend,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts'
import type { ClockPoint } from '../useGroundLink'
import type { Clock, Hk } from '../types'

const axisTick = { fontSize: 11, fill: 'var(--ink-3)' }
const tooltipStyle = {
  border: '1px solid var(--rule)',
  borderRadius: 4,
  fontFamily: 'var(--font-ui)',
  fontSize: 13,
}

function utcTime(iso: string): string {
  // "2026-11-17T14:46:15.123Z" -> "2026-11-17 14:46:15.123 UTC"
  return `${iso.replace('T', ' ').replace('Z', '')} UTC`
}

interface Props {
  clock: Clock | null
  points: ClockPoint[]
  latestHk: Hk | null
}

/** How the ground maps on-board time to UTC, and how sure it is. */
export function ClockPanel({ clock, points, latestHk }: Props) {
  const plotted = points.filter((p) => p.drift !== null)
  return (
    <section className="panel" aria-labelledby="clock-title">
      <div className="panel-head">
        <h2 id="clock-title">Clock correlation</h2>
        <a className="download" href="/api/l1.csv" download>
          Download Level 1 housekeeping (CSV)
        </a>
      </div>
      <p className="hint">
        The on-board clock runs fast, more so when the instrument is warm. A Kalman filter on the ground learns
        how its rate depends on temperature, predicts the drift from housekeeping, and stamps every housekeeping
        packet with UTC.
      </p>

      {clock === null ? (
        <p className="empty">Waiting for time reports. The instrument sends one every second.</p>
      ) : (
        <>
          <dl className="clock-readouts">
            <div>
              <dt>Drift estimate</dt>
              <dd>
                {clock.drift_ppm.toFixed(1)} <small>± {(2 * clock.drift_sigma_ppm).toFixed(1)} ppm (2σ)</small>
              </dd>
            </div>
            <div>
              <dt>Simulator model</dt>
              <dd>
                {clock.model_drift_ppm === null ? '–' : clock.model_drift_ppm.toFixed(1)} <small>ppm</small>
              </dd>
            </div>
            <div>
              <dt>Learned temperature coefficient</dt>
              <dd>
                {clock.tempco_ppm_per_c.toFixed(2)}{' '}
                <small>± {(2 * clock.tempco_sigma).toFixed(2)} ppm/°C</small>
              </dd>
            </div>
            <div>
              <dt>On-board clock read zero at</dt>
              <dd className="clock-utc">{utcTime(clock.obt_zero_utc)}</dd>
            </div>
            <div>
              <dt>Latest UTC stamp accuracy</dt>
              <dd>
                {latestHk?.utc_sigma_ms !== undefined ? `± ${latestHk.utc_sigma_ms.toFixed(2)}` : '–'} <small>ms (1σ)</small>
              </dd>
            </div>
            <div>
              <dt>Reports used / rejected</dt>
              <dd>
                {clock.samples} / {clock.rejected}
              </dd>
            </div>
          </dl>

          <div className="clock-charts">
            <figure className="chart">
              <figcaption>
                <span className="chart-title">Clock rate error, estimate vs simulator model</span>
              </figcaption>
              <div className="chart-plot chart-plot-tall">
                <ResponsiveContainer width="100%" height="100%">
                  <ComposedChart data={plotted} margin={{ top: 6, right: 16, bottom: 0, left: 0 }}>
                    <CartesianGrid vertical={false} stroke="var(--grid)" />
                    <XAxis
                      dataKey="obt"
                      type="number"
                      domain={['dataMin', 'dataMax']}
                      tickFormatter={(t: number) => `${Math.round(t)} s`}
                      tick={axisTick}
                      tickLine={false}
                      axisLine={{ stroke: 'var(--rule)' }}
                      minTickGap={40}
                    />
                    <YAxis width={48} tick={axisTick} tickLine={false} axisLine={false} unit="" domain={['auto', 'auto']} />
                    <Tooltip
                      isAnimationActive={false}
                      cursor={{ stroke: 'var(--ink-3)', strokeWidth: 1 }}
                      labelFormatter={(t) => `On-board time ${Number(t).toFixed(0)} s`}
                      formatter={(v, name) =>
                        Array.isArray(v)
                          ? [`${Number(v[0]).toFixed(1)} to ${Number(v[1]).toFixed(1)} ppm`, name]
                          : [`${Number(v).toFixed(1)} ppm`, name]
                      }
                      contentStyle={tooltipStyle}
                    />
                    <Legend wrapperStyle={{ fontSize: 12, color: 'var(--ink-2)' }} />
                    <Area
                      dataKey="band"
                      name="Estimate, 2σ band"
                      legendType="square"
                      stroke="none"
                      fill="var(--series)"
                      fillOpacity={0.15}
                      isAnimationActive={false}
                    />
                    <Line
                      dataKey="model"
                      name="Simulator model"
                      legendType="plainline"
                      stroke="var(--g-secondary)"
                      strokeWidth={2}
                      dot={false}
                      isAnimationActive={false}
                    />
                    <Line
                      dataKey="drift"
                      name="Kalman estimate"
                      legendType="plainline"
                      stroke="var(--series)"
                      strokeWidth={2}
                      dot={false}
                      isAnimationActive={false}
                    />
                  </ComposedChart>
                </ResponsiveContainer>
              </div>
            </figure>

            <figure className="chart">
              <figcaption>
                <span className="chart-title">Residual per report, ms</span>
              </figcaption>
              <div className="chart-plot chart-plot-tall">
                <ResponsiveContainer width="100%" height="100%">
                  <LineChart data={points} margin={{ top: 6, right: 16, bottom: 0, left: 0 }}>
                    <CartesianGrid vertical={false} stroke="var(--grid)" />
                    <XAxis
                      dataKey="obt"
                      type="number"
                      domain={['dataMin', 'dataMax']}
                      tickFormatter={(t: number) => `${Math.round(t)} s`}
                      tick={axisTick}
                      tickLine={false}
                      axisLine={{ stroke: 'var(--rule)' }}
                      minTickGap={40}
                    />
                    <YAxis width={48} tick={axisTick} tickLine={false} axisLine={false} domain={[-2, 2]} allowDataOverflow />
                    <ReferenceLine y={0} stroke="var(--ink-3)" />
                    <Tooltip
                      isAnimationActive={false}
                      labelFormatter={(t) => `On-board time ${Number(t).toFixed(0)} s`}
                      formatter={(v) => [`${Number(v).toFixed(3)} ms`, 'Residual']}
                      contentStyle={tooltipStyle}
                    />
                    <Line
                      dataKey="residual_ms"
                      name="Residual"
                      stroke="none"
                      dot={{ r: 2.5, fill: 'var(--series)', stroke: 'var(--panel)', strokeWidth: 1 }}
                      isAnimationActive={false}
                    />
                  </LineChart>
                </ResponsiveContainer>
              </div>
            </figure>
          </div>
        </>
      )}
    </section>
  )
}
