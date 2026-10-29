// Machine profiles: the machine-specific settings (boiler model, delays, gains, pump calibration, flush to target, tank
// warming, safety windows) as one set, built in or as a JSON file. Seasonal values (tank fill / tank water
// temperature) are left out. Ranges match lag_limits (lib/NanoPbComm/src/LagCompensationLimits.h) where they apply.

export const PROFILE_FORMAT = 'gaggimate-machine-profile';
export const PROFILE_VERSION = 1;

// key -> { min, max } for numbers (0 is always allowed: off / default), or { csv: n } for comma-separated numbers.
export const PROFILE_FIELDS = {
  modelHeaterW: { min: 200, max: 2500 },
  modelHeatRate: { min: 0.05, max: 3 },
  sensorDeadTimeS: { min: 2, max: 60 },
  waterDeadTimeS: { min: 1, max: 60 },
  derivativeFilterS: { min: 0.5, max: 10 },
  integralZone: { min: 0.5, max: 5 },
  valveDumpMl: { min: 0.5, max: 30 },
  flushTargetFactor: { min: 0.5, max: 1.2 },
  flushTargetHoldS: { min: 1, max: 120 },
  tankRiseC: { min: 1, max: 40 },
  tankWarmMin: { min: 10, max: 600 },
  noRiseBrewS: { min: 30, max: 300 },
  noRiseSteamS: { min: 60, max: 900 },
  temperatureOffset: { min: -20, max: 20, allowNegative: true },
  pid: { csv: 3 }, // Kp,Ki,Kd as the form holds them; Kf separately
  kf: { min: 0, max: 10 },
  pumpModelCoeffs: { csv: 2 },
};

// Measured on a Rancilio Silvia 230 V with a GaggiMate Pro, 2026-10-28/29 (ROADMAP.md, bench results).
export const BUILT_IN_PROFILES = [
  {
    name: 'Rancilio Silvia 230 V',
    values: {
      modelHeaterW: 1100,
      modelHeatRate: 0.6,
      sensorDeadTimeS: 20,
      waterDeadTimeS: 12,
      derivativeFilterS: 2,
      integralZone: 1.5,
      valveDumpMl: 5,
      flushTargetFactor: 0.91,
      flushTargetHoldS: 30,
      tankRiseC: 18,
      tankWarmMin: 120,
      noRiseBrewS: 60,
      noRiseSteamS: 180,
      temperatureOffset: 5,
      pid: '60,0.658,1500',
      kf: 0,
      pumpModelCoeffs: '8.644,4.676',
    },
  },
];

function sameValue(a, b) {
  if (a === undefined || a === null || a === '') return false;
  const as = String(a).split(','),
    bs = String(b).split(',');
  return as.length === bs.length && as.every((x, i) => Math.abs(Number(x) - Number(bs[i])) < 1e-6);
}

// The built-in profile the form's values match (every value in it), or null: custom values.
export function matchingProfile(formData) {
  return (
    BUILT_IN_PROFILES.find(p =>
      Object.entries(p.values).every(([key, value]) => sameValue(formData[key], value)),
    ) || null
  );
}

// The profile fields as they are in the settings form now.
export function profileFromForm(name, formData) {
  const values = {};
  for (const key of Object.keys(PROFILE_FIELDS)) {
    if (formData[key] !== undefined && formData[key] !== '') values[key] = formData[key];
  }
  return { format: PROFILE_FORMAT, version: PROFILE_VERSION, name, values };
}

function checkCsv(value, count) {
  const parts = String(value)
    .split(',')
    .map(p => p.trim());
  return parts.length === count && parts.every(p => p !== '' && Number.isFinite(Number(p)));
}

// Validates a parsed profile file. Returns { values, errors }: only known, in-range fields are kept.
export function validateProfile(profile) {
  const errors = [];
  if (!profile || typeof profile !== 'object' || profile.format !== PROFILE_FORMAT) {
    return { values: {}, errors: ['not a GaggiMate machine profile'] };
  }
  if (Number(profile.version) > PROFILE_VERSION)
    errors.push(`newer profile version ${profile.version}`);
  const values = {};
  for (const [key, value] of Object.entries(profile.values || {})) {
    const spec = PROFILE_FIELDS[key];
    if (!spec) {
      errors.push(`unknown setting ${key} (ignored)`);
      continue;
    }
    if (spec.csv) {
      if (checkCsv(value, spec.csv)) values[key] = String(value);
      else errors.push(`${key}: expected ${spec.csv} numbers`);
      continue;
    }
    const v = Number(value);
    const ok =
      Number.isFinite(v) &&
      (v === 0 ||
        (v >= spec.min && v <= spec.max) ||
        (spec.allowNegative && v >= spec.min && v <= spec.max));
    if (ok) values[key] = v;
    else errors.push(`${key}: ${value} outside ${spec.min}–${spec.max}`);
  }
  return { values, errors };
}
