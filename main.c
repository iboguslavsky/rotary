#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

#include "dbg_putchar.h"

// Turn on/off a corresponding bit
#define ON(byte, bit) (byte |= (1 << bit))
#define OFF(byte, bit) (byte &= ~(1 << bit))
#define ROTARY PB5

// How many ADC conversions before the level is considered settled?
// This sets minimum recognized rotation speed
#define TRESHOLD 30

// Setup STDOUT so that printf(), etc work - costs about 300 bytes
static FILE mystdout = FDEV_SETUP_STREAM (dbg_putchar, NULL, _FDEV_SETUP_WRITE);

void setup_timer1 ();
void setup_adc ();

enum rotary_t {IDLE, BUTTON, FORWARD, BACKWARD};
enum state_t {OFF, S1, S2, S1S2, BUTTON_DWN = 0b00111111};

#define FORWARD  (OFF << 6 | S2 << 4 | S1S2 << 2 | S1)
#define BACKWARD (OFF << 6 | S1 << 4 | S1S2 << 2 | S2)
#define BUTTON   (OFF << 6 | BUTTON_DWN)

volatile unsigned char flag = IDLE;

ISR (ADC_vect) {
unsigned int adch;
enum state_t state = OFF; 
static enum state_t prev_state = OFF;
static uint8_t count = 0, vec = 0;

  adch = ADCH;

  if (adch == 0) state = BUTTON_DWN;
    else 
      if (adch > 0 && adch < 100) state = S1S2;
        else 
          if (adch >= 100 && adch < 150) state = S2;
	    else
  	      if (adch >= 150 && adch < 200) state = S1;
		else 
  		  if (adch >= 200) state = OFF;

  // State is holding steady
  if (state == prev_state) {
    if (count == 255) return;
    count++;
  }
  else {

    // Still bouncing
    if (count < TRESHOLD) {
       prev_state = state;
       count = 1;
       return;
    }

    // Transition complete. Put it in the FIFO
    if (prev_state == BUTTON_DWN) {
      vec = BUTTON_DWN;
    }
    else { 
      vec <<= 2; vec |= ((uint8_t) prev_state) & 0b00000011; 
    }
    
    // Button has just one transition (BUTTON_DWN -> OFF)
    if (vec == BUTTON)
      flag = BUTTON;
    
    if (vec == FORWARD)
      flag = FORWARD;

    if (vec == BACKWARD)
      flag = BACKWARD;

    prev_state = state;
    count = 1;
  }
}

ISR (TIMER1_OVF_vect) {
// uint8_t lower, higher;

  // Fire off ADC conversion
  ON (ADCSRA, ADSC);

  // Wait for results (when ADSC is 0 again)
  while (ADCSRA & (1 << ADSC)) {
  }

  // lower = ADCL; higher = ADCH;
  // printf ("TEMP: %d C\r\n", (higher * 256 + lower) - 300 + 25);

  // just 8 bit resolution
  printf ("ADC: %d\r\n", ADCH);
}

int main(void) {

  stdout = &mystdout;

  dbg_tx_init ();

  // setup_timer1 ();
  // printf ("Timer1 setup OK\r\n");

  setup_adc ();
  printf ("ADC setup OK\r\n");

  sei ();

  // Fire off ADC conversion
  ON (ADCSRA, ADSC);

  while (1) {

    if (flag) {

      if (flag == FORWARD)
        printf ("FORWARD\r\n");

      if (flag == BACKWARD)
        printf ("BACKWARD\r\n");

      if (flag == BUTTON)
        printf ("BUTTON\r\n");

      flag = IDLE;
    }
  }

  return 0;
}


void setup_timer1 () {

  // Tclk = CLK / 16384 ~0.5ms
  ON (TCCR1, CS13);
  ON (TCCR1, CS12);
  ON (TCCR1, CS11);
  ON (TCCR1, CS10);

  // Enable Overflow IRQ
  // ON (TIMSK, TOIE1);
}

void setup_adc () {

  // 2.56V internal voltage ref 
  ON (ADMUX, REFS2);
  ON (ADMUX, REFS1);
  OFF (ADMUX, REFS0);

  // 1.1V internal voltage ref 
  // OFF (ADMUX, REFS2);
  // ON (ADMUX, REFS1);
  // OFF (ADMUX, REFS0);

  // Use internal TEMP sensor
  ON (ADMUX, MUX3);
  ON (ADMUX, MUX2);
  ON (ADMUX, MUX1);
  ON (ADMUX, MUX0);

  // USE ADC2 (PB4) Single - ended input
  OFF (ADMUX, MUX3);
  OFF (ADMUX, MUX2);
  ON  (ADMUX, MUX1);
  OFF (ADMUX, MUX0);

  // Left-adjust the conversion results (only use 8bit resolution)
  ON (ADMUX, ADLAR);

  // Prescaler set to 64 (125 Khz conv clock, ~0.1ms per conversion)
  ON (ADCSRA, ADPS2);
  ON (ADCSRA, ADPS1);
  OFF (ADCSRA, ADPS0);

  // Enable auto-triggering of ADC conversions (for now)
  OFF (ADCSRB, ADTS0);
  OFF (ADCSRB, ADTS1);
  OFF (ADCSRB, ADTS2);

  ON (ADCSRA, ADATE);

  // Enable ADC interrupts
  ON (ADCSRA, ADIE);

  // Enable ADC
  ON (ADCSRA, ADEN);
}
