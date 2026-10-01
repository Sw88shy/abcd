#pragma once

#include "person_navigation.h"
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <thread>
#include <vector>

class MotorDriver {
public:
    MotorDriver(bool enabled, int chip, bool invertLeft, bool invertRight,
                double timeout, const volatile std::sig_atomic_t* stopped);
    ~MotorDriver();
    MotorDriver(const MotorDriver&) = delete;
    MotorDriver& operator=(const MotorDriver&) = delete;
    void set(DriveCommand command);
private:
    void motor(int enable, int in1, int in2, double speed, double& previous);
    void halt() noexcept;
    int handle_ = -1;
    bool invertLeft_, invertRight_;
    double left_ = 0, right_ = 0;
    bool closing_ = false;
    bool failed_ = false;
    double timeout_;
    const volatile std::sig_atomic_t* stopped_;
    std::chrono::steady_clock::time_point lastCommand_;
    std::vector<int> claimed_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread watchdog_;
};
