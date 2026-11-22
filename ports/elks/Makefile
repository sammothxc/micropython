# MicroPython for ELKS (8086/8088).
#
#   make            cross-build build/micropython with Open Watcom (large model)
#   make HOST=1     native build with the host gcc, to test the port layer
#
# The cross build needs:
#   WATCOM  Open Watcom v2 installation     (default ~/open-watcom-v2)
#   ELKS    ELKS source tree, with its libc built for Watcom large model:
#             cd $ELKS && . ./env.sh && cd libc && make -f watcom.mk MODEL=l

HOST ?= 0
ifeq ($(HOST),1)
BUILD ?= build-host
endif

include ../../py/mkenv.mk

QSTR_DEFS = qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION = 0

include $(TOP)/py/py.mk

INC += -I. -I$(TOP) -I$(BUILD)

SRC_C = \
	main.c \
	elks_mphal.c \
	elks_libm.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	extmod/modtime.c \

SRC_QSTR += $(SRC_C)

OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

MPY = $(BUILD)/micropython

ifeq ($(HOST),1)
###############################################################################
# Native build

CFLAGS += $(INC) -Wall -Werror -std=gnu99 -Os -g
LDLIBS = -lm

all: $(MPY)

$(MPY): $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(CC) -o $@ $^ $(LDLIBS)

else
###############################################################################
# ELKS cross build with Open Watcom

WATCOM ?= $(HOME)/open-watcom-v2
ELKS ?= $(HOME)/elks
export WATCOM
export PATH := $(WATCOM)/binl64:$(WATCOM)/binl:$(PATH)

ELKS_LIBC = $(ELKS)/libc/libcl.lib
ELKS_INC = -I$(ELKS)/libc/include -I$(ELKS)/elks/include -I$(ELKS)/libc/include/watcom

# C stack and near (malloc) heap sizes; both live in the 64 KB data segment.
# The Python heap is separate far memory, see main.c.
STACK_SIZE ?= 0x4000
NEAR_HEAP_SIZE ?= 0x800

# Optimize for time.  Besides speed this makes Open Watcom place data objects
# at even addresses, which MicroPython's object representation depends on;
# tools/checkmap.py verifies that after linking.
COPT ?= -Wc,-ot

# Number of segments the ELKS kernel accepts in one program (MAX_SEGS).
ELKS_MAX_SEGS ?= 5

CC = owcc
# Generated headers are preprocessed with the host cpp, see tools/elkscpp.
CPP = $(CURDIR)/tools/elkscpp

# -zp2 aligns structure members to 2 bytes.  All data stays in the 64 KB
# default data segment: a far data segment (-zt) would use up one of the few
# segments the kernel loads, see ELKS_MAX_SEGS.
# -aa allows non-constant initializers for local aggregates.  Warning 140 is
# the harmless redefinition of alloca by the ELKS headers.
CFLAGS += \
	-bnone -mcmodel=l -march=i86 -std=c99 -fsigned-char -msoft-float \
	-fnostdlib -fno-stack-check $(COPT) \
	-Wc,-zev -Wc,-zls -Wc,-x -Wc,-aa -Wc,-zp2 -Wc,-wcd=303 -Wc,-wcd=140 \
	$(ELKS_INC) $(INC) -DNDEBUG

CSUPEROPT = $(COPT)

LDFLAGS = \
	-bos2 -s \
	-Wl,option -Wl,start=_start \
	-Wl,option -Wl,dosseg \
	-Wl,option -Wl,nodefaultlibs \
	-Wl,option -Wl,stack=$(STACK_SIZE) \
	-Wl,option -Wl,heapsize=$(NEAR_HEAP_SIZE) \
	-Wl,option -Wl,map=$(MPY).map \
	-Wl,option -Wl,statics \
	-Wl,library -Wl,$(ELKS_LIBC)

all: $(MPY)

$(MPY): $(OBJ) $(ELKS_LIBC)
	$(ECHO) "LINK $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $(OBJ)
	$(Q)$(PYTHON) tools/checkmap.py $(MPY).map $(ELKS_MAX_SEGS)
	$(Q)ls -l $@

$(ELKS_LIBC):
	@echo "Missing $@: build the ELKS C library for Watcom large model first:"
	@echo "  cd $(ELKS) && . ./env.sh && cd libc && make -f watcom.mk MODEL=l"
	@false

endif

include $(TOP)/py/mkrules.mk

ifneq ($(HOST),1)
# owcc does not take gcc's dependency-file options, so header dependencies are
# not tracked per file: a change to any core or port header rebuilds everything.
$(OBJ): $(wildcard $(TOP)/py/*.h) $(wildcard *.h)

define compile_c
$(ECHO) "CC $<"
$(Q)$(CC) $(CFLAGS) -c -o $@ $<
endef
endif
