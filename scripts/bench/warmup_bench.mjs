// Unattended warm-up bench: waits for the boiler to cool to room temperature in Standby, wakes it to Brew, logs the
// warm-up, then puts it back in Standby, rotating group head estimate settings between runs. Runs for HOURS, then
// restores the settings and exits. Meant to run beside the logger on the NAS (node, no dependencies).
//
// Normal use doesn't upset it: while it waits, any brew just warms the boiler and the wait starts over; during a run,
// someone else changing the mode, water flowing or a shot starting marks the run "disturbed", restores the settings
// and leaves the machine alone (no Standby). No run starts in the morning window (the 6:45 wake-up and first brews).
//
// usage: node warmup_bench.mjs <out dir>   env: GM_HOST, HOURS (24), RUN_MIN (25), QUIET (05:30-09:00)
import { appendFileSync, writeFileSync } from "node:fs";

const [, , outDir = "."] = process.argv;
const HOST = process.env.GM_HOST || "192.168.20.34";
const BASE = `http://${HOST}`;
const HOURS = Number(process.env.HOURS || 24);
const RUN_MIN = Number(process.env.RUN_MIN || 25);
const [QUIET_FROM, QUIET_TO] = (process.env.QUIET || "05:30-09:00").split("-");
const TZ = process.env.TZ || "Australia/Brisbane";
// Cooled: in Standby, under COLD_MAX and falling less than COLD_RATE °C over the last 30 min (with a ~70 min time
// constant, that's within ~2.5 °C of the room). After 8 h of waiting, a looser rate will do (a warm afternoon room).
const COLD_MAX = 35;
const COLD_RATE = 1.0;
const COLD_RATE_LATE = 1.5;
const VARIANTS = [
  { name: "group-1343-4.3", groupHeatCapacity: 1343, groupCoupling: 4.3 },
  { name: "group-1343-3.0", groupHeatCapacity: 1343, groupCoupling: 3.0 },
  { name: "group-off", groupHeatCapacity: 0, groupCoupling: 0 },
];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const hhmm = (d = new Date()) =>
  new Intl.DateTimeFormat("en-GB", {
    timeZone: TZ,
    hour: "2-digit",
    minute: "2-digit",
    hour12: false,
  }).format(d);
const stamp = () => new Date().toLocaleString("sv-SE", { timeZone: TZ });
const log = (msg) => {
  const line = `${stamp()} ${msg}`;
  console.log(line);
  appendFileSync(`${outDir}/warmup-bench.log`, line + "\n");
};
const inQuiet = () => {
  const t = hhmm();
  return t >= QUIET_FROM && t < QUIET_TO;
};
const post = async (fields) => {
  try {
    const r = await fetch(`${BASE}/api/settings`, {
      method: "POST",
      body: new URLSearchParams(
        Object.entries(fields).map(([k, v]) => [k, String(v)]),
      ),
      signal: AbortSignal.timeout(10000),
    });
    return r.status;
  } catch (e) {
    return e.message;
  }
};
const getSettings = async () =>
  (
    await fetch(`${BASE}/api/settings`, { signal: AbortSignal.timeout(10000) })
  ).json();

// Telemetry over the websocket; reconnects when it goes quiet (the display drops its oldest client when another
// connects, sometimes without closing).
let ws = null;
let last = { ct: null, tt: null, hp: 0, fl: 0, mode: null, active: false };
let lastMs = 0;
const history = []; // {at, ct, mode} for the cooling check
let runFile = null;
let runT0 = 0;
function connect() {
  try {
    ws = new WebSocket(`ws://${HOST}/ws`);
  } catch {
    return;
  }
  ws.onmessage = (e) => {
    let m;
    try {
      m = JSON.parse(e.data);
    } catch {
      return;
    }
    if (m.tp !== "evt:status") return;
    if (m.m !== undefined) last.mode = m.m;
    if (m.process !== undefined) last.active = !!m.process?.a;
    if (m.ct === undefined) return;
    Object.assign(last, {
      ct: m.ct,
      tt: m.tt,
      hp: Number(m.hp) || 0,
      fl: Number(m.fl) || 0,
    });
    lastMs = Date.now();
    if (!history.length || Date.now() - history[history.length - 1].at > 30000)
      history.push({ at: Date.now(), ct: m.ct, mode: last.mode });
    while (history.length && Date.now() - history[0].at > 45 * 60000)
      history.shift();
    if (runFile)
      appendFileSync(
        runFile,
        `${((Date.now() - runT0) / 1000).toFixed(1)},${m.ct},${m.tt},${m.hp},${m.fl},${last.mode},${last.active ? 1 : 0}\n`,
      );
  };
  ws.onclose = () => setTimeout(connect, 5000);
  ws.onerror = () => {};
}
setInterval(() => {
  if (ws && ws.readyState === 1 && Date.now() - lastMs > 20000) {
    try {
      ws.close();
    } catch {}
  }
}, 5000);
const send = (o) => ws && ws.readyState === 1 && ws.send(JSON.stringify(o));
async function setMode(m) {
  for (let i = 0; i < 15; i++) {
    send({ tp: "req:change-mode", mode: m });
    for (let j = 0; j < 4; j++) {
      await sleep(500);
      if (last.mode === m) return true;
    }
  }
  return false;
}

function cooled(waitedMs) {
  if (last.mode !== 0 || last.ct === null || last.ct > COLD_MAX) return false;
  const old = history.find((h) => Date.now() - h.at <= 31 * 60000);
  if (!old || Date.now() - old.at < 29 * 60000) return false;
  if (history.some((h) => h.at >= old.at && h.mode !== 0)) return false; // woken in the last 30 min
  const rate = old.ct - last.ct;
  return (
    rate >= 0 && rate < (waitedMs > 8 * 3600000 ? COLD_RATE_LATE : COLD_RATE)
  );
}

