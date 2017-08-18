#include <stdint.h>
#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>

#include "dbg_putchar.h"

// Turn on/off a corresponding bit
#define ON(byte, bit) (byte |= (1 << bit))
#define OFF(byte, bit) (byte &= ~(1 << bit))

// How many ADC conversions before the level is considered settled?
// This sets minimum recognized rotation speed (and click/double click for the button)
#define TRESHOLD_ROTARY 5 
#define TRESHOLD_BUTTON 8000
#define TRESHOLD_LONG_CLICK 65534

#define SPACE_BETWEEN_BUCKETS 50

// Max number of ADC conversions ("ticks") in between button clicks to consider it a double-click
#define TRESHOLD_DOUBLE_CLICK 3000

void setup_adc ();

enum state_t {IDLE, S1, S2, S1S2, BTTN_DWN};

#define FORWARD  (S2 << 6 | S1S2 << 4 | S1 << 2 | IDLE)
#define BACKWARD (S1 << 6 | S1S2 << 4 | S2 << 2 | IDLE)

volatile uint8_t fifo = IDLE;
volatile uint8_t button = IDLE;

volatile uint8_t calibrating = 0;
volatile uint16_t states[5][2];

void bubbleSort16 (uint16_t arr[], uint16_t n);
void swap16 (uint16_t *xp, uint16_t *yp);

ISR (ADC_vect) {
uint16_t adc;
static uint16_t candidate_cnt = 0;
static int8_t state, prev_state = -1, candidate_state;
static uint16_t dbl_click = 0, long_click = 0;

// Calibration-related
static uint8_t size = 0;
uint8_t found;
static uint8_t allocated = 0;
static uint16_t *keys;
static uint16_t *values;
uint16_t *tmp;
uint16_t *tmpkeys;

  adc = ADCL;
  adc |= (ADCH << 8);

  if (calibrating) {

    if (!allocated) {

      keys = calloc (32, sizeof (uint16_t));
      values = calloc (32, sizeof (uint16_t));

      allocated = 1;
    }

    // don't count the obvious
    if (adc < 10)
      return;

    if (adc > 1000)
      return;

    found = 0;

    // Does key already exist? Inc count
    for (uint8_t i = 0; i < size; i++) {

      if (adc == keys[i]) {

        found++;
        values[i]++;
	break;
      }
    }

    // It doesn't - add it
    if (!found && (size < 32)) {

      keys[size] = adc;
      values[size] = 1;

      size++;
    }

    // Any of the keys has > 2000 values? Stop and sort
    for (uint8_t i = 0; i < size; i++) {

      if (values[i] > 2000) {

	/*
        for (uint8_t i = 0; i < size; i++) {
          printf ("%u[%d] => %u\r\n", i, keys[i], values[i]);
        }
	*/

	tmp = calloc (size, 2);

        if (tmp == NULL) {
          printf ("Crikey!\r\n");
          calibrating = 0;
  	  return;
	}

	// copy values to a temp array for sorting
        for (uint8_t j = 0; j < size; j++)
          *(tmp + j) = values[j];
        
	// sort values
        bubbleSort16 (tmp, size);

        tmpkeys = calloc (16, sizeof (uint16_t));

	// take all values *above* median 
	// find corresponding keys and put them in tmpkeys
        for (uint8_t j = 0; j < 16; j++)
          for (uint8_t i = 0; i < size; i++) 
            if (*(tmp + j) == values[i])
	      *(tmpkeys + j) = keys[i];

	free (tmp);
	free (keys);
	free (values);

	// sort "top 10" keys
        bubbleSort16 (tmpkeys, 12);

        printf ("\r\n\r\n");

	// Edge states that do not need ADC sampling
	states[0][0] = 1023; states[0][1] = 1000; 
	states[4][0] = 10;   states[4][1] = 0; 

        // put them in "buckets"
        uint16_t cur = tmpkeys[0]; 
	uint8_t bucket = 1;
	states[bucket][0] = cur;

        for (uint8_t i = 0; i < 12; i++) {

          if (abs (tmpkeys[i] - cur) > SPACE_BETWEEN_BUCKETS) {
	    states[bucket++][1] = cur;
	    states[bucket][0] = tmpkeys[i];
          }

          cur = tmpkeys[i];
        }

	states[bucket][1] = tmpkeys[11]; // last element of tempkeys

	free (tmpkeys);

	for (uint16_t i = 0; i < 5; i++) {

	  printf ("Bin #%d: [%u - %u]\r\n", i, states[i][0], states[i][1]);

	  eeprom_write_word ((uint16_t *)(i * 4),     states[i][0]);
	  eeprom_write_word ((uint16_t *)(i * 4 + 2), states[i][1]);

	  if (states[i][0] == 0xffff || states[i][1] == 0xffff) {
   	    puts ("Bad set, re-doing calibration...\r\n");
            allocated = 0;
	    size = 0;
	    calibrating = 1;
            return;
	  }
        }

        allocated = 0;
	size = 0;
        calibrating = 0;
      }
    }

    return;
  }

  // end of calibration

  if (dbl_click) {

    dbl_click++;

    if (dbl_click > TRESHOLD_BUTTON) { // Button double-click threshol reached; it's a sinle click then
      dbg_putchar ('C');
      dbl_click = 0;
    }
  }

  state = -1;

  // We only have 5 recognizable rotary switch states (see enum states_t above)
  for (uint8_t i = 0; i < 5; i++)
   if (adc >= states[i][1] && adc <= states[i][0]) {
     state = i;
     break;
   }

  if (state == -1) // Outlier value
    return;

  if (long_click && state == BTTN_DWN) {
    long_click++;

    if (long_click > TRESHOLD_LONG_CLICK) {

      dbg_putchar ('L');
      calibrating = 1;
      long_click = 0;
    }
  }

  if (state != prev_state) {

    if (!candidate_cnt) {
      candidate_state = state; 
      candidate_cnt = 1;
      return;
    }
    else {

      if (state == candidate_state) {

        candidate_cnt++;

        if (candidate_cnt > TRESHOLD_ROTARY) { 	// Do we have a new state?

          if (prev_state == BTTN_DWN && state == IDLE) {  // Button press
            if (!dbl_click) {	// Start double-click timer
              dbl_click++;
            }
	    else {

              if (dbl_click <= TRESHOLD_BUTTON) { // Second click within threshold? It's a double click
                dbl_click = 0;
	        dbg_putchar ('D');
              }
            }
	  }
          else {	// rotary turn

            fifo <<= 2; fifo |= state; 

	    if (fifo == FORWARD)
              dbg_putchar ('F');

	    if (fifo == BACKWARD)
              dbg_putchar ('B');
          }

          prev_state = state;  		// We do!
          candidate_cnt = 0;
          
          if (state == BTTN_DWN)
            long_click = 1;
        }
      }
      else {
        candidate_cnt = 0;  // Candidate never turned into new state
      }
    }
  }
}

