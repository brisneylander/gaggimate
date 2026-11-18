#ifndef PRESSURE_EXIT_H
#define PRESSURE_EXIT_H

// Whether a phase's "pressure >= X" exit target counts as reached.
//
// With the brew valve closed the boiler side is sealed (the 3-way valve vents only the group), and the heater keeps
// warming the water in it, so pressure builds up between shots: logged at 1-7 bar on a Silvia minutes after a shot.
// The sensor reads that pressure the moment a shot starts, so a first phase like "fill until 2.5 bar" ended before the
// pump had done anything and the preinfusion was skipped. The trapped pressure escapes into the puck within about a
// second of the valve opening (3.2 -> 0.3 bar in ~1 s), so a rising target only counts once the pressure has been
// below it at some point since the shot started: then it's pressure the pump built. On a shot that starts without
// trapped pressure that's true from the first reading, so nothing changes. "Pressure <= X" targets are unaffected.
//
// lowest_pressure: the lowest pressure seen since the shot started (0 when not tracked: the old behaviour).
inline bool pressureRiseReached(float target, float current_pressure, float lowest_pressure) {
    return current_pressure >= target && lowest_pressure < target;
}

#endif // PRESSURE_EXIT_H
