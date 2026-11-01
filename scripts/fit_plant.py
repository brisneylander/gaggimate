"""Fit the test plant (test_heat_balance_predictor.cpp simulate()) to Silvia bench logs.

Replays each log's recorded heater output (hp, %) and pump flow (fl, ml/s) open loop into the plant and fits its
parameters so the simulated sensor follows the logged temperature (ct). Plant, in shown °C (boiler = shown + offset,
which only shifts the loss term):
    element/wall node  Cw  --k-->  water node  Cb  --> transport delay D --> first-order lag L --> sensor
usage: python -I scripts/fit_plant.py <dir of bench CSVs (t,ct,hp,fl)> [two|three|group]   (group = the fitted plant)
"""
import csv
import math
import os
import sys

DIAG = sys.argv[1]
MODEL = sys.argv[2] if len(sys.argv) > 2 else "two"
W = 1100.0
DT = 0.25
ROOM_REL = 93.0 - 20.0  # loss reference: holding power at 93 shown
HOLD_W = 45.0  # holding power at 93 shown: 3-5 % of 1100 W in every idle log
INLET = {"flush5": 35.0, "flush-target": 35.0, "flush": 35.0}

RUNS = [
    ("flush5-R1-off.csv", 20, 300),
    ("flush5-R1-reheat.csv", 20, 300),
    ("flush5-R2-off.csv", 20, 300),
    ("flush5-R2-reheat.csv", 20, 300),
    ("flush-target-5.csv", 0, 480),
    ("flush-target-6.csv", 0, 480),
]


def load(name, before, after):
    rows = list(csv.DictReader(open(os.path.join(DIAG, name))))
    t = [float(r["t"]) for r in rows]
    ct = [float(r["ct"]) for r in rows]
    hp = [float(r["hp"]) / 100.0 for r in rows]
    fl = [float(r["fl"]) for r in rows]
    if name.startswith("flush-target"):
        start = 0.0
    else:
        i = next(i for i, v in enumerate(fl) if v > 0.5)
        start = t[i] - before
    end = start + before + after
    idx = [i for i in range(len(t)) if start <= t[i] <= end]
    inlet = next(v for k, v in INLET.items() if name.startswith(k))
    return dict(name=name, t=[t[i] - start for i in idx], ct=[ct[i] for i in idx], hp=[hp[i] for i in idx],
                fl=[fl[i] for i in idx], inlet=inlet)


DATA = [load(*r) for r in RUNS]


def unpack(p):
    q = dict(Cw=math.exp(p[0]), Cb=math.exp(p[1]), k=math.exp(p[2]), D=abs(p[3]), L=max(abs(p[4]), 0.3))
    if MODEL == "three":  # extra sensor-mount node (slow shell/brass the thermocouple sits on)
        q["Ls"] = max(abs(p[5]), 0.3)
    if MODEL == "group":  # slow mass (group head) trading heat with the water; flush water also runs through it
        q["Cg"] = math.exp(p[5])
        q["kg"] = math.exp(p[6])
        q["fg"] = min(max(p[7], 0.0), 1.0)  # share of the standing loss that leaves through the group
        q["a"] = min(max(p[8], 0.0), 1.0)  # share of the flush's cooling taken from the group
        if len(p) > 9:  # tank temperature per session, fitted (the tank warms beside the boiler)
            q["in_f5"], q["in_ft"] = p[9], p[10]
    return q


def simulate(q, run):
    t, hp, fl = run["t"], run["hp"], run["fl"]
    T0 = run["ct"][0]
    p0 = hp[0] * W  # heater power just before the run starts (holding, or ~0 coming out of standby)
    h = HOLD_W / ROOM_REL
    water = T0
    wall = T0 + p0 / q["k"]
    nd = max(1, int(round(q["D"] / DT)))
    pipe = [T0] * nd
    pi = 0
    lag = T0
    mount = T0
    group = T0
    if MODEL == "group":
        group = T0 - q["fg"] * HOLD_W / q["kg"]
    out = []
    j = 0
    tt = 0.0
    tend = t[-1]
    cur_hp, cur_fl = hp[0], fl[0]
    while tt <= tend + 1e-9:
        while j < len(t) and t[j] <= tt:
            out.append(mount if MODEL == "three" else lag)
            cur_hp, cur_fl = hp[j], fl[j]
            j += 1
        heat = W * cur_hp
        to_water = q["k"] * (wall - water)
        inlet = q.get("in_f5" if run["name"].startswith("flush5") else "in_ft", run["inlet"])
        cold = cur_fl * 4.18 * (water - inlet)
        wall += (heat - to_water) * DT / q["Cw"]
        loss = h * (water - T0 + ROOM_REL)
        if MODEL == "group":
            to_group = q["kg"] * (water - group)
            group += (to_group - q["fg"] * loss - q["a"] * cold) * DT / q["Cg"]
            water += (to_water - (1 - q["a"]) * cold - (1 - q["fg"]) * loss - to_group) * DT / q["Cb"]
        else:
            water += (to_water - cold - loss) * DT / q["Cb"]
        delayed = pipe[pi]
        pipe[pi] = water
        pi = (pi + 1) % nd
        lag += (delayed - lag) * DT / q["L"]
        if MODEL == "three":
            mount += (lag - mount) * DT / q["Ls"]
        tt += DT
    while len(out) < len(t):
        out.append(out[-1])
    return out


