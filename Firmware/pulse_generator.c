#include "pico/stdlib.h"
#include "pulse_generator.h"
#include "CC_Charger.h"
#include "hardware/pwm.h"

#define OUTPUT_EN_PIN 2
#define OUTPUT_CURRENT_TRIP_PIN 10
#define SPARK_THRESHOLD_PWM_PIN 14

bool cutting_enabled = false;
bool off_time_insufficient = false;

//Enum to keep track of where we are in the pulse sequence
enum pulse_state{   WAITING_FOR_IGNITION,
                    SPARK_ON,
                    SPARK_OFF};

enum pulse_state output_state = SPARK_OFF;

//Pulse definition waveforms
uint32_t pulse_on_time      = 0;
uint32_t pulse_off_time     = 0;
bool     iso_pulse_mode     = false;
uint32_t pulse_timeout_time = 0; //time in us

//Prototype functions
int64_t begin_off_time(alarm_id_t id, void *user_data);

//Set the CC timing to the standard charging parameters
int64_t change_CC_timing(alarm_id_t id, void *user_data){

    LIMIT_set_timing(185, 7, true);

    alarm_pool_cancel_alarm(alarm_pool_get_default(), id);

    return 0;

}

void output_current_trip_irq(void) {

    if (gpio_get_irq_event_mask(OUTPUT_CURRENT_TRIP_PIN) & GPIO_IRQ_EDGE_RISE) {

        gpio_acknowledge_irq(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE);

        gpio_set_irq_enabled(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE, false);   //Turn off the interrupt on the ignition sense

        output_state = SPARK_ON;                                                    //Update the pulse generator state

        add_alarm_in_us(pulse_on_time, begin_off_time, NULL, true);                 //Set alarm to turn off the pulse after the pulse on time

    }

}

int64_t begin_on_time(alarm_id_t id, void *user_data){

    disable_CC_timing();                                                            //Turn off the CC Charger

    if(cutting_enabled) {                                                           //Check that cutting is still enabled
        
        if(iso_pulse_mode) {                                                        //Check pulse mode is iso-pulse
            
            add_alarm_in_us(pulse_timeout_time, begin_off_time, NULL, true);        //Set the alarm in case of pulse timeout

            output_state = WAITING_FOR_IGNITION;                                    //Update pulse generator state

            gpio_put(OUTPUT_EN_PIN, true);                                          //Turn on output FET

            gpio_set_irq_enabled(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE, true);//Configure detection of the spark
            gpio_add_raw_irq_handler(OUTPUT_CURRENT_TRIP_PIN, &output_current_trip_irq);

        } else {                                                                    //If pulse mode is iso-tonic

            add_alarm_in_us(pulse_on_time, begin_off_time, NULL, true);             //Set alarm for the off transition

            output_state = SPARK_ON;                                                //Update pulse generator state

            gpio_put(OUTPUT_EN_PIN, true);                                          //Turn on output FET

        }
    } else {

        gpio_put(OUTPUT_EN_PIN, false);

    }

    alarm_pool_cancel_alarm(alarm_pool_get_default(), id);

    return 0;

}


int64_t begin_off_time(alarm_id_t id, void *user_data){

    if(output_state == WAITING_FOR_IGNITION || iso_pulse_mode == false) {               //Spark has timed out or if iso pulse is off, restart pulse cycle

        gpio_put(OUTPUT_EN_PIN, false);                                                 //Turn off output MOSFET

        output_state = SPARK_OFF;                                                       //Update pulse generator state

        gpio_set_irq_enabled(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE, false);       //Disable the ignition sense irq

        LIMIT_set_timing(97, 7, false);                                                 //Limit CC Charger PWM duty cycle to avoid inrush (was 97)

        enable_CC_timing();                                                             //Start CC Charger

        add_alarm_in_us(15, change_CC_timing, NULL, true);                              //Setup the alarm to correct CC charger timing
        add_alarm_in_us(pulse_off_time-11, begin_on_time, NULL, true);                     //Setup the alarm to turn on the output MOSFET after the off time

    } else if(output_state == SPARK_ON && iso_pulse_mode == true) {                     //Spark is already ignited, ignore this timer callback

        //Do nothing

    }

    alarm_pool_cancel_alarm(alarm_pool_get_default(), id);

    return 0;

}

void first_begin_off_time() {

    LIMIT_set_timing(97, 7, false);                                                 //Set the CC Charger PWM max duty cycle lower to prevent inrush

    enable_CC_timing();                                                             //Start the CC Charger

    add_alarm_in_us(15, change_CC_timing, NULL, false);                              //Setup the alarm to correct CC charger timing
    add_alarm_in_us(pulse_off_time-11, begin_on_time, NULL, true);                     //Setup the alarm to turn on the output MOSFET after the off time
}

void begin_output_pulses(uint32_t on_time, uint32_t off_time, bool iso_pulse) {

    //Load in the pulse parameters
    pulse_on_time = on_time;
    pulse_off_time = off_time;
    pulse_timeout_time = pulse_on_time / 2;
    iso_pulse_mode = iso_pulse;

    //Update pulse state based on pulse mode
    if(iso_pulse_mode) {
        output_state = WAITING_FOR_IGNITION;
    } else {
        output_state = SPARK_OFF;
    }

    first_begin_off_time();

}

void pulse_generator_init(uint32_t trip_current) {

    gpio_init(OUTPUT_EN_PIN);
    gpio_set_dir(OUTPUT_EN_PIN, GPIO_OUT);

    gpio_init(OUTPUT_CURRENT_TRIP_PIN);
    gpio_set_dir(OUTPUT_CURRENT_TRIP_PIN, GPIO_IN);

    gpio_init(1);
    gpio_set_dir(1, GPIO_OUT);

}