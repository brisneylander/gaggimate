# Fork roadmap: boiler temperature control

Future additions for this fork (Rancilio Silvia 230 V / 1100 W, GaggiMate Pro Rev 1.1), plus what bench testing
on 2026-10-28 left open. See [FORK.md](FORK.md) for building and releasing.

## Planned

1. **A small thermal model at the centre.** Three heat stores (element and wall, water, group head), driven by
   heater duty, pump flow and inlet temperature. A Kalman-filter state estimator, corrected by the sensor, gives the
   water temperature *now*; the controller acts on that instead of a reading ~20 s old. The heat-balance predictor
   (`lib/GaggiMateController/src/peripherals/HeatBalancePredictor.*`) is the minimal version.
2. **Plan heat for known disturbances.** The display knows when a shot or flush starts and roughly how much water
   the profile will move: pre-heat slightly before a shot, supply the energy the water takes as it flows, put back
   exactly what the model says was removed afterwards, then stop. A small PI on the estimate only mops up model error.
3. **Calibrate from normal use, with the user in control.** Identify heat-up rate, dead time and losses from
   ordinary warm-ups; keep them per machine within sane limits (`lib/NanoPbComm/src/LagCompensationLimits.h`) and show
   them for approval. Machine presets (heater power, boiler size); raw PID numbers in an advanced section.
4. **Finer heater control.** Switch the SSR on mains half-cycles (10 ms at 50 Hz, ~100 levels per second) instead of
   the 1 s window, so output is smoother and sensor noise doesn't become heater chatter.
5. **Safety separate from control.** Keep the hardware thermal fuse. Firmware hard limits (raw sensor above X → off,
   lost link → off) in a tiny, separately tested layer the control code can't override.
6. **Simulation and logging from day one.** Keep the plant model in the repo (`test/test_heat_balance_predictor`
   flush replay) and check every change against it. Log temperature, duty and flow at a few Hz into shot history so
   tuning can use real data.

## Bench results so far

5 s flush from 93 °C, Kp 60 / Ki 0.658 / Kd 1500, 40 °C tank, calibrated pump (8.644, 4.676):

| Configuration (runs) | Overshoot | Settled ±0.3 °C |
|---|---|---|
| Compensation off (3) | +2.23, +3.75, +2.75 — avg +2.91 °C | avg 318 s |
| On, single 20 s delay (5) | +1.75, +1.89, +1.50, +1.79, +1.47 — avg +1.68 °C | avg 174 s |
| **On, 12 s water delay (2), simon006** | **+0.75, +0.50 — avg +0.63 °C** | **avg 126 s** |

**Full-power reheat (simon007) does not help on the Silvia.** Flush pairs on 2026-10-29: with reheat the sensor took
124–129 s back to 93 °C (overshoot +0.40–0.45 °C) against 62–100 s without (+0.49–1.25 °C). Energy is delivered in
~12 s, but heat then needs ~2 min to reach the thermocouple; the simulated plant moves heat far faster, so it
promised 51 s. Reaching 93 °C sooner means over-delivering heat. Next idea: reheat with a deliberate surplus, then
"flush to target" to take it off. (One "off" run showed a reheat-like burst; most likely the PID's own D term, but
switching reheat on → off should be re-checked with a single flush.)

**"Flush to target" after steaming** (heat to 100 °C in Steam, back to Brew, flush at the peak; target 93 °C, tank
~35 °C), 2026-10-29:

| Run | Release | Start | Flush | Lowest | Within 0.5 °C from |
| --- | --- | --- | --- | --- | --- |
| 1 | simon007 (PID heats during the flush) | 105.75 °C | 79 ml | 90.66 °C, rebound to 97 | 287 s |
| 2 | simon008 (heater held off) | 106.5 °C | 86 ml | 90.85 °C | 317 s |
| 3 | simon009 (+ standing-loss allowance) | 108.0 °C | 88–90 ml | 90.83 °C | 291 s |
| **4** | **simon010 (counts 85 % of the shown surplus)** | **106.56 °C** | **68 ml, 7.8 s** | **92.24 °C** | **146 s** |
| **5** | **simon010** | **106.73 °C** | **68 ml** | **91.85 °C** | **149 s** |
| **6** | **simon010** | **108.13 °C** | **74 ml** | **92.18 °C** | **157 s** |

