import { Trans, useLingui } from '@lingui/react/macro';
import Section from '../../../components/Card.jsx';
import { InputGroupField } from '../../../components/SettingsFormField.jsx';

// Brew-boiler lag compensation; every field 0 keeps the plain PID loop.
const FIELDS = [
  { key: 'derivativeFilterS', unit: 's', step: '0.5', min: '0', max: '60' },
  { key: 'integralZone', unit: '°C', step: '0.1', min: '0', max: '20' },
  { key: 'sensorDeadTimeS', unit: 's', step: '1', min: '0', max: '120' },
];

export function LagCompensationSection({ formData, onChange }) {
  const { t } = useLingui();
  const labels = {
    derivativeFilterS: t`Derivative filter`,
    integralZone: t`Integral zone`,
    sensorDeadTimeS: t`Sensor dead time`,
  };
  const help = {
    derivativeFilterS: t`Smooths the D term so thermocouple steps don't switch the heater. Try 2 s; long filters delay its braking.`,
    integralZone: t`Integral only runs within this distance of the target, and pauses while water flows and for the dead time after. Try 1.5 °C.`,
    sensorDeadTimeS: t`Delay from heater on to the temperature starting to rise.`,
  };
  return (
    <Section title={t`Boiler Lag Compensation`} className='h-full'>
      <p className='mb-4 text-sm opacity-70'>
        <Trans>
          For boilers where the temperature sensor reacts long after the heater. Leave a field at 0
          to disable it.
        </Trans>
      </p>
      <div className='grid grid-cols-1 gap-4 md:grid-cols-2'>
        {FIELDS.map(({ key, unit, step, min, max }) => (
          <InputGroupField
            key={key}
            label={labels[key]}
            htmlFor={key}
            unit={unit}
            helpText={help[key]}
            noMargin
          >
            <input
              id={key}
              name={key}
              type='number'
              step={step}
              min={min}
              max={max}
              className='grow'
              placeholder='0'
              value={formData[key] ?? 0}
              onChange={onChange(key)}
            />
          </InputGroupField>
        ))}
      </div>
    </Section>
  );
}
