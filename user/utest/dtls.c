/* Copyright (c) 2016 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details. */

#include <utest/utest.h>
#include <parlib/dtls.h>
#include <stdio.h>

TEST_SUITE("DTLS");

/* <--- Begin definition of test cases ---> */

bool test_get_without_set(void)
{
	dtls_key_t key;
	void *got_val;

	key = dtls_key_create(0);
	got_val = get_dtls(key);
	UT_ASSERT_FMT("Expected 0, got %p", got_val == 0, 0, got_val);
	destroy_dtls();
	return TRUE;
}

/* This catches a bug, but it had to have a set happen at some point. */
bool test_get_after_reset(void)
{
	dtls_key_t key;
	void *set_val = (void*)0x15;
	void *got_val;

	key = dtls_key_create(0);
	set_dtls(key, set_val);
	destroy_dtls();

	key = dtls_key_create(0);
	got_val = get_dtls(key);
	UT_ASSERT_FMT("Expected 0, got %p", got_val == 0, 0, got_val);
	destroy_dtls();
	return TRUE;
}

bool test_set_and_get(void)
{
	dtls_key_t key;
	void *set_val = (void*)0x15;
	void *got_val;

	key = dtls_key_create(0);
	set_dtls(key, set_val);
	got_val = get_dtls(key);
	UT_ASSERT_FMT("Expected %p, got %p", got_val == set_val, set_val,
		      got_val);
	destroy_dtls();
	return TRUE;
}

bool test_set_twice(void)
{
	dtls_key_t key;
	void *set_val = (void*)0x15;
	void *got_val;

	key = dtls_key_create(0);
	set_dtls(key, set_val);
	set_dtls(key, set_val + 1);
	destroy_dtls();
	return TRUE;
}

static dtls_key_t sfd_global_key;

static void setting_dtor(void *arg)
{
	set_dtls(sfd_global_key, (void*)0xf00);
}

/* Users can set from a destructor.  In some implementations of pthread keys,
 * you can't safely set_specific from within a destructor without the risk of an
 * infinite loop or storage loss.  Our DTLS implementation shouldn't be doing
 * either, though we can't check for storage loss. */
bool test_set_from_dtor(void)
{
	void *set_val = (void*)0x15;

	sfd_global_key = dtls_key_create(setting_dtor);
	set_dtls(sfd_global_key, set_val);
	destroy_dtls();
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(get_without_set),
	UTEST_REG(get_after_reset),
	UTEST_REG(set_and_get),
	UTEST_REG(set_twice),
	UTEST_REG(set_from_dtor),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
