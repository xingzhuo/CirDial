#pragma once
#include <cstdint>

namespace dial {
// Animation/input has priority. Hidden maintenance only needs to service
// connection recovery; visible shell-menu tracking retains its 100ms cadence.
inline unsigned TimerDelay(bool busy, bool hidden) {
    return busy ? 16u : hidden ? 500u : 100u;
}
inline bool PendingDeadline(std::uint64_t deadline, std::uint64_t now) {
    return deadline != 0 && now < deadline;
}
inline bool SchedulerTest() {
    return TimerDelay(true, true)==16 && TimerDelay(true, false)==16 &&
        TimerDelay(false, true)==500 && TimerDelay(false, false)==100 &&
        !PendingDeadline(0, 10) && PendingDeadline(11, 10) &&
        !PendingDeadline(10, 10) && !PendingDeadline(9, 10);
}
}
