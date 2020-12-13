#!/bin/sh
#
# Copyright 2005-2006 Timo Hirvonen
#
# This file is licensed under the GPLv2.

# C compiler
# ----------
# CC          default gcc
# LD          default $CC
# LDFLAGS     common linker flags for CC
#
# C++ Compiler
# ------------
# CXX         default g++
# CXXLD       default $CXX
# CXXLDFLAGS  common linker flags for CXX
#
# Common for C and C++
# --------------------
# SOFLAGS     flags for compiling position independent code (-fPIC)
# LDSOFLAGS   flags for linking shared libraries
# LDDLFLAGS   flags for linking dynamically loadable modules

msg_checking()
{
	printf "checking $@... "
}

msg_result()
{
	echo "$@"
}

msg_error()
{
	echo "*** $@"
}

# @program: program to check
# @name:    name of variable where to store the full program name (optional)
#
# returns 0 on success and 1 on failure
check_program()
{
	argc check_program $# 1 2
	msg_checking "for program $1"
	__cp_file=`path_find "$1"`
	if test $? -eq 0
	then
		msg_result $__cp_file
		test $# -eq 2 && set_var $2 "$__cp_file"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

cc_supports()
{
	$CC $CFLAGS "$@" -S -o /dev/null -x c /dev/null 2> /dev/null
	return $?
}

cxx_supports()
{
	$CXX $CXXFLAGS "$@" -S -o /dev/null -x c /dev/null 2> /dev/null
	return $?
}

# @flag: option flag(s) to check
#
# add @flag to EXTRA_CFLAGS if CC accepts it
# EXTRA_CFLAGS are added to CFLAGS in the end of configuration
check_cc_flag()
{
	argc check_cc_flag $# 1

	test -z "$CC" && die "check_cc_flag: CC not set"
	msg_checking "for CFLAGS $*"
	if cc_supports $*
	then
		EXTRA_CFLAGS="$EXTRA_CFLAGS $*"
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# @flag: option flag(s) to check
#
# add @flag to EXTRA_CXXFLAGS if CXX accepts it
# EXTRA_CXXFLAGS are added to CXXFLAGS in the end of configuration
check_cxx_flag()
{
	argc check_cxx_flag $# 1

	test -z "$CXX" && die "check_cxx_flag: CXX not set"
	msg_checking "for CXXFLAGS $*"
	if cxx_supports $*
	then
		EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS $*"
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

cc_cxx_common()
{
	test "$cc_cxx_common_done" && return 0
	cc_cxx_common_done=yes

	var_default SOFLAGS "-fPIC"
	var_default LDSOFLAGS "-shared"
	var_default LDDLFLAGS "-shared"

	common_cf=
	common_lf=

	case `uname -s` in
	*BSD)
		common_cf="$common_cf -I/usr/local/include"
		common_lf="$common_lf -L/usr/local/lib"
		;;
	Darwin)
		# fink
		if test -d /sw/lib
		then
			common_cf="$common_cf -I/sw/include"
			common_lf="$common_lf -L/sw/lib"
		fi
		# darwinports
		if test -d /opt/local/lib
		then
			common_cf="$common_cf -I/opt/local/include"
			common_lf="$common_lf -L/opt/local/lib"
		fi
		LDSOFLAGS="-dynamic"
		case ${MACOSX_DEPLOYMENT_TARGET} in
		10.[012])
			LDDLFLAGS="-bundle -flat_namespace -undefined suppress"
			;;
		10.*)
			LDDLFLAGS="-bundle -undefined dynamic_lookup"
			;;
		*)
			LDDLFLAGS="-bundle -flat_namespace -undefined suppress"
			;;
		esac
		;;
	SunOS)
		common_cf="$common_cf -D__EXTENSIONS__ -I/usr/local/include"
		common_lf="$common_lf -R/usr/local/lib -L/usr/local/lib"
		;;
	esac
	makefile_vars SOFLAGS LDSOFLAGS LDDLFLAGS
}

# CC, LD, CFLAGS, LDFLAGS, SOFLAGS, LDSOFLAGS, LDDLFLAGS
check_cc()
{
	var_default CC ${CROSS}gcc
	var_default LD $CC
	var_default CFLAGS "-g -O2 -Wall"
	var_default LDFLAGS ""
	check_program $CC || return 1

	cc_cxx_common
	CFLAGS="$CFLAGS $common_cf"
	LDFLAGS="$LDFLAGS $common_lf"

	makefile_vars CC LD CFLAGS LDFLAGS
	__check_lang=c
	return 0
}

