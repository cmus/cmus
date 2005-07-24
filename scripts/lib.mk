#
# Copyright 2005 Timo Hirvonen <tihirvon@ee.oulu.fi>
#
# This file is licensed under the GPLv2.

ifeq ($(BUILD_VERBOSITY),2)
  quiet =
  Q =
else
  ifeq ($(BUILD_VERBOSITY),1)
    quiet = quiet_
    Q = @
  else
    quiet = silent_
    Q = @
  endif
endif

ifeq ($(srcdir),)
$(error srcdir not defined)
endif

SHELL		:= /bin/bash
VPATH		:= $(srcdir)
INSTALL		:= @$(scriptdir)/install
SRCDIR_INSTALL	:= @cd $(srcdir) && $(scriptdir)/install
RST2HTML	:= rst2html.py
RST2HTML_FLAGS	:=
CTAGS		:= ctags

EMPTY		:=
SPACE		:= $(EMPTY) $(EMPTY)
COMMA		:= ,

%: %.in $(top_builddir)/Makefile
	$(call cmd,sed_in)

%.o: %.S
	$(call cmd,as)

# object files for programs and static libs
%.o: %.c
	$(call cmd,cc)

%.o: %.cc
	$(call cmd,cxx)

# object files for shared libs
%.lo: %.c
	$(call cmd,cc_lo)

%.lo: %.cc
	$(call cmd,cxx_lo)

# CC for programs and shared libraries
quiet_cmd_cc    = CC     $@
quiet_cmd_cc_lo = CC     $@
ifeq ($(CC_GENERATE_DEPS),y)
      cmd_cc    = $(CC) -c $(CFLAGS) -MD -MP -MF .dep-$@ -o $@ $<
      cmd_cc_lo = $(CC) -c $(CFLAGS) $(SOFLAGS) -MD -MP -MF .dep-$@ -o $@ $<
else
      cmd_cc    = $(CC) -c $(CFLAGS) -o $@ $<
      cmd_cc_lo = $(CC) -c $(CFLAGS) $(SOFLAGS) -o $@ $<
endif

# LD for programs, optional parameter: libraries
quiet_cmd_ld = LD     $@
      cmd_ld = $(LD) $(LDFLAGS) -o $@ $^ $(1)

# LD for shared libraries, optional parameter: libraries
quiet_cmd_ld_so = LD     $@
      cmd_ld_so = $(LD) -shared $(LDFLAGS) -o $@ $^ $(1)

# CXX for programs and shared libraries
quiet_cmd_cxx    = CXX    $@
quiet_cmd_cxx_lo = CXX    $@
ifeq ($(CXX_GENERATE_DEPS),y)
      cmd_cxx    = $(CXX) -c $(CXXFLAGS) -MD -MP -MF .dep-$@ -o $@ $<
      cmd_cxx_lo = $(CXX) -c $(CXXFLAGS) $(SOFLAGS) -MD -MP -MF .dep-$@ -o $@ $<
else
      cmd_cxx    = $(CXX) -c $(CXXFLAGS) -o $@ $<
      cmd_cxx_lo = $(CXX) -c $(CXXFLAGS) $(SOFLAGS) -o $@ $<
endif

# CXXLD for programs, optional parameter: libraries
quiet_cmd_cxxld = CXXLD  $@
      cmd_cxxld = $(CXXLD) $(CXXLDFLAGS) -o $@ $^ $(1)

# CXXLD for shared libraries, optional parameter: libraries
quiet_cmd_cxxld_so = CXXLD  $@
      cmd_cxxld_so = $(CXXLD) -shared $(LDFLAGS) -o $@ $^ $(1)

# create archive
quiet_cmd_ar = AR     $@
      cmd_ar = $(AR) $(ARFLAGS) $@ $^

# assembler
quiet_cmd_as = AS     $@
      cmd_as = $(AS) -c $(ASFLAGS) -o $@ $<

# filter file (.in) with sed
quiet_cmd_sed_in = SED    $@
      cmd_sed_in = $(scriptdir)/sedin $< $@

