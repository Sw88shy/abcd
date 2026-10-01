#pragma once

#include "motor_driver.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

// Independent of detection: check each motor and then simultaneous forward drive.
inline void startupMovementCheck(MotorDriver& motors,
                                 const volatile std::sig_atomic_t& stopped) {
    using Clock = std::chrono::steady_clock;
    constexpr double duty = 0.50;
    const DriveCommand commands[] = {{duty, 0}, {0, duty}, {duty, duty}};
    const char* labels[] = {"LEFT motor only", "RIGHT motor only", "BOTH motors forward"};
    for (int stage = 0; stage < 3 && !stopped; ++stage) {
        std::cout << "Startup check: " << labels[stage] << " (1 second, 50% duty)" << std::endl;
        motors.set(commands[stage]);
        const auto end = Clock::now() + std::chrono::seconds(1);
        while (!stopped && Clock::now() < end) {
            // Renew the watchdog while the camera loop is not yet running.
            motors.set(commands[stage]);
            std::this_thread::sleep_until(std::min(end, Clock::now() + std::chrono::milliseconds(20)));
        }
        motors.set({});
        const auto pauseEnd = Clock::now() + std::chrono::milliseconds(250);
        while (!stopped && Clock::now() < pauseEnd)
            std::this_thread::sleep_until(std::min(pauseEnd, Clock::now() + std::chrono::milliseconds(20)));
    }
    motors.set({});
    if (!stopped) std::cout << "Startup check complete. Starting person following." << std::endl;
}
