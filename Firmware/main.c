#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/pwm.h"

#include "CC_Charger.h"
#include "pulse_generator.h"

//Pinout definitions
#define CUT_nEN_PIN 0

//OUTPUT PULSE PARAMETERS (times in usecs)
#define OUTPUT_ON_TIME 20
#define OUTPUT_OFF_TIME 80
#define ISO_PULSE false
#define CAP_VOLTAGE_SETPOINT 65         //Don't set higher than 65

//CC Charger Parameters (don't change unless you know what you're doing)
#define CHARGER_CURRENT 850

//Math stuff DO NOT EDIT
#define CAP_VOLTAGE_PWM_LEVEL CAP_VOLTAGE_SETPOINT * 303 / 48

bool tripped = false;

void cut_on_off_irq(void) {
    
    if(gpio_get_irq_event_mask(CUT_nEN_PIN) & GPIO_IRQ_EDGE_FALL) {

        gpio_acknowledge_irq(CUT_nEN_PIN, GPIO_IRQ_EDGE_FALL);

        cutting_enabled = true;
        begin_output_pulses(OUTPUT_ON_TIME, OUTPUT_OFF_TIME, ISO_PULSE);
    }

    if(gpio_get_irq_event_mask(CUT_nEN_PIN) & GPIO_IRQ_EDGE_RISE) {

        gpio_acknowledge_irq(CUT_nEN_PIN, GPIO_IRQ_EDGE_RISE);

        cutting_enabled = false;
    }
}

void default_gpio_callback(uint gpio, uint32_t event_mask) {
    gpio_acknowledge_irq(gpio, event_mask);
    return;
}

int main() {

    // stdio_init_all();                                                                    //Start the Serial console

    // gpio_init(CUT_nEN_PIN);
    // gpio_set_dir(CUT_nEN_PIN, GPIO_IN);
    // gpio_set_pulls(cut_NEN, true, false);

    sleep_ms(1000);

    // while(gpio_get(23)) {}

    CC_Charger_init(CHARGER_CURRENT, CAP_VOLTAGE_PWM_LEVEL);                                //Setup the CC Charger Outputs

    pulse_generator_init(10);

    gpio_init(CUT_nEN_PIN);
    gpio_set_dir(CUT_nEN_PIN, GPIO_IN);
    gpio_set_pulls(CUT_nEN_PIN, true, false);

    gpio_init(1);
    gpio_set_dir(1, GPIO_OUT);
    gpio_put(1, false);

    irq_set_enabled(IO_IRQ_BANK0, true);

    sleep_ms(1000);

    gpio_set_irq_enabled(CUT_nEN_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_add_raw_irq_handler(CUT_nEN_PIN, &cut_on_off_irq);

    while (true) {
        
        // sleep_us(10);
        // gpio_put(3, true);
        // sleep_us(10);
        // gpio_put(3, false);
    }
}