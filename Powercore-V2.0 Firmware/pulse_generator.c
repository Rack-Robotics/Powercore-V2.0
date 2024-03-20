#include "pico/stdlib.h"
#include "pulse_generator.h"
#include "CC_Charger.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"

#define OUTPUT_EN_PIN 2
#define OUTPUT_CURRENT_TRIP_PIN 11
#define SPARK_THRESHOLD_PWM_PIN 10
#define SHORT_ALERT_PIN 1

#define CAP_VSENSE_PIN 27

#define SHORT_THRESHOLD 513

bool cutting_enabled = false;
bool short_tripped = false; 

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

alarm_id_t timeout_alarm_id;

//Pulse history tracking (512 cycles, 1 bit per cycle)
uint32_t pulse_history[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint32_t pulse_counter = 0;

//Prototype functions
int64_t begin_off_time(alarm_id_t id, void *user_data);

//Set the CC timing to the standard charging parameters
int64_t change_CC_timing(alarm_id_t id, void *user_data){

    LIMIT_set_timing(185, 7, true);

    return 0;

}

void short_alert_off() {

    pwm_set_gpio_level(SHORT_ALERT_PIN, 0);

}

void output_current_trip_irq(void) {

    if (gpio_get_irq_event_mask(OUTPUT_CURRENT_TRIP_PIN) & GPIO_IRQ_EDGE_RISE) {

        gpio_acknowledge_irq(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE);

        gpio_set_irq_enabled(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE, false);   //Turn off the interrupt on the ignition sense

        output_state = SPARK_ON;                                                    //Update the pulse generator state

        if(iso_pulse_mode == true){

            cancel_alarm(timeout_alarm_id);                                         //Turn off timeout alarm
            add_alarm_in_us(pulse_on_time, begin_off_time, NULL, true);             //Set alarm to turn off the pulse after the pulse on time

        }

    }

}

int64_t begin_on_time(alarm_id_t id, void *user_data){

    disable_CC_timing();                                                            //Turn off the CC Charger

    if(cutting_enabled) {                                                           //Check that cutting is still enabled
        
        output_state = WAITING_FOR_IGNITION;                                        //Update pulse generator state

        gpio_set_irq_enabled(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE, true);    //Configure detection of the spark

        if(iso_pulse_mode) {                                                        //Check pulse mode is iso-pulse
            
            timeout_alarm_id = add_alarm_in_us(pulse_timeout_time, begin_off_time, NULL, true);        //Set the alarm in case of pulse timeout

        } else {                                                                    //If pulse mode is iso-tonic

            add_alarm_in_us(pulse_on_time, begin_off_time, NULL, true);             //Set alarm for the off transition

        }

        gpio_put(OUTPUT_EN_PIN, true);                                              //Turn on output FET

    } else {

        gpio_put(OUTPUT_EN_PIN, false);

    }

    return 0;

}


int64_t begin_off_time(alarm_id_t id, void *user_data){

    gpio_put(OUTPUT_EN_PIN, false);                                                 //Turn off output MOSFET

    gpio_set_irq_enabled(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE, false);       //Disable the ignition sense irq

    LIMIT_set_timing(97, 7, false);                                             //Limit CC Charger PWM duty cycle to avoid inrush (was 97)

    enable_CC_timing(true);                                                             //Start CC Charger

    add_alarm_in_us(15, change_CC_timing, NULL, true);                          //Setup the alarm to correct CC charger timing
    add_alarm_in_us(pulse_off_time-11, begin_on_time, NULL, true);                  //Setup the alarm to turn on the output MOSFET after the off time

    pulse_counter -= pulse_history[0] & (uint32_t)0x1;                                          //Subtract the 512th pulse state from the counter

    for(int i = 0; i < 15; i++) {                                                        

        pulse_history[i] = ((pulse_history[i+1] & (uint32_t)0x1) << 31) + (pulse_history[i] >> 1);   //Binary shift the whole array (left shift each int and load in the first bit of the one above it)

    }

    pulse_history[15] = pulse_history[15] >> 1;                                       //Binary shift the last value, done outside the for loop because nowhere to pull the MSB from

    if(output_state == SPARK_ON) {                                                  //If successful spark then load a 1 into the MSB of the shift register and increment pulse counter

        pulse_history[15] = pulse_history[15] + (1 << 31);

        pulse_counter += 1;

    }

    output_state = SPARK_OFF;                                                       //Set state machine state to SPARK_OFF

    if(pulse_counter > SHORT_THRESHOLD) {
        cutting_enabled = false;
        short_tripped = true;
        disable_CC_timing();
        disable_gate_driver();
        pwm_set_gpio_level(SHORT_ALERT_PIN, 512);
    }else {
        pwm_set_gpio_level(SHORT_ALERT_PIN, pulse_counter);
    }

    return 0;

}

void first_off_time(){

    gpio_put(OUTPUT_EN_PIN, false);                                                 //Turn off output MOSFET

    gpio_set_irq_enabled(OUTPUT_CURRENT_TRIP_PIN, GPIO_IRQ_EDGE_RISE, false);       //Disable the ignition sense irq

    LIMIT_set_timing(97, 7, false);                                                 //Limit CC Charger PWM duty cycle to avoid inrush (was 97)

    enable_CC_timing(true);                                                             //Start CC Charger

    add_alarm_in_us(15, change_CC_timing, NULL, true);                              //Setup the alarm to correct CC charger timing
    add_alarm_in_us(pulse_off_time-11, begin_on_time, NULL, true);                  //Setup the alarm to turn on the output MOSFET after the off time

}

void begin_output_pulses(uint32_t on_time, uint32_t off_time, bool iso_pulse) {

    //Load in the pulse parameters
    pulse_on_time = on_time;
    pulse_off_time = off_time;
    pulse_timeout_time = pulse_on_time / 2;
    iso_pulse_mode = iso_pulse;

    for (int i = 0; i < 15; i++)
        pulse_history[i] = 0;
    pulse_counter = 0;

    first_off_time();

}

void pulse_generator_init(uint32_t trip_current) {

    gpio_init(OUTPUT_EN_PIN);
    gpio_set_dir(OUTPUT_EN_PIN, GPIO_OUT);

    gpio_init(OUTPUT_CURRENT_TRIP_PIN);
    gpio_set_dir(OUTPUT_CURRENT_TRIP_PIN, GPIO_IN);

    gpio_set_function(SHORT_ALERT_PIN, GPIO_FUNC_PWM);

    pwm_set_wrap(pwm_gpio_to_slice_num(SHORT_ALERT_PIN), 512);

    pwm_set_gpio_level(SHORT_ALERT_PIN, 0);

    pwm_set_enabled(pwm_gpio_to_slice_num(SHORT_ALERT_PIN), true);

    gpio_set_function(SPARK_THRESHOLD_PWM_PIN, GPIO_FUNC_PWM);

    pwm_set_wrap(pwm_gpio_to_slice_num(SPARK_THRESHOLD_PWM_PIN), 1000);

    pwm_set_gpio_level(SPARK_THRESHOLD_PWM_PIN, trip_current);

    pwm_set_enabled(pwm_gpio_to_slice_num(SPARK_THRESHOLD_PWM_PIN), true);

    gpio_add_raw_irq_handler(OUTPUT_CURRENT_TRIP_PIN, &output_current_trip_irq);

    adc_gpio_init(CAP_VSENSE_PIN);

}