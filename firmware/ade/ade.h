//!
//! \file       ade.h
//! \author     Elias Illeris Poggi
//! \date       2026-10-05
//!
//! \brief      Controls for Acoustic Droplet Ejection (ADE) module.
//!
//! Built only when -DADE=ON. The ADE board is a plug-in on top of the
//! pulse-echo board, sharing the transducer GPIOs (GPIO11/12 drive,
//! GPIO16/17 gate). Two modes:
//!   Calibration — uses standard pulser + ADC (arm calibration)
//!   Ejection    — burst_drive + burst_gate + camera/strobe (arm eject)
//!   Trigger     — camera/strobe fired by an edge on ADE_TRIGGER_IN, no burst
//!                 (arm trigger); continuous until another mode is armed
//!

#ifndef ADE_H
#define ADE_H

//---------------------------------------------------------------------------
// INCLUDES
//---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

//---------------------------------------------------------------------------
// CONSTANTS
//---------------------------------------------------------------------------

#define ADE_OUTPUT_P_POS    11      // GPIO11: P+ (shared with pulser drive)
#define ADE_OUTPUT_P_NEG    12      // GPIO12: P- (shared with pulser drive)
#define ADE_CAMERA_TRIGGER  18      // GPIO18: camera trigger output
#define ADE_STROBE_TRIGGER  21      // GPIO21: strobe trigger output
#define ADE_TRIGGER_IN      20      // GPIO20: external trigger input (3.3 V max)
#define ADE_MOTOR_STEP      99      // stepper step (unused; GPIO20 is now trigger-in)
#define ADE_MOTOR_DIR       99      // stepper dir  (unused; GPIO21 is now strobe)

#define ADE_CLK             2000000     // delayed_pulse SM clock (2 MHz, 0.5 µs/tick)
#define ADE_BURST_CLK       125000000   // burst_drive/gate SM clock (125 MHz, 8 ns/tick)
#define ADE_DAMP_US         10u         // default post-burst PDAMP duration (µs)

//---------------------------------------------------------------------------
// TYPES
//---------------------------------------------------------------------------

typedef enum {
    ADE_MODE_CALIBRATION = 0,
    ADE_MODE_EJECTION,
    ADE_MODE_TRIGGER,
} ade_mode_t;

typedef struct {
    uint camera_exposure;   // camera pulse width  (µs)
    uint camera_trigger;    // camera delay from burst start / trigger edge (µs)
    uint strobe_width;      // strobe pulse width  (µs)
    uint strobe_trigger;    // strobe delay from burst start / trigger edge (µs)
    uint burst_count;       // number of bipolar cycles per ejection
    uint burst_freq;        // burst frequency (Hz)
    bool camera_enabled;
    bool strobe_enabled;
    bool log_enabled;
    bool trigger_falling;   // trigger mode: react on falling (true) / rising edge
} ade_config_t;

//---------------------------------------------------------------------------
// LIFECYCLE
//---------------------------------------------------------------------------

void ade_init(void);

//---------------------------------------------------------------------------
// MODE SWITCH
//---------------------------------------------------------------------------

void ade_arm_ejection(const char *args);
void ade_arm_calibration(const char *args);
void ade_arm_trigger(const char *args);

//---------------------------------------------------------------------------
// EJECTION
//---------------------------------------------------------------------------

void ade_fire(const char *args);

//---------------------------------------------------------------------------
// CONFIGURATION SETTERS  (REPL command handlers: parse args string)
//---------------------------------------------------------------------------

void burstcount_set(const char *args);
void burstfreq_set(const char *args);

void camtrig_set(const char *args);
void camexp_set(const char *args);
void camera_enable(const char *args);

void strobtrig_set(const char *args);
void strobwidth_set(const char *args);
void strobe_enable(const char *args);

void trigedge_set(const char *args);

void logging_enable(const char *args);

#endif
