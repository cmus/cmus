#
# Copyright 2005 Timo Hirvonen <tihirvon@ee.oulu.fi>
#
# This file is licensed under the GPLv2.

ifndef top_srcdir
$(error top_srcdir not defined)
endif

ifndef top_builddir
$(error top_builddir not defined)
endif

ifndef scriptdir
$(error scriptdir not defined)
endif

_cur_dir := $(shell pwd)
ifeq ($(_cur_dir),$(top_builddir))
  currelpath :=
else
  currelpath := $(subst $(top_builddir)/,,$(_cur_dir))
endif

ifeq ($(currelpath),)
  srcdir := $(top_srcdir)
else
  srcdir := $(top_srcdir)/$(currelpath)
endif

restricted := 0
ifeq ($(MAKELEVEL),0)
  # if in subdir
  ifneq ($(currelpath),)
    # make was executed by user in a subdir
    # disable distclean, uninstall and dist
    restricted := 1
  endif
endif

all: build

extra: extra-build

.PHONY: all extra

# constants
builddir	:= .

# these can be set from Dir.mk or common.mk
clean		:=
distclean	:=
targets		:=
extra-targets	:=
subdirs		:=
ctags-languages	:= all,-html
ctags-dirs	:=

include $(scriptdir)/lib.mk
-include $(top_srcdir)/common.mk
include $(srcdir)/Dir.mk

dep-files	:= $(wildcard .dep-*)
clean		+= $(targets) $(extra-targets) $(dep-files) core core.[0-9]*
distclean	+= Makefile

ifeq ($(currelpath),)
distclean	+= config.h config.mk install.log
endif

# create directories
ifneq ($(top_builddir),$(top_srcdir))
_dummy := $(foreach d,$(subdirs),$(shell [ -d $(d) ] || mkdir $(d)))
endif

clean: recursive-clean
	$(call cmd,clean)

define top_distclean
	@if [[ -f .distclean ]]; \
	then \
		exec < .distclean || exit 0; \
		while read line; \
		do \
			rm -f $$line; \
			dir=$$(dirname $$line); \
			if [[ $(top_srcdir) != $(top_builddir) ]] && [[ $$dir != . ]]; \
			then \
				rmdir -p $$dir 2>/dev/null; \
			fi; \
		done; \
		rm -f .distclean; \
	fi
endef

ifeq ($(restricted),0)
distclean: recursive-distclean
	$(call cmd,clean)
	$(call cmd,distclean)
ifeq ($(currelpath),)
	$(call top_distclean)
endif
	$(Q)rm -f $(srcdir)/tags
ifneq ($(top_builddir),$(top_srcdir))
	$(shell rmdir $(subdirs) 2>/dev/null || exit 0)
endif
else
distclean:
	@echo "can't make $@ in a subdir" >&2
	@/bin/false
endif

build: $(targets) recursive-build
extra-build: $(extra-targets) recursive-extra-build

# minimize recursion
ifeq ($(MAKELEVEL),0)

ifneq ($(filter install,$(MAKECMDGOALS)),)
install: install-exec install-data recursive-install
install-exec: build
install-data: build
recursive-install: build
else
install: install-exec install-data
install-exec: build recursive-install-exec
install-data: build recursive-install-data
recursive-install-exec: build
recursive-install-data: build
endif

ifneq ($(filter extra-install,$(MAKECMDGOALS)),)
extra-install: extra-install-exec extra-install-data recursive-extra-install
extra-install-exec: extra-build
extra-install-data: extra-build
recursive-extra-install: extra-build
else
extra-install: extra-install-exec extra-install-data
extra-install-exec: extra-build recursive-extra-install-exec
extra-install-data: extra-build recursive-extra-install-data
recursive-extra-install-exec: extra-build
recursive-extra-install-data: extra-build
endif

else
# sub-make: build already done

ifneq ($(filter install,$(MAKECMDGOALS)),)
install: install-exec install-data recursive-install
install-exec:
install-data:
else
install: install-exec install-data
install-exec: recursive-install-exec
install-data: recursive-install-data
endif

ifneq ($(filter extra-install,$(MAKECMDGOALS)),)
extra-install: extra-install-exec extra-install-data recursive-extra-install
extra-install-exec:
extra-install-data:
else
extra-install: extra-install-exec extra-install-data
extra-install-exec: recursive-extra-install-exec
extra-install-data: recursive-extra-install-data
endif

endif

# these might not have been defined in Dir.mk
install-exec:
install-data:
extra-install-exec:
extra-install-data:

_targets		:= build install install-exec install-data
_targets		+= extra-build extra-install extra-install-exec extra-install-data
_targets		+= clean distclean
_recursive_targets	:= $(addprefix recursive-,$(_targets))

$(_recursive_targets):
	$(call recurse)

.PHONY: $(_targets) $(_recursive_targets)

ifneq ($(dep-files),)
-include $(dep-files)
endif
