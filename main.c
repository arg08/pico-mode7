// Test harness for Mode7 display output.

// Compile option to generate sync pulses (standalone display),
// otherwise the sync pin is an input and the Electron assumed to generate them
#define	GENERATE_SYNCS	1




#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

#include "mode7.pio.h"



int pollchar(void)
{
  int c = getchar_timeout_us(0);
  return c;
}


// -----------------------------------------------------------------------------
int main(void)
{
	unsigned offset;

	// The system clock speed is set as a constant in the PIO file
	// NB. needs to be a multiple of 12MHz for Mode 7
	set_sys_clock_khz(SYSCLK_MHZ * 1000, true);

	// Set up all the pins that will be GPIOs
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_init(PIN_RGB_EN);
	gpio_set_dir(PIN_RGB_EN, GPIO_OUT);
	gpio_put(PIN_RGB_EN, 0);

	// Output enables for the 74lvc245 buffers
	// We aren't using these, so just set the pullup to give a safe state.
	gpio_pull_up(PIN_SELAL);
	gpio_pull_up(PIN_SELAH);
	gpio_pull_up(PIN_SELDT);

	// Discard any character that got in the UART during powerup
	getchar_timeout_us(10);

	for (;;)
	{
		int c = getchar_timeout_us(100);
		if (c >= 0) printf("You pressed: %02x\n", c);
    }
}
