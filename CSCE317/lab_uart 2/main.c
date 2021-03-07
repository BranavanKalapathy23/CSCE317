#include <avr/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdbool.h>


int uart_putchar(char c, FILE *stream) {
    /* wait for the transmitter to become ready */
    loop_until_bit_is_set(UCSR0A, UDRE0);

    /* because it is assumed by most serial consoles, we automatically
     * rewrite plain LF endings to CRLF line endings */
    if (c == '\n') {
        uart_putchar('\r', stream);
    }

    /* wait for the transmitter to become ready, since we might have just
     * transmitted a CR */
    loop_until_bit_is_set(UCSR0A, UDRE0);

    /* transmit the character */
    UDR0 = c;

    /* signal success */
    return 0;
}

/**
 * @brief Read one character from UART0, blocking
 *
 * @param stream for compatibility
 *
 * @return the character that was read
 */
int uart_getchar(FILE *stream) {

    /* wait for the data to arrive */
    loop_until_bit_is_set(UCSR0A, RXC0);

    /* return the received data */
    return UDR0;

}

/**
 * @brief Initialize UART for use with stdio
 */
void uart_init(void) {
    /* initialize USART0 */
    UBRR0=(((F_CPU/(UART_BAUDRATE*16UL)))-1); // set baud rate
    UCSR0B|=(1<<TXEN0); //enable TX
    UCSR0B|=(1<<RXEN0); //enable RX

    UCSR0C |= (1 << UCSZ01) | (1 << UCSZ00); // character size of 8
    UCSR0C &= ~(1 << USBS0); // 1 stop bit
    UCSR0C &= ~( (1 << UPM01) | (1 << UPM00) ); // disable parity

    /* initialize file descriptors, notice the functions we wrote earlier
     * are used as callbacks here */
    fdevopen(uart_putchar, NULL);
    fdevopen(NULL, uart_getchar);
    printf("\n\nUART initialized (%i 8N1)\n", UART_BAUDRATE);
}

/**
 * @brief Shift all elements the given buffer starting at element 'start' one
 * element to the right
 *
 * @param buf buffer to modify in-place
 * @param used number of used elements in the buffer, must be at least one less
 * than the buffer size (validated by caller)
 * @param start the index of the first element which should be shifted
 */
void shiftright(char* buf, size_t used, size_t start) {
	for (char* c = &(buf[used-1]); c >= &(buf[start]); c -= sizeof(char)) {
		*(c+sizeof(char)) = *c;
	}
}

/**
 * @brief As with shiftright(), but in the other direction
 *
 * @param buf
 * @param used
 * @param start
 */
void shiftleft(char* buf, size_t used, size_t start) {
	for (char* c = &(buf[start]); c <= &(buf[used-1]); c += sizeof(char)) {
		*c = *(c+sizeof(char));
	}

}

/**
 * @brief Implements a simple line editor.
 *
 * This function will read text from the given stream using fgetc, and will
 * echo back appropriately on stdout. It terminates when the user enters
 * return.
 *
 * This function supports VT100-style home, end, delete, backspace, and arrow
 * keys. However, history is not supported (up arrow is an error).
 *
 * @param buf buffer to write user data into (will be overwritten)
 * @param prompt prompt to display
 * @param bufsz length of buffer
 * @param stream stream to read from
 */
