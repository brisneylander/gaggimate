// Cool the boiler and group head with back-to-back plain flushes, then leave the machine in Standby: a "cold" start
// for testing a warm-up from below the brew temperature (the 2026-10-30 morning case started at 71 °C).
// usage: node cool_down.mjs <out csv> [target °C, default 74]    GM_HOST=<display address> to point elsewhere
import { writeFileSync, appendFileSync } from 'node:fs';

const [, , out, targetArg] = process.argv;
const target = Number(targetArg) || 74;
const host = process.env.GM_HOST || '192.168.20.34';
const MAX_FLUSHES = 25;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const log = msg => console.log(`${new Date().toISOString().slice(11, 19)} ${msg}`);

writeFileSync(out, 't,ct,tt,hp,fl,m\n');
const t0 = Date.now();
let ct = null;
let fl = 0;
let mode = null;
let ws;
function connect() {
  ws = new WebSocket(`ws://${host}/ws`);
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.tp !== 'evt:status') return;
    if (m.m !== undefined) mode = m.m;
    if (m.ct === undefined) return;
    ct = m.ct;
    fl = Number(m.fl) || 0;
    appendFileSync(out, `${((Date.now() - t0) / 1000).toFixed(2)},${m.ct},${m.tt},${m.hp},${m.fl},${mode}\n`);
  };
  ws.onclose = () => setTimeout(connect, 3000);
  ws.onerror = () => {};
}
const send = obj => ws.readyState === 1 && ws.send(JSON.stringify(obj));
async function confirmMode(m) {
  for (let i = 0; i < 15; i++) {
    send({ tp: 'req:change-mode', mode: m });
    for (let j = 0; j < 4; j++) {
      await sleep(500);
      if (mode === m) return true;
    }
  }
  return false;
}

connect();
await sleep(3000);
log(`start: ${ct} °C, mode ${mode}; cooling to ${target} °C`);
if (!(await confirmMode(1))) {
  log('could not switch to Brew');
  process.exit(1);
}
let n = 0;
while (ct > target && n < MAX_FLUSHES) {
  send({ tp: 'req:flush:start', rid: 1 });
  n++;
  // Wait for the flow to start and then stop for 2 s.
  await sleep(3000);
  let still = 0;
  while (still < 2) {
    await sleep(1000);
    still = fl > 0.1 ? 0 : still + 1;
  }
  log(`flush ${n}: ${ct} °C`);
}
log(`${n} flushes, ${ct} °C; Standby`);
log(await confirmMode(0) ? 'in Standby' : 'could not switch to Standby');
await sleep(1000);
process.exit(0);
