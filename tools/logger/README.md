# GaggiMate telemetry logger

Logs everything the GaggiMate display reports over its websocket to daily CSV files, so the boiler can be tuned
against weeks of normal use. Runs as a container on the NAS.

## Where it runs

- **OMV** (`192.168.0.5`): `compose/appdata/gaggimate-logger/` — the compose file in OMV's Compose UI, and
  `logger.mjs` from this folder beside it.
- **Files**: `compose/appdata/gaggimate-logger/logs/` — `YYYY-MM-DD.csv` per Brisbane day, and
  `settings-YYYY-MM-DD.json` (the settings each time they change). Files older than 2 days are gzipped.
- **Network**: the coffee machine is on the isolated IoT LAN (`192.168.20.34`, a fixed reservation). The UDR7 policy
  **"OMV -> GaggiMate logger"** (index 10005) allows `192.168.0.5` → `192.168.20.34` TCP 80 only, added 2026-11-01.

## What's in a log

| Column                     |                                                                                              |
| -------------------------- | -------------------------------------------------------------------------------------------- |
| `time`, `unix_ms`          | Brisbane local time, and Unix milliseconds                                                   |
| `ct`, `tt`                 | boiler temperature and target, as the display shows them (sensor − temperature offset)       |
| `hp`                       | heater output %                                                                              |
| `pr`, `pt`                 | pressure and target (bar)                                                                    |
| `fl`, `tf`                 | pump flow and target (ml/s)                                                                  |
| `cw`                       | weight (g), with a scale                                                                     |
| `pw`                       | pump power %                                                                                 |
| `mode`                     | 0 Standby, 1 Brew, 2 Steam, 3 Water                                                          |
| `active`, `state`, `phase` | a shot or flush running, its state and phase (e.g. `brew`, `Flush`, `Peak 9 bar`)            |
| `event`                    | `mode`, `connect`, `disconnect`, or an `evt:*` message such as `evt:shot-finished-stats`     |
| `vo`                       | brew valve as the controller reports it (1 open); blank before La Simona 5                   |
| `rtx`, `lat`               | commands the display has had to resend since boot, and the round trip to the controller (ms) |

`logs/valve-timing.csv` adds one row per shot or flush start in Brew: the pressure it started from, then the seconds until
the valve reported open (`valve_s`), the pressure fell by 1.5 bar (`release_s`) and flow appeared (`flow_s`), and how many
commands the link resent meanwhile (`rtx_added`). A late `valve_s` means the command was slow to arrive; `valve_s` on time
with a late `release_s` means the valve itself. Times are as frames arrive, so ±0.5 s.

While the machine is on, every status frame is written (a few per second, ~1 MB an hour). In Standby, one row a
minute.

The display serves at most 3 websocket clients and drops the oldest when a fourth connects. The logger holds one; if
it is dropped three times in 10 minutes it waits 5 minutes before reconnecting, so a phone and a browser can still
connect.

A connection attempt that hasn't opened within 20 s is retried, and if no status arrives for 5 minutes (the display
sends about two a second, Standby included) the logger exits and Docker restarts it: on 2026-11-02 a reconnect attempt
hung, neither opening nor failing, and nothing was logged for 11 hours. Settings snapshots ignore values that drift on
their own (the tank estimate, the learned loss, the shot counters) when deciding whether anything changed.

The stack is managed in the OMV web UI (Services → Compose → Files, `gaggimate-logger`, saved 2026-11-02); `logger.mjs`
sits beside OMV's files in the same folder.

## Updating

Copy a changed `logger.mjs` to the NAS folder and `docker compose restart`.
