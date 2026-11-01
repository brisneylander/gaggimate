// GaggiMate telemetry logger: keeps a websocket open to the display and writes everything it reports to daily CSV
// files, for tuning the boiler over weeks of normal use. Runs as a container on the NAS (compose.yml beside this file).
//
//   - While the machine is on: every status frame (temperature and target, heater %, pressure, flow, pump, weight, mode,
//     shot phase), a few per second. In Standby: one row a minute (still shows the overnight cool-down).
//   - Events (evt:* other than status, e.g. shot finished) as rows with the event name.
//   - The settings (/api/settings) whenever they change, to settings-<date>.json, so each day's log has the PID and
//     boiler profile it ran with.
//   - One file per Brisbane date; files older than KEEP_PLAIN_DAYS are gzipped.
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
];
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
    ...state,
  });
}

// Settings: saved when they change, so a day's log can be read against the PID and boiler profile in force.
let lastSettings = "";
async function snapshotSettings() {
  try {
    const res = await fetch(`http://${HOST}/api/settings`, {
      signal: AbortSignal.timeout(10000),
    });
    if (!res.ok) return;
    const text = JSON.stringify(await res.json());
    if (text === lastSettings) return;
    lastSettings = text;
    appendFileSync(
      join(DIR, `settings-${localDate(new Date())}.json`),
      `${JSON.stringify({ time: localTime(new Date()), settings: JSON.parse(text) })}\n`,
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
  ws.onopen = () => {
    opened = true;
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
    if (m.tp === "evt:status") onStatus(m);
    else if (typeof m.tp === "string" && m.tp.startsWith("evt:"))
      write({ ...state, event: m.tp });
  };
  ws.onerror = () => {};
  ws.onclose = () => {
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
connect();
