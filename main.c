// Test harness for Mode7 display output.

// Compile option to generate sync pulses (standalone display),
// otherwise the sync pin is an input and the Electron assumed to generate them
#define	GENERATE_SYNCS	1




#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

// Declarations for this program.
#include "mode7_demo.h"

// Our video output programs for the PIO
#include "mode7.pio.h"

// This gets the git revision number and state into the binary
#include "git.h"

static void core1_main_loop(void)
{
	mode7_init();

#if GENERATE_SYNCS
	// Launch the sync generator, which continues under IRQ.
	// We can afford to do it from this core, as the IRQ fires aligned
	// with HSYNC, which is just when the main code is idle waiting
	// for the first pixel on the next line.
	syncgen_start();
#endif

	for (;;)
	{
	}
}

int pollchar(void)
{
  int c = getchar_timeout_us(0);
  return c;
}


// -----------------------------------------------------------------------------
int main(void)
{
	unsigned offset;
	bool launched = false;
	bool clock_ok;

	// The system clock speed is set as a constant in the PIO file
	// NB. needs to be a multiple of 12MHz for Mode 7
	clock_ok = set_sys_clock_khz(SYSCLK_MHZ * 1000, false);

	// USB console for monitoring
	stdio_init_all();
	printf("Wahoo!\n");

	// Set up all the pins that will be GPIOs
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
	gpio_put(PICO_DEFAULT_LED_PIN, 1);

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
		if (c >= 0)
		{
			if (!clock_ok) printf("Failed to set clock\n");
			printf("Mode 7 on Pi Pico - %s\n%s%s\n", git_Describe(),
				git_CommitDate(),
				git_AnyUncommittedChanges() ? " ***modified***" : "");

			printf("'L' to launch display, 'B' to revert to bootrom\n");
			if (c == 'L')
			{
				if (launched) printf("Already launched\n");
				else
				{
					printf("Launching video output\n");
					launched = true;
					multicore_launch_core1(core1_main_loop);
				}
			}
			else if (c == 'B')
			{
				// 1st parameter is LED to use for mass storage activity
				// 2nd param enables both USB mass storage and PICOBOOT
				reset_usb_boot(1 << PICO_DEFAULT_LED_PIN, 0);

			}
			else printf("You pressed: %02x\n", c);
		}

    }
}
