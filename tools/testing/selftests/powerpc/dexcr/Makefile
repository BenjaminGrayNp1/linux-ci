TEST_GEN_PROGS := hashchk_test

TEST_FILES := settings
top_srcdir = ../../../../..
include ../../lib.mk

HASHCHK_TEST_CFLAGS = -no-pie $(call cc-option,-mno-rop-protect)

$(TEST_GEN_PROGS): ../harness.c ../utils.c ./dexcr.c
