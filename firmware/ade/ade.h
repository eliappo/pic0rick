//!
//! \file       max14866.h
//! \author     Elias Illeris Poggi
//! \date       2026-10-05
//!
//! \brief      Controls for acoustic droplet ejection
//!
//! Built only when -ADE is set. 
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
#include "hardware/gpio.h"
#include "hardware/clocks.h"

//---------------------------------------------------------------------------
// CONSTANTS
//---------------------------------------------------------------------------

#define ADE_CAMERA_TRIGGER  18
#define ADE_STROBE_TRIGGER  19
#define ADE_MOTOR_STEP      20
#define ADE_MOTOR_DIR       21
#define ADE_ENABLE          28

#define ADE_CLK   2000000

//---------------------------------------------------------------------------
// ADE INIT FUNCTION
//---------------------------------------------------------------------------
void ade_init();

//---------------------------------------------------------------------------
// ADE FIRE FUNCTION
//---------------------------------------------------------------------------

void ade_fire();

//---------------------------------------------------------------------------
// CAMERA FUNCTIONS
//---------------------------------------------------------------------------

uint camtrig_set(uint delay_us);
uint camexp_set(uint width_us);
bool camera_enable();

//---------------------------------------------------------------------------
// CAMERA FUNCTIONS
//---------------------------------------------------------------------------

uint strobtrig_set(uint delay_us);
uint strobwidth_set(uint delay_us);
bool strobe_enable();

//---------------------------------------------------------------------------
// LOGGING FUNCTIONS
//---------------------------------------------------------------------------

bool logging_enable();

#endif
