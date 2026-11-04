// GaggiMate telemetry logger: keeps a websocket open to the display and writes everything it reports to daily CSV
// files, for tuning the boiler over weeks of normal use. Runs as a container on the NAS (compose.yml beside this file).
//
//   - While the machine is on: every status frame (temperature and target, heater %, pressure, flow, pump, weight, mode,
//     shot phase), a few per second. In Standby: one row a minute (still shows the overnight cool-down).
//   - Events (evt:* other than status, e.g. shot finished) as rows with the event name.
//   - The settings (/api/settings) whenever they change, to settings-<date>.json, so each day's log has the PID and
//     boiler profile it ran with.
//   - One file per Brisbane date; files older than KEEP_PLAIN_DAYS are gzipped.
//   - valve-timing.csv: one row per shot or flush start, how long the valve, the pressure and the flow took to respond.
//
// The display serves at most 3 websocket clients and drops the oldest when a 4th connects. If this logger keeps being
// dropped (a phone and a browser already connected), it backs off for BACKOFF_MS instead of fighting them.
import {
  appendFileSync,
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  writeFileSync,
  unlinkSync,
  createReadStream,
  createWriteStream,
} from "node:fs";
import { createGzip } from "node:zlib";
import { pipeline } from "node:stream/promises";
import { join } from "node:path";

const HOST = process.env.GM_HOST || "192.168.20.34";
const DIR = process.env.LOG_DIR || "/logs";
const TZ = process.env.TZ || "Australia/Brisbane";
const STANDBY_EVERY_MS = 60000;
const RECONNECT_MS = 30000;
const BACKOFF_MS = 5 * 60000;
const KEEP_PLAIN_DAYS = 2;
const SETTINGS_EVERY_MS = 10 * 60000;
const OPEN_TIMEOUT_MS = 20000;
// The display sends status about twice a second, Standby included: this long without any means the connection is stuck
// (on 2026-11-02 a reconnect attempt hung, neither opening nor failing, and logged nothing for 11 h). Exit, and Docker's
// restart policy starts afresh.
const STALL_EXIT_MS = 5 * 60000;
let lastStatusMs = Date.now();

const COLUMNS = [
  "time",
  "unix_ms",
  "ct",
  "tt",
  "hp",
  "pr",
  "pt",
  "fl",
  "tf",
  "cw",
  "pw",
  "mode",
  "active",
  "state",
  "phase",
  "event",
  "vo",
  "rtx",
  "lat",
];
const VALVE_FILE = "valve-timing.csv";
const VALVE_COLUMNS = [
  "time",
  "phase",
  "ct",
  "pr_start",
  "valve_s",
  "release_s",
  "flow_s",
  "rtx_added",
  "lat_ms",
];
const VALVE_WATCH_S = 15;
const PRESSURE_RELEASED = 1.5; // bar fallen, or the start pressure too low to count
mkdirSync(DIR, { recursive: true });

const dateFmt = new Intl.DateTimeFormat("en-CA", {
  timeZone: TZ,
  year: "numeric",
  month: "2-digit",
  day: "2-digit",
});
const timeFmt = new Intl.DateTimeFormat("en-GB", {
  timeZone: TZ,
  hour: "2-digit",
  minute: "2-digit",
  second: "2-digit",
  hour12: false,
});
const localDate = (d) => dateFmt.format(d); // YYYY-MM-DD
const localTime = (d) =>
  `${localDate(d)}T${timeFmt.format(d)}.${String(d.getMilliseconds()).padStart(3, "0")}`;
const log = (msg) => console.log(`${localTime(new Date())} ${msg}`);

function write(fields) {
  const now = new Date();
  const file = join(DIR, `${localDate(now)}.csv`);
  if (!existsSync(file)) writeFileSync(file, COLUMNS.join(",") + "\n");
  const row = COLUMNS.map((c) => {
    if (c === "time") return localTime(now);
    if (c === "unix_ms") return now.getTime();
    const v = fields[c];
    return v === undefined || v === null
      ? ""
      : String(v).replace(/[,\n]/g, ";");
  });
  appendFileSync(file, row.join(",") + "\n");
}

