"""Time correlation: the Kalman filter, its least-squares cross-check, and L0 -> L1."""

from datetime import datetime

import numpy as np
import pytest

from tinyground.l0_l1 import to_csv, to_l1
from tinyground.pus import Telemetry
from tinyground.services import InstrumentHk
from tinyground.timecorr import TimeCorrelator, least_squares_drift

UTC_START = 1.79e9  # November 2026


def reports(n, noise_s, temp_at=lambda k: 20.0, base=250.0, tempco=10.0, seed=0):
    """Synthetic time reports, one per on-board second, from a clock running
    base + tempco * (T - 25) ppm fast. Yields (obt, utc, temp, true_ppm)."""
    rng = np.random.default_rng(seed)
    obt, utc, out = 0.0, UTC_START, []
    for k in range(n):
        temp = temp_at(k)
        ppm = base + tempco * (temp - 25.0)
        utc += 1.0 / (1.0 + ppm * 1e-6)  # real time for one on-board second
        obt += 1.0
        out.append((obt, utc + rng.normal(0.0, noise_s), temp, ppm))
    return out


def fault_profile(n=360):
    """Detector temperature through warm-up, MEASURE, heater fault and SAFE."""
    temp, out, safe = 20.0, [], False
    for k in range(n):
        target = 20 if k < 60 else 38 if k < 150 else 22 if safe else 60
        if k >= 150 and not safe and temp > 45.5:
            safe, target = True, 22
        temp += (target - temp) / 8
        out.append(temp)
    return out


def run(corr, data, with_temp=True):
    for obt, utc, temp, _ in data:
        corr.add(obt, utc, temp if with_temp else None)
    return corr.estimate()


def test_not_ready_until_three_reports():
    c = TimeCorrelator()
    assert c.estimate() is None
    with pytest.raises(ValueError):
        c.obt_to_utc(0.0)
    run(c, reports(2, 0.0))
    assert not c.ready


def test_kalman_without_process_noise_matches_numpy_least_squares():
    """At constant temperature with no process noise the filter is recursive
    least squares: its drift must equal numpy's batch fit."""
    data = reports(120, 0.3e-3, seed=1)
    c = TimeCorrelator(meas_sigma_s=0.3e-3, phase_noise=0.0, base_noise=0.0, tempco_noise=0.0)
    kalman = run(c, data).drift_ppm
    lstsq, _ = least_squares_drift([d[0] for d in data], [d[1] for d in data])
    assert kalman == pytest.approx(lstsq, abs=1e-3)


def test_constant_drift_recovered_within_its_uncertainty():
    est = run(TimeCorrelator(), reports(180, 0.4e-3, seed=2))
    assert est.drift_ppm == pytest.approx(200, abs=3 * est.drift_sigma_ppm)  # 250 + 10 x (20 - 25)
    assert est.drift_sigma_ppm < 10
    assert est.rejected == 0


def test_learns_temperature_coefficient_and_tracks_the_heater_fault():
    """Warm-up makes the temperature coefficient observable; after that the filter
    predicts the drift swing of the heater fault from housekeeping temperatures."""
    temps = fault_profile()
    data = reports(len(temps), 0.35e-3, temp_at=lambda k: temps[k], seed=3)
    c = TimeCorrelator()
    inside = []
    for k, (obt, utc, temp, ppm) in enumerate(data):
        c.add(obt, utc, temp)
        if c.ready and k > 20:
            e = c.estimate()
            inside.append(abs(e.drift_ppm - ppm) <= 2 * e.drift_sigma_ppm)
    est = c.estimate()
    assert est.tempco_ppm_per_c == pytest.approx(10.0, abs=1.0)
    assert est.base_ppm == pytest.approx(250.0, abs=10.0)
    assert np.mean(inside) >= 0.95   # truth inside the 2-sigma band nearly always
    assert est.rejected == 0


