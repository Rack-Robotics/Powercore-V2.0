#ifndef PULSE_GENERATOR_H_INCLUDED
#define PULSE_GENERATOR_H_INCLUDED

#include "pico/stdlib.h"

extern bool cutting_enabled;
extern bool short_tripped;

/*! \brief Turn off the short alert board output
    \ingroup pulse_generator
*/
void short_alert_off();

/*! \brief Start the EDM machine, ie, begin cutting
    \ingroup pulse_generator
*/
void begin_output_pulses(uint32_t on_time, uint32_t off_time, bool iso_pulse);

/*! \brief Setup pins/dirs for the pulse generator
    \ingroup pulse_generator
*/
void pulse_generator_init(uint32_t trip_current);

#endif