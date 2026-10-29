import { Trans, useLingui } from '@lingui/react/macro';
import Section from '../../../components/Card.jsx';
import { InputGroupField } from '../../../components/SettingsFormField.jsx';

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
];

const inRangeOrOff = (value, min, max) => {
  const v = Number(value);
  return value === '' || v === 0 || (v >= min && v <= max);
};

export function LagCompensationSection({ formData, onChange }) {
  const { t } = useLingui();
  const labels = {
    derivativeFilterS: t`Derivative filter`,
    integralZone: t`Integral zone`,
    sensorDeadTimeS: t`Sensor dead time`,
    waterDeadTimeS: t`Water dead time`,
    modelHeaterW: t`Heater power`,
    modelHeatRate: t`Full-power heat-up rate`,
    inletTempC: t`Tank water temperature`,
  };
  const help = {
    derivativeFilterS: t`Smooths the D term so thermocouple steps don't switch the heater. Try 2 s; long filters delay its braking.`,
    integralZone: t`Integral only runs within this distance of the target, and pauses while water flows and for the dead time after. Try 1.5 °C.`,
    sensorDeadTimeS: t`Delay from heater on to the temperature starting to rise.`,
    waterDeadTimeS: t`Time from water starting to flow until the temperature has dropped about half way; often shorter than the heater's (about 12 s on a Silvia). 0 uses the sensor dead time.`,
    modelHeaterW: t`Element power, e.g. 1100 W for a 230 V Rancilio Silvia.`,
    modelHeatRate: t`Steady rise at full power, measured mid-warm-up (autotune logs it as k').`,
    inletTempC: t`Temperature of the water drawn in by a shot or flush. A tank beside the boiler warms up; 0 assumes 23 °C.`,
  };
  const learnedLoss = Math.round(Number(formData.modelLossW) || 0);
  return (
    <Section title={t`Boiler Lag Compensation`} className='h-full'>
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
      <label htmlFor='reheatFullPower' className='label mt-4 cursor-pointer justify-start gap-3'>
        <input
          id='reheatFullPower'
          name='reheatFullPower'
          type='checkbox'
          className='toggle toggle-primary'
          checked={!!formData.reheatFullPower}
          onChange={onChange('reheatFullPower')}
        />
        <span>
          <Trans>Reheat at full power after a flush or shot</Trans>
        </span>
      </label>
      <p className='text-sm opacity-70'>
        <Trans>
          Puts back the heat the water took as fast as the element allows, then hands back to the
          PID. Needs heater power, heat-up rate and dead time.
        </Trans>
      </p>
      <label htmlFor='thermalObserver' className='label mt-4 cursor-pointer justify-start gap-3'>
        <input
          id='thermalObserver'
          name='thermalObserver'
          type='checkbox'
          className='toggle toggle-primary'
          checked={!!formData.thermalObserver}
          onChange={onChange('thermalObserver')}
        />
        <span>
          <Trans>Thermal model (experimental)</Trans>
        </span>
      </label>
      <p className='text-sm opacity-70'>
        <Trans>
          Control on a model's estimate of where the water temperature is heading, instead of the
          sensor plus the predictor. Allows a stronger Kp (about 100 on a Silvia) without overshoot.
          Needs heater power, heat-up rate and water dead time.
        </Trans>
      </p>
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
