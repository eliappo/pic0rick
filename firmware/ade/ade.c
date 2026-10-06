//!
//! \file       ade.c
//! \author     Elias Illeris Poggi
//! \date       2026-10-05
//!
//! \brief      Controls for Acoustic droplet ejection.
//!
//! Built only when -ADE is set (the ADE is a plug-in module board on the
//! pulse-echo board).
//!

//---------------------------------------------------------------------------
// INCLUDES
//---------------------------------------------------------------------------

#include "ade.h"

#include "ade.pio.h"


PIO pio_ade;
uint sm4;

//---------------------------------------------------------------------------
// ADE INIT FUNCTION
//---------------------------------------------------------------------------
void ade_init()
{
    pio_ade = pio1;
    // Claim an unused PIO1 SM instead of hardcoding sm0: the shared pulser
    // (u4rk_acquisition_init) claims pio1 sm0/sm1 first, so a hardcoded sm0 here
    // would clobber the pulser drive SM and kill the transmit pulse.
    sm4 = pio_claim_unused_sm(pio_ade, true);
    uint offset = pio_add_program(pio_ade, &ade_program);
    ade_program_init(pio_ade, sm4, offset, ADE_CAMERA_TRIGGER, ADE_STROBE_TRIGGER);
    gpio_init(ADE_CAMERA_TRIGGER);
    gpio_init(ADE_STROBE_TRIGGER);
    gpio_init(ADE_MOTOR_STEP);
    gpio_init(ADE_MOTOR_DIR);
    gpio_init(ADE_ENABLE);
    gpio_set_dir(ADE_CAMERA_TRIGGER, GPIO_OUT);
    gpio_set_dir(ADE_STROBE_TRIGGER, GPIO_OUT);
    gpio_set_dir(ADE_MOTOR_STEP, GPIO_OUT);
    gpio_set_dir(ADE_MOTOR_DIR, GPIO_OUT);
    gpio_set_dir(ADE_ENABLE, GPIO_OUT);
    gpio_put(ADE_CAMERA_TRIGGER, 1);
    gpio_put(ADE_STROBE_TRIGGER, 0);
    gpio_put(ADE_MOTOR_STEP, 1);
    gpio_put(ADE_MOTOR_DIR, 1);
    gpio_put(ADE_ENABLE, 1);
} 
  
      
       
          
