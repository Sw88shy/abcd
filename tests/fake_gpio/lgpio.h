#pragma once

// Test double for the subset of lgpio used by MotorDriver; no hardware access.
#define LG_TX_PWM 0
int lgGpiochipOpen(int chip);
int lgGpiochipClose(int handle);
int lgGpioClaimOutput(int handle, int flags, int pin, int level);
int lgGpioWrite(int handle, int pin, int level);
int lgTxPwm(int handle, int pin, float frequency, float duty, int offset, int cycles);
int lgTxBusy(int handle, int pin, int kind);
const char* lguErrorText(int error);
