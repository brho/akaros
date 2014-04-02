/* See COPYRIGHT for copyright information. */

#include <arch/x86.h>
#include <arch/arch.h>
#include <arch/console.h>
#include <arch/kbdreg.h>
#include <atomic.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <sys/queue.h>
#include <arch/coreid.h>

#include <ros/memlayout.h>

/***** Serial I/O code *****/

#define COM1			0x3F8	/* irq 4 */
#define COM2			0x2F8	/* irq 3 */
#define COM3			0x3E8	/* irq 4 */
#define COM4			0x2E8	/* irq 3 */

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
#define	COM_MCR_GLB_IRQ	0x08	/* global irq controlled via MCR */
#define COM_LSR			5		// In:	Line Status Register
#define COM_LSR_DATA	0x01	//   Data available
#define COM_LSR_READY	0x20	//   Ready to send
#define COM_SCRATCH		7		/* Scratch register */

/* List of all initialized console devices */
struct cons_dev_slist cdev_list = SLIST_HEAD_INITIALIZER(cdev_list);
/* need to statically allocate these, since cons_init is called so damn early */
struct cons_dev com1, com2, com3, com4, kb;

static int __serial_get_char(int com, uint8_t *data)
{
	if (!(inb(com + COM_LSR) & COM_LSR_DATA))
		return -1;
	*data = inb(com + COM_RX);
	/* serial input sends \r a lot, but we interpret them as \n later on.  this
	 * will help userspace too, which isn't expecting the \rs.  the right answer
	 * might involve telling userspace what sort of console this is. */
	if (*data == '\r')
		*data = '\n';
	return 0;
}

static int serial_get_char(struct cons_dev *cdev, uint8_t *data)
{
	return __serial_get_char(cdev->val, data);
}

static void __serial_put_char(int com, uint8_t c)
{
	while (!(inb(com + COM_LSR) & COM_LSR_READY))
		cpu_relax();
	outb(com, c);
}

/* Writes c (or some variant of) to the serial cdev */
static void serial_put_char(struct cons_dev *cdev, uint8_t c)
{
	assert(cdev->type == CONS_SER_DEV);
	/* We do some funky editing of a few chars, to suit what minicom seems to
	 * expect (at least for brho). */
	switch (c & 0xff) {
		case '\b':
		case 0x7f:
		#ifdef CONFIG_PRINTK_NO_BACKSPACE
			__serial_put_char(cdev->val, (uint8_t)('^'));
			__serial_put_char(cdev->val, (uint8_t)('H'));
		#else
			__serial_put_char(cdev->val, '\b');
			__serial_put_char(cdev->val, (uint8_t)(' '));
			__serial_put_char(cdev->val, '\b');
		#endif /* CONFIG_PRINTK_NO_BACKSPACE */
			break;
		case '\n':
		case '\r':
			__serial_put_char(cdev->val, (uint8_t)('\n'));
			__serial_put_char(cdev->val, (uint8_t)('\r'));
			break;
		default:
			__serial_put_char(cdev->val, (uint8_t)(c & 0xff));
			break;
	}
}

/* Writes c to every initialized serial port */
static void serial_spam_char(int c)
{
	struct cons_dev *i;
	SLIST_FOREACH(i, &cdev_list, next) {
		if (i->type == CONS_SER_DEV)
			serial_put_char(i, c);
	}
}

/* http://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming \
 * #Software_Identification_of_the_UART
 *
 * We return 0 for unknown (probably not there), and the char * o/w */
static char *__serial_detect_type(int com)
{
	uint8_t val;
	char *model = 0;
	/* First, check that the port actually exists.  I haven't seen any
	 * documentation of the LSR 0xff check, but it seems to work on qemu and
	 * hardware (brho's nehalem).  Perhaps 0xff is the default state for
	 * 'unmapped' ports. */
	/* Serial port doesn't exist if COM_LSR returns 0xff */
	if (inb(com + COM_LSR) == 0xff)
		return model;
	/* Try to set FIFO, then based on the bits enabled, we can tell what model
	 * it is */
	outb(com + COM_FCR, 0xe7);
	val = inb(com + COM_IIR);
	if (val & (1 << 6)) {
		if (val & (1 << 7)) {
			if (val & (1 << 5))
				model = "UART 16750";
			else
				model = "UART 16550A";
		} else {
			model = "UART 16550";
		}
	} else {
		/* no FIFO at all.  the 8250 had a buggy scratch register. */
		outb(com + COM_SCRATCH, 0x2a);
		val = inb(com + COM_SCRATCH);
		if (val == 0x2a)
			model = "UART 16450";
		else
			model = "UART 8250";
	}
	return model;
}

