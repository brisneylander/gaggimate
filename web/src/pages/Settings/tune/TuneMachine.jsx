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
        profile: s.selectedProfile || '',
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

  const startRecording = (seconds, onDone, kind = '') => {
    record.current = { samples: [], until: Date.now() / 1000 + seconds, onDone, kind };
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
  const startTargetFlush = async st => {
    setPhase('target');
    // Wait for the machine's answer: it refuses (with a reason) when a flush to target isn't possible, and then there
    // is no landing to watch. On the first real run it went unanswered and the page watched a flush that never ran.
    let reply;
    try {
      reply = await api.request({ tp: 'req:flush:to-target' });
    } catch (e) {
      reply = { success: false, reason: e.message };
    }
    if (!reply?.success) {
      steamRef.current = null;
      setTargetResult({ error: t`The machine didn't flush: ${reply?.reason || t`no reply`}` });
      setPhase('done');
      return;
    }
    // From the flush's own start (the reading it flushed at), not the earlier peak.
    st.peak = live.ct > 0 ? Math.max(st.peak, live.ct) : st.peak;
    // Accepted should mean water within seconds; if none flows, stop rather than watch a landing that isn't there.
    const acceptedAt = Date.now();
    const flowCheck = setInterval(() => {
      const rec = record.current;
      if (!rec || rec.kind !== 'target') return clearInterval(flowCheck);
      if (rec.samples.some(x => x.fl > 0.5)) return clearInterval(flowCheck);
      if (Date.now() - acceptedAt > 10000) {
        clearInterval(flowCheck);
        record.current = null;
        steamRef.current = null;
        setTargetResult({
          error: t`The machine accepted the flush to target but no water flowed.`,
        });
        setPhase('done');
      }
    }, 1000);
    startRecording(
      TARGET_RECORD_S,
      samples => {
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
      },
      'target',
    );
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
    setNote(t`Applied to the machine profile: Save to keep them.`);
  };

  const busy = phase === 'flushing' || phase === 'steam' || phase === 'peak' || phase === 'target';
  const modelSet = Number(formData.modelHeaterW) > 0 && Number(formData.modelHeatRate) > 0;
  const connected = Number.isFinite(live.ct);
  const stepThreeRunning = phase === 'steam' || phase === 'peak' || phase === 'target';
  // Brew mode and the steady-temperature wait: shown under whichever step is about to start.
  const steadyLine =
    live.mode !== 1 ? (
      <button
        type='button'
        className='btn btn-primary btn-sm'
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
            Waiting for a steady {live.tt} °C ({live.ct?.toFixed(1)} °C now, {Math.round(steadyFor)}{' '}
            of {STEADY_S} s)
          </Trans>
        )}
      </span>
    );
  // Why a step can't start yet (the first unmet condition), or null when it can.
  const commonBlock = () => {
    if (!connected) return t`waiting for the machine's readings`;
    if (!modelSet) return t`set heater power and heat-up rate first (step 1)`;
    if (busy) return t`a test is running`;
    if (!ready) return t`tick the checklist above`;
    if (live.mode !== 1) return t`switch to Brew`;
    if (live.active) return t`wait for the shot or flush to finish`;
    return null;
  };
  const flushBlock =
    commonBlock() ??
    (flushResult ? t`a result is showing: empty the cup and tray, then Run again` : null) ??
    (isSteady ? null : t`waiting for a steady temperature`);
  const targetBlock = commonBlock() ?? (isSteady ? null : t`waiting for a steady temperature`);

  return (
    <Section title={t`Tune this machine`}>
      <p className='mb-4 text-sm opacity-70'>
        <Trans>
          Builds your Machine Boiler Profile from the machine's own readings. Each step measures
          part of it and suggests values: Apply to machine profile, then Save. To keep a copy or
          share it, use Export JSON under Machine → Machine Boiler Profile.
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
            <div className='mt-2 text-sm'>
              <Trans>
                Run Autotune above with the machine cold and the heater wattage from its rating
                plate: it saves the heat-up rate, dead time and heater power.
              </Trans>
            </div>
          )}
        </li>

        <li>
          <strong>
            <Trans>A 5 s flush, logged and weighed</Trans>
          </strong>
          <div className='mt-2 text-sm'>
            <p>
              <Trans>
                This short flush shows how quickly fresh water reaches the temperature sensor, how
                much water the valve drops into the drip tray, and whether the pump reading is
                accurate.
              </Trans>
            </p>
            <ol className='mt-2 list-decimal space-y-1 pl-5'>
              <li>
                <Trans>
                  Select the brew profile you normally use and switch to Brew: the test runs at its
                  temperature.
                </Trans>{' '}
                {live.mode === 1 ? (
                  <Trans>
                    Now: <strong>{live.profile}</strong>, {live.tt} °C.
                  </Trans>
                ) : (
                  <Trans>The machine isn't in Brew mode yet.</Trans>
                )}
              </li>
              <li>
                <Trans>Take the portafilter out and empty the drip tray.</Trans>
              </li>
              <li>
                {live.scale ? (
                  <Trans>
                    Put an empty cup on your Bluetooth scale under the group: it weighs the flush
                    for you.
                  </Trans>
                ) : (
                  <Trans>
                    Put an empty cup under the group. Weigh it first, or tare your kitchen scale
                    with it.
                  </Trans>
                )}
              </li>
              <li>
                <Trans>
                  Tick the box below, wait for a steady temperature, then press Start flush. Leave
                  everything alone for 2 minutes.
                </Trans>
              </li>
              <li>
                {live.scale ? (
                  <Trans>
                    When the results appear, weigh the water in the drip tray and enter it.
                  </Trans>
                ) : (
                  <Trans>
                    When the results appear, weigh the water in the cup and in the drip tray, and
                    enter both.
                  </Trans>
                )}
              </li>
            </ol>
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
            {!flushResult && !stepThreeRunning && steadyLine}{' '}
            {/* One flush per weighing: a second press would add to the cup already being measured. */}
            {flushResult ? (
              <button
                type='button'
                className='btn btn-sm'
                disabled={busy}
                onClick={() => {
                  setFlushResult(null);
                  setCupG('');
                  setTrayG('');
                }}
              >
                <Trans>Run again (empty the cup and drip tray first)</Trans>
              </button>
            ) : (
              <button
                type='button'
                className='btn btn-primary btn-sm'
                disabled={flushBlock !== null}
                onClick={startFlushTest}
              >
                <Trans>Start flush</Trans>
              </button>
            )}
            {flushBlock && !flushResult && <span className='ml-2 opacity-70'>({flushBlock})</span>}
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
              {weighed?.error && cupG !== '' && (
                <p className='text-warning font-bold'>{weighed.error}</p>
              )}
              {weighed && !weighed.error && (
                <p className='font-bold'>
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
              {weighed && !weighed.error && (
                <p className='opacity-70'>
                  <Trans>You can enter the weighed values below.</Trans>
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
              <button
                type='button'
                className='btn btn-sm'
                disabled={!!flushResult.dead.error && !weighed}
                onClick={applyFlush}
              >
                <Trans>Apply to machine profile</Trans>
              </button>
            </div>
          )}
        </li>

        <li>
          <strong>
            <Trans>Flush to target after steam</Trans>
          </strong>
          <div className='mt-2 text-sm'>
            <p>
              <Trans>
                Flush to target works out how much fresh tank water is required to bring the boiler
                back down to the brew temperature. It estimates this from how far over it is, the
                boiler's size and the tank temperature, and flushes the required amount.
              </Trans>{' '}
              <Trans>
                If <strong>Flush to brew target when hot</strong> is toggled on (General → Flush),
                the flush button does this automatically if the boiler is 2 °C or more over target.
              </Trans>{' '}
              {formData.flushButtonToTarget ? (
                <Trans>It's currently toggled on.</Trans>
              ) : (
                <Trans>It's currently toggled off.</Trans>
              )}
            </p>
            <p className='mt-2'>
              <Trans>
                This step checks that sum on your machine. It heats {STEAM_OVERSHOOT_C} °C over the
                brew target in Steam, goes back to Brew, flushes to target at the peak, and watches
                where the boiler lands for {TARGET_RECORD_S / 60} minutes. If it lands too low, the
                flush took out too much heat and the correction factor is lowered; too high, and it
                is raised.
              </Trans>
            </p>
            <ol className='mt-2 list-decimal space-y-1 pl-5'>
              <li>
                <Trans>Save the earlier steps first: the flush uses the saved settings.</Trans>
              </li>
              <li>
                <Trans>
                  Put a jug under the group (about 70-90 ml comes out) and keep the steam wand
                  closed.
                </Trans>
              </li>
              <li>
                <Trans>Press Start and leave the machine alone for about 6 minutes.</Trans>
              </li>
            </ol>
          </div>
          <button
            type='button'
            className='btn btn-primary btn-sm mt-2'
            disabled={targetBlock !== null}
            onClick={startTargetTest}
          >
            <Trans>Start</Trans>
          </button>
          {flushResult && !stepThreeRunning && !targetResult && (
            <div className='mt-2'>{steadyLine}</div>
          )}
          {targetBlock && phase !== 'steam' && phase !== 'peak' && phase !== 'target' && (
            <span className='ml-2 opacity-70'>({targetBlock})</span>
          )}
          {phase === 'steam' && (
            <p className='mt-2'>
              <Trans>
                Heating in Steam to {(steamRef.current?.brewTarget ?? 0) + STEAM_OVERSHOOT_C} °C,
                then back to Brew; it coasts on from there. Now {live.ct?.toFixed(1)} °C…
              </Trans>
            </p>
          )}
          {phase === 'peak' && (
            <p className='mt-2'>
              <Trans>
                Back in Brew, coasting up to its peak: {live.ct?.toFixed(1)} °C (highest so far{' '}
                {steamRef.current?.peak?.toFixed(1)} °C)…
              </Trans>
            </p>
          )}
          {phase === 'target' && (
            <p className='mt-2'>
              <Trans>
                Peaked at {steamRef.current?.peak?.toFixed(1)} °C, flushed to target{' '}
                {steamRef.current?.brewTarget} °C. Watching the landing: {live.ct?.toFixed(1)} °C…
              </Trans>
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
                    onClick={() => {
                      setField('flushTargetFactor', targetResult.factor);
                      setNote(t`Applied to the machine profile: Save to keep them.`);
                    }}
                  >
                    <Trans>Apply to machine profile</Trans>
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
