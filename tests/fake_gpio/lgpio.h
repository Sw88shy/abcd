#pragma once

// Test double for the subset of lgpio used by MotorDriver; no hardware access.
int lgGpiochipOpen(int chip);
int lgGpiochipClose(int handle);
int lgGpioClaimOutput(int handle, int flags, int pin, int level);
int lgGpioWrite(int handle, int pin, int level);
int lgTxPwm(int handle, int pin, float frequency, float duty, int offset, int cycles);
const char* lguErrorText(int error);