void readline(char* buf, char* prompt, size_t bufsz, FILE* stream) {
	size_t cursor;
	size_t used;
	char recv;
	char prev;
	typedef enum {NORM, ESC, ESC3} mode_t;
	mode_t mode;

	mode = NORM;

	cursor = 0;
	used = 0;
	buf[0] = '\0';
	recv = '\0';

	/* display the prompt */
	printf(prompt);

	while (1) {
		prev = recv;
		recv = fgetc(stream);

		if (recv == 0x1b) { /* ESC */
			/* handles things like arrow keys and DEL which use
			 * ANSI escapes */
			mode = ESC;

		} else if ( ((recv == ']') || (recv == ']')) && (mode == ESC)) {
			/* do nothing -- we're about to get an escape code */

		} else if ((recv == 'H') && (mode == ESC)) { /* home */
			/* return the cursor to the beginning of the buffer */
			cursor = 0;
			mode = NORM;

			/* move the terminal cursor to the beginning of the
			 * line, then to the end of the prompt */
			printf("%c[%iD", 0x1b, 1000);
			printf("%c[%iC", 0x1b, strlen(prompt));

		} else if ((recv == 'F') && (mode == ESC)) { /* end */
			/* move terminal cursor to the beginning of the line */
			printf("%c[%iD", 0x1b, 1000);

			/* move the cursor to the end of the buffer */
			cursor = used;

			/* move the terminal cursor to the end of the line,
			 * which is the buffer length plus the prompt length */
			printf("%c[%iC", 0x1b, cursor + strlen(prompt));

			mode = NORM;

		} else if ( (mode == NORM) && (recv >= ' ') && (recv != 0x7F) ) {
			if (cursor >= (bufsz -1)) {
				/* overflow */

				/* note that \a is the terminal bell */
				fputc('\a', stdout);

			} else if (cursor == used) { /* append */
				buf[cursor] = recv;

				/* ensure that the string remains null
				 * terminated */
				buf[cursor+1] = '\0';

				cursor ++;
				used++;

				/* echo back the character we just read */
				printf("%c", recv);

			} else { /* insert */

				/* shift everything after the cursor in the
				 * edit buffer one slot to the right */
				shiftright(buf, used, cursor);

				/* overwrite the current location in the
				 * edit buffer */
				buf[cursor] = recv;

				/* The cursor position will already be where we
				 * inserted, so just overwrite the rest of the
				 * line with here on in the edit buffer */
				printf("%s", &(buf[cursor]));

				cursor++;
				used++;

				/* return the beginning of the line, then put
				 * the terminal cursor back at the appropriate
				 * position */
				printf("%c[%iD", 0x1b, 1000);
				printf("%c[%iC", 0x1b, cursor + strlen(prompt));
			}

		} else if ( (mode == NORM) && (recv == 0x8) ){  /* backspace */
			if (cursor == 0) {
				/* underflow */
				fputc('\a', stdout);

			} else if (cursor == used) {
				/* this is the new end of the string */
				buf[cursor-1] = '\0';
				cursor--;
				used--;

				/* go back one, output a space, then go back
				 * one again */
				printf("\b \b");

			} else {

				/* shift everything in the edit buffer one slot
				 * to the left, this will implicitly overwrite
				 * the current cursor position */
				shiftleft(buf, used, cursor-1);
				cursor--;
				used--;

				/* the end of the string has shifted left by
				 * one, so we need to update the null
				 * terminator to reflect this */
				buf[used] = '\0';

				/* go back one spot, re-draw everything in the
				 * edit buffer after the position we just
				 * overwrote, then reset the terminal cursor
				 * position appropriately */
				printf("\b");
				printf("%s ", &(buf[cursor]));
				printf("%c[%iD", 0x1b, 1000);
				printf("%c[%iC", 0x1b, cursor + strlen(prompt));

			}

		} else if ( (mode == NORM) && (recv == 0x12) ) { /* C-r */
			/* Ctrl + R re-draws the line and re-sets the cursor
			 * position. This is useful if garbage has been written
			 * to the terminal */

			/* return to the beginning of the line */
			printf("\r");

			/* overwrite the entire space that the buffer may
			 * occupy with empty spaces */
			for (int i = 0 ; i < bufsz; i++) {
				printf(" ");
			}

			/* return to the beginning of the line, re-draw
			 * everything, and set the terminal cursor position */
			printf("\r");
			printf("%s%s", prompt, buf);
			printf("%c[%iD", 0x1b, 1000);
			printf("%c[%iC", 0x1b, cursor + strlen(prompt));

		} else if ( (mode == NORM) && (recv == 0x03) ) { /* C-c */
			/* Ctrl + C clears the edit buffer */

			/* beginning of the buffer is now also the end */
			buf[0] = '\0';
			cursor = 0;
			used = 0;

			/* clear the line */
			printf("\r");
			for (int i = 0 ; i < bufsz; i++) {
				printf(" ");
			}

			/* redraw the prompt and the now empty buffer */
			printf("\r");
			printf("%s%s", prompt, buf);

		} else if ( (mode == ESC) && (recv == 0x5b) ) {
			/* do nothing (probably about to get arrow key or DEL */

		} else if ( (mode == ESC) && (recv == 'D') && (prev == 0x5b)) {
			/* left arrow key */
			if (cursor > 0) {
				cursor --;
				printf("%c[%iD", 0x1b, 1);
			} else {
				fputc('\a', stdout);
			}
			mode = NORM;

		} else if ( (mode == ESC) && (recv == 'C') && (prev == 0x5b)) {
			/* right arrow key */
			if (cursor < (bufsz-1)) {
				cursor ++;
				printf("%c[%iC", 0x1b, 1);
			} else {
				fputc('\a', stdout);
			}
			mode = NORM;

		} else if ( (mode == ESC) && (recv == 0x33) ) {
			/* we expect to see a DEL next */
			mode = ESC3;

		} else if ( (mode == ESC3) && (recv == 0x7e)) {
			/* delete */
			if (cursor == used) {
				/* already at end */
				fputc('\a', stdout);
			} else {
				/* this works the same as backspace, except
				 * one character to the right */
				shiftleft(buf, used, cursor);
				used--;
				buf[used] = '\0';
				printf("\b");
				printf("%s ", &(buf[cursor-1]));
				printf("%c[%iD", 0x1b, 1000);
				printf("%c[%iC", 0x1b, cursor + strlen(prompt));

			}

			mode = NORM;


		} else if (mode == NORM) {
			/* the user has pressed return, so we are done reading
			 * the line */
			printf("\n");
			return;

		/* everything from here down is just error handling */
		} else if (mode == ESC) {
			printf("Don't know how to handle 0x%x in ESC mode\n", recv);
			mode = NORM;

		} else if (mode == ESC3) {
			printf("Don't know how to handle 0x%x in ESC3 mode\n", recv);
			mode = NORM;

		} else {
			printf("Don't know how to handle 0x%x\n", recv);
		}
	}
}
/* be sure to set CS1[2:0] consistently */
#define CLOCK_PRESCALE 1024UL
/* blink interval */
#define INTERVAL_MS 500UL

