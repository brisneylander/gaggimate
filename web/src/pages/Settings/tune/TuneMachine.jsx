import { useContext, useEffect, useRef, useState } from 'preact/hooks';
import { Trans, useLingui } from '@lingui/react/macro';
import { ApiServiceContext } from '../../../services/ApiService.js';
import Section from '../../../components/Card.jsx';
import {
  flushTargetFactor,
  lowestAfterFlush,
  pumpedMl,
  scaleGain,
  scaleCoeffs,
  waterDeadTime,
  weighedFlush,
} from './tuneAnalysis.js';

const STEADY_BAND_C = 0.5;
const STEADY_S = 30;
const FLUSH_RECORD_S = 120;
const PRE_FLUSH_MS = 3000;
const TARGET_RECORD_S = 240;
const STEAM_OVERSHOOT_C = 12; // heat this far over the brew target before the flush-to-target test

const postSettings = async fields => {
  const res = await fetch('/api/settings', { method: 'POST', body: new URLSearchParams(fields) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
};

// Guided measurement of the machine-specific settings: each step logs the machine's own readings and proposes values,
// which go into the settings form only when applied (then Save as usual). The analysis is in tuneAnalysis.js.
export function TuneMachine({ formData, setField }) {
  const { t } = useLingui();
  const api = useContext(ApiServiceContext);
  const [live, setLive] = useState({});
  const [steadySince, setSteadySince] = useState(null);
  const [ready, setReady] = useState(false); // portafilter out, cup on the scale
  const [phase, setPhase] = useState('idle'); // idle | flushing | weigh | steam | peak | target | done
  const [note, setNote] = useState('');
  const [flushResult, setFlushResult] = useState(null);
  const [cupG, setCupG] = useState('');
  const [trayG, setTrayG] = useState('');
  const [targetResult, setTargetResult] = useState(null);
  const record = useRef(null); // { samples, until, onDone }
  const steamRef = useRef(null); // { stage, brewTarget, peak }
  // The status listener is registered once; it reaches the latest form and handlers through these.
  const formRef = useRef(formData);
  formRef.current = formData;
  const startTargetFlushRef = useRef(null);

  useEffect(() => {
    const id = api.on('evt:status', () => {
      const s = api.state.value.status || {};
      const now = Date.now() / 1000;
      const sample = {
        t: now,
        ct: Number(s.currentTemperature),
        fl: Number(s.currentFlow) || 0,
        w: s.bluetoothConnected ? Number(s.bluetoothWeight) : NaN,
      };
      setLive({
        ct: sample.ct,
        tt: Number(s.targetTemperature),
        mode: s.mode,
        fl: sample.fl,
        active: !!s.process?.a,
        scale: !!s.bluetoothConnected,
      });
      const steady =
        s.mode === 1 && Math.abs(sample.ct - Number(s.targetTemperature)) <= STEADY_BAND_C;
      setSteadySince(prev => (steady ? (prev ?? now) : null));
      const rec = record.current;
      if (rec) {
        rec.samples.push(sample);
        if (now >= rec.until) {
          record.current = null;
          rec.onDone(rec.samples);
        }
      }
      const st = steamRef.current;
      if (st && st.stage === 'steam' && sample.ct >= st.brewTarget + STEAM_OVERSHOOT_C) {
        st.stage = 'peak';
        st.peak = sample.ct;
        api.send({ tp: 'req:change-mode', mode: 1 });
        setPhase('peak');
      } else if (st && st.stage === 'peak') {
        if (sample.ct > st.peak) st.peak = sample.ct;
        else if (sample.ct <= st.peak - 0.2 && s.mode === 1) {
          st.stage = 'target';
          startTargetFlushRef.current?.(st);
        }
      }
    });
    return () => api.off('evt:status', id);
  }, [api]);

  const now = Date.now() / 1000;
  const steadyFor = steadySince ? now - steadySince : 0;
  const isSteady = steadyFor >= STEADY_S;

  const startRecording = (seconds, onDone) => {
    record.current = { samples: [], until: Date.now() / 1000 + seconds, onDone };
  };

  // Step 2: a 5 s flush the machine runs itself (the dead-time rule was checked on those), then weigh it.
  const startFlushTest = async () => {
    const previous = formData.flushDuration;
    try {
      await postSettings({ flushDuration: 5 });
    } catch (e) {
      setNote(t`Could not set the flush duration: ${e.message}`);
      return;
    }
    setFlushResult(null);
    setPhase('flushing');
    startRecording(FLUSH_RECORD_S, async samples => {
      try {
        await postSettings({ flushDuration: previous ?? 5 });
      } catch {
        /* the form still holds the old value; Save restores it */
      }
      const dead = waterDeadTime(samples);
      // With a paired Bluetooth scale under the cup it weighs the flush; without one the weight is typed in.
      const weighedByScale = scaleGain(samples);
      setFlushResult({ dead, readingMl: pumpedMl(samples), weighedByScale });
      if (weighedByScale !== null) setCupG(String(weighedByScale));
      setPhase('weigh');
    });
    // A few seconds of readings first: the scale's starting weight (and the temperature it falls from).
    setTimeout(() => api.send({ tp: 'req:flush:start' }), PRE_FLUSH_MS);
  };

  // Step 3: steam up, back to Brew, flush to target at the peak, watch the landing.
  const startTargetTest = () => {
    setTargetResult(null);
    steamRef.current = { stage: 'steam', brewTarget: live.tt, peak: -Infinity };
    api.send({ tp: 'req:change-mode', mode: 2 });
    setPhase('steam');
  };
  const startTargetFlush = st => {
    setPhase('target');
    startRecording(TARGET_RECORD_S, samples => {
      steamRef.current = null;
      const low = lowestAfterFlush(samples);
      setTargetResult(
        low
          ? {
              ...flushTargetFactor({
                startC: st.peak,
                targetC: st.brewTarget,
                lowC: low.lowC,
                factor: Number(formRef.current.flushTargetFactor) || 1,
              }),
              startC: st.peak,
              lowC: low.lowC,
            }
          : { error: t`no flush seen` },
      );
      setPhase('done');
    });
    api.send({ tp: 'req:flush:to-target' });
  };
  startTargetFlushRef.current = startTargetFlush;

  const weighed =
    flushResult && cupG !== ''
      ? weighedFlush({
          cupG: Number(cupG),
          trayG: Number(trayG) || 0,
          readingMl: flushResult.readingMl,
        })
      : null;

  const applyFlush = () => {
    if (flushResult?.dead?.seconds)
      setField('waterDeadTimeS', Math.round(flushResult.dead.seconds));
    if (weighed && !weighed.error) {
      setField('valveDumpMl', weighed.valveDumpMl);
      if (weighed.pumpNeedsScaling && formData.pumpModelCoeffs)
        setField('pumpModelCoeffs', scaleCoeffs(formData.pumpModelCoeffs, weighed.pumpRatio));
    }
    setNote(t`Applied to the form: Save to keep them.`);
  };

  const busy = phase === 'flushing' || phase === 'steam' || phase === 'peak' || phase === 'target';
  const modelSet = Number(formData.modelHeaterW) > 0 && Number(formData.modelHeatRate) > 0;

  return (
    <Section title={t`Tune this machine`}>
      <p className='mb-4 text-sm opacity-70'>
        <Trans>
          Measures the machine-specific settings from the machine's own readings. Each step proposes
          values; apply them to the form, then Save. Export the result as a machine profile from
          Machine → Boiler Lag Compensation.
        </Trans>
      </p>

      <ol className='list-decimal space-y-4 pl-5 text-sm'>
        <li>
          <strong>
            <Trans>Autotune from a cold boiler</Trans>
          </strong>{' '}
          {modelSet ? (
            <Trans>
              done: {formData.modelHeaterW} W, {formData.modelHeatRate} °C/s, dead time{' '}
              {formData.sensorDeadTimeS} s.
            </Trans>
          ) : (
            <Trans>
              Run Autotune above with the machine cold and the heater wattage from its rating plate:
              it saves the heat-up rate, dead time and heater power.
            </Trans>
          )}
        </li>

        <li>
          <strong>
            <Trans>A 5 s flush, logged and weighed</Trans>
          </strong>
          <div className='mt-1 opacity-70'>
            <Trans>
              Measures how soon flush water reaches the sensor, the water the valve dumps into the
              drip tray, and checks the pump calibration. Portafilter out, an empty cup under the
              group, drip tray emptied.
            </Trans>{' '}
            {live.scale ? (
              <Trans>
                A Bluetooth scale is connected: put the cup on it and it weighs the flush; only the
                drip tray needs weighing by hand.
              </Trans>
            ) : (
              <Trans>
                Weigh the cup and drip tray afterwards (or pair a Bluetooth scale and it weighs the
                cup).
              </Trans>
            )}
          </div>
          <label className='mt-2 flex items-center gap-2'>
            <input
              type='checkbox'
              className='checkbox checkbox-sm'
              checked={ready}
              onChange={e => setReady(e.target.checked)}
            />
            <Trans>Portafilter out, cup in place, drip tray empty</Trans>
          </label>
          <div className='mt-2'>
            {live.mode !== 1 ? (
              <button
                type='button'
                className='btn btn-sm'
                onClick={() => api.send({ tp: 'req:change-mode', mode: 1 })}
              >
                <Trans>Switch to Brew</Trans>
              </button>
            ) : (
              <span className='opacity-70'>
                {isSteady ? (
                  <Trans>Steady at {live.ct?.toFixed(1)} °C</Trans>
                ) : (
                  <Trans>
                    Waiting for a steady {live.tt} °C ({live.ct?.toFixed(1)} °C now,{' '}
                    {Math.round(steadyFor)} of {STEADY_S} s)
                  </Trans>
                )}
              </span>
            )}{' '}
            <button
              type='button'
              className='btn btn-primary btn-sm'
              disabled={!ready || !isSteady || busy || !modelSet}
              onClick={startFlushTest}
            >
              <Trans>Start flush</Trans>
            </button>
          </div>
          {phase === 'flushing' && (
            <p className='mt-2'>
              <Trans>Flushing and logging for {FLUSH_RECORD_S} s…</Trans>
            </p>
          )}
          {flushResult && (
            <div className='mt-2 space-y-2'>
              {flushResult.dead.error ? (
                <p className='text-error'>{flushResult.dead.error}</p>
              ) : (
                <p>
                  <Trans>
                    Water dead time {flushResult.dead.seconds.toFixed(1)} s (dip{' '}
                    {flushResult.dead.depth.toFixed(1)} °C). Pump reading{' '}
                    {flushResult.readingMl.toFixed(1)} ml.
                  </Trans>
                </p>
              )}
              {flushResult.weighedByScale !== null && (
                <p>
                  <Trans>The scale weighed {flushResult.weighedByScale} g in the cup.</Trans>
                </p>
              )}
              <div className='flex flex-wrap items-center gap-2'>
                <label className='flex items-center gap-1'>
                  <Trans>Water in the cup (g)</Trans>
                  <input
                    type='number'
                    step='0.1'
                    className='input input-bordered input-sm w-24'
                    value={cupG}
                    onInput={e => setCupG(e.target.value)}
                  />
                </label>
                <label className='flex items-center gap-1'>
                  <Trans>Drip tray (g)</Trans>
                  <input
                    type='number'
                    step='0.1'
                    className='input input-bordered input-sm w-24'
                    value={trayG}
                    onInput={e => setTrayG(e.target.value)}
                  />
                </label>
              </div>
              {weighed && !weighed.error && (
                <p>
                  <Trans>
                    Valve dump {weighed.valveDumpMl} ml. The cup got{' '}
                    {(weighed.pumpRatio * 100).toFixed(0)} % of the pump reading
                  </Trans>
                  {weighed.pumpNeedsScaling ? (
                    <Trans>: the pump coefficients will be scaled to match.</Trans>
                  ) : (
                    <Trans>: the pump calibration is fine.</Trans>
                  )}
                </p>
              )}
              <button
                type='button'
                className='btn btn-sm'
                disabled={!!flushResult.dead.error && !weighed}
                onClick={applyFlush}
              >
                <Trans>Apply to the form</Trans>
              </button>
            </div>
          )}
        </li>

        <li>
          <strong>
            <Trans>Flush to target after steam</Trans>
          </strong>
          <div className='mt-1 opacity-70'>
            <Trans>
              Heats {STEAM_OVERSHOOT_C} °C over the brew target in Steam, returns to Brew, flushes
              to target at the peak and watches where the boiler lands for {TARGET_RECORD_S / 60}{' '}
              minutes. Save the earlier steps first: the flush uses the saved settings.
            </Trans>
          </div>
          <button
            type='button'
            className='btn btn-primary btn-sm mt-2'
            disabled={!ready || busy || live.mode !== 1 || !modelSet}
            onClick={startTargetTest}
          >
            <Trans>Start</Trans>
          </button>
          {phase === 'steam' && (
            <p className='mt-2'>
              <Trans>Heating in Steam: {live.ct?.toFixed(1)} °C…</Trans>
            </p>
          )}
          {phase === 'peak' && (
            <p className='mt-2'>
              <Trans>Back in Brew, waiting for the peak: {live.ct?.toFixed(1)} °C…</Trans>
            </p>
          )}
          {phase === 'target' && (
            <p className='mt-2'>
              <Trans>Flushed to target; watching the landing: {live.ct?.toFixed(1)} °C…</Trans>
            </p>
          )}
          {targetResult && (
            <div className='mt-2 space-y-2'>
              {targetResult.error ? (
                <p className='text-error'>{targetResult.error}</p>
              ) : (
                <>
                  <p>
                    <Trans>
                      From {targetResult.startC.toFixed(1)} °C it landed at{' '}
                      {targetResult.lowC.toFixed(1)} °C ({targetResult.landedC >= 0 ? '+' : ''}
                      {targetResult.landedC.toFixed(1)} °C). Suggested flush to target factor:{' '}
                      {targetResult.factor}.
                    </Trans>
                  </p>
                  <button
                    type='button'
                    className='btn btn-sm'
                    onClick={() => setField('flushTargetFactor', targetResult.factor)}
                  >
                    <Trans>Apply to the form</Trans>
                  </button>
                </>
              )}
            </div>
          )}
        </li>
      </ol>
      {note && <p className='mt-4 text-sm'>{note}</p>}
    </Section>
  );
}