function summarise(rows, sp) {
  // rows: [t s, ct]; sp: shown target. Returns the warm-up's landmarks.
  const at = (i) => rows[i][0] / 60;
  const iReach = rows.findIndex((r) => r[1] >= sp - 0.5);
  if (iReach < 0) return { reached: false };
  let iPk = iReach;
  for (
    let i = iReach;
    i < rows.length && rows[i][0] - rows[iReach][0] < 240;
    i++
  )
    if (rows[i][1] > rows[iPk][1]) iPk = i;
  let iSag = iPk;
  for (let i = iPk; i < rows.length; i++)
    if (rows[i][1] < rows[iSag][1]) iSag = i;
  let iPk2 = iSag;
  for (let i = iSag; i < rows.length; i++)
    if (rows[i][1] > rows[iPk2][1]) iPk2 = i;
  const lastOut = (band) => {
    let k = -1;
    for (let i = 0; i < rows.length; i++)
      if (Math.abs(rows[i][1] - sp) > band) k = i;
    return k < 0 ? 0 : +at(k).toFixed(1);
  };
  return {
    startC: rows[0][1],
    reachMin: +at(iReach).toFixed(1),
    peak: +(rows[iPk][1] - sp).toFixed(2),
    peakMin: +at(iPk).toFixed(1),
    sag: +(rows[iSag][1] - sp).toFixed(2),
    sagMin: +at(iSag).toFixed(1),
    secondPeak: +(rows[iPk2][1] - sp).toFixed(2),
    secondPeakMin: +at(iPk2).toFixed(1),
    within1Min: lastOut(1.0),
    within05Min: lastOut(0.5),
    within03Min: lastOut(0.3),
  };
}

const started = Date.now();
const endAt = started + HOURS * 3600000;
const original = await getSettings();
const restoreFields = {
  groupHeatCapacity: original.groupHeatCapacity,
  groupCoupling: original.groupCoupling,
};
writeFileSync(
  `${outDir}/warmup-bench-original.json`,
  JSON.stringify(restoreFields),
);
log(
  `start: ${HOURS} h, variants ${VARIANTS.map((v) => v.name).join(", ")}, original ${JSON.stringify(restoreFields)}`,
);
connect();
let shuttingDown = false;
async function shutdown(why) {
  if (shuttingDown) return;
  shuttingDown = true;
  log(
    `${why}: restoring ${JSON.stringify(restoreFields)} -> ${await post(restoreFields)}`,
  );
  process.exit(0);
}
process.on("SIGTERM", () => shutdown("stopped"));
process.on("SIGINT", () => shutdown("stopped"));

let run = 0;
let waitStart = Date.now();
let lastNote = 0;
while (Date.now() < endAt) {
  await sleep(10000);
  if (Date.now() - lastMs > 120000) {
    if (Date.now() - lastNote > 30 * 60000) {
      log("no telemetry for 2 min (display off or busy), waiting");
      lastNote = Date.now();
    }
    continue;
  }
  if (Date.now() - lastNote > 60 * 60000) {
    log(`waiting: mode ${last.mode}, ${last.ct} °C`);
    lastNote = Date.now();
  }
  if (!cooled(Date.now() - waitStart) || inQuiet()) continue;
  if (Date.now() + RUN_MIN * 60000 > endAt) break;

  // A run.
  const v = VARIANTS[run % VARIANTS.length];
  run++;
  const name = `warmup-${run}-${v.name}`;
  log(
    `${name}: cooled to ${last.ct} °C, settings ${JSON.stringify(v)} -> ${await post(v)}`,
  );
  await sleep(5000);
  runFile = `${outDir}/${name}.csv`;
  writeFileSync(runFile, "t,ct,tt,hp,fl,mode,active\n");
  runT0 = Date.now();
  if (!(await setMode(1))) {
    log(`${name}: couldn't switch to Brew, skipping`);
    runFile = null;
    await post(restoreFields);
    waitStart = Date.now();
    continue;
  }
  const rows = [];
  let disturbed = null;
  let sp = null;
  while (Date.now() - runT0 < RUN_MIN * 60000) {
    await sleep(1000);
    if (last.ct === null) continue;
    sp = sp ?? last.tt;
    rows.push([(Date.now() - runT0) / 1000, last.ct]);
    if (last.mode !== 1) disturbed = `mode changed to ${last.mode}`;
    else if (last.fl > 0.2) disturbed = `water flowing (${last.fl} ml/s)`;
    else if (last.active) disturbed = "a process started";
    else if (last.tt !== sp) disturbed = `target changed to ${last.tt}`;
    if (disturbed) break;
  }
  runFile = null;
  log(
    `${name}: restoring ${JSON.stringify(restoreFields)} -> ${await post(restoreFields)}`,
  );
  const result = {
    run,
    variant: v.name,
    at: stamp(),
    disturbed,
    target: sp,
    ...summarise(rows, sp),
  };
  appendFileSync(
    `${outDir}/warmup-bench-results.jsonl`,
    JSON.stringify(result) + "\n",
  );
  log(`${name}: ${JSON.stringify(result)}`);
  if (!disturbed)
    log(
      `${name}: back to Standby -> ${(await setMode(0)) ? "ok" : "not confirmed"}`,
    );
  else log(`${name}: disturbed (${disturbed}), leaving the machine as it is`);
  waitStart = Date.now();
  history.length = 0; // the next run needs a fresh 30 min of cooling history
}
await shutdown(`done after ${run} runs`);