def cost(p):
    q = unpack(p)
    if MODEL == "group" and any(not 5 <= q.get(k, 35) <= 70 for k in ("in_f5", "in_ft")):
        return 1e9
    if MODEL == "group" and (q["Cg"] < 50 or q["Cg"] > 3000 or q["kg"] > 500):
        return 1e9
    if q["Cw"] < 20 or q["Cb"] < 300 or q["Cw"] + q["Cb"] > 4000 or q["D"] > 60 or q["L"] > 120:
        return 1e9
    s = n = 0
    for run in DATA:
        sim = simulate(q, run)
        for a, b in zip(sim, run["ct"]):
            s += (a - b) ** 2
            n += 1
    return math.sqrt(s / n)


def nelder_mead(f, x0, step, iters=600):
    pts = [list(x0)]
    for i in range(len(x0)):
        x = list(x0)
        x[i] += step[i]
        pts.append(x)
    vals = [f(x) for x in pts]
    for it in range(iters):
        order = sorted(range(len(pts)), key=lambda i: vals[i])
        pts = [pts[i] for i in order]
        vals = [vals[i] for i in order]
        if it % 50 == 0:
            print(f"  iter {it} rms {vals[0]:.3f} {fmt(unpack(pts[0]))}", flush=True)
        c = [sum(p[i] for p in pts[:-1]) / (len(pts) - 1) for i in range(len(x0))]
        xr = [c[i] + (c[i] - pts[-1][i]) for i in range(len(x0))]
        fr = f(xr)
        if fr < vals[0]:
            xe = [c[i] + 2 * (c[i] - pts[-1][i]) for i in range(len(x0))]
            fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = [c[i] + 0.5 * (pts[-1][i] - c[i]) for i in range(len(x0))]
            fc = f(xc)
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                for k in range(1, len(pts)):
                    pts[k] = [pts[0][i] + 0.5 * (pts[k][i] - pts[0][i]) for i in range(len(x0))]
                    vals[k] = f(pts[k])
    i = min(range(len(pts)), key=lambda i: vals[i])
    return pts[i], vals[i]


def fmt(q):
    return " ".join(f"{k}={v:.1f}" for k, v in q.items()) + f" tau_wall={q['Cw'] / q['k']:.1f}s rate={W / (q['Cw'] + q['Cb']):.3f}"


if __name__ == "__main__":
    # Start from the current test plant.
    x0 = [math.log(700), math.log(1133), math.log(35), 6.0, 4.0] + ([10.0] if MODEL == "three" else [])
    if MODEL == "group":
        x0 = [math.log(150), math.log(1300), math.log(10), 8.0, 4.0, math.log(1240), math.log(6), 0.9, 0.05, 35.0, 35.0]
    print("current plant rms", round(cost(x0), 3), fmt(unpack(x0)))
    extra = {"two": [], "three": [10.0], "group": [0.5, 0.5, 0.3, 0.3, 8.0, 8.0]}[MODEL]
    extra2 = {"two": [], "three": [4.0], "group": [0.2, 0.2, 0.1, 0.1, 3.0, 3.0]}[MODEL]
    best, rms = nelder_mead(cost, x0, [0.4, 0.3, 0.4, 4.0, 4.0] + extra, 900)
    best, rms = nelder_mead(cost, best, [0.15, 0.15, 0.15, 2.0, 2.0] + extra2, 600)
    q = unpack(best)
    print("FIT rms", round(rms, 3), fmt(q))
    for run in DATA:
        sim = simulate(q, run)
        cur = simulate(dict(Cw=700, Cb=1133, k=35, D=6, L=4), run) if MODEL != "group" else sim
        e = math.sqrt(sum((a - b) ** 2 for a, b in zip(sim, run["ct"])) / len(sim))
        e0 = math.sqrt(sum((a - b) ** 2 for a, b in zip(cur, run["ct"])) / len(cur))
        print(f"  {run['name']:24s} rms fit {e:.2f} (current plant {e0:.2f})")