/* 0xFFFF is the max value for timer/counter 1, and we want TCNTVAL to be
 * the number to initialize TCNT1 to such that it will overflow (triggering our
 * interrupt) after INTERVAL_MS many ms. F_CPU * INTERVAL_MS has units of
 * MHz * ms (unitless), and 10000 ms/s * CLOCK_PRESCALE is also unitless. */
#define TCNTVAL (0xFFFF - ((F_CPU * INTERVAL_MS) / (1000UL * CLOCK_PRESCALE)));

#define LEDPORT PORTC
#define LEDPIN 0
#define LEDDR DDRC

bool led_state;
//set a volatile flag to control interrupt
volatile bool should_blink;

ISR(TIMER1_OVF_vect) {
	if(should_blink){
		if (led_state) {
			/* LED is on, turn it off */
			LEDPORT &= ~(1 << LEDPIN);
		} else {
			/* LED is off, turn it on */
			LEDPORT |= (1 << LEDPIN);
		}

		/* reset so we overflow again after the given interval */
		TCNT1 = TCNTVAL;

		/* flip states */;
		led_state = !led_state;
	}
}

void init(void) {

	/* Note that we want to use T/C 1, because it is 16 bits, rather than
	 * 8. T/C 0 or 2 would overflow too fast for our use case (we would
	 * have to maintain our own counter to keep track of when to toggle the
	 * LED). */

	/* Set the clock prescaler to 1024^-1, so we should interrupt at a rate
	 * of 16Mhz / 1024 = .015625Mhz. */
	TCCR1B |= (1 << CS12) | (0 << CS11) | (1 << CS10);

	TCNT1 = TCNTVAL;

	/* Enable overflow interrupt */
	TIMSK1 |= (1 << TOIE1);

	/* set the LED's GPIO to output mode */
	LEDDR |= (1 << LEDPIN);

	led_state = false;

	// enable interrupts globally
	sei();
}

#define BUFSZ 64

int main(void) {

	//Part B
	uart_init();
	init();
	//printf("hello world!\n");
	
    char buf[BUFSZ];
	char * str1 = "on";
    char * str2 = "end";
    //Part C
    while(1) {
        printf("ready");
		readline(buf, "> ", BUFSZ, stdin);
		//printf("contents of buf: %s\n", buf);

        if(strcmp(str1,buf)==0){
            printf("on is selected\n");
	    	should_blink = true;
        }
        else if(strcmp(str2,buf)==0){
            printf("end is selected\n");
	    	should_blink = false;
			LEDPORT &= ~(1 << LEDPIN);
		}
        else{
            printf("you broke it\n");
			continue;
        }
    }


	//Part A
	/*DDRC |= (1<<0);

	while(1){
		PORTC |= 1;
		_delay_ms(500);
		PORTC &= ~(1<<0);
		_delay_ms(500);
	}*/
}



/* initialize PC0 as an output */

	/* while true ... */

		/* sleep for 1/2 second */

		/* set PC0 high */

		/* sleep for 1/2 second */

		/* set PC0 to low */
