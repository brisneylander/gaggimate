import assert from 'node:assert/strict';
import test from 'node:test';
import {
  BUILT_IN_PROFILES,
  matchingProfile,
  valuesToApply,
} from '../src/pages/Settings/tabs/machineProfiles.js';

const silvia = BUILT_IN_PROFILES.find(p => p.name === 'Rancilio Silvia 230 V');

test('a profile keeps a temperature offset that is already set', () => {
  const applied = valuesToApply(silvia.values, { temperatureOffset: '3' });
  assert.equal('temperatureOffset' in applied, false);
  assert.equal(applied.modelHeaterW, silvia.values.modelHeaterW);
  assert.equal(applied.pid, silvia.values.pid);
});

test('a profile fills in the temperature offset while it is unset', () => {
  for (const unset of [undefined, null, '', ' ', 0, '0']) {
    const applied = valuesToApply(silvia.values, { temperatureOffset: unset });
    assert.equal(
      applied.temperatureOffset,
      silvia.values.temperatureOffset,
      `offset ${JSON.stringify(unset)}`,
    );
  }
});

test('negative offsets count as set', () => {
  assert.equal(
    'temperatureOffset' in valuesToApply(silvia.values, { temperatureOffset: -2 }),
    false,
  );
});

test('a starting point keeps a set offset too', () => {
  const gaggia = BUILT_IN_PROFILES.find(p => p.startingPoint);
  const applied = valuesToApply(gaggia.values, { temperatureOffset: 6 });
  assert.deepEqual(Object.keys(applied).sort(), ['kf', 'pid']);
});

test('the built-in in use is recognised whatever the offset', () => {
  assert.equal(matchingProfile({ ...silvia.values, temperatureOffset: 7 })?.name, silvia.name);
  assert.equal(matchingProfile({ ...silvia.values })?.name, silvia.name);
  assert.equal(matchingProfile({ ...silvia.values, kf: 0.1 }), null);
});
