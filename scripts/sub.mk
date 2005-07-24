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

# ---------------------------------------
# variables used in Dir.mk and commmon.mk

# low-level
clean		:=
distclean	:=
targets-n	:=
targets-y	:=
extra-targets-n	:=
extra-targets-y	:=

# high-level (make all)
archives-n	:=
archives-y	:=
libs-n		:=
libs-y		:=
programs-n	:=
programs-y	:=
lib-archives-n	:=
lib-archives-y	:=
lib-libs-n	:=
lib-libs-y	:=
bin-programs-n	:=
bin-programs-y	:=
sbin-programs-n	:=
sbin-programs-y	:=
# same as above but extra versions (make extra)
extra-archives-n	:=
extra-archives-y	:=
extra-libs-n		:=
extra-libs-y		:=
extra-programs-n	:=
extra-programs-y	:=
extra-lib-archives-n	:=
extra-lib-archives-y	:=
extra-lib-libs-n	:=
extra-lib-libs-y	:=
extra-bin-programs-n	:=
extra-bin-programs-y	:=
extra-sbin-programs-n	:=
extra-sbin-programs-y	:=

# misc
subdirs		:=
ctags-languages	:= all,-html
ctags-dirs	:=
# ---------------------------------------

include $(scriptdir)/lib.mk
-include $(top_srcdir)/common.mk
include $(srcdir)/Dir.mk

ifneq ($(targets),)
$(warning targets is deprecated)
endif

# ---------------------------------------------------------------------------------------------
# do "foo = $(foo-y) $(foo-n) $(extra-foo-y) $(extra-foo-n)"
# to simplify generation of build/install targets
$(foreach i,archives libs programs lib-archives lib-libs bin-programs sbin-programs,$(eval $(i) := $$($(i)-y) $$($(i)-n) $$(extra-$(i)-y) $$(extra-$(i)-n)))

# generate build targets
$(foreach i,                 $(archives) $(lib-archives),$(eval $(call archive_template,$(i))))
$(foreach i,                         $(libs) $(lib-libs),$(eval $(call     lib_template,$(i))))
$(foreach i,$(programs) $(bin-programs) $(sbin-programs),$(eval $(call program_template,$(i))))

# generate install targets
$(foreach i, $(lib-archives),$(eval $(call    a_install_template,$(i))))
$(foreach i,     $(lib-libs),$(eval $(call   so_install_template,$(i))))
$(foreach i, $(bin-programs),$(eval $(call  bin_install_template,$(i))))
$(foreach i,$(sbin-programs),$(eval $(call sbin_install_template,$(i))))
# ---------------------------------------------------------------------------------------------

# virtual targets. usually same as real ones for programs
all-y		:= $(foreach i,archives libs programs lib-archives lib-libs bin-programs sbin-programs,$($(i)-y))
all-n		:= $(foreach i,archives libs programs lib-archives lib-libs bin-programs sbin-programs,$($(i)-n))
all-extra-y	:= $(foreach i,archives libs programs lib-archives lib-libs bin-programs sbin-programs,$(extra-$(i)-y))
all-extra-n	:= $(foreach i,archives libs programs lib-archives lib-libs bin-programs sbin-programs,$(extra-$(i)-n))

# real target files
targets-y	+= $(foreach i,$(all-y),$($(i)-target))
targets-n	+= $(foreach i,$(all-n),$($(i)-target))
extra-targets-y	+= $(foreach i,$(all-extra-y),$($(i)-target))
extra-targets-n	+= $(foreach i,$(all-extra-n),$($(i)-target))

# clean target files
clean		+= $(targets-y) $(targets-n) $(extra-targets-y) $(extra-targets-n)

# clean object files
clean		+= $(foreach i,$(all-y) $(all-n) $(all-extra-y) $(all-extra-n),$($(i)-objs-y) $($(i)-objs-n))

dep-files	:= $(wildcard .dep-*)
clean		+= $(dep-files) core core.[0-9]*
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

build: $(targets-y) recursive-build
extra-build: $(extra-targets-y) recursive-extra-build

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

get-installs		= $(foreach j,$(foreach i,$(1),$($(i)-y)),$(j)-install)

install-exec: $(call get-installs,bin-programs sbin-programs lib-libs)
install-data: $(call get-installs,lib-archives)
extra-install-exec: $(call get-installs,extra-bin-programs extra-sbin-programs extra-lib-libs)
extra-install-data: $(call get-installs,extra-lib-archives)

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