def test_unexplained_drift_change_is_reacquired():
    """A jump the model cannot predict (temperature unchanged): after a short run of
    outliers the filter widens its uncertainty and converges again."""
    rng = np.random.default_rng(4)
    obt, utc, data = 0.0, UTC_START, []
    for k in range(300):
        ppm = 200.0 if k < 120 else 420.0
        utc += 1.0 / (1.0 + ppm * 1e-6)
        obt += 1.0
        data.append((obt, utc + rng.normal(0, 0.35e-3), 20.0, ppm))
    c = TimeCorrelator()
    est = run(c, data)
    assert c.reacquisitions >= 1
    assert est.drift_ppm == pytest.approx(420, abs=15)


def test_single_outlier_is_rejected():
    data = reports(100, 0.3e-3, seed=5)
    obt, utc, temp, ppm = data[60]
    data[60] = (obt, utc + 0.050, temp, ppm)   # one report delayed by 50 ms
    c = TimeCorrelator(meas_sigma_s=0.3e-3)
    est = run(c, data)
    assert est.rejected == 1 and c.reacquisitions == 0
    assert est.drift_ppm == pytest.approx(200, abs=3 * est.drift_sigma_ppm)


def test_reboot_restarts_correlation():
    c = TimeCorrelator()
    run(c, reports(30, 0.0))
    upd = c.add(0.5, UTC_START + 100.0, 20.0)   # on-board clock back at zero
    assert upd.accepted and upd.reason == "on-board clock restarted"
    assert c.samples == 1


def test_stale_report_rejected():
    c = TimeCorrelator()
    run(c, reports(10, 0.0))
    assert not c.add(c.last_obt, UTC_START + 10, 20.0).accepted


def test_obt_to_utc_accuracy():
    c = TimeCorrelator(meas_sigma_s=0.2e-3)
    run(c, reports(120, 0.2e-3, base=350.0, seed=6))   # 300 ppm at 20 degC
    true_utc = UTC_START + 60 / (1.0 + 300e-6)
    utc, sigma = c.obt_to_utc(60.0)
    assert abs(utc - true_utc) < max(4 * sigma, 0.5e-3)
    assert c.estimate().utc_of_obt_zero == pytest.approx(UTC_START, abs=2e-3)


def hk_packet(obt):
    data = bytes([1, 1, 0x09, 0xC4, 0, 120, 0x6D, 0x60, 0, 0, 0, 0, 0, 1, 0, 2, 0, 0,
                  0, 0x02, 0x0F, 0xA0, 0x11, 0x94])
    return InstrumentHk.decode(Telemetry(100, 0, 3, 25, data=data)), obt


def test_level1_stamps_utc_and_exports_csv():
    c = TimeCorrelator(meas_sigma_s=0.2e-3)
    hk, obt = hk_packet(30.0)
    assert to_l1(hk, obt, c) is None            # no correlation yet: no Level 1
    run(c, reports(60, 0.2e-3, temp_at=lambda k: 25.0, seed=7))   # 250 ppm at 25 degC
    rec = to_l1(hk, obt, c)
    # A clock running 250 ppm fast reads 30 s after only 30 / 1.00025 s of real
    # time: the UTC stamp must be 7.5 ms earlier than a naive OBT + epoch.
    stamped = datetime.fromisoformat(rec.utc.replace("Z", "+00:00")).timestamp()
    assert stamped == pytest.approx(UTC_START + 30 / (1 + 250e-6), abs=1.5e-3)
    assert (UTC_START + 30) - stamped == pytest.approx(7.5e-3, abs=1.5e-3)
    assert 0 < rec.utc_sigma_ms < 1.0
    assert (rec.mode, rec.temperature_c, rec.temperature_check) == ("STANDBY", 25.0, "within limits")
    csv_text = to_csv([rec])
    assert csv_text.splitlines()[0].startswith("utc,utc_sigma_ms,obt_s,mode")
    assert len(csv_text.splitlines()) == 2
