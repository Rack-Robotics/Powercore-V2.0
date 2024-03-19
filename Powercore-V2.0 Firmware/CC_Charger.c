#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "CC_Charger.h"
#include "hardware/adc.h"

// Our assembled programs:
#include "CLK.pio.h"
#include "TIMING.pio.h"

//Pinout Definitions

//CC_Charger Timing:
#define CLK_PIN 6
#define LIMIT_PIN 9
#define BLANKING_PIN 13

//CC_Charger Control:
#define CC_CHARGER_EN_PIN 5

//Ideal Diode Controls:
#define DIODE_ON_PIN 4

//Gate Bias PWM:
#define GATE_BIAS_CLK_PIN 3

//PWM Voltage Sets:
#define CC_I_LIMIT_PIN 15
#define V_CAP_SET_PIN 14

//Telemetry:
#define VSENSE_TRIP_PIN 12

PIO pio;

int sm, sm_clk, sm_limit, sm_blanking;
uint offset_clk, offset_timing;

uint32_t default_clk_period         = 133;                                                      //3.216usec
uint32_t default_limit_wait_time    = 185;                                                      //3.024usec
uint32_t default_limit_on_time      = 7;                                                        //0.136usec
uint32_t default_blanking_wait_time = 185;                                                      //0.112usec
uint32_t default_blanking_on_time   = 12;                                                       //3.032usec

bool caps_charged = false;

void VSENSE_TRIP_callback(void) {

    if(gpio_get_irq_event_mask(VSENSE_TRIP_PIN) & GPIO_IRQ_EDGE_RISE) {
        
        gpio_acknowledge_irq(VSENSE_TRIP_PIN, GPIO_IRQ_EDGE_RISE);

        //Flag that the caps have been charged
        caps_charged = true;

        //Turn off the CC_Charger
        disable_CC_timing();
    }

    if(gpio_get_irq_event_mask(VSENSE_TRIP_PIN) & GPIO_IRQ_EDGE_FALL) {
        
        gpio_acknowledge_irq(VSENSE_TRIP_PIN, GPIO_IRQ_EDGE_FALL);

        //Flag that the caps are not fully charged
        caps_charged = false;

    }

}

void disable_gate_driver() {
    gpio_put(CC_CHARGER_EN_PIN, false);
}

void disable_CC_timing() {

    gpio_put(DIODE_ON_PIN, false);                                                              //Turn off ideal diode

    pio_sm_set_enabled(pio, sm_clk, false);                                                     //Disable CC Charger Clock
    pio_sm_exec(pio, sm_clk, pio_encode_nop() | pio_encode_sideset_opt(1, 0));                  //Set Clock output to 0
    pio_sm_exec(pio, sm_clk, pio_encode_irq_set(false, 0));

    pio_sm_set_enabled(pio, sm_limit, false);                                                   //Disable PWM limit SM
    pio_sm_exec(pio, sm_limit, pio_encode_nop() | pio_encode_sideset_opt(1, 1));                //Set PWM limit to 1 (keeps !CLR on Flip Flop low)

    pio_sm_set_enabled(pio, sm_blanking, false);                                                //Disable Slope blanking/reset 
    pio_sm_exec(pio, sm_blanking, pio_encode_nop() | pio_encode_sideset_opt(1, 1));             //Set Slope blanking to  (Allows the compensated current limit to decay to 0)

}

void enable_CC_timing(bool diode_on) {

    //Don't start the charger if the caps are already charged
    if(caps_charged == false){

        //Reset all PIOs to the start of the program:
        pio_sm_exec(pio, sm_clk,        pio_encode_jmp(offset_clk   ));
        pio_sm_exec(pio, sm_blanking,   pio_encode_jmp(offset_timing));
        pio_sm_exec(pio, sm_limit,      pio_encode_jmp(offset_timing));

        //Renable PIOs
        pio_sm_set_enabled(pio, sm_limit,       true);
        pio_sm_set_enabled(pio, sm_blanking,    true);
        pio_sm_set_enabled(pio, sm_clk,         true);

        gpio_put(CC_CHARGER_EN_PIN, true);                                                      //Enable the CC Charger gate driver

        gpio_put(DIODE_ON_PIN, diode_on);                                                           //Turn on the ideal diode
    }

}

void CLK_set_period(uint32_t period) {

    pio_sm_put_blocking(pio, sm_clk, period);                                                   //Load new period into FIFO
    pio_sm_exec(pio, sm_clk, pio_encode_pull(false, false));                                    //Pull new period into OSR
    pio_sm_exec(pio, sm_clk, pio_encode_out(pio_isr, 32));                                      //Shift new period from OSR to ISR

    pio_sm_put_blocking(pio, sm_clk, period/2);                                                 //Load new on time into FIFO        
    pio_sm_exec(pio, sm_clk, pio_encode_pull(false, false));                                    //Pull new on time into OSR
    pio_sm_exec(pio, sm_clk, pio_encode_out(pio_x, 32));                                        //Shift new on time into X register

}

