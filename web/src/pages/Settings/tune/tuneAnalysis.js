// Analysis for the Tune this machine routine: turns logged machine readings into setting values. Pure functions over
// samples { t (s), ct (°C), fl (ml/s) }, so they can be checked against bench logs (test/tune-analysis.test.js).

// Water dead time: from water starting to flow until the reading has fallen half way to its lowest point. On a Silvia a
// 5 s flush started by the machine gives 12.7-13.6 s, where 12 s tuned best on the bench (ROADMAP.md).
export function waterDeadTime(samples, { flowOn = 0.5, windowS = 90 } = {}) {
  const i0 = samples.findIndex(s => s.fl > flowOn);
  if (i0 < 0) return { error: 'no flow seen' };
  const t0 = samples[i0].t;
  const before = samples.slice(Math.max(0, i0 - 6), i0 + 1);
  const start = before.reduce((a, s) => a + s.ct, 0) / before.length;
  const win = samples.slice(i0).filter(s => s.t - t0 <= windowS);
  let low = win[0];
  for (const s of win) if (s.ct < low.ct) low = s;
  const depth = start - low.ct;
  if (!(depth >= 1.5)) return { error: `dip too small (${depth.toFixed(1)} °C)` };
  const half = win.find(s => s.ct <= start - depth / 2);
  return { seconds: half.t - t0, depth, start, low: low.ct, lowAt: low.t - t0 };
}

// What a paired Bluetooth scale gained over the flush: the median of the readings before the flow starts against the
// median of the last few seconds (after the drips). samples carry w (g); null without enough scale readings.
export function scaleGain(samples, { flowOn = 0.5, tailS = 5 } = {}) {
  const median = xs => {
    const v = xs.filter(Number.isFinite).sort((a, b) => a - b);
    return v.length ? v[Math.floor(v.length / 2)] : NaN;
  };
  const i0 = samples.findIndex(s => s.fl > flowOn);
  if (i0 < 1) return null;
  const before = median(samples.slice(0, i0).map(s => s.w));
  const tEnd = samples[samples.length - 1].t;
  const after = median(samples.filter(s => tEnd - s.t <= tailS).map(s => s.w));
  const gain = after - before;
  return Number.isFinite(gain) && gain > 1 ? Math.round(gain * 10) / 10 : null;
}

// Pump flow reading integrated over the samples (ml).
export function pumpedMl(samples) {
  let ml = 0;
  for (let i = 1; i < samples.length; i++) {
    const dt = samples[i].t - samples[i - 1].t;
    if (dt > 0 && dt < 5) ml += ((samples[i].fl + samples[i - 1].fl) / 2) * dt;
  }
  return ml;
}

// A weighed flush: what reached the cup against the reading, and the drip tray's share (the valve dump). The pump
// calibration is right when the cup matches the reading (on a Silvia: 89.1 g vs 88.2 ml over five 10 s flushes).
export function weighedFlush({ cupG, trayG, readingMl }) {
  if (!(cupG > 0) || !(readingMl > 0)) return { error: 'enter the cup weight' };
  const tray = trayG > 0 ? trayG : 0;
  const ratio = cupG / readingMl;
  // Far outside one flush's spread: the wrong cup weight, or more than one flush in it (two flushes read 180 % on the
  // first try of this routine). Nothing is offered then.
  if (ratio < 0.6 || ratio > 1.4)
    return {
      error: `the cup holds ${Math.round(ratio * 100)} % of the pump reading: more than one flush, or the cup's own weight included?`,
    };
  return {
    valveDumpMl: Math.round(tray * 10) / 10,
    pumpRatio: ratio,
    // Offer to rescale the pump coefficients only past the flush-to-flush spread of one flush (~±9 %) halved.
    pumpNeedsScaling: Math.abs(ratio - 1) > 0.05,
  };
}

// Scale "a,b" pump model coefficients by a ratio.
export function scaleCoeffs(csv, ratio) {
  return String(csv)
    .split(',')
    .map(v => (Number(v) * ratio).toFixed(3))
    .join(',');
}

// Flush to target after steaming: where the boiler bottoms out within 4 minutes, against where it should (0.3 °C under
// the target is a good landing: the group head keeps pulling the water down for a while). The factor moves half way
// towards the value that would have landed there. On the Silvia: run 2 (factor 1.0, from 106.5 to a low of 90.85 for a
// 93 °C target) proposes 0.88; run 7 (0.91 with the 5 ml dump, 106.64 to 87.70 for 88 °C) keeps 0.91.
export const TARGET_UNDERSHOOT_C = 0.3;
export function flushTargetFactor({ startC, targetC, lowC, factor }) {
  const want = startC - targetC + TARGET_UNDERSHOOT_C;
  const got = startC - lowC;
  if (!(want > 2) || !(got > 0)) return { error: 'not enough surplus to judge' };
  const f = factor > 0 ? factor : 1;
  const proposal = f * (want / got);
  const next = Math.min(1.2, Math.max(0.5, f + 0.5 * (proposal - f)));
  return { factor: Math.round(next * 100) / 100, landedC: lowC - targetC };
}

// The lowest reading within windowS after the flush starts (the flush-to-target landing).
export function lowestAfterFlush(samples, { flowOn = 0.5, windowS = 240 } = {}) {
  const i0 = samples.findIndex(s => s.fl > flowOn);
  if (i0 < 0) return null;
  const t0 = samples[i0].t;
  let low = null;
  for (const s of samples.slice(i0)) {
    if (s.t - t0 > windowS) break;
    if (low === null || s.ct < low.ct) low = s;
  }
  return low ? { lowC: low.ct, at: low.t - t0 } : null;
}
