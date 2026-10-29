#ifndef FLUSH_BOOST_H
#define FLUSH_BOOST_H

#include "FlushToTarget.h"

// "Boost after flush": after an ordinary flush the PID heats towards target + surplus instead of the target, and once
// the reading is there (or has peaked above the target) a flush to target takes the surplus off. On a slow-sensing
// boiler the heater then runs harder for longer than a plain recovery would let it, and the closing flush lands the
// boiler on target without waiting for the overshoot to fade. Experimental; off while the surplus is 0.
namespace flush_boost {

constexpr float REACHED_MARGIN_C = 0.3f; // "there" this close to target + surplus
constexpr float PEAK_DROP_C = 0.2f;      // a fall this far from the highest reading counts as a peak
constexpr unsigned long MAX_MS = 300000; // give up (back to the plain target) after 5 min

enum class Action { None, Flush, GiveUp };

class Boost {
  public:
    void start(unsigned long nowMs) {
        active = true;
        startedMs = nowMs;
        peak = -1000.0f;
    }
    void cancel() { active = false; }
    bool isActive() const { return active; }

    // Called every control loop while active with the shown reading and target (the selected profile's temperature).
    Action step(unsigned long nowMs, float reading, float target, float surplus) {
        if (!active)
            return Action::None;
        if (nowMs - startedMs > MAX_MS) {
            active = false;
            return Action::GiveUp;
        }
        peak = std::max(peak, reading);
        const bool reached = reading >= target + surplus - REACHED_MARGIN_C;
        // Only a peak with enough surplus for a flush to target; one in the flush's own dip doesn't count.
        const bool peaked = peak >= target + flush_to_target::MIN_SURPLUS_C + PEAK_DROP_C && reading <= peak - PEAK_DROP_C;
        if (reached || peaked) {
            active = false;
            return Action::Flush;
        }
        return Action::None;
    }

  private:
    bool active = false;
    unsigned long startedMs = 0;
    float peak = -1000.0f;
};

} // namespace flush_boost

#endif // FLUSH_BOOST_H
