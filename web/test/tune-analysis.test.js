import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import {
  waterDeadTime,
  pumpedMl,
  weighedFlush,
  scaleCoeffs,
  flushTargetFactor,
  lowestAfterFlush,
} from '../src/pages/Settings/tune/tuneAnalysis.js';

// Two 5 s flushes the machine started itself, from a settled 93 °C on a Silvia (2026-10-29), as [t, ct, fl].
const fixtures = JSON.parse(readFileSync(new URL('./fixtures-tune.json', import.meta.url)));
const samples = rows => rows.map(([t, ct, fl]) => ({ t, ct, fl }));

test('water dead time on real Silvia flushes is near the 12 s that tuned best', () => {
  for (const rows of Object.values(fixtures)) {
    const r = waterDeadTime(samples(rows));
    assert.equal(r.error, undefined);
    assert.ok(r.seconds > 11 && r.seconds < 15, `got ${r.seconds}`);
    assert.ok(r.depth > 5);
  }
});

test('water dead time needs flow and a real dip', () => {
  assert.ok(waterDeadTime([{ t: 0, ct: 93, fl: 0 }]).error);
  const flat = Array.from({ length: 60 }, (_, i) => ({
    t: i,
    ct: 93,
    fl: i > 5 && i < 10 ? 8 : 0,
  }));
  assert.ok(waterDeadTime(flat).error);
});

test('pumped volume of a 5 s flush is about 43 ml', () => {
  const ml = pumpedMl(samples(fixtures.flush5s_machine));
  assert.ok(ml > 38 && ml < 48, `got ${ml}`);
});

test('weighed flush: valve dump is the tray, pump ratio is cup over reading', () => {
  const r = weighedFlush({ cupG: 93.8, trayG: 5.2, readingMl: 88.3 });
  assert.equal(r.valveDumpMl, 5.2);
  assert.ok(Math.abs(r.pumpRatio - 1.062) < 0.001);
  assert.equal(r.pumpNeedsScaling, true);
  assert.equal(weighedFlush({ cupG: 89, trayG: 5, readingMl: 88.2 }).pumpNeedsScaling, false);
  assert.ok(weighedFlush({ cupG: 0, trayG: 5, readingMl: 88 }).error);
});

test('scaling pump coefficients', () => {
  assert.equal(scaleCoeffs('8.644,4.676', 1.1), '9.508,5.144');
});

test('flush to target factor from the Silvia bench runs', () => {
  // Run 2: factor 1.0 undershot to 90.85 for 93 from 106.5 -> moves towards ~0.88.
  const r2 = flushTargetFactor({ startC: 106.5, targetC: 93, lowC: 90.85, factor: 1.0 });
  assert.ok(Math.abs(r2.factor - 0.94) < 0.011, `got ${r2.factor}`); // half way to 0.88
  // Run 7: 0.91 with the valve dump landed 0.3 under -> stays.
  const r7 = flushTargetFactor({ startC: 106.64, targetC: 88, lowC: 87.7, factor: 0.91 });
  assert.equal(r7.factor, 0.91);
  // Overshoot (landed high) raises it, within the allowed range.
  const high = flushTargetFactor({ startC: 108, targetC: 93, lowC: 95, factor: 1.15 });
  assert.ok(high.factor <= 1.2 && high.factor > 1.15);
  assert.ok(flushTargetFactor({ startC: 94, targetC: 93, lowC: 92.5, factor: 1 }).error);
});

test('lowest reading after the flush', () => {
  const low = lowestAfterFlush(samples(fixtures.flush5s_machine));
  assert.ok(low.lowC < 88 && low.at > 15 && low.at < 40);
});
