"""Time correlation: how the on-board clock relates to UTC.

The spacecraft stamps everything with on-board time (OBT) from a
free-running oscillator. Science needs UTC. Every ST[9] time report
gives one pair: the OBT it carries, and the UTC at which the ground
received it (minus the link delay). Their difference

    z = UTC - OBT

changes at minus the on-board clock's rate error. That rate error
depends on the oscillator's temperature, as for any uncompensated
crystal, and the ground knows the temperature from housekeeping. So
instead of chasing the drift after it changes, the filter models it:

    rate error (ppm) = b + k * (T - 25 degC)

and a Kalman filter estimates the state

    x = [theta, b, k]    theta = UTC - OBT              (seconds)
                         b     = rate error at 25 degC  (ppm)
                         k     = temperature coefficient (ppm/degC)

Between two reports dt apart, at temperature T:

    theta' = theta - 1e-6 * (b + k (T - 25)) * dt  + phase noise
    b'     = b + random walk (ageing)
    k'     = k + small random walk

At constant temperature only the combination b + k (T - 25) can be
observed; k becomes observable as soon as the temperature changes (the
instrument warming up), after which the filter predicts drift changes
from housekeeping instead of lagging behind them. Without temperatures
it reduces to the classic offset-and-drift clock model, the same problem
as synchronising independently clocked data streams or the clocks of
LISA's three spacecraft. With no process noise the filter is recursive
least squares, which the tests check against numpy.linalg.lstsq.

Everything is kept relative to the first report, so float64 keeps
sub-microsecond resolution even with UTC near 1.8e9 s.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Sequence, Tuple

import numpy as np

T_REF_C = 25.0
PPM = 1e-6


@dataclass(frozen=True)
class Update:
    """What happened to one time report."""

    accepted: bool
    residual_s: float       # measurement minus prediction, before the update
    reason: str = ""


@dataclass(frozen=True)
class ClockEstimate:
    drift_ppm: float          # current rate error, positive = on-board clock runs fast
    drift_sigma_ppm: float
    base_ppm: float           # rate error at 25 degC
    tempco_ppm_per_c: float   # temperature coefficient
    tempco_sigma: float
    utc_of_obt_zero: float    # UTC (epoch seconds) when the on-board clock read 0
    offset_sigma_s: float     # 1-sigma of the current UTC - OBT estimate
    samples: int
    rejected: int


class TimeCorrelator:
    """Kalman filter estimating the UTC <-> on-board time relation.

    meas_sigma_s:  timing noise of one report (arrival jitter), seconds
    phase_noise:   white phase noise per report, s/sqrt(s)
    base_noise:    random walk of the base rate error, ppm/sqrt(s)
    tempco_noise:  random walk of the temperature coefficient, ppm/degC/sqrt(s)
    gate_sigma:    reject reports whose residual exceeds this many sigma
    link_delay_s:  one-way delay to subtract from the arrival time
    base_prior:    1-sigma of the base rate error before any data, ppm
    tempco_prior:  1-sigma of the temperature coefficient before any data
    """

    def __init__(self, meas_sigma_s: float = 0.5e-3, phase_noise: float = 1e-5,
                 base_noise: float = 0.5, tempco_noise: float = 0.02,
                 gate_sigma: float = 5.0, link_delay_s: float = 0.0,
                 base_prior: float = 2000.0, tempco_prior: float = 30.0,
                 max_consecutive_outliers: int = 5):
        self.R = meas_sigma_s ** 2
        self.q = np.array([phase_noise ** 2, base_noise ** 2, tempco_noise ** 2])
        self.gate = gate_sigma
        self.delay = link_delay_s
        self.priors = (base_prior, tempco_prior)
        self.max_outliers = max_consecutive_outliers
        self.reset()

    def reset(self) -> None:
        self.x = np.zeros(3)
        self.P = np.diag([self.R, self.priors[0] ** 2, self.priors[1] ** 2])
        self.obt0: Optional[float] = None
        self.utc0: Optional[float] = None
        self.last_obt: Optional[float] = None
        self.last_temp = T_REF_C
        self.samples = 0
        self.rejected = 0
        self.outlier_run = 0
        self.reacquisitions = 0

    @property
    def ready(self) -> bool:
        """Enough reports for a drift estimate."""
        return self.samples >= 3

    @staticmethod
    def _rate_row(dt: float, temp_c: float) -> np.ndarray:
        """Change of theta over dt seconds, as a linear function of [b, k]."""
        return np.array([-PPM * dt, -PPM * dt * (temp_c - T_REF_C)])

    def add(self, obt: float, utc: float, temp_c: Optional[float] = None) -> Update:
        """Feed one time report: its on-board time, its UTC arrival time and,
        if known, the oscillator temperature (latest housekeeping)."""
        utc -= self.delay
        temp = self.last_temp if temp_c is None else float(temp_c)
        if self.obt0 is None or (self.last_obt is not None and obt < self.last_obt - 1.0):
            # First report, or the on-board clock restarted (reboot): start over.
            reason = "" if self.obt0 is None else "on-board clock restarted"
            self.reset()
            self.obt0, self.utc0, self.last_obt, self.last_temp = obt, utc, obt, temp
            self.samples = 1
            return Update(True, 0.0, reason)

        dt = obt - self.last_obt
        if dt <= 0:
            self.rejected += 1
            return Update(False, 0.0, "not newer than the previous report")

        # Predict, using the mean temperature over the interval.
        F = np.eye(3)
        F[0, 1:] = self._rate_row(dt, 0.5 * (temp + self.last_temp))
        x = F @ self.x
        P = F @ self.P @ F.T + np.diag(self.q * dt)

        z = (utc - self.utc0) - (obt - self.obt0)
        residual = z - x[0]
        S = P[0, 0] + self.R
        self.last_obt, self.last_temp = obt, temp
        reason = ""
        if self.ready and abs(residual) > self.gate * np.sqrt(S):
            self.outlier_run += 1
            if self.outlier_run < self.max_outliers:
                self.x, self.P = x, P  # keep the prediction, skip the measurement
                self.rejected += 1
                return Update(False, float(residual), f"outlier ({residual * 1e3:+.2f} ms)")
            # Several in a row are not outliers: the clock changed in a way the
            # model did not predict. Widen the base-rate uncertainty and accept,
            # so the filter re-converges instead of rejecting everything forever.
            P = P.copy()
            P[0, 0] += residual ** 2
            P[1, 1] += self.priors[0] ** 2 / 100.0
            S = P[0, 0] + self.R
            self.reacquisitions += 1
            reason = "re-acquired after a run of outliers"
        self.outlier_run = 0

        K = P[:, 0] / S
        self.x = x + K * residual
        I_KH = np.eye(3) - np.outer(K, [1.0, 0.0, 0.0])
        self.P = I_KH @ P @ I_KH.T + np.outer(K, K) * self.R  # Joseph form: stays symmetric
        self.samples += 1
        return Update(True, float(residual), reason)

    def drift(self, temp_c: Optional[float] = None) -> Tuple[float, float]:
        """Rate error (ppm) and its 1-sigma at a temperature (default: the latest)."""
        dT = (self.last_temp if temp_c is None else temp_c) - T_REF_C
        h = np.array([1.0, dT])
        return float(h @ self.x[1:]), float(np.sqrt(h @ self.P[1:, 1:] @ h))

    def obt_to_utc(self, obt: float) -> Tuple[float, float]:
        """UTC (epoch seconds) for an on-board time, and its 1-sigma in seconds."""
        if self.obt0 is None:
            raise ValueError("no time reports yet")
        dt = obt - self.last_obt
        h = np.concatenate([[1.0], self._rate_row(dt, self.last_temp)])
        theta = float(h @ self.x)
        var = float(h @ self.P @ h)
        return self.utc0 + (obt - self.obt0) + theta, float(np.sqrt(max(var, 0.0)))

    def estimate(self) -> Optional[ClockEstimate]:
        if not self.ready:
            return None
        drift, drift_sigma = self.drift()
        utc_zero, _ = self.obt_to_utc(0.0)
        return ClockEstimate(
            drift_ppm=drift,
            drift_sigma_ppm=drift_sigma,
            base_ppm=float(self.x[1]),
            tempco_ppm_per_c=float(self.x[2]),
            tempco_sigma=float(np.sqrt(self.P[2, 2])),
            utc_of_obt_zero=float(utc_zero),
            offset_sigma_s=float(np.sqrt(self.P[0, 0])),
            samples=self.samples,
            rejected=self.rejected,
        )


def least_squares_drift(obt: Sequence[float], utc: Sequence[float]) -> Tuple[float, float]:
    """Batch reference: fit UTC - OBT = a + b (OBT - OBT0) with numpy.

    Returns (drift_ppm, a). Used to cross-check the Kalman filter at
    constant temperature.
    """
    obt = np.asarray(obt, dtype=float)
    utc = np.asarray(utc, dtype=float)
    t = obt - obt[0]
    z = (utc - utc[0]) - t
    A = np.column_stack([np.ones_like(t), t])
    (a, b), *_ = np.linalg.lstsq(A, z, rcond=None)
    return float(-b * 1e6), float(a)