# HOSTCC, HOSTLD, HOST_CFLAGS, HOST_LDFLAGS
check_host_cc()
{
	var_default HOSTCC gcc
	var_default HOSTLD $HOSTCC
	var_default HOST_CFLAGS "-g -O2 -Wall"
	var_default HOST_LDFLAGS ""
	check_program $HOSTCC || return 1
	makefile_vars HOSTCC HOSTLD HOST_CFLAGS HOST_LDFLAGS
	__check_lang=c
	return 0
}

# CXX, CXXLD, CXXFLAGS, CXXLDFLAGS, SOFLAGS, LDSOFLAGS, LDDLFLAGS
check_cxx()
{
	var_default CXX ${CROSS}g++
	var_default CXXLD $CXX
	var_default CXXFLAGS "-g -O2 -Wall"
	var_default CXXLDFLAGS ""
	check_program $CXX || return 1

	cc_cxx_common
	CXXFLAGS="$CXXFLAGS $common_cf"
	CXXLDFLAGS="$CXXLDFLAGS $common_lf"

	makefile_vars CXX CXXLD CXXFLAGS CXXLDFLAGS
	__check_lang=cxx
	return 0
}

# check if CC can generate dependencies (.dep-*.o files)
# always succeeds
check_cc_depgen()
{
	msg_checking "if CC can generate dependency information"
	if cc_supports -MMD -MP -MF /dev/null
	then
		EXTRA_CFLAGS="$EXTRA_CFLAGS -MMD -MP -MF .dep-\$(subst /,-,\$@)"
		msg_result yes
	else
		msg_result no
	fi
	return 0
}

# check if CXX can generate dependencies (.dep-*.o files)
# always succeeds
check_cxx_depgen()
{
	msg_checking "if CXX can generate dependency information"
	if cxx_supports -MMD -MP -MF /dev/null
	then
		EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS -MMD -MP -MF .dep-\$(subst /,-,\$@)"
		msg_result yes
	else
		msg_result no
	fi
	return 0
}

# adds AR to config.mk
check_ar()
{
	var_default AR ${CROSS}ar
	var_default ARFLAGS "-cr"
	if check_program $AR
	then
		makefile_vars AR ARFLAGS
		return 0
	fi
	return 1
}

# adds AS to config.mk
check_as()
{
	var_default AS ${CROSS}gcc
	if check_program $AS
	then
		makefile_vars AS
		return 0
	fi
	return 1
}

check_pkgconfig()
{
	if test -z "$PKG_CONFIG"
	then
		if check_program pkg-config PKG_CONFIG
		then
			makefile_vars PKG_CONFIG
		else
			# don't check again
			PKG_CONFIG="no"
			return 1
		fi
	fi
	return 0
}

# check for library FOO and add FOO_CFLAGS and FOO_LIBS to config.mk
#
# @name:    variable prefix (e.g. CURSES -> CURSES_CFLAGS, CURSES_LIBS)
# @cflags:  CFLAGS for the lib
# @libs:    LIBS to check
#
# adds @name_CFLAGS and @name_LIBS to config.mk
# CFLAGS are not checked, they are assumed to be correct
check_library()
{
	argc check_library $# 3 3
	msg_checking "for ${1}_LIBS ($3)"
	if try_link $3
	then
		msg_result yes
		makefile_var ${1}_CFLAGS "$2"
		makefile_var ${1}_LIBS "$3"
		return 0
	else
		msg_result no
		return 1
	fi
}

# run pkg-config
#
# @prefix:  variable prefix (e.g. GLIB -> GLIB_CFLAGS, GLIB_LIBS)
# @modules: the argument for pkg-config
# @cflags:  CFLAGS to use if pkg-config failed (optional)
# @libs:    LIBS to use if pkg-config failed (optional)
#
# if pkg-config fails and @libs are given check_library is called
#
# example:
#   ---
#   check_glib()
#   {
#           pkg_config GLIB "glib-2.0 >= 2.2"
#           return $?
#   }
#
#   check check_cc
#   check check_glib
#   ---
#   GLIB_CFLAGS and GLIB_LIBS are automatically added to Makefile
pkg_config()
{
	argc pkg_config $# 2 4

	# optional
	__pc_cflags="$3"
	__pc_libs="$4"

	check_pkgconfig
	msg_checking "for ${1}_LIBS (pkg-config)"
	if test "$PKG_CONFIG" != "no" && $PKG_CONFIG --exists "$2" >/dev/null 2>&1
	then
		# pkg-config is installed and the .pc file exists
		__pc_libs="`$PKG_CONFIG --libs ""$2""`"
		msg_result "$__pc_libs"

		msg_checking "for ${1}_CFLAGS (pkg-config)"
		__pc_cflags="`$PKG_CONFIG --cflags ""$2""`"
		msg_result "$__pc_cflags"

		makefile_var ${1}_CFLAGS "$__pc_cflags"
		makefile_var ${1}_LIBS "$__pc_libs"
		return 0
	fi

	# no pkg-config or .pc file
	msg_result "no"

	if test -z "$__pc_libs"
	then
		if test "$PKG_CONFIG" = "no"
		then
			# pkg-config not installed and no libs to check were given
			msg_error "pkg-config required for $1"
		else
			# pkg-config is installed but the required .pc file wasn't found
			$PKG_CONFIG --errors-to-stdout --print-errors "$2" | sed 's:^:*** :'
		fi
		return 1
	fi

	check_library "$1" "$__pc_cflags" "$__pc_libs"
	return $?
}