Run 4 was within 1 °C 56 s after the flush, bottomed out 2 min later, and held 93.0 °C from ~4 min. The remaining
−0.8 °C dip is the heater's dead time after the 30 s hold, not the volume. The 0.85 factor held over three runs (lowest 91.85–92.24 °C, within 0.5 °C after 146–157 s).

**Simulator plant fitted to the Silvia** (2026-10-29, `scripts/fit_plant.py`): the test plant in
`test/test_heat_balance_predictor` now has three nodes fitted to the bench logs. Heater heat reaches the water with a
~21 s time constant; the group head (819 J/°C, ~2.4 min to the water) carries the standing loss and keeps pulling the
water down after a flush; the sensor sees the water through 4.6 s + 15 s. On flushes left out of the fit it tracks
the sensor to 0.45 °C rms (old plant 1.09 °C) and puts the dip's low within ~6 s (old: 7-18 s early). Two findings:
- **Flush water cools ~20 % more per ml than (boiler - tank setting) predicts**: sessions fit as if the tank were
  24-27 °C with the setting at 35. The same shortfall explains flush to target's 0.85 factor, and the full-power
  reheat being sized short on the bench. Next: find out why (flow under-read, colder intake, cold water mixing at the
  sensor), then fix the per-ml figure once in the shared code instead of the 0.85 factor.
- The fitted plant still flatters full-power reheat a little in closed loop; trust bench runs over it for timing.

With the 12 s water delay the heater puts back about what the flush took (net −0.8 to +0.3 kJ), so an explicit heat
budget for flushes looks unnecessary; recovery to 93 °C is a little slower (69–81 s vs 56–62 s). Next: try 10 and
15 s, then shots, where the planned-heat idea (pre-heat before a shot) still applies.

## Open from bench testing (v1.9.0-simon003/004)

- **Flush A/B, one run each** (5 s flush from 93 °C, Kp 60 / Ki 0.658 / Kd 1500): overshoot 2.23 → 1.75 °C, settling
  283 → 182 s, dip 86.45 → 87.17 °C with everything on. Much less than the simulation's 2.0 → 0.25 °C. Repeat as
  alternating pairs before drawing conclusions (identical settings varied by 1–3 °C in hand tests).
- **Two different delays.** The sensor sees a flush within ~5 s but heater heat only after ~20 s; the model uses one
  dead time for both. The heater ran at full power for only ~10–20 s and the whole overshoot is heat put in during
  the first ~25 s. Next experiment: a higher model heat-up rate (0.8–0.9 °C/s) so the predictor backs off sooner.
- **Tank water temperature drifts.** It's ~25 °C normally but reached 40 °C after a few hours of testing, beside the
  hot boiler. The setting (simon005) is a single fixed value; estimate it instead (machine on-time and water drawn)
  or measure it with a cheap sensor in the tank. Set back to 0 (the upstream 23 °C default) after the flush tests.
- **Pump flow calibration needs a Bluetooth scale.** Without one the screen can't save; let it accept a typed-in
  weight. A single weight was enough here (pump model read 18–21 % high; scaled to 8.644, 4.676, then confirmed
  within 3 % on a second run).
- **Standing loss per degree.** The learned loss works (45 W saved) but should be W/°C × (T − 23 °C) so brew and steam
  share one value.
- **Steam switch input stays "on" with the switch off.** Tested 2026-10-28: switching on registered, switching off
  never did (no debounce, 100 ms sampling, reliable messages, and still "on" with the heater off), but it has read
  "off" right after some restarts. So the pin is held active electrically, intermittently: suspect leakage through
  the Silvia's illuminated switch keeping the input partly on. Check the wiring against the GaggiMate Silvia guide.
  Workaround in place: button behaviour `brew,none,water` (steam switch ignored; steam from the screen or web UI).
  Restore with `brew,steam,water`. The display's "steam switch is on" warning only clears after a restart.
- **Autotune.** Offset-aware, predictive safety cutoff (a run coasted to 131 °C), stop once dead time and k' are
  identified, remember the Heater Wattage field, optional relay test.
- **Phone app** loads its web UI from the official releases (`web/src/app/updates.js`), so it never shows fork
  features.
- **Upstream PRs** (`pr/pid-derivative-on-measurement` → `pr/heater-dfilter-integral-zone` →
  `pr/heater-heat-balance-predictor`): need the CLA, screenshots and bench data.
- **Upstream sync**: a weekly sync pull request rather than a daily auto-merge.