// Setup STDOUT so that printf(), etc work - costs about 300 bytes
static FILE mystdout = FDEV_SETUP_STREAM (dbg_putchar, NULL, _FDEV_SETUP_WRITE);

int main (void) {

  setup_adc ();

  // Fire off ADC conversion
  ON (ADCSRA, ADSC);

  sei ();

  stdout = &mystdout;
  dbg_tx_init();

  puts ("\r\n");

  uint8_t eeprom_clean = 1;
  uint16_t min = 0, max = 0;

  for (uint8_t i = 0; i < 5; i++) {

    min = eeprom_read_word ((uint16_t *)(i * 4));
    max = eeprom_read_word ((uint16_t *)(i * 4 + 2));

    states[i][0] = min; states[i][1] = max;

    printf ("Bin #%d: [%u..%u]\r\n", i, min, max);

    if (min != 0xffff || max != 0xffff)
      eeprom_clean = 0;
  }

  // EEPROM is wiped, re-calibrate rotary switch
  if (eeprom_clean)
    calibrating = 1;

  while (1) {
  };

  return 0;
}

void setup_adc () {

  // 2.56V internal voltage ref 
  // ON (ADMUX, REFS2);
  // ON (ADMUX, REFS1);
  // OFF (ADMUX, REFS0);

  // Vcc internal voltage ref 
  OFF (ADMUX, REFS1);
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

  // USE ADC2 (PB4, Pin 3) Single - ended input
  // OFF (ADMUX, MUX3);
  // OFF (ADMUX, MUX2);
  // ON  (ADMUX, MUX1);
  // OFF  (ADMUX, MUX0);

  // USE ADC3 (PB3, Pin 2) Single - ended input
  OFF (ADMUX, MUX3);
  OFF (ADMUX, MUX2);
  ON  (ADMUX, MUX1);
  ON  (ADMUX, MUX0);

  // Left-adjust the conversion results (only use 8bit resolution)
  // ON (ADMUX, ADLAR);

  // Right-adjust the conversion results (10bit resolution)
  OFF (ADMUX, ADLAR);

  // Prescaler set to 64 (125 Khz conv clock, ~0.1 ms per conversion)
  // ON (ADCSRA, ADPS2);
  // ON (ADCSRA, ADPS1);
  // OFF (ADCSRA, ADPS0);

  // Prescaler set to 16 (500 Khz clock, ~0.025 ms per conversion)
  ON (ADCSRA, ADPS2);
  OFF (ADCSRA, ADPS1);
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

void swap16 (uint16_t *xp, uint16_t *yp) {

  uint16_t temp = *xp;
  *xp = *yp;
  *yp = temp;
}

void bubbleSort16 (uint16_t arr[], uint16_t n) {
uint16_t i, j;

  for (i = 0; i < n-1; i++)

    // Last i elements are already in place
    for (j = 0; j < n-i-1; j++)
      if (arr[j+1] > arr[j])
        swap16 (&arr[j], &arr[j+1]);
}
