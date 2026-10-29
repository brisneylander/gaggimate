import { Trans, useLingui } from '@lingui/react/macro';
import { useContext } from 'preact/hooks';
import { ApiServiceContext } from '../../../services/ApiService.js';
import Section from '../../../components/Card.jsx';
import { InputGroupField } from '../../../components/SettingsFormField.jsx';
import { MachineProfilePicker } from './MachineProfilePicker.jsx';

// Brew-boiler lag compensation; every field 0 keeps the plain PID loop. Each value is 0 (off) or within
// [min, max]: the same ranges as lag_limits in lib/NanoPbComm/src/LagCompensationLimits.h, which the display and
// controller enforce too.
const FIELDS = [
  { key: 'derivativeFilterS', unit: 's', min: 0.5, max: 10 },
  { key: 'integralZone', unit: '°C', min: 0.5, max: 5 },
  { key: 'sensorDeadTimeS', unit: 's', min: 2, max: 60 },
  { key: 'waterDeadTimeS', unit: 's', min: 1, max: 60 },
  { key: 'modelHeaterW', unit: 'W', min: 200, max: 2500 },
  { key: 'modelHeatRate', unit: '°C/s', min: 0.05, max: 3 },
  { key: 'inletTempC', unit: '°C', min: 5, max: 70 },
  { key: 'tankFillC', unit: '°C', min: 5, max: 40 },
  { key: 'valveDumpMl', unit: 'ml', min: 0.5, max: 30 },
  { key: 'flushTargetFactor', unit: '×', min: 0.5, max: 1.2 },
  { key: 'flushTargetHoldS', unit: 's', min: 1, max: 120 },
  { key: 'tankRiseC', unit: '°C', min: 1, max: 40 },
  { key: 'tankWarmMin', unit: 'min', min: 10, max: 600 },
  { key: 'noRiseBrewS', unit: 's', min: 30, max: 300 },
  { key: 'noRiseSteamS', unit: 's', min: 60, max: 900 },
];

const inRangeOrOff = (value, min, max) => {
  const v = Number(value);
  return value === '' || v === 0 || (v >= min && v <= max);
};

