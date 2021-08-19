TEST_GEN_PROGS := rop-user

TEST_FILES := settings
top_srcdir = ../../../../..
include ../../lib.mk

ROP_USER_CFLAGS = -std=gnu99 -fno-stack-protector -O0 -mcpu=power10
$(shell gcc -v --help 2> /dev/null | grep mrop 1>/dev/null)
ifeq ($(.SHELLSTATUS),0)
	ROP_USER_CFLAGS += -mrop-protect
endif

$(OUTPUT)/rop-user: CFLAGS += $(ROP_USER_CFLAGS) rop-gadget.S