/* Helper: attempts to initialize the serial device cdev with COM com.  If it
 * succeeds, the cdev will be on the cdev_list. */ 
static void serial_com_init(struct cons_dev *cdev, int com)
{
	cdev->model = __serial_detect_type(com);
	/* Bail if detection failed */
	if (!cdev->model)
		return;
	/* Set up the struct */
	cdev->type = CONS_SER_DEV;
	cdev->val = com;
	switch (com) {
		case (COM1):
		case (COM3):
			cdev->irq = 4;
			break;
		case (COM2):
		case (COM4):
			cdev->irq = 3;
			break;
		default:
			/* not that printing is the safest thing right now... */
			panic("Unknown COM %d", com);
	}
	cdev->getc = serial_get_char;
	/* Turn off the FIFO (not sure this is needed) */
	outb(com + COM_FCR, 0);
	/* Set speed; requires DLAB latch */
	outb(com + COM_LCR, COM_LCR_DLAB);
	/* Setting speed to 115200 (setting the divider to 1) */
	outb(com + COM_DLL, 1);
	outb(com + COM_DLM, 0);
	/* 8 data bits, 1 stop bit, parity off; turn off DLAB latch */
	outb(com + COM_LCR, COM_LCR_WLEN8 & ~COM_LCR_DLAB);
	/* This should turn on hardware flow control and make sure the global irq
	 * bit is on.  This bit is definitely used some hardware's 16550As, though
	 * not for qemu.  Also, on both qemu and hardware, this whole line is a
	 * noop, since the COM_MCR is already 0x0b, so we're just making sure the
	 * three bits are still turned on (and leaving other bits unchanged) */
	outb(com + COM_MCR, inb(com + COM_MCR) | COM_MCR_RTS | COM_MCR_DTR |
	                                         COM_MCR_GLB_IRQ);
	/* Enable rx interrupts */
	outb(com + COM_IER, COM_IER_RDI);
	/* Clear any preexisting overrun indications and interrupts */
	inb(com + COM_IIR);
	inb(com + COM_RX);
	/* Put us on the list of initialized cdevs (now that it is init'd) */
	SLIST_INSERT_HEAD(&cdev_list, cdev, next);
}

