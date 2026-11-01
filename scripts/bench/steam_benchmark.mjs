// After-steam benchmark (issue #1): steam to 100 °C, back to Brew, wait for the reading to peak, then press the flush
// button (or nothing), and log until settled. Compares stock-style settings with the fork's.
// usage: node steam_benchmark.mjs <out dir> [run names...]
import { writeFileSync, appendFileSync } from "node:fs";

// Optional run names after the out dir pick runs (e.g. A3-silvia-doc-flush-twice); otherwise all except A3.
const [, , outDir, ...only] = process.argv;
const host = process.env.GM_HOST || "192.168.20.34"; // GM_HOST=<display address> to point elsewhere
const base = `http://${host}`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const T0 = Date.now();
const log = (msg) => {
  const line = `${new Date().toISOString().slice(11, 19)} ${msg}`;
  console.log(line);
  appendFileSync(`${outDir}/steam-bench.log`, line + "\n");
};
const OVERSHOOT_TO = 100;
const ABORT_ABOVE = 130; // stock settings coast to ~115 °C after the switch back to Brew
const LOG_AFTER_S = 420;
const HOLD_S = 300;

// Stock-style: everything the fork adds set to 0 (off), flush button does the plain 5 s flush.
const STOCK = {
  derivativeFilterS: 0,
  integralZone: 0,
  sensorDeadTimeS: 0,
  waterDeadTimeS: 0,
  modelHeaterW: 0,
  modelHeatRate: 0,
  valveDumpMl: 0,
  flushButtonToTarget: false,
  flushDuration: 5,
};
const SILVIA_DOC_PID = "112.315,0.658,1436.887,0"; // docs.gaggimate.eu, Rancilio Silvia
const GAGGIMATE_PID = "58.397,1.027,249.055,0"; // firmware default (DEFAULT_PID)

const current = await (await fetch(`${base}/api/settings`)).json();
const KEYS = [
  "pid",
  "temperatureOffset",
  "flushDuration",
  "flushButtonToTarget",
  ...Object.keys(STOCK),
];
const saved = Object.fromEntries(
  [...new Set(KEYS)].map((k) => [k, current[k]]),
);
writeFileSync(
  `${outDir}/steam-bench-saved-settings.json`,
  JSON.stringify(saved, null, 2),
);
const FORK = { ...saved };

const RUNS = [
  // Stock setups press flush twice (flush, then again 15 s after it ends): what an owner does after steam.
  {
    name: "A3-silvia-doc-flush-twice",
    cfg: { ...STOCK, pid: SILVIA_DOC_PID },
    flush: true,
    flushes: 2,
  },
  {
    name: "A2-gaggimate-default-flush-twice",
    cfg: { ...STOCK, pid: GAGGIMATE_PID },
    flush: true,
    flushes: 2,
  },
  { name: "C-fork-flush-to-target", cfg: FORK, flush: true },
  {
    name: "A3-silvia-doc-flush-twice-2",
    cfg: { ...STOCK, pid: SILVIA_DOC_PID },
    flush: true,
    flushes: 2,
  },
  {
    name: "A2-gaggimate-default-flush-twice-2",
    cfg: { ...STOCK, pid: GAGGIMATE_PID },
    flush: true,
    flushes: 2,
  },
  { name: "C-fork-flush-to-target-2", cfg: FORK, flush: true },
  // Your autotuned PID with the compensation off: separates the gains' effect from the compensation's.
  {
    name: "D-tuned-pid-flush-twice",
    cfg: { ...STOCK, pid: saved.pid },
    flush: true,
    flushes: 2,
    extra: true,
  },
  {
    name: "D-tuned-pid-flush-twice-2",
    cfg: { ...STOCK, pid: saved.pid },
    flush: true,
    flushes: 2,
    extra: true,
  },
  // Not in the default set: one press of the stock flush, and a plain wait.
  {
    name: "A1-silvia-doc-flush",
    cfg: { ...STOCK, pid: SILVIA_DOC_PID },
    flush: true,
    extra: true,
  },
  {
    name: "B-silvia-doc-wait",
    cfg: { ...STOCK, pid: SILVIA_DOC_PID },
    flush: false,
    extra: true,
  },
];
const SELECTED = only.length
  ? RUNS.filter((r) => only.includes(r.name))
  : RUNS.filter((r) => !r.extra);

let ws;
let file = null;
let t0 = Date.now();
let mode = "";
let ct = null;
let lastFlow = 0;
let tt = null;
let lastTelemetry = Date.now();
let event = "";
const recent = [];
function connect() {
  ws = new WebSocket(`ws://${host}/ws`);
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.tp !== "evt:status") return;
    if (m.m !== undefined) mode = m.m;
    if (m.ct === undefined) return;
    ct = m.ct;
    tt = m.tt;
    lastFlow = Number(m.fl) || 0;
    lastTelemetry = Date.now();
    recent.push({ at: Date.now(), ct: m.ct });
    while (recent.length && Date.now() - recent[0].at > 90000) recent.shift();
    if (file)
      appendFileSync(
        file,
        `${((Date.now() - t0) / 1000).toFixed(2)},${m.ct},${m.tt},${m.hp},${m.fl},${m.pr},${mode},${event}\n`,
      );
    event = "";
  };
  ws.onclose = () => setTimeout(connect, 2000);
  ws.onerror = () => {};
}
const send = (obj) => ws.readyState === 1 && ws.send(JSON.stringify(obj));
const setMode = (m) => send({ tp: "req:change-mode", mode: m });
// Change mode and confirm it from the status stream, resending every 2 s (a send during a reconnect is lost).
async function confirmMode(m) {
  for (let i = 0; i < 15; i++) {
    setMode(m);
    for (let j = 0; j < 4; j++) {
      await sleep(500);
      if (mode === m) return;
    }
  }
  await bail(`mode ${m} not confirmed after 30 s (still ${mode})`);
}
const post = async (fields) =>
  (
    await fetch(`${base}/api/settings`, {
      method: "POST",
      body: new URLSearchParams(
        Object.entries(fields).map(([k, v]) => [k, String(v)]),
      ),
    })
  ).status;