export function LagCompensationSection({ formData, onChange, setField }) {
  const { t } = useLingui();
  const labels = {
    derivativeFilterS: t`Derivative filter`,
    integralZone: t`Integral zone`,
    sensorDeadTimeS: t`Sensor dead time`,
    waterDeadTimeS: t`Water dead time`,
    modelHeaterW: t`Heater power`,
    modelHeatRate: t`Full-power heat-up rate`,
    inletTempC: t`Tank water temperature`,
    tankFillC: t`Tank fill temperature`,
    valveDumpMl: t`Valve dump per flush`,
    flushTargetFactor: t`Flush to target factor`,
    flushTargetHoldS: t`Flush to target heater hold`,
    tankRiseC: t`Tank warming`,
    tankWarmMin: t`Tank warming time`,
    noRiseBrewS: t`Safety: no-rise time (brew)`,
    noRiseSteamS: t`Safety: no-rise time (steam)`,
  };
  const help = {
    derivativeFilterS: t`Smooths the D term so thermocouple steps don't switch the heater. Try 2 s; long filters delay its braking.`,
    integralZone: t`Integral only runs within this distance of the target, and pauses while water flows and for the dead time after. Try 1.5 °C.`,
    sensorDeadTimeS: t`Delay from heater on to the temperature starting to rise.`,
    waterDeadTimeS: t`Time from water starting to flow until the temperature has dropped about half way; often shorter than the heater's (about 12 s on a Silvia). 0 uses the sensor dead time.`,
    modelHeaterW: t`Element power, e.g. 1100 W for a 230 V Rancilio Silvia.`,
    modelHeatRate: t`Steady rise at full power, measured mid-warm-up (k'). Autotune measures and saves it, with the dead time and heater power.`,
    inletTempC: t`Temperature of the water drawn in by a shot or flush. A tank beside the boiler warms up; 0 assumes 23 °C. Used while the tank fill temperature is 0.`,
    tankFillC: t`Temperature of fresh tank water (lower in winter). Set it to estimate the tank instead: it warms while the machine is on and resets on a refill. 0 uses the fixed tank water temperature.`,
    valveDumpMl: t`Water the 3-way valve dumps into the drip tray when a flush or shot ends: weigh the tray after a flush. It leaves the boiler too. 0 counts none (about 5 ml on a Silvia).`,
    flushTargetFactor: t`Share of the shown surplus a flush to target takes off. Lower it if flushes to target land under the target. 0 uses 1.0 (0.91 on a Silvia).`,
    flushTargetHoldS: t`How long the heater stays off after a flush to target, until the flush's effect reaches the sensor. 0 uses 30 s.`,
    tankRiseC: t`How far above the fill temperature the tank settles after hours beside the hot boiler. 0 uses 18 °C.`,
    tankWarmMin: t`Time constant of that warming while the machine is on. 0 uses 120 minutes.`,
    noRiseBrewS: t`The heater stops (until a restart) after this long near full power with no water flowing and the temperature not rising: a sensor off the boiler. Raise it for a slow sensor. 0 uses 60 s.`,
    noRiseSteamS: t`The same at steam temperatures, where steaming milk holds the heater flat out. 0 uses 180 s.`,
  };
  const learnedLoss = Math.round(Number(formData.modelLossW) || 0);
  const tankEstimate = Math.round(Number(formData.tankEstimateC) || 0);
  const apiService = useContext(ApiServiceContext);
  const onTankRefilled = () => apiService.send({ tp: 'req:tank:refilled' });
  return (
    <Section title={t`Machine Boiler Profile`} className='h-full'>
      {setField && <MachineProfilePicker formData={formData} setField={setField} />}
      <p className='mb-4 text-sm opacity-70'>
        <Trans>
          For boilers where the temperature sensor reacts long after the heater. Heater power,
          heat-up rate and dead time together enable a heat-balance predictor, so the PID backs off
          before the overshoot shows. Leave a field at 0 to disable it.
        </Trans>
      </p>
      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        {FIELDS.map(({ key, unit, min, max }) => {
          const range = t`0 (off) or ${min}–${max} ${unit}`;
          return (
            <InputGroupField
              key={key}
              label={labels[key]}
              htmlFor={key}
              unit={unit}
              helpText={`${help[key]} ${t`Allowed: ${range}.`}`}
              noMargin
            >
              <input
                id={key}
                name={key}
                type='number'
                step='any'
                min='0'
                max={max}
                className='grow'
                placeholder='0'
                value={formData[key] ?? 0}
                onInput={e =>
                  // Values between 0 and the minimum are as silly as values above the maximum; block saving.
                  e.target.setCustomValidity(
                    inRangeOrOff(e.target.value, min, max) ? '' : t`Use ${range}.`,
                  )
                }
                onChange={onChange(key)}
              />
            </InputGroupField>
          );
        })}
      </div>
      <label
        htmlFor='flushButtonToTarget'
        className='label mt-4 cursor-pointer justify-start gap-3'
      >
        <input
          id='flushButtonToTarget'
          name='flushButtonToTarget'
          type='checkbox'
          className='toggle toggle-primary'
          checked={!!formData.flushButtonToTarget}
          onChange={onChange('flushButtonToTarget')}
        />
        <span>
          <Trans>Flush button flushes to target when hot</Trans>
        </span>
      </label>
      <p className='text-sm opacity-70'>
        <Trans>
          From 2 °C over the selected profile's temperature (after steaming, say), the flush button
          runs just enough water to bring the boiler back to it. Closer than that it runs the normal
          flush. Needs heater power and heat-up rate.
        </Trans>
      </p>
      {tankEstimate > 0 && (
        <div className='mt-4 flex items-center gap-3 text-sm'>
          <span className='opacity-70'>
            <Trans>Estimated tank water now: {tankEstimate} °C</Trans>
          </span>
          <button type='button' className='btn btn-sm btn-outline' onClick={onTankRefilled}>
            <Trans>Tank refilled</Trans>
          </button>
        </div>
      )}
      {learnedLoss > 0 && (
        <p className='mt-4 text-sm opacity-70'>
          <Trans>
            Learned standing heat loss: {learnedLoss} W. The predictor starts from this after a
            restart.
          </Trans>
        </p>
      )}
    </Section>
  );
}