// Shot or flush starts. On 2026-11-05 a shot started with 7.2 bar trapped behind the valve and the pressure held for 5.4 s
// where it normally goes in 0.5 s. valve_s late means the command was slow reaching the controller (rtx_added says if the
// link had to resend); valve_s on time with release_s late means the valve itself. Times are as frames arrive: ±0.5 s.
let valveStart = null;
let wasActive = false;
let lastPressure = 0;
function closeValveRow(m) {
  const v = valveStart;
  valveStart = null;
  const file = join(DIR, VALVE_FILE);
  if (!existsSync(file)) writeFileSync(file, VALVE_COLUMNS.join(",") + "\n");
  const fmt = (s) => (s === undefined ? "" : s.toFixed(2));
  appendFileSync(
    file,
    [
      v.time,
      String(v.phase).replace(/[,\n]/g, ";"),
      v.ct,
      v.pr,
      fmt(v.valveS),
      fmt(v.releaseS),
      fmt(v.flowS),
      m.rtx === undefined ? "" : m.rtx - v.rtx,
      m.lat ?? "",
    ].join(",") + "\n",
  );
}
function watchValve(m) {
  const now = Date.now();
  const active = !!state.active;
  if (active && !wasActive && state.mode === 1)
    valveStart = {
      at: now,
      time: localTime(new Date(now)),
      phase: state.phase,
      ct: m.ct,
      pr: lastPressure,
      rtx: m.rtx,
    };
  wasActive = active;
  lastPressure = m.pr;
  if (!valveStart) return;
  const v = valveStart;
  const s = (now - v.at) / 1000;
  if (m.vo && v.valveS === undefined) v.valveS = s;
  if (m.fl > 0.5 && v.flowS === undefined) v.flowS = s;
  if (m.pr < v.pr - PRESSURE_RELEASED && v.releaseS === undefined)
    v.releaseS = s;
  const released = v.pr < PRESSURE_RELEASED || v.releaseS !== undefined;
  const valveSeen = m.vo === undefined || v.valveS !== undefined;
  if (
    (released && valveSeen && v.flowS !== undefined) ||
    s > VALVE_WATCH_S ||
    !active
  )
    closeValveRow(m);
}

// Status frames are partial (fast telemetry every tick, slow state only on change): keep the latest of each.
const state = { mode: "", active: "", state: "", phase: "" };
let lastStandbyRow = 0;
function onStatus(m) {
  if (m.m !== undefined && m.m !== state.mode) {
    state.mode = m.m;
    write({ ...state, event: "mode" });
  }
  if (m.process !== undefined) {
    const p = m.process || {};
    state.active = p.a ? 1 : 0;
    state.state = p.s || "";
    state.phase = p.l || "";
  }
  if (m.ct === undefined) return;
  watchValve(m);
  const idle = state.mode === 0 && !state.active;
  const now = Date.now();
  if (idle && now - lastStandbyRow < STANDBY_EVERY_MS) return;
  lastStandbyRow = now;
  write({
    ct: m.ct,
    tt: m.tt,
    hp: m.hp,
    pr: m.pr,
    pt: m.pt,
    fl: m.fl,
    tf: m.tf,
    cw: m.cw,
    pw: m.pw,
    vo: m.vo,
    rtx: m.rtx,
    lat: m.lat,
    ...state,
  });
}

