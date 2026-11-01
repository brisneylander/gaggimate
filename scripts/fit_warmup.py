"""Fit the test plant to logged warm-ups (the GaggiMate logger's daily CSVs, tools/logger).

Replays the logged heater output (hp, %) open loop into the three-node plant of test_heat_balance_predictor.cpp
(element/wall -> water -> group head -> room; the sensor sees the water through a delay and a first-order lag, in
0.25 °C steps) and fits it so the simulated reading follows the logged one (ct). Temperatures in the plant are the
sensor's (shown + offset).
usage: python -I scripts/fit_warmup.py <logger csv> <HH:MM:SS start> <seconds> [<csv> <start> <seconds> ...]
"""
import csv
import math
import sys

W = 1100.0
DT = 0.25
OFFSET = 5.0
ROOM = 22.0
# The plant fitted to the flush logs (test_heat_balance_predictor.cpp simulate()).
FLUSH_FIT = dict(Cw=200.0, Cb=1296.0, k=9.6, Cg=819.0, kg=5.7, loss=0.59, D=4.6, L=15.0)


def load(path, start, seconds):
    rows = [r for r in csv.DictReader(open(path)) if r["ct"]]
    i0 = next(i for i, r in enumerate(rows) if r["time"][11:19] >= start)
    t0 = int(rows[i0]["unix_ms"]) / 1000.0
    out = []
    for r in rows[i0:]:
        t = int(r["unix_ms"]) / 1000.0 - t0
        if t > seconds:
            break
        out.append((t, float(r["ct"]) + OFFSET, float(r["hp"] or 0) / 100.0))
    return dict(name=f"{path[-14:-4]} {start}", rows=out)


def simulate(q, run, groupOffset):
    rows = run["rows"]
    start = rows[0][1]
    # At rest at the start: wall and water at the reading, the group head groupOffset under (a cool-down leaves it
    # warmer or colder than the water).
    wall, boiler, group = start, start, start + groupOffset
    delay = max(int(q["D"] / DT), 1)
    pipe = [boiler] * delay
    lagged = ema = boiler
    out = []
    j = 0
    hp = rows[0][2]
    t = 0.0
    err2 = 0.0
    n = 0
    while j < len(rows):
        while j < len(rows) and rows[j][0] <= t:
            # compare at each logged sample
            err = ema - rows[j][1]
            err2 += err * err
            n += 1
            hp = rows[j][2]
            j += 1
        heat = W * hp
        toWater = q["k"] * (wall - boiler)
        toGroup = q["kg"] * (boiler - group)
        wall += (heat - toWater) * DT / q["Cw"]
        group += (toGroup - q["loss"] * (group - ROOM)) * DT / q["Cg"]
        boiler += (toWater - toGroup) * DT / q["Cb"]
        pipe.append(boiler)
        delayed = pipe.pop(0)
        lagged += (delayed - lagged) * DT / q["L"]
        ema = 0.2 * (math.floor(lagged * 4.0) / 4.0) + 0.8 * ema
        out.append(ema)
        t += DT
    return math.sqrt(err2 / max(n, 1))


def nelder_mead(f, x0, step, iters=800):
    n = len(x0)
    pts = [list(x0)] + [[x0[j] + (step[j] if i == j else 0.0) for j in range(n)] for i in range(n)]
    vals = [f(p) for p in pts]
    for _ in range(iters):
        order = sorted(range(n + 1), key=lambda i: vals[i])
        pts = [pts[i] for i in order]
        vals = [vals[i] for i in order]
        c = [sum(p[j] for p in pts[:-1]) / n for j in range(n)]
        xr = [c[j] + (c[j] - pts[-1][j]) for j in range(n)]
        fr = f(xr)
        if fr < vals[0]:
            xe = [c[j] + 2.0 * (c[j] - pts[-1][j]) for j in range(n)]
            fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = [c[j] + 0.5 * (pts[-1][j] - c[j]) for j in range(n)]
            fc = f(xc)
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                pts = [pts[0]] + [[pts[0][j] + 0.5 * (p[j] - pts[0][j]) for j in range(n)] for p in pts[1:]]
                vals = [vals[0]] + [f(p) for p in pts[1:]]
    i = min(range(n + 1), key=lambda i: vals[i])
    return pts[i], vals[i]


KEYS = ["Cw", "Cb", "k", "Cg", "kg"]
# Standing loss pinned to the holding power: 47 W at 93 °C shown (98 at the sensor) in the 2026-11-02 log, as in the
# flush fit (45 W). Left free, the fit puts the cold group head's uptake into the loss instead.
HOLD_LOSS = 47.0 / (93.0 + OFFSET - ROOM)


def unpack(p):
    q = {k: math.exp(p[i]) for i, k in enumerate(KEYS)}
    q["loss"] = HOLD_LOSS
    q["D"] = FLUSH_FIT["D"]
    q["L"] = FLUSH_FIT["L"]
    return q


def main():
    args = sys.argv[1:]
    runs = [load(args[i], args[i + 1], float(args[i + 2])) for i in range(0, len(args), 3)]
    # Group offsets: 0 for a run from room temperature, fitted otherwise.
    fixed = [abs(r["rows"][0][1] - ROOM - OFFSET) < 5.0 for r in runs]

    def cost(p):
        q = unpack(p)
        offs = p[len(KEYS):]
        k = 0
        total = 0.0
        for r, fx in zip(runs, fixed):
            o = 0.0 if fx else offs[k]
            if not fx:
                k += 1
            total += simulate(q, r, o) ** 2
        return math.sqrt(total / len(runs))

    x0 = [math.log(FLUSH_FIT[k]) for k in KEYS] + [0.0] * fixed.count(False)
    print("flush-fitted plant on these warm-ups:")
    for r, fx in zip(runs, fixed):
        print(f"  {r['name']}: rms {simulate(FLUSH_FIT, r, 0.0):.2f} °C")
    from scipy.optimize import minimize

    res = minimize(cost, x0, method="Nelder-Mead", options={"maxiter": 4000, "xatol": 1e-3, "fatol": 1e-4, "adaptive": True})
    best, rms = list(res.x), res.fun
    q = unpack(best)
    print(f"refit: rms {rms:.2f} °C")
    print("  " + ", ".join(f"{k} {q[k]:.1f}" for k in KEYS))
    offs = best[len(KEYS):]
    k = 0
    for r, fx in zip(runs, fixed):
        o = 0.0 if fx else offs[k]
        if not fx:
            k += 1
        print(f"  {r['name']}: rms {simulate(q, r, o):.2f} °C, group head start {o:+.1f} °C vs water")


if __name__ == "__main__":
    main()
