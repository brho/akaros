/* See COPYRIGHT for copyright information. */

#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/console.h>
#include <arch/kbdreg.h>
#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <ros/memlayout.h>

void cons_intr(int (*proc)(void));
void scroll_screen(void);


/***** Serial I/O code *****/

#define COM1		0x3F8

#define	COM_RX			0		// In:	Receive buffer (DLAB=0)
#define COM_DLL			0		// Out: Divisor Latch Low (DLAB=1)
#define COM_DLM			1		// Out: Divisor Latch High (DLAB=1)
#define COM_IER			1		// Out: Interrupt Enable Register
#define	COM_IER_RDI		0x01	//   Enable receiver data interrupt
#define COM_IIR			2		// In:	Interrupt ID Register
#define COM_FCR			2		// Out: FIFO Control Register
#define COM_LCR			3		// Out: Line Control Register
#define	COM_LCR_DLAB	0x80	//   Divisor latch access bit
#define	COM_LCR_WLEN8	0x03	//   Wordlength: 8 bits
#define COM_MCR			4		// Out: Modem Control Register
#define	COM_MCR_RTS		0x02	// RTS complement
#define	COM_MCR_DTR		0x01	// DTR complement
#define	COM_MCR_OUT2	0x08	// Out2 complement
#define COM_LSR			5		// In:	Line Status Register
#define COM_LSR_DATA	0x01	//   Data available
#define COM_LSR_READY	0x20	//   Ready to send

static bool SREADONLY serial_exists;

int
serial_proc_data(void)
{
	if (!(inb(COM1+COM_LSR) & COM_LSR_DATA))
		return -1;
	return inb(COM1+COM_RX);
}

int serial_read_byte()
{
	return serial_proc_data();
}

void
serial_intr(void)
{
	if (serial_exists)
		cons_intr(serial_proc_data);
}

void
serial_init(void)
{
	// Turn off the FIFO
	outb(COM1+COM_FCR, 0);
	
	// Set speed; requires DLAB latch
	outb(COM1+COM_LCR, COM_LCR_DLAB);
	// Setting speed to 115200 (setting the divider to 1)
	outb(COM1+COM_DLL, 1);
	outb(COM1+COM_DLM, 0);

	// 8 data bits, 1 stop bit, parity off; turn off DLAB latch
	outb(COM1+COM_LCR, COM_LCR_WLEN8 & ~COM_LCR_DLAB);

	// This should turn on hardware flow control
	outb(COM1+COM_MCR, COM_MCR_RTS | COM_MCR_DTR);
	// Enable rcv interrupts
	outb(COM1+COM_IER, COM_IER_RDI);

	// Clear any preexisting overrun indications and interrupts
	// Serial port doesn't exist if COM_LSR returns 0xFF
	{
		bool lbool = ((inb(COM1+COM_LSR) != 0xFF));
		serial_exists = SINIT(lbool);
	}
	(void) inb(COM1+COM_IIR);
	(void) inb(COM1+COM_RX);

}

void serial_send_byte(uint8_t b)
{
	while (!(inb(COM1+COM_LSR) & COM_LSR_READY));
	outb(COM1, b);
}

static void
serial_putc(int c)
{
	switch (c & 0xff) {
	case '\b':
		serial_send_byte('\b');
		serial_send_byte((uint8_t)(' '));
		serial_send_byte('\b');
		break;
	case '\n':
	case '\r':
		serial_send_byte((uint8_t)('\n'));
		serial_send_byte((uint8_t)('\r'));
		break;
	default:
		serial_send_byte((uint8_t)(c & 0xff));
		break;
	}
	return;
}



/***** Parallel port output code *****/
// For information on PC parallel port programming, see the class References
// page.

// Stupid I/O delay routine necessitated by historical PC design flaws
static void
delay(void)
{
	inb(0x84);
	inb(0x84);
	inb(0x84);
	inb(0x84);
}

static void
lpt_putc(int c)
{
	int i;

	for (i = 0; !(inb(0x378+1) & 0x80) && i < 12800; i++)
		delay();
	outb(0x378+0, c);
	outb(0x378+2, 0x08|0x04|0x01);
	outb(0x378+2, 0x08);
}




/***** Text-mode CGA/VGA display output with scrolling *****/
#define MAX_SCROLL_LENGTH	20
#define SCROLLING_CRT_SIZE	(MAX_SCROLL_LENGTH * CRT_SIZE)

static spinlock_t SRACY lock = SPINLOCK_INITIALIZER;