// Settings: saved when they change, so a day's log can be read against the PID and boiler profile in force.
let lastSettings = "";
// These drift on their own (the tank estimate every few minutes, the learned loss, the shot counters): saved with each
// snapshot, but not a reason for one.
const VOLATILE = [
  "tankEstimateC",
  "modelLossW",
  "shotsSinceBackflush",
  "shotsSinceDescaling",
  "lastBackflushTime",
  "lastDescalingTime",
];
async function snapshotSettings() {
  try {
    const res = await fetch(`http://${HOST}/api/settings`, {
      signal: AbortSignal.timeout(10000),
    });
    if (!res.ok) return;
    const settings = await res.json();
    const text = JSON.stringify(
      Object.fromEntries(
        Object.entries(settings).filter(([k]) => !VOLATILE.includes(k)),
      ),
    );
    if (text === lastSettings) return;
    lastSettings = text;
    appendFileSync(
      join(DIR, `settings-${localDate(new Date())}.json`),
      `${JSON.stringify({ time: localTime(new Date()), settings })}\n`,
    );
    log("settings changed, snapshot saved");
  } catch {
    // machine off or unreachable: try again next time
  }
}

async function compressOld() {
  const cutoff = localDate(new Date(Date.now() - KEEP_PLAIN_DAYS * 86400000));
  for (const name of readdirSync(DIR)) {
    const m = name.match(/^(?:settings-)?(\d{4}-\d{2}-\d{2})\.(csv|json)$/);
    if (!m || m[1] >= cutoff) continue;
    const path = join(DIR, name);
    try {
      await pipeline(
        createReadStream(path),
        createGzip(),
        createWriteStream(`${path}.gz`),
      );
      unlinkSync(path);
      log(`compressed ${name}`);
    } catch (e) {
      log(`compress ${name} failed: ${e.message}`);
    }
  }
}

let drops = [];
function connect() {
  const ws = new WebSocket(`ws://${HOST}/ws`);
  let opened = false;
  let retried = false;
  // A connection attempt that neither opens nor fails: give up on it and try again.
  const openTimer = setTimeout(() => {
    if (opened || retried) return;
    retried = true;
    log(`no connection after ${OPEN_TIMEOUT_MS / 1000} s; retrying`);
    try {
      ws.close();
    } catch {}
    setTimeout(connect, RECONNECT_MS);
  }, OPEN_TIMEOUT_MS);
  ws.onopen = () => {
    opened = true;
    clearTimeout(openTimer);
    log(`connected to ${HOST}`);
    write({ ...state, event: "connect" });
    snapshotSettings();
  };
  ws.onmessage = (e) => {
    let m;
    try {
      m = JSON.parse(e.data);
    } catch {
      return;
    }
    if (m.tp === "evt:status") {
      lastStatusMs = Date.now();
      onStatus(m);
    } else if (typeof m.tp === "string" && m.tp.startsWith("evt:"))
      write({ ...state, event: m.tp });
  };
  ws.onerror = () => {};
  ws.onclose = () => {
    clearTimeout(openTimer);
    if (retried) return; // the open timeout already scheduled the retry
    retried = true;
    const now = Date.now();
    if (opened) {
      write({ ...state, event: "disconnect" });
      drops = drops.filter((t) => now - t < 10 * 60000).concat(now);
    }
    // Dropped three times in 10 minutes: other clients want the display's slots; give way for a while.
    const wait = drops.length >= 3 ? BACKOFF_MS : RECONNECT_MS;
    if (wait === BACKOFF_MS) drops = [];
    if (opened) log(`disconnected; retrying in ${wait / 1000} s`);
    setTimeout(connect, wait);
  };
}

log(`GaggiMate logger: ${HOST} -> ${DIR} (${TZ})`);
await compressOld();
setInterval(compressOld, 6 * 3600000);
setInterval(snapshotSettings, SETTINGS_EVERY_MS);
setInterval(() => {
  if (Date.now() - lastStatusMs > STALL_EXIT_MS) {
    log(
      `no status for ${Math.round((Date.now() - lastStatusMs) / 60000)} min; exiting so Docker restarts the logger`,
    );
    process.exit(1);
  }
}, 30000);
connect();