# old name
pkg_check_modules()
{
	pkg_config "$@"
}

# run *-config
#
# @prefix:  variable prefix (e.g. ARTS -> ARTS_CFLAGS, ARTS_LIBS)
# @program: the -config program
#
# example:
#   ---
#   check_arts()
#   {
#           app_config ARTS artsc-config
#           return $?
#   }
#
#   check check_cc
#   check check_arts
#   ---
#   ARTS_CFLAGS and ARTS_LIBS are automatically added to config.mk
app_config()
{
	argc app_config $# 2 2
	check_program $2 || return 1

	msg_checking "for ${1}_CFLAGS"
	__ac_cflags="`$2 --cflags`"
	msg_result "$__ac_cflags"

	msg_checking "for ${1}_LIBS"
	__ac_libs="`$2 --libs`"
	msg_result "$__ac_libs"

	makefile_var ${1}_CFLAGS "$__ac_cflags"
	makefile_var ${1}_LIBS "$__ac_libs"
	return 0
}

# @contents:  file contents to compile
# @cflags:    extra cflags (optional)
try_compile()
{
	argc try_compile $# 1
	case $__check_lang in
	c)
		__src=`tmp_file prog.c`
		__obj=`tmp_file prog.o`
		echo "$1" > $__src || exit 1
		shift
		__cmd="$CC -c $CFLAGS $@ $__src -o $__obj"
		$CC -c $CFLAGS "$@" $__src -o $__obj 2>/dev/null
		;;
	cxx)
		__src=`tmp_file prog.cc`
		__obj=`tmp_file prog.o`
		echo "$1" > $__src || exit 1
		shift
		__cmd="$CXX -c $CXXFLAGS $@ $__src -o $__obj"
		$CXX -c $CXXFLAGS "$@" $__src -o $__obj 2>/dev/null
		;;
	esac
	return $?
}

# @contents:  file contents to compile and link
# @flags:     extra flags (optional)
try_compile_link()
{
	argc try_compile $# 1
	case $__check_lang in
	c)
		__src=`tmp_file prog.c`
		__exe=`tmp_file prog`
		echo "$1" > $__src || exit 1
		shift
		__cmd="$CC $__src -o $__exe $CFLAGS $LDFLAGS $@"
		$CC $__src -o $__exe $CFLAGS $LDFLAGS "$@" 2>/dev/null
		;;
	cxx)
		__src=`tmp_file prog.cc`
		__exe=`tmp_file prog`
		echo "$1" > $__src || exit 1
		shift
		__cmd="$CXX $__src -o $__exe $CXXFLAGS $CXXLDFLAGS $@"
		$CXX $__src -o $__exe $CXXFLAGS $CXXLDFLAGS "$@" 2>/dev/null
		;;
	esac
	return $?
}

# optionally used after try_compile or try_compile_link
__compile_failed()
{
	warn
	warn "Failed to compile simple program:"
	warn "---"
	cat $__src >&2
	warn "---"
	warn "Command: $__cmd"
	case $__check_lang in
	c)
		warn "Make sure your CC and CFLAGS are sane."
		;;
	cxx)
		warn "Make sure your CXX and CXXFLAGS are sane."
		;;
	esac
	exit 1
}

# tries to link against a lib
#
# @function:  some function
# @flags:     extra flags (optional)
check_function()
{
	argc check_function $# 1
	__func="$1"
	shift
	msg_checking "for function $__func"
	if try_compile_link "char $__func(); int main(int argc, char *argv[]) { return $__func; }" "$@"
	then
		msg_result yes
		return 0
	fi

	msg_result no
	return 1
}

# tries to link against a lib
#
# @ldadd:  something like -L/usr/X11R6/lib -lX11
try_link()
{
	try_compile_link "int main(int argc, char *argv[]) { return 0; }" "$@"
	return $?
}

# compile and run
#
# @code:  simple program code to run
run_code()
{
	if test $CROSS
	then
		msg_error "cannot run code when cross compiling"
		exit 1
	fi
	try_compile_link "$1" || __compile_failed
	./$__exe
	return $?
}

