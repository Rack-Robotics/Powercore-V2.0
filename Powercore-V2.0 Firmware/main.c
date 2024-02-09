#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"

#include "CC_Charger.h"
#include "pulse_generator.h"

// digital definitions
#define CUT_nEN_PIN 0

// analog definitions
#define GPIO29_ADC3_PIN 29
#define GPIO29_ADC3_CHANNEL 3

//OUTPUT PULSE PARAMETERS (times in usecs)
#define OUTPUT_ON_TIME 20
#define OUTPUT_OFF_TIME_MIN 64
#define OUTPUT_OFF_TIME_MAX 512
#define ISO_PULSE false
#define CAP_VOLTAGE_SETPOINT 70

//CC Charger Parameters (don't change unless you know what you're doing)
#define CHARGER_CURRENT 850

//Math stuff DO NOT EDIT
#define CAP_VOLTAGE_PWM_LEVEL CAP_VOLTAGE_SETPOINT * 303 / 48

// initiate GPIO pin 29, ADC channel 3
void gpio29_adc3_init() {

    // initialize ADC
    adc_init();

    // make sure GPIO is high-impedance, no pull-ups
    adc_gpio_init(GPIO29_ADC3_PIN);

}

// read ADC value from GPIO pin 29 channel 3, and return the value as a 16-bit +integer
uint16_t gpio29_adc3_read() {

    // select ADC channel
    adc_select_input(GPIO29_ADC3_CHANNEL);

    // read ADC value
    return adc_read();

}

bool cut_on_off_irq(repeating_timer_t *rt) {

    if(short_tripped) {

        if(gpio_get(CUT_nEN_PIN)) {

            short_tripped = false;
            short_alert_off();

        }

    } else {

        if(!gpio_get(CUT_nEN_PIN) && !cutting_enabled) {

            cutting_enabled = true;

            // read adc channel 3, and map value from OUTPUT_OFF_TIME_MIN to OUTPUT_OFF_TIME_MAX
            uint16_t OUTPUT_OFF_TIME = gpio29_adc3_read() * (OUTPUT_OFF_TIME_MAX - OUTPUT_OFF_TIME_MIN) / 4095 + OUTPUT_OFF_TIME_MIN;

            begin_output_pulses(OUTPUT_ON_TIME, OUTPUT_OFF_TIME, ISO_PULSE);

        } else if(gpio_get(CUT_nEN_PIN)) { 

            cutting_enabled = false;
            disable_gate_driver();

        }

    }

    return true;

}

void default_gpio_callback(uint gpio, uint32_t event_mask) {

    gpio_acknowledge_irq(gpio, event_mask);

    return;

}

int main() {

    sleep_ms(1000);

    gpio29_adc3_init();

    CC_Charger_init(CHARGER_CURRENT, CAP_VOLTAGE_PWM_LEVEL);                                //Setup the CC Charger Outputs

    pulse_generator_init(70);                                                               //Setup Pulse generator

    gpio_init(CUT_nEN_PIN);
    gpio_set_dir(CUT_nEN_PIN, GPIO_IN);
    gpio_set_pulls(CUT_nEN_PIN, false, false);

    irq_set_enabled(IO_IRQ_BANK0, true);

    sleep_ms(1000);

    repeating_timer_t timer;

    add_repeating_timer_ms(100, cut_on_off_irq, NULL, &timer);

    while (true) {
        //All executed code is contained in interrupts / timers
    }
}