static unsigned SREADONLY addr_6845;
static uint16_t *SLOCKED(&lock) COUNT(CRT_SIZE) crt_buf;
static uint16_t SLOCKED(&lock) crt_pos;

static uint16_t (SLOCKED(&lock) scrolling_crt_buf)[SCROLLING_CRT_SIZE];
static uint16_t SLOCKED(&lock) scrolling_crt_pos;
static uint8_t	SLOCKED(&lock) current_crt_buf;

void
cga_init(void)
{
	volatile uint16_t SLOCKED(&lock)*COUNT(CRT_SIZE) cp;
	uint16_t was;
	unsigned pos;

	cp = (uint16_t *COUNT(CRT_SIZE)) TC(KERNBASE + CGA_BUF);
	was = *cp;
	*cp = (uint16_t) 0xA55A;
	if (*cp != 0xA55A) {
		cp = (uint16_t *COUNT(CRT_SIZE)) TC(KERNBASE + MONO_BUF);
		addr_6845 = SINIT(MONO_BASE);
	} else {
		*cp = was;
		addr_6845 = SINIT(CGA_BASE);
	}
	
	/* Extract cursor location */
	outb(addr_6845, 14);
	pos = inb(addr_6845 + 1) << 8;
	outb(addr_6845, 15);
	pos |= inb(addr_6845 + 1);

	crt_buf = (uint16_t SLOCKED(&lock)*COUNT(CRT_SIZE)) cp;
	crt_pos = pos;
	scrolling_crt_pos = 0;
	current_crt_buf = 0;

}

static void set_screen(uint8_t screen_num)
{
	uint16_t leftovers = (scrolling_crt_pos % CRT_COLS);
	leftovers = (leftovers) ? CRT_COLS - leftovers : 0;
	
	int offset = scrolling_crt_pos + leftovers - (screen_num + 1)*CRT_SIZE;
	offset = (offset > 0) ? offset : 0;

	memcpy(crt_buf, scrolling_crt_buf + offset, CRT_SIZE * sizeof(uint16_t));
}

static void scroll_screen_up(void)
{
	if(current_crt_buf <  (scrolling_crt_pos / CRT_SIZE))
		current_crt_buf++;
	set_screen(current_crt_buf);
}

static void scroll_screen_down(void)
{
	if(current_crt_buf > 0) 
		current_crt_buf--;
	set_screen(current_crt_buf);
}

static void reset_screen(void)
{
	current_crt_buf = 0;
	set_screen(current_crt_buf);
}

