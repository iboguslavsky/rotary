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
// This sets minimum recognized rotation speed (and click/double click for the button)
#define TRESHOLD_ROTARY 10 
#define TRESHOLD_BUTTON 200

// Max number of ADC conversions ("ticks") in between button clicks to consider it a double-click
#define TRESHOLD_DOUBLE_CLICK 2000

// Setup STDOUT so that printf(), etc work - costs about 300 bytes
static FILE mystdout = FDEV_SETUP_STREAM (dbg_putchar, NULL, _FDEV_SETUP_WRITE);

void setup_timer1 ();
void setup_adc ();

enum state_t {IDLE, S1, S2, S1S2, BTTN_DWN, BTTN_DBL};

#define FORWARD  (IDLE << 6 | S2 << 4 | S1S2 << 2 | S1)
#define BACKWARD (IDLE << 6 | S1 << 4 | S1S2 << 2 | S2)

volatile uint8_t fifo = IDLE;
volatile uint8_t button = IDLE;

ISR (ADC_vect) {
unsigned int adch;
enum state_t state = IDLE; 
static enum state_t prev_state = IDLE;
static uint8_t count = 0;
static uint16_t ticks = 0;

  adch = ADCH;

  // ADC tresholds for various button states
  if (adch == 0) state = BTTN_DWN;
    else 
      if (adch > 0 && adch < 100) state = S1S2;
        else 
          if (adch >= 100 && adch < 150) state = S2;
	    else
  	      if (adch >= 150 && adch < 200) state = S1;
		else 
  		  if (adch >= 200) state = IDLE;

  // Track button double-click
  if (ticks) {
    // If we ended up here, the second button click never came
    // It's a single click then!
    if (++ticks >= TRESHOLD_DOUBLE_CLICK) {
      button = BTTN_DWN;
      ticks = 0;
    }
  }

  // State is holding steady
  if (state == prev_state) {
    if (count == 255) return;
    count++;
  }

  // State change
  else {

    // Button has separate treshold and just one transition (BUTTON_DWN -> IDLE)
    if (prev_state == BTTN_DWN && count >= TRESHOLD_BUTTON) {

      // First click? Start counter and wait for double click - or timeout
      if (!ticks)
        ticks++;
      else 
	// Second click within TRESHOLD_DOUBLE_CLICK ticks?
        if (ticks < TRESHOLD_DOUBLE_CLICK) {
          ticks = 0;
          button = BTTN_DBL;
	}
    }
    else 

    // Has state been stable long enough (not bouncing)?
    if (count >= TRESHOLD_ROTARY) {

      // Put state in the FIFO, where we keep pattern of states for grey code rotary
      fifo <<= 2; fifo |= ((uint8_t) prev_state) & 0b00000011; 
    }

    prev_state = state;
    count = 1;
  }
}

int main(void) {

  stdout = &mystdout;

  dbg_tx_init ();

  setup_adc ();
  printf ("ADC setup OK\r\n");

  sei ();

  // Fire off ADC conversion
  ON (ADCSRA, ADSC);

  while (1) {

    // Check FIFO for recognizable patterns
    if (fifo == FORWARD) {

      fifo = IDLE;
      puts ("FORWARD\r");
    }
    else 
      if (fifo == BACKWARD) {

        fifo = IDLE;
        puts ("BACKWARD\r");
      }

    if (button == BTTN_DWN) {

       button = IDLE;
       puts ("CLICK\r");
    }
    else 
      if (button == BTTN_DBL) {

         button = IDLE;
         puts ("DOUBLE CLICK\r");
      }
  }

  return 0;
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
  // ON (ADMUX, MUX3);
  // ON (ADMUX, MUX2);
  // ON (ADMUX, MUX1);
  // ON (ADMUX, MUX0);

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
