#ifndef CC_CHARGER_H_INCLUDED
#define CC_CHARGER_H_INCLUDED

#include "pico/stdlib.h"

extern bool caps_charged;

/*! \brief Disable Constant Current Charger Gate Driver, turns off charger (timing signals still on) prevents ringing on VS_DIODE
    \ingroup CC_Charger
*/
void disable_gate_driver();

/*! \brief Disable Constant Current Charger timing outputs, effectively turns off CC charger
    \ingroup CC_Charger
*/
void disable_CC_timing();

/*! \brief Enable Constant Current Charger timing outputs, effectively turns on CC charger
    \ingroup CC_Charger
*/
void enable_CC_timing(bool diode_on);

/*! \brief Sets the CC charger clock period, indirectly sets the switching frequency of the CC charger
    \ingroup CC_Charger

    \param period Clock period = 3 * (1 + period) / Sys_clock (default 125MHz)
*/
void CLK_set_period(uint32_t period);

/*! \brief Sets the timing for the PWM limiting output
    \ingroup CC_Charger

    \param wait_time output low time = 2 * (1 + wait_time) / Sys_clock (default 125MHz)
    \param on_time output high time = (1 + 2 * (1 * on_time)) / Sys_clock (default 125MHz) 
*/
void LIMIT_set_timing(uint32_t wait_time, uint32_t on_time, bool enabled);

/*! \brief Sets the timing for the PWM limiting output
    \ingroup CC_Charger

    \param wait_time output low time = 2 * (1 + wait_time) / Sys_clock (default 125MHz)
    \param on_time output high time = (1 + 2 * (1 * on_time)) / Sys_clock (default 125MHz) 
*/
void BLANKING_set_timing(uint32_t wait_time, uint32_t on_time, bool enabled);

/*! \brief Setup the CC charger
    \ingroup CC_Charger
*/
void CC_Charger_init(int charger_current, int cap_voltage);

/*! \brief Whether or not the output caps are at the set voltage
    \ingroup CC_charger
*/

#endif