# .rst (restructured text) -> .html
quiet_cmd_rst = RST    $@
      cmd_rst = $(RST2HTML) $(RST2HTML_FLAGS) $< $@

# quiet_cmd_clean = CLEAN  Temporary files
      cmd_clean = rm -f $(clean)

# quiet_cmd_distclean = CLEAN  All generated files
      cmd_distclean = rm -f $(distclean)

cmd = @$(if $($(quiet)cmd_$(1)),echo '   $(call $(quiet)cmd_$(1),$(2))' &&) $(call cmd_$(1),$(2))

# -----------------------------------------------------------------------------
# Templates
#
# -objs-y     Object files to build
# -objs-n     Object files which are not built
# -cpp        Use C++ linker instead of C linker? y or n. default is n.
# -mode       File mode when installed. Default is almost always what you want
# -owner      File owner when installed. Default is undefined
# -group      File group when installed. Default is undefined
# -target     Target file name. Default is the prefix (foo for foo-target)
#             with .a or .so appended for libs. You don't usually need to
#             define this for programs

# build templates

define template_common
$(1)-cpp	?= n
$(1)-linkprefix	:= $$(if $$(subst n,,$$($(1)-cpp)),cxx,)
$(1)-instflags	:= $$(if $$($(1)-owner),--owner=$$($(1)-owner)) $$(if $$($(1)-group),--group=$$($(1)-group)) $$(if $$($(1)-mode),--fmode=$$($(1)-mode))
endef

define archive_template
$(1)-mode	?= 0644
$(1)-target	?= $(1).a
$(call template_common,$(1))

$$($(1)-target): $$($(1)-objs-y)
	$$(call cmd,ar,$$($(1)-libs))
endef

define lib_template
$(1)-mode	?= 0755
$(1)-target	?= $(1).so
$(call template_common,$(1))

$$($(1)-target): $$($(1)-objs-y)
	$$(call cmd,$$($(1)-linkprefix)ld_so,$$($(1)-libs))
endef

define program_template
$(1)-mode	?= 0755
$(1)-target	?= $(1)
$(call template_common,$(1))

$$($(1)-target): $$($(1)-objs-y)
	$$(call cmd,$$($(1)-linkprefix)ld,$$($(1)-libs))
endef

# install templates

define a_install_template
$(1)-install: $$($(1)-target)
	$(INSTALL) $$($(1)-instflags) $(libdir) $$($(1)-target)
endef

define so_install_template
$(1)-install: $$($(1)-target)
	$(INSTALL) $$($(1)-instflags) $(libdir) $$($(1)-target)
endef

define bin_install_template
$(1)-install: $$($(1)-target)
	$(INSTALL) $$($(1)-instflags) $(bindir) $$($(1)-target)
endef

define sbin_install_template
$(1)-install: $$($(1)-target)
	$(INSTALL) $$($(1)-instflags) $(sbindir) $$($(1)-target)
endef
# -----------------------------------------------------------------------------

# Run target recursively
#
# Usage:
#     recursive-SOMETHING:
#             $(call recurse)
#
#     SOMETHING:
#             ...
#
#     .PHONY: recursive-SOMETHING SOMETHING
#
# NOTE: 'for subdir in ;' does not work with old bash, but
#       'subdirs=""; for subdir in $subdirs;' does!
define recurse
	+@fail=no; \
	subdirs="$(subdirs)"; \
	dir=$(currelpath); \
	[[ -n $$dir ]] && dir=$$dir/; \
	for subdir in $$subdirs; \
	do \
		target=$(subst recursive-,,$@); \
		cmd="$(MAKE) -f $(scriptdir)/sub.mk -C $$subdir $$target"; \
		case $(BUILD_VERBOSITY) in \
			1) \
				echo " * Making $$target in $$dir$$subdir"; \
				;; \
			2) \
				echo "$$cmd"; \
				;; \
		esac; \
		if ! $$cmd; \
		then \
			fail=yes; \
			[[ -z "$(filter -k,$(MAKEFLAGS))" ]] && break; \
		fi; \
	done; \
	[[ $$fail = no ]]
endef