# check if the architecture is big-endian
# parts are from autoconf 2.67
#
# defines WORDS_BIGENDIAN=y/n
check_endianness()
{
	msg_checking "byte order"
	WORDS_BIGENDIAN=n
	# See if sys/param.h defines the BYTE_ORDER macro.
	if try_compile_link "
#include <sys/types.h>
#include <sys/param.h>
int main() {
#if ! (defined BYTE_ORDER && defined BIG_ENDIAN \
		&& defined LITTLE_ENDIAN && BYTE_ORDER && BIG_ENDIAN \
		&& LITTLE_ENDIAN)
	bogus endian macros
#endif
	return 0;
}"
	then
		# It does; now see whether it defined to BIG_ENDIAN or not.
		if try_compile_link "
#include <sys/types.h>
#include <sys/param.h>
int main() {
#if BYTE_ORDER != BIG_ENDIAN
	not big endian
#endif
	return 0;
}"
		then
			WORDS_BIGENDIAN=y
		fi
	# See if <limits.h> defines _LITTLE_ENDIAN or _BIG_ENDIAN (e.g., Solaris).
	elif try_compile_link "
#include <limits.h>
int main() {
#if ! (defined _LITTLE_ENDIAN || defined _BIG_ENDIAN)
	bogus endian macros
#endif
	return 0;
}"
	then
		# It does; now see whether it defined to _BIG_ENDIAN or not.
		if try_compile_link "
#include <limits.h>
int main() {
#ifndef _BIG_ENDIAN
	not big endian
#endif
	return 0;
}"
		then
			WORDS_BIGENDIAN=y
		fi
	elif run_code "
int main(int argc, char *argv[])
{
	unsigned int i = 1;
	return *(char *)&i;
}"
	then
		WORDS_BIGENDIAN=y
	fi
	if test "$WORDS_BIGENDIAN" = y
	then
		msg_result "big-endian"
	else
		msg_result "little-endian"
	fi
	return 0
}

# check if @header can be included
#
# @header
# @cflags   -I/some/path (optional)
check_header()
{
	argc check_header $# 1
	__header="$1"
	shift
	msg_checking "for header <$__header>"
	if try_compile "#include <$__header>" "$@"
	then
		msg_result yes
		return 0
	fi
	msg_result no
	return 1
}

# check X11 libs
#
# adds X11_LIBS (and empty X11_CFLAGS) to config.mk
check_x11()
{
	for __libs in "-lX11" "-L/usr/X11R6/lib -lX11"
	do
		check_library X11 "" "$__libs" && return 0
	done
	return 1
}

# check posix threads
#
# adds PTHREAD_CFLAGS and PTHREAD_LIBS to config.mk
check_pthread()
{
	for __libs in "$PTHREAD_LIBS" -lpthread -lc_r -lkse
	do
		test -z "$__libs" && continue
		check_library PTHREAD "-D_REENTRANT" "$__libs" && return 0
	done
	echo "using -pthread gcc option"
	makefile_var PTHREAD_CFLAGS "-pthread -D_THREAD_SAFE"
	makefile_var PTHREAD_LIBS "-pthread"
	return 0
}

# check dynamic linking loader
#
# adds DL_LIBS to config.mk
check_dl()
{
	for DL_LIBS in "-ldl -Wl,--export-dynamic" "-ldl -rdynamic" "-Wl,--export-dynamic" "-rdynamic" "-ldl"
	do
		check_library DL "" "$DL_LIBS" && return 0
	done
	echo "assuming -ldl is not needed"
	DL_LIBS=
	makefile_vars DL_LIBS DL_CFLAGS
	return 0
}

# check for iconv
#
# adds ICONV_CFLAGS and ICONV_LIBS to config.mk
check_iconv()
{
	HAVE_ICONV=n
	if check_library ICONV "" "-liconv"
	then
		echo "taking iconv from libiconv"
	else
		echo "assuming libc contains iconv"
		makefile_var ICONV_CFLAGS ""
		makefile_var ICONV_LIBS ""
	fi
	msg_checking "for working iconv"
	if try_compile_link '
#include <stdio.h>
#include <string.h>
#include <iconv.h>
int main(int argc, char *argv[]) {
	char buf[128], *out = buf, *in = argv[1];
	size_t outleft = 127, inleft = strlen(in);
	iconv_t cd = iconv_open("UTF-8", "ISO-8859-1");
	iconv(cd, &in, &inleft, &out, &outleft);
	*out = 0;
	printf("%s", buf);
	iconv_close(cd);
	return 0;
}' $ICONV_CFLAGS $ICONV_LIBS
	then
		msg_result "yes"
		HAVE_ICONV=y
	else
		msg_result "no"
		msg_error "Your system doesn't have iconv!"
		msg_error "This means that no charset conversion can be done, so all"
		msg_error "your tracks need to be encoded in your system charset!"
	fi

	return 0
}
