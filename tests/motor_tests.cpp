#include "motor_driver.h"
#include <lgpio.h>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
std::mutex fakeMutex;
std::array<int, 28> levels{};
std::array<float, 28> duties{};
int opens = 0, closes = 0;
int failPin = -1;
int failWritePin = -1;
bool changedWhileEnabled = false;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool disabled() {
    std::lock_guard<std::mutex> lock(fakeMutex);
    return duties[18] == 0 && duties[13] == 0 && levels[18] == 0 && levels[13] == 0;
}
}

int lgGpiochipOpen(int) { ++opens; return 1; }
int lgGpiochipClose(int) { ++closes; return 0; }
int lgGpioClaimOutput(int, int, int pin, int level) {
    std::lock_guard<std::mutex> lock(fakeMutex);
    if (pin == failPin) return -1;
    levels[pin] = level;
    return 0;
}
int lgGpioWrite(int, int pin, int level) {
    std::lock_guard<std::mutex> lock(fakeMutex);
    if (pin == failWritePin) return -1;
    const int enable = (pin == 17 || pin == 27) ? 18 : (pin == 23 || pin == 24) ? 13 : -1;
    if (enable >= 0 && levels[pin] != level && duties[enable] != 0) changedWhileEnabled = true;
    levels[pin] = level;
    return 0;
}
int lgTxPwm(int, int pin, float frequency, float duty, int, int) {
    std::lock_guard<std::mutex> lock(fakeMutex);
    duties[pin] = frequency == 0 ? 0 : duty;
    return 0;
}
const char* lguErrorText(int) { return "simulated error"; }

int main() {
    volatile std::sig_atomic_t stopped = 0;
    try {
        { MotorDriver preview(false, 0, false, false, 0.1, &stopped); preview.set({0.3, 0.3}); }
        require(opens == 0, "Preview must never open GPIO");
        {
            MotorDriver motor(true, 0, false, true, 0.1, &stopped);
            require(disabled(), "Initialisation must disable motors");
            motor.set({0.3, 0.4});
            {
                std::lock_guard<std::mutex> lock(fakeMutex);
                require(levels[17] == 1 && levels[27] == 0, "Left forward polarity");
                require(levels[23] == 0 && levels[24] == 1, "Right inversion must apply");
                require(std::abs(duties[18] - 30) < 0.01 && std::abs(duties[13] - 40) < 0.01,
                        "Commands must map to PWM duty percentages");
            }
            motor.set({-0.3, -0.4});
            require(!changedWhileEnabled, "Disable enable pins before reversing direction");
            std::this_thread::sleep_for(std::chrono::milliseconds(180));
            require(disabled(), "Watchdog must halt motion when updates stop");
            motor.set({0.3, 0.3});
        }
        require(disabled() && closes == 1, "Destructor must disable motors and close chip");
        failPin = 23;
        bool failed = false;
        try { MotorDriver bad(true, 0, false, false, 0.1, &stopped); }
        catch (const std::runtime_error&) { failed = true; }
        require(failed && disabled() && closes == 2, "Partial startup failure must release chip and halt");
        failPin = -1;
        {
            MotorDriver motor(true, 0, false, false, 0.1, &stopped);
            failWritePin = 24;
            failed = false;
            try { motor.set({0.3, 0.3}); } catch (const std::runtime_error&) { failed = true; }
            require(failed && disabled(), "GPIO write failure must disable both motors");
            failWritePin = -1;
        }
        std::cout << "Motor tests passed (simulated GPIO)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
