// Start a GaggiMate OTA update and follow it, then wait for the device to report the expected versions.
// usage: node tools/release/gmota.mjs <display|controller> <expected version e.g. v1.9.0-simon002> [host]
const [, , component, expected, host = '192.168.20.34'] = process.argv;
if (!['display', 'controller'].includes(component) || !expected) {
  console.error('usage: node gmota.mjs <display|controller> <expected version> [host]');
  process.exit(2);
}
const url = `ws://${host}/ws`;
const sleep = ms => new Promise(r => setTimeout(r, ms));

function query() {
  return new Promise(resolve => {
    let ws;
    const t = setTimeout(() => {
      try {
        ws.close();
      } catch {}
      resolve(null);
    }, 8000);
    try {
      ws = new WebSocket(url);
    } catch {
      clearTimeout(t);
      return resolve(null);
    }
    ws.onopen = () => ws.send(JSON.stringify({ tp: 'req:ota-settings' }));
    ws.onmessage = e => {
      const m = JSON.parse(e.data);
      if (m.tp !== 'res:ota-settings') return;
      clearTimeout(t);
      ws.close();
      resolve(m);
    };
    ws.onerror = () => {};
  });
}

const key = component === 'display' ? 'displayVersion' : 'controllerVersion';
// The display only re-checks GitHub every 30 min; force a check (the UI's "Save Channel & Refresh") and wait
// until it has seen the release we expect.
let before = null;
for (let i = 0; i < 12; i++) {
  before = await new Promise(resolve => {
    const ws = new WebSocket(url);
    const t = setTimeout(() => {
      try {
        ws.close();
      } catch {}
      resolve(null);
    }, 15000);
    ws.onopen = () => ws.send(JSON.stringify({ tp: 'req:ota-settings', update: true, channel: 'latest' }));
    ws.onmessage = e => {
      const m = JSON.parse(e.data);
      if (m.tp !== 'res:ota-settings' || `v${m.latestVersion}` !== expected) return;
      clearTimeout(t);
      ws.close();
      resolve(m);
    };
    ws.onerror = () => {};
  });
  if (before) break;
  await sleep(5000);
}
if (!before) {
  console.log(`device did not see release ${expected}`);
  process.exit(1);
}
console.log(`before: display ${before.displayVersion}, controller ${before.controllerVersion}, latest ${before.latestVersion}`);
if (before[key] === expected) {
  console.log(`${component} already on ${expected}`);
  process.exit(0);
}

await new Promise(resolve => {
  const ws = new WebSocket(url);
  let last = -10;
  ws.onopen = () => ws.send(JSON.stringify({ tp: 'req:ota-start', cp: component }));
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.tp !== 'evt:ota-progress') return;
    if (m.progress - last >= 20 || m.progress === 100) {
      console.log(`phase ${m.phase} ${m.progress}%`);
      last = m.progress;
    }
  };
  ws.onclose = () => resolve();
  ws.onerror = () => {};
  setTimeout(() => {
    try {
      ws.close();
    } catch {}
    resolve();
  }, 600000);
});

for (let i = 0; i < 40; i++) {
  await sleep(10000);
  const m = await query();
  if (m && m[key] === expected) {
    console.log(`after: display ${m.displayVersion}, controller ${m.controllerVersion}, updating ${m.updating}`);
    process.exit(0);
  }
}
console.log(`${component} did not report ${expected} within 400 s`);
process.exit(1);
