/* Copyright (c) 2015 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <utest/utest.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <parlib/net.h>

TEST_SUITE("DEVVARS");

static bool test_read_var(const char *name, const char *val)
{
	char buf[128];
	int fd, ret;

	ret = snprintf(buf, sizeof(buf), "#vars/%s", name);
	if (snprintf_error(ret, sizeof(buf)))
		UT_ASSERT_FMT("snprintf failed!", FALSE);
	fd = open(buf, O_READ);
	UT_ASSERT_FMT("Could not open vars file %s, check CONFIG_DEVVARS_TEST",
	              fd >= 0, buf);
	ret = read(fd, buf, sizeof(buf));
	UT_ASSERT_FMT("Could not read vars file %s", ret > 0, buf);
	UT_ASSERT_FMT("Value differs, got %s, wanted %s", strcmp(buf, val) == 0,
	              buf, val);
	return TRUE;
}

bool test_devvars_fmt(void)
{
	if (!test_read_var("s!s", "string"))
		return FALSE;
	if (!test_read_var("c!c", "x"))
		return FALSE;
	if (!test_read_var("u8!ub", "8"))
		return FALSE;
	if (!test_read_var("u16!uh", "16"))
		return FALSE;
	if (!test_read_var("u32!uw", "32"))
		return FALSE;
	if (!test_read_var("u64!ug", "64"))
		return FALSE;
	if (!test_read_var("d8!db", "-8"))
		return FALSE;
	if (!test_read_var("d16!dh", "-16"))
		return FALSE;
	if (!test_read_var("d32!dw", "-32"))
		return FALSE;
	if (!test_read_var("d64!dg", "-64"))
		return FALSE;
	if (!test_read_var("x8!xb", "0x8"))
		return FALSE;
	if (!test_read_var("x16!xh", "0x16"))
		return FALSE;
	if (!test_read_var("x32!xw", "0x32"))
		return FALSE;
	if (!test_read_var("x64!xg", "0x64"))
		return FALSE;
	if (!test_read_var("o8!ob", "01"))
		return FALSE;
	if (!test_read_var("o16!oh", "016"))
		return FALSE;
	if (!test_read_var("o32!ow", "032"))
		return FALSE;
	if (!test_read_var("o64!og", "064"))
		return FALSE;
	return TRUE;
}

static bool test_new_var(const char *name, const char *val)
{
	char buf[128];
	char path[128];
	int fd, ret;

	ret = snprintf(path, sizeof(path), "#vars/%s", name);
	if (snprintf_error(ret, sizeof(path)))
		UT_ASSERT_FMT("snprintf failed!", FALSE);
	fd = open(path, O_READ | O_CREATE, S_IRUSR);
	UT_ASSERT_FMT("Could not open vars file %s, check CONFIG_DEVVARS_TEST",
	              fd >= 0, path);
	ret = read(fd, buf, sizeof(buf));
	UT_ASSERT_FMT("Could not read vars file %s", ret > 0, path);
	UT_ASSERT_FMT("Value differs, got %s, wanted %s", strcmp(buf, val) == 0,
	              buf, val);
	ret = unlink(path);
	UT_ASSERT_FMT("Could not remove %s", ret == 0, path);
	return TRUE;
}

bool test_devvars_newfile(void)
{
	if (!test_new_var("devvars_foobar!s", "foobar"))
		return FALSE;
	return TRUE;
}

/* Make sure test_read_var() knows how to fail */
bool test_devvars_test(void)
{
	UT_ASSERT_FMT("Opened when it shouldn't have",
	              !test_read_var("NO_SUCH_FILE!xw", "0x32"));
	UT_ASSERT_FMT("Got the wrong value but thought it was fine",
	              !test_read_var("x32!xw", "0xdeadbeef"));
	return TRUE;
}

struct utest utests[] = {
	UTEST_REG(devvars_fmt),
	UTEST_REG(devvars_newfile),
	UTEST_REG(devvars_test),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