static void serial_init(void)
{
	/* attempt to init all four COMs */
	serial_com_init(&com1, COM1);
	serial_com_init(&com2, COM2);
	serial_com_init(&com3, COM3);
	serial_com_init(&com4, COM4);
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
#define MONO_BASE	0x3B4
#define MONO_BUF	0xB0000
#define CGA_BASE	0x3D4
#define CGA_BUF		0xB8000

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

#define MAX_SCROLL_LENGTH	20
#define SCROLLING_CRT_SIZE	(MAX_SCROLL_LENGTH * CRT_SIZE)

static spinlock_t console_lock = SPINLOCK_INITIALIZER_IRQSAVE;

static unsigned SREADONLY addr_6845;
static uint16_t *crt_buf;
static uint16_t crt_pos;

static uint16_t scrolling_crt_buf[SCROLLING_CRT_SIZE];
static uint16_t scrolling_crt_pos;
static uint8_t	current_crt_buf;

void
cga_init(void)
{
	volatile uint16_t *cp;
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

	crt_buf = (uint16_t*)cp;
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
	case 0x7f:
	#ifdef CONFIG_PRINTK_NO_BACKSPACE
		cga_putc('^');
		cga_putc('H');
	#else
		if (crt_pos > 0) {
			crt_pos--;
			scrolling_crt_pos--;
			crt_buf[crt_pos] = (c & ~0xff) | ' ';
			scrolling_crt_buf[scrolling_crt_pos] = crt_buf[crt_pos];
		}
	#endif /* CONFIG_PRINTK_NO_BACKSPACE */
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
static uint32_t shift;
static bool crt_scrolled = FALSE;

/* TODO: i'm concerned about the (lack of) locking when scrolling the screen. */
static int
kbd_proc_data(void)
{
#ifdef CONFIG_X86_DISABLE_KEYBOARD
	/* on some machines with usb keyboards, any keyboard input triggers SMM
	 * interference on all of the cores. */
	return -1;
#endif /* CONFIG_X86_DISABLE_KEYBOARD */

	int c;
	uint8_t data;

#ifdef CONFIG_KB_CORE0_ONLY
	/* Ghetto hack to avoid crashing brho's buggy nehalem. */
	uint32_t eax, ebx, ecx, edx, family, model, stepping;
	cpuid(0x1, 0x0, &eax, &ebx, &ecx, &edx);
	family = ((eax & 0x0FF00000) >> 20) + ((eax & 0x00000F00) >> 8);
	model = ((eax & 0x000F0000) >> 12) + ((eax & 0x000000F0) >> 4);
	stepping = eax & 0x0000000F;
	if (family == 6 && model == 26 && stepping == 4)
		if (core_id())
			return -1;
#endif /* CONFIG_KB_CORE0_ONLY */

	if ((inb(KBSTATP) & KBS_DIB) == 0)
		return -1;

	data = inb(KBDATAP);

	if (data == 0xE0) {
		// E0 escape character
		shift |= E0ESC;
		return 0;
	} else if (data & 0x80) {
		/* TODO: need a better check for bad key releases */
		if (data == 0xff)
			return -1;
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

static int kb_get_char(struct cons_dev *cdev, uint8_t *data)
{
 	int kb_d;
	/* kbd_proc_data returns 0 if we should keep asking.  It return -1 when
	 * there is no data, and anything else is a char */
	while ((kb_d = kbd_proc_data()) == 0)
		cpu_relax();
	if (kb_d == -1)
		return -1;
	*data = (uint8_t)kb_d;
	return 0;
}

void kbd_init(void)
{
	/* init and post the kb cons_dev */
	kb.type = CONS_KB_DEV;
	kb.val = 0;
	kb.irq = 1;		/* default PC keyboard IRQ */
	kb.model = "PC Keyboard";
	kb.getc = kb_get_char;
	SLIST_INSERT_HEAD(&cdev_list, &kb, next);
}

/***** General device-independent console code *****/

/* Initialize the console devices */
void cons_init(void)
{
	cga_init();
	kbd_init();
	serial_init();
}

/* Returns 0 on success, with the char in *data */
int cons_get_char(struct cons_dev *cdev, uint8_t *data)
{
	return cdev->getc(cdev, data);
}

/* Returns any available character, or 0 for none (legacy helper) */
int cons_get_any_char(void)
{
	uint8_t c;
	struct cons_dev *i;
	/* First to succeed gets returned */
	SLIST_FOREACH(i, &cdev_list, next) {
		if (!cons_get_char(i, &c))
			return c;
	}
	return 0;
}

/* output a character to all console outputs (monitor and all serials) */
void cons_putc(int c)
{
	void logbuf(int c);
	#ifdef CONFIG_TRACE_LOCKS
	int8_t irq_state = 0;
	disable_irqsave(&irq_state);
	__spin_lock(&console_lock);
	#else
	spin_lock_irqsave(&console_lock);
	#endif

	#ifndef CONFIG_SERIAL_IO
		serial_spam_char(c);
	#endif
	//lpt_putc(c); 	/* very slow on the nehalem */
	cga_putc(c);
	logbuf(c);

	#ifdef CONFIG_TRACE_LOCKS
	__spin_unlock(&console_lock);
	enable_irqsave(&irq_state);
	#else
	spin_unlock_irqsave(&console_lock);
	#endif
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

	while ((c = cons_get_any_char()) == 0)
		/* do nothing */;
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}

/* TODO: remove us (and serial IO) */
void serial_send_byte(uint8_t b)
{
}

int serial_read_byte(void)
{
	return -1;
}