void SM_set_timing(PIO pio, uint sm, uint32_t wait_time, uint32_t on_time, bool enabled) {

    pio_set_sm_mask_enabled(pio, sm_clk | sm_blanking | sm_limit, false);
    // pio_sm_set_enabled(pio, sm, false);                                                      //Temporarily freeze SM

    pio_sm_put_blocking(pio, sm, wait_time);                                                    //Load new wait time into FIFO
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));                                        //Pull new wait time into OSR
    pio_sm_exec(pio, sm, pio_encode_out(pio_isr, 32));                                          //Shift new wait time into ISR
    
    pio_sm_put_blocking(pio, sm, on_time);                                                      //Load new on time into FIFO
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));                                        //Pull new wait time into OSR

    // pio_sm_set_enabled(pio, sm, enabled);                                                    //Renable SM
    pio_set_sm_mask_enabled(pio, sm_clk | sm_blanking | sm_limit, enabled);

}

void LIMIT_set_timing(uint32_t wait_time, uint32_t on_time, bool enabled) {

    SM_set_timing(pio, sm_limit, wait_time, on_time, enabled);                                  //Update timing registers in SM

}

void BLANKING_set_timing(uint32_t wait_time, uint32_t on_time, bool enabled) {

    SM_set_timing(pio, sm_blanking, wait_time, on_time, enabled);                               //Update timing registers in SM

}

void CC_Charger_init(int charger_current, int cap_voltage) {

    //Select a PIO bank
    pio = pio0;

    //Load programs into PIO memory
    offset_clk     = pio_add_program(pio, &CLK_program);
    offset_timing  = pio_add_program(pio, &TIMING_program);

    //Select state machines for each output
    sm_clk      = pio_claim_unused_sm(pio, true);
    sm_limit    = pio_claim_unused_sm(pio, true);
    sm_blanking = pio_claim_unused_sm(pio, true);

    //Initialize each state machine
    CLK_program_init    (pio,   sm_clk,         offset_clk,     CLK_PIN         );
    TIMING_program_init (pio,   sm_limit,       offset_timing,  LIMIT_PIN       );
    TIMING_program_init (pio,   sm_blanking,    offset_timing,  BLANKING_PIN    );

    //Load in default timing
    CLK_set_period      (default_clk_period);
    LIMIT_set_timing    (default_limit_wait_time,       default_limit_on_time,      false);
    BLANKING_set_timing (default_blanking_wait_time,    default_blanking_on_time,   false);

    //Setup VSENSE_TRIP pin
    gpio_init(VSENSE_TRIP_PIN);
    gpio_set_dir(VSENSE_TRIP_PIN, GPIO_IN);
    gpio_set_pulls(VSENSE_TRIP_PIN, false, true);

    gpio_set_irq_enabled(VSENSE_TRIP_PIN, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
    gpio_add_raw_irq_handler(VSENSE_TRIP_PIN, &VSENSE_TRIP_callback);

    //Setup CC_CHARGER_EN pin
    gpio_init(CC_CHARGER_EN_PIN);
    gpio_set_dir(CC_CHARGER_EN_PIN, GPIO_OUT);

    gpio_put(CC_CHARGER_EN_PIN, false); 

    //Setup PWM -> Analog pins
    gpio_set_function(CC_I_LIMIT_PIN, GPIO_FUNC_PWM);
    gpio_set_function(V_CAP_SET_PIN, GPIO_FUNC_PWM);

    uint v_i_set_pwm_slice = pwm_gpio_to_slice_num(V_CAP_SET_PIN);

    pwm_set_wrap(v_i_set_pwm_slice, 1000);

    pwm_set_gpio_level(CC_I_LIMIT_PIN, charger_current);
    pwm_set_gpio_level(V_CAP_SET_PIN, cap_voltage);

    pwm_set_enabled(v_i_set_pwm_slice, true);

    //Setup Ideal Diode driver
    gpio_init(DIODE_ON_PIN);
    gpio_set_dir(DIODE_ON_PIN, GPIO_OUT);

    gpio_put(DIODE_ON_PIN, false);

    //Setup Gate Bias Charge Pump
    gpio_set_function(GATE_BIAS_CLK_PIN, GPIO_FUNC_PWM);

    uint bias_pwm_slice_num = pwm_gpio_to_slice_num(GATE_BIAS_CLK_PIN);
    pwm_set_wrap(bias_pwm_slice_num, 2500);

    pwm_set_gpio_level(GATE_BIAS_CLK_PIN, 1250);

    pwm_set_enabled(bias_pwm_slice_num, true);

}