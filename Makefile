################################################
# Toplevel makefile for rtl8710_tools    code  #
################################################

ifeq ($(V),)
    Q := @
    # Do not print "Entering directory ...".
    MAKEFLAGS += --no-print-directory
endif

$(info Making "$(MAKECMDGOALS)")

# Build directory
ifdef O
	BUILD_DIR := $(shell readlink -f $(O))
else
	BUILD_DIR := $(CURDIR)/build
endif

# Source directory
SRC_DIR := $(CURDIR)/src
BIN_DIR := $(BUILD_DIR)

# Configure toolchain

CC          := gcc
LD          ?= gcc

SRC         := $(wildcard $(SRC_DIR)/*.c)
BINS        := $(subst $(SRC_DIR),$(BIN_DIR), $(SRC:%.c=%))

CFLAGS      += -I$(SRC_DIR) -Wall

.PHONY: clean

all: $(BINS)

$(BIN_DIR)/%: $(SRC_DIR)/%.c 
	$(Q)mkdir -p `dirname $@`
	$(if $(Q), @echo " (CC)        $(subst $(BIN_DIR)/,,$@)")
	$(Q)$(CC) $(CPPFLAGS) -I`dirname $<` -o $@ $<

clean:  
	$(Q)rm -rf $(BIN_DIR)
