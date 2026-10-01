#include "motor_driver.h"

#include <cmath>
#include <stdexcept>
#include <string>
#ifdef HAVE_LGPIO
#include <lgpio.h>
#endif

namespace {
constexpr int ena = 18, in1 = 17, in2 = 27;
constexpr int enb = 13, in3 = 23, in4 = 24;
#ifdef HAVE_LGPIO
void check(int result) {
    if (result < 0) throw std::runtime_error(std::string("Motor GPIO error: ") + lguErrorText(result));
}
#endif
}

MotorDriver::MotorDriver(bool enabled, int chip, bool invertLeft, bool invertRight,
                         double timeout, const volatile std::sig_atomic_t* stopped)
    : invertLeft_(invertLeft), invertRight_(invertRight), timeout_(timeout), stopped_(stopped),
      lastCommand_(std::chrono::steady_clock::now()) {
    if (!std::isfinite(timeout) || timeout <= 0 || chip < 0)
        throw std::invalid_argument("Invalid motor timeout or GPIO chip.");
    if (!enabled) return;
#ifdef HAVE_LGPIO
    handle_ = lgGpiochipOpen(chip);
    check(handle_);
    try {
        // Claim enables first, all outputs initially low.
        for (int pin : {ena, enb, in1, in2, in3, in4}) {
            check(lgGpioClaimOutput(handle_, 0, pin, 0));
            claimed_.push_back(pin);
        }
        watchdog_ = std::thread([this] {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!closing_) {
                wake_.wait_for(lock, std::chrono::milliseconds(20));
                const double age = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - lastCommand_).count();
                if (*stopped_ || age > timeout_) halt();
            }
        });
    } catch (...) {
        halt();
        lgGpiochipClose(handle_);
        handle_ = -1;
        throw;
    }
#else
    throw std::runtime_error("Motor support not built. Install liblgpio-dev and build with -DENABLE_MOTORS=ON.");
#endif
}

MotorDriver::~MotorDriver() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true;
    }
    wake_.notify_all();
    if (watchdog_.joinable()) watchdog_.join();
    halt();
#ifdef HAVE_LGPIO
    if (handle_ >= 0) lgGpiochipClose(handle_);
#endif
}

void MotorDriver::motor(int enable, int positive, int negative, double speed, double& previous) {
#ifdef HAVE_LGPIO
    if (speed == previous) return;
    if (speed == 0 || previous == 0 || std::signbit(speed) != std::signbit(previous)) {
        check(lgTxPwm(handle_, enable, 0, 0, 0, 0));
        check(lgGpioWrite(handle_, enable, 0));
        // Allow cancellation of software PWM before reversing direction.
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        check(lgGpioWrite(handle_, positive, speed > 0));
        check(lgGpioWrite(handle_, negative, speed < 0));
    }
    if (speed != 0) check(lgTxPwm(handle_, enable, 100, static_cast<float>(100 * std::abs(speed)), 0, 0));
    previous = speed;
#else
    (void)enable; (void)positive; (void)negative; (void)speed; (void)previous;
#endif
}

void MotorDriver::set(DriveCommand command) {
    if (!std::isfinite(command.left) || !std::isfinite(command.right) ||
        std::abs(command.left) > 1 || std::abs(command.right) > 1)
        throw std::invalid_argument("Motor command must be finite and in [-1, 1].");
    if (handle_ < 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_) throw std::runtime_error("Motor shutdown failed; check GPIO hardware.");
    if (*stopped_) { halt(); return; }
    try {
        motor(ena, in1, in2, invertLeft_ ? -command.left : command.left, left_);
        motor(enb, in3, in4, invertRight_ ? -command.right : command.right, right_);
        lastCommand_ = std::chrono::steady_clock::now();
    } catch (...) { halt(); throw; }
}

void MotorDriver::halt() noexcept {
#ifdef HAVE_LGPIO
    if (handle_ >= 0) {
        for (int pin : claimed_) {
            if (pin == ena || pin == enb) {
                if (lgTxPwm(handle_, pin, 0, 0, 0, 0) < 0) failed_ = true;
            }
            if (lgGpioWrite(handle_, pin, 0) < 0) failed_ = true;
        }
    }
#endif
    left_ = right_ = 0;
}