let restored = false;
async function restore() {
  if (restored) return;
  restored = true;
  setMode(1);
  log(`restoring settings: POST ${await post(saved)}`);
}
async function bail(reason) {
  log(`ABORT: ${reason}`);
  await restore();
  process.exit(1);
}
process.on("SIGINT", () => bail("interrupted"));
setInterval(() => {
  if (ct !== null && ct > ABORT_ABOVE)
    bail(`temperature ${ct} > ${ABORT_ABOVE}`);
  if (Date.now() - lastTelemetry > 60000) bail("no telemetry for 60 s");
}, 2000);

connect();
await sleep(3000);
log(`saved settings: ${JSON.stringify(saved)}`);

for (const run of SELECTED) {
  // Settle on the fork's settings (they hold 93 °C), then switch to the run's settings for the hold test and the run.
  log(`${run.name}: settling on the fork settings, POST ${await post(FORK)}`);
  await confirmMode(1);
  if (ct !== null && ct > 95) {
    log(`${run.name}: ${ct} °C, flushing to target to cool down`);
    await sleep(2000);
    send({ tp: "req:flush:to-target", rid: 2 });
    await sleep(90000);
  }
  file = `${outDir}/steam-${run.name}-approach.csv`;
  writeFileSync(file, "t,ct,tt,hp,fl,pr,m,event" + String.fromCharCode(10));
  t0 = Date.now();
  const nearBy = Date.now() + 20 * 60000;
  recent.length = 0;
  for (;;) {
    await sleep(3000);
    const span = recent.length
      ? recent[recent.length - 1].at - recent[0].at
      : 0;
    if (
      span > 60000 &&
      recent
        .filter((s) => Date.now() - s.at < 60000)
        .every((s) => Math.abs(s.ct - 93) <= 0.5)
    )
      break;
    if (Date.now() > nearBy && Math.abs(ct - 93) <= 1) {
      log(
        `${run.name}: not steady within ±0.5 °C after 20 min (now ${ct}), starting anyway`,
      );
      break;
    }
  }
  log(
    `${run.name}: settled at ${ct}, run settings POST ${await post(run.cfg)}`,
  );
  // Hold test: 5 min of plain idle holding at the brew temperature, logged, so each setup's steadiness is on record.
  file = `${outDir}/steam-${run.name}-hold.csv`;
  writeFileSync(file, "t,ct,tt,hp,fl,pr,m,event\n");
  t0 = Date.now();
  log(`${run.name}: holding test, 5 min from ${ct}`);
  await sleep(HOLD_S * 1000);
  file = null;
  file = `${outDir}/steam-${run.name}.csv`;
  writeFileSync(file, "t,ct,tt,hp,fl,pr,m,event\n");
  t0 = Date.now();
  log(`${run.name}: steady at ${ct}, Steam`);
  event = "steam";
  await confirmMode(2);
  const heatBy = Date.now() + 8 * 60000;
  while (ct === null || ct < OVERSHOOT_TO) {
    if (Date.now() > heatBy) await bail("did not reach 100 °C in 8 min");
    await sleep(500);
  }
  event = "brew";
  await confirmMode(1);
  let peak = ct;
  let sincePeak = 0;
  while (sincePeak < 15) {
    await sleep(1000);
    if (ct > peak + 0.02) {
      peak = ct;
      sincePeak = 0;
    } else sincePeak++;
  }
  if (mode !== 1) await bail(`not in Brew at the peak (mode ${mode})`);
  if (run.flush) {
    event = "flush";
    send({ tp: "req:flush:start", rid: 1 });
    log(`${run.name}: peaked at ${peak.toFixed(2)} °C, pressed flush`);
    for (let n = 2; n <= (run.flushes || 1); n++) {
      // Wait for the flush to end (flow back to 0 for 3 s), then 15 s, then press again.
      await sleep(3000);
      let still = 0;
      while (still < 3) {
        await sleep(1000);
        still = lastFlow > 0.1 ? 0 : still + 1;
      }
      await sleep(15000);
      event = "flush";
      send({ tp: "req:flush:start", rid: 1 });
      log(`${run.name}: flush ${n} at ${ct} °C`);
    }
  } else {
    event = "peak";
    log(`${run.name}: peaked at ${peak.toFixed(2)} °C, waiting (no flush)`);
  }
  await sleep(LOG_AFTER_S * 1000);
  log(`${run.name}: done, ${ct} °C`);
  file = null;
}
await restore();
log(`all runs done in ${((Date.now() - T0) / 60000).toFixed(0)} min`);
process.exit(0);
