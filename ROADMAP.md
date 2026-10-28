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

## Open from bench testing (v1.9.0-simon003/004)

- **Flush A/B, one run each** (5 s flush from 93 °C, Kp 60 / Ki 0.658 / Kd 1500): overshoot 2.23 → 1.75 °C, settling
  283 → 182 s, dip 86.45 → 87.17 °C with everything on. Much less than the simulation's 2.0 → 0.25 °C. Repeat as
  alternating pairs before drawing conclusions (identical settings varied by 1–3 °C in hand tests).
- **Two different delays.** The sensor sees a flush within ~5 s but heater heat only after ~20 s; the model uses one
  dead time for both. The heater ran at full power for only ~10–20 s and the whole overshoot is heat put in during
  the first ~25 s. Next experiment: a higher model heat-up rate (0.8–0.9 °C/s) so the predictor backs off sooner.
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
