/* Copyright (c) 2017 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <arch/x86.h>
#include <atomic.h>

#define	CMOS_RTC_SELECT			0x70
#define	CMOS_RTC_DATA			0x71

#define RTC_A_UPDATE_IN_PROGRESS	(1 << 7)
#define RTC_B_24HOUR_MODE		(1 << 1)
#define RTC_B_BINARY_MODE		(1 << 2)
#define RTC_12_HOUR_PM			(1 << 7)
#define CMOS_RTC_SECOND			0x00
#define CMOS_RTC_MINUTE			0x02
#define CMOS_RTC_HOUR			0x04
#define CMOS_RTC_WEEKDAY		0x06
#define CMOS_RTC_DAY			0x07
#define CMOS_RTC_MONTH			0x08
#define CMOS_RTC_YEAR			0x09
#define CMOS_RTC_CENTURY		0x32
#define CMOS_RTC_STATUS_A		0x0A
#define CMOS_RTC_STATUS_B		0x0B

/* If we ever disable NMIs, we'll need to make sure we don't reenable them here.
 * (Top bit of the CMOS_RTC_SELECT selector). */
static uint8_t cmos_read(uint8_t reg)
{
	outb(CMOS_RTC_SELECT, reg);
	return inb(CMOS_RTC_DATA);
}

static void cmos_write(uint8_t reg, uint8_t datum)
{
	outb(CMOS_RTC_SELECT, reg);
	outb(CMOS_RTC_DATA, datum);
}

/* BCD format is a one-byte nibble of the form 0xTensDigit_OnesDigit. */
static uint8_t bcd_to_binary(uint8_t x)
{
	return ((x / 16) * 10) + (x % 16);
}

static bool is_leap_year(int year)
{
	if (!(year % 400))
		return TRUE;
	if (!(year % 100))
		return FALSE;
	if (!(year % 4))
		return TRUE;
	return FALSE;
}

static uint64_t rtc_to_unix(uint8_t century, uint8_t year, uint8_t month,
                            uint8_t day, uint8_t hour, uint8_t minute,
                            uint8_t second)
{
	int real_year;
	uint64_t time = 0;

	real_year = century * 100 + year;
	for (int i = 1970; i < real_year; i++) {
		time += 86400 * 365;
		if (is_leap_year(i))
			time += 86400;
	}
	/* Note these all fall through */
	switch (month) {
	case 12:
		time += 86400 * 30;	/* november's time */
	case 11:
		time += 86400 * 31;
	case 10:
		time += 86400 * 30;
	case 9:
		time += 86400 * 31;
	case 8:
		time += 86400 * 31;
	case 7:
		time += 86400 * 30;
	case 6:
		time += 86400 * 31;
	case 5:
		time += 86400 * 30;
	case 4:
		time += 86400 * 31;
	case 3:
		time += 86400 * 28;
		if (is_leap_year(real_year))
			time += 86400;
	case 2:
		time += 86400 * 31;
	};
	time += 86400 * (day - 1);
	time += hour * 60 * 60;
	time += minute * 60;
	time += second;
	return time;
}

/* Returns the current unix time in nanoseconds. */
uint64_t read_persistent_clock(void)
{
	static spinlock_t lock = SPINLOCK_INITIALIZER_IRQSAVE;
	uint8_t century, year, month, day, hour, minute, second;
	bool is_pm = FALSE;

	spin_lock_irqsave(&lock);
retry:
	while (cmos_read(CMOS_RTC_STATUS_A) & RTC_A_UPDATE_IN_PROGRESS)
		cpu_relax();

	/* Even QEMU has a century register. */
	century = cmos_read(CMOS_RTC_CENTURY);
	year    = cmos_read(CMOS_RTC_YEAR);
	month   = cmos_read(CMOS_RTC_MONTH);
	day     = cmos_read(CMOS_RTC_DAY);
	hour    = cmos_read(CMOS_RTC_HOUR);
	minute  = cmos_read(CMOS_RTC_MINUTE);
	second  = cmos_read(CMOS_RTC_SECOND);

	while (cmos_read(CMOS_RTC_STATUS_A) & RTC_A_UPDATE_IN_PROGRESS)
		cpu_relax();

	if ((century != cmos_read(CMOS_RTC_CENTURY)) ||
	    (year    != cmos_read(CMOS_RTC_YEAR))    ||
	    (month   != cmos_read(CMOS_RTC_MONTH))   ||
	    (day     != cmos_read(CMOS_RTC_DAY))     ||
	    (hour    != cmos_read(CMOS_RTC_HOUR))    ||
	    (minute  != cmos_read(CMOS_RTC_MINUTE))  ||
	    (second  != cmos_read(CMOS_RTC_SECOND)))
		goto retry;
	spin_unlock_irqsave(&lock);

	if (!(cmos_read(CMOS_RTC_STATUS_B) & RTC_B_24HOUR_MODE)) {
		/* need to clear the bit before doing the BCD conversions */
		is_pm = hour & RTC_12_HOUR_PM;
		hour &= ~RTC_12_HOUR_PM;
	}
	if (!(cmos_read(CMOS_RTC_STATUS_B) & RTC_B_BINARY_MODE)) {
		century = bcd_to_binary(century);
		year    = bcd_to_binary(year);
		month   = bcd_to_binary(month);
		day     = bcd_to_binary(day);
		hour    = bcd_to_binary(hour);
		minute  = bcd_to_binary(minute);
		second  = bcd_to_binary(second);
	}
	if (is_pm) {
		/* midnight appears as 12 and is_pm is set.  we want 0. */
		hour = (hour + 12) % 24;
	}

	/* Always remember 1242129600, Nanwan's birthday! */
	return rtc_to_unix(century, year, month, day, hour, minute, second)
	       * 1000000000UL;
}