void
cga_putc(int c)
{
	// if no attribute given, then use black on white
	if (!(c & ~0xFF))
		c |= 0x0700;

	switch (c & 0xff) {
	case '\b':
		if (crt_pos > 0) {
			crt_pos--;
			scrolling_crt_pos--;

			crt_buf[crt_pos] = (c & ~0xff) | ' ';
			scrolling_crt_buf[scrolling_crt_pos] = crt_buf[crt_pos];
		}
		break;
	case '\n':
		crt_pos += CRT_COLS;
		scrolling_crt_pos += CRT_COLS;
		/* fallthru */
	case '\r':
		crt_pos -= (crt_pos % CRT_COLS);
		scrolling_crt_pos -= (scrolling_crt_pos % CRT_COLS);
		break;
	case '\t':
		cga_putc(' ');
		cga_putc(' ');
		cga_putc(' ');
		cga_putc(' ');
		cga_putc(' ');
		break;
	default:
		crt_buf[crt_pos++] = c;		/* write the character */
		scrolling_crt_buf[scrolling_crt_pos++] = c;
		break;
	}

	// The purpose of this is to allow the screen to appear as if it is scrolling as
	// more lines are added beyond the size of the monitor.  The top line is dropped
	// and everything is shifted up by one.
	if (crt_pos >= CRT_SIZE) {
		int i;

		memcpy(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
		for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
			crt_buf[i] = 0x0700 | ' ';
		crt_pos -= CRT_COLS;
	}
	// Do the same for the scrolling crt buffer when it hits its capacity
	if (scrolling_crt_pos >= SCROLLING_CRT_SIZE) {
		int i;

		memcpy(scrolling_crt_buf, scrolling_crt_buf + CRT_COLS, 
		       (SCROLLING_CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
		for (i = SCROLLING_CRT_SIZE - CRT_COLS; i < SCROLLING_CRT_SIZE; i++)
			scrolling_crt_buf[i] = 0x0700 | ' ';
		scrolling_crt_pos -= CRT_COLS;
	}


	/* move that little blinky thing */
	outb(addr_6845, 14);
	outb(addr_6845 + 1, crt_pos >> 8);
	outb(addr_6845, 15);
	outb(addr_6845 + 1, crt_pos);
}


/***** Keyboard input code *****/

#define NO		0

#define SHIFT	(1<<0)
#define CTL		(1<<1)
#define ALT		(1<<2)

#define CAPSLOCK	(1<<3)
#define NUMLOCK		(1<<4)
#define SCROLLLOCK	(1<<5)

#define E0ESC		(1<<6)

static uint8_t (SREADONLY shiftcode)[256] = 
{
	[0x1D] CTL,
	[0x2A] SHIFT,
	[0x36] SHIFT,
	[0x38] ALT,
	[0x9D] CTL,
	[0xB8] ALT
};

static uint8_t (SREADONLY togglecode)[256] = 
{
	[0x3A] CAPSLOCK,
	[0x45] NUMLOCK,
	[0x46] SCROLLLOCK
};

static uint8_t normalmap[256] =
{
	NO,   0x1B, '1',  '2',  '3',  '4',  '5',  '6',	// 0x00
	'7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',	// 0x10
	'o',  'p',  '[',  ']',  '\n', NO,   'a',  's',
	'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',	// 0x20
	'\'', '`',  NO,   '\\', 'z',  'x',  'c',  'v',
	'b',  'n',  'm',  ',',  '.',  '/',  NO,   '*',	// 0x30
	NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
	NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',	// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,	// 0x50
	[0xC7] KEY_HOME,	[0x9C] '\n' /*KP_Enter*/,
	[0xB5] '/' /*KP_Div*/,	[0xC8] KEY_UP,
	[0xC9] KEY_PGUP,	[0xCB] KEY_LF,
	[0xCD] KEY_RT,		[0xCF] KEY_END,
	[0xD0] KEY_DN,		[0xD1] KEY_PGDN,
	[0xD2] KEY_INS,		[0xD3] KEY_DEL
};

static uint8_t shiftmap[256] = 
{
	NO,   033,  '!',  '@',  '#',  '$',  '%',  '^',	// 0x00
	'&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
	'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',	// 0x10
	'O',  'P',  '{',  '}',  '\n', NO,   'A',  'S',
	'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',	// 0x20
	'"',  '~',  NO,   '|',  'Z',  'X',  'C',  'V',
	'B',  'N',  'M',  '<',  '>',  '?',  NO,   '*',	// 0x30
	NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
	NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',	// 0x40
	'8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
	'2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,	// 0x50
	[0xC7] KEY_HOME,	[0x9C] '\n' /*KP_Enter*/,
	[0xB5] '/' /*KP_Div*/,	[0xC8] KEY_UP,
	[0xC9] KEY_PGUP,	[0xCB] KEY_LF,
	[0xCD] KEY_RT,		[0xCF] KEY_END,
	[0xD0] KEY_DN,		[0xD1] KEY_PGDN,
	[0xD2] KEY_INS,		[0xD3] KEY_DEL
};

#define C(x) (x - '@')

static uint8_t ctlmap[256] = 
{
	NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO, 
	NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO, 
	C('Q'),  C('W'),  C('E'),  C('R'),  C('T'),  C('Y'),  C('U'),  C('I'),
	C('O'),  C('P'),  NO,      NO,      '\r',    NO,      C('A'),  C('S'),
	C('D'),  C('F'),  C('G'),  C('H'),  C('J'),  C('K'),  C('L'),  NO, 
	NO,      NO,      NO,      C('\\'), C('Z'),  C('X'),  C('C'),  C('V'),
	C('B'),  C('N'),  C('M'),  NO,      NO,      C('/'),  NO,      NO,
	[0x97] KEY_HOME,
	[0xB5] C('/'),		[0xC8] KEY_UP,
	[0xC9] KEY_PGUP,	[0xCB] KEY_LF,
	[0xCD] KEY_RT,		[0xCF] KEY_END,
	[0xD0] KEY_DN,		[0xD1] KEY_PGDN,
	[0xD2] KEY_INS,		[0xD3] KEY_DEL
};

static uint8_t * COUNT(256) (SREADONLY charcode)[4] = {
	normalmap,
	shiftmap,
	ctlmap,
	ctlmap
};

/*
 * Get data from the keyboard.  If we finish a character, return it.  Else 0.
 * Return -1 if no data.
 */
static uint32_t SLOCKED(&lock) shift;
static bool SLOCKED(&lock) crt_scrolled = FALSE;

static int
kbd_proc_data(void)
{
	int c;
	uint8_t data;


	if ((inb(KBSTATP) & KBS_DIB) == 0)
		return -1;

	data = inb(KBDATAP);

	if (data == 0xE0) {
		// E0 escape character
		shift |= E0ESC;
		return 0;
	} else if (data & 0x80) {
		// Key released
		data = (shift & E0ESC ? data : data & 0x7F);
		shift &= ~(shiftcode[data] | E0ESC);
		return 0;
	} else if (shift & E0ESC) {
		// Last character was an E0 escape; or with 0x80
		data |= 0x80;
		shift &= ~E0ESC;
	}

	shift |= shiftcode[data];
	shift ^= togglecode[data];

	c = charcode[shift & (CTL | SHIFT)][data];

	//Scrolling screen functionality
	if((shift & SHIFT) && ((c == KEY_UP) || (c == KEY_PGUP))) {
		crt_scrolled = TRUE;
		scroll_screen_up();
		return 0;
	}
	else if((shift & SHIFT) && ((c == KEY_DN) || (c == KEY_PGDN))) {
		crt_scrolled = TRUE;
		scroll_screen_down();
		return 0;
	}
	else if((shift & SHIFT) && c == KEY_RT) {
		crt_scrolled = FALSE;
		reset_screen();
		return 0;
	}

	// On keypress other than SHIFT, reset if we were scrolled
	if(crt_scrolled && (!(shift & SHIFT))) {
		crt_scrolled = FALSE;
		reset_screen();
	}

	//Force character to capital if caps lock on
	if (shift & CAPSLOCK) {
		if ('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if ('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}

	// Process special keys
	// Ctrl-Alt-Del: reboot
	if (!(~shift & (CTL | ALT)) && c == KEY_DEL) {
		cprintf("Rebooting!\n");
		outb(0x92, 0x3); // courtesy of Chris Frost
	}

	return c;
}

void
kbd_intr(void)
{
	cons_intr(kbd_proc_data);
}

void
kbd_init(void)
{
}



/***** General device-independent console code *****/
// Here we manage the console input buffer,
// where we stash characters received from the keyboard or serial port
// whenever the corresponding interrupt occurs.

#define CONSBUFSIZE	512
struct cons {
	uint8_t buf[CONSBUFSIZE];
	uint32_t rpos;
	uint32_t wpos;
};

static struct cons SLOCKED(&lock) cons;

// called by device interrupt routines to feed input characters
// into the circular console input buffer.
void
cons_intr(int (*proc)(void))
{
	int c;

	while ((c = (*proc)()) != -1) {
		if (c == 0)
			continue;
		spin_lock_irqsave(&lock);
		cons.buf[cons.wpos++] = c;
		if (cons.wpos == CONSBUFSIZE)
			cons.wpos = 0;
		spin_unlock_irqsave(&lock);
	}
}

// return the next input character from the console, or 0 if none waiting
int
cons_getc(void)
{
	int c;

	// poll for any pending input characters,
	// so that this function works even when interrupts are disabled
	// (e.g., when called from the kernel monitor).
	#ifndef __CONFIG_SERIAL_IO__
		serial_intr();
	#endif
	kbd_intr();

	// grab the next character from the input buffer.
	spin_lock_irqsave(&lock);
	if (cons.rpos != cons.wpos) {
		c = cons.buf[cons.rpos++];
		if (cons.rpos == CONSBUFSIZE)
			cons.rpos = 0;
		spin_unlock_irqsave(&lock);
		return c;
	}
	spin_unlock_irqsave(&lock);
	return 0;
}

// output a character to the console
void
cons_putc(int c)
{
	//static uint32_t lock; zra: moving up for sharC annotations
	spin_lock_irqsave(&lock);
	#ifndef __CONFIG_SERIAL_IO__
		serial_putc(c);
	#endif
	//lpt_putc(c);
	cga_putc(c);
	spin_unlock_irqsave(&lock);
}

// initialize the console devices
void
cons_init(void)
{
	cga_init();
	kbd_init();
	serial_init();

	if (!serial_exists)
		cprintf("Serial port does not exist!\n");
}


// `High'-level console I/O.  Used by readline and cprintf.

void
cputchar(int c)
{
	cons_putc(c);
}

void
cputbuf(const char*COUNT(len) buf, int len)
{
	int i;
	for(i = 0; i < len; i++)
		cons_putc(buf[i]);
}

int
getchar(void)
{
	int c;

	while ((c = cons_getc()) == 0)
		/* do nothing */;
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}
