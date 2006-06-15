#!/bin/sh
#
# Copyright 2005-2006 Timo Hirvonen
#
# This file is licensed under the GPLv2.

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
	__cp_file=$(path_find "$1")
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
# add @flag to CFLAGS if CC accepts it
check_cc_flag()
{
	argc check_cc_flag $# 1

	test -z "$CC" && die "check_cc_flag: CC not set"
	msg_checking "for CFLAGS $*"
	if cc_supports $*
	then
		CFLAGS="$CFLAGS $*"
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# @flag: option flag(s) to check
#
# add @flag to CXXFLAGS if CXX accepts it
check_cxx_flag()
{
	argc check_cxx_flag $# 1

	test -z "$CXX" && die "check_cxx_flag: CXX not set"
	msg_checking "for CXXFLAGS $*"
	if cxx_supports $*
	then
		CXXFLAGS="$CXXFLAGS $*"
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# extra flags for linking shared libraries and dynamically loadable modules
check_shared_flags()
{
	# most of this is from Autoconf
	case $(uname -s) in
	rhapsody* | darwin1.[012])
		LDSOFLAGS="-dynamic"
		LDDLFLAGS="-bundle -undefined suppress"
		;;
	darwin*)
		# Darwin 1.3
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
	*)
		LDSOFLAGS="-shared"
		LDDLFLAGS="-shared"
		;;
	esac
	echo "LDSOFLAGS = $LDSOFLAGS"
	echo "LDDLFLAGS = $LDDLFLAGS"
	makefile_vars LDSOFLAGS LDDLFLAGS
}

# adds CC, LD, CFLAGS, LDFLAGS and SOFLAGS to config.mk
check_cc()
{
	var_default CC ${CROSS}gcc
	var_default LD $CC
	var_default CFLAGS "-O2"
	var_default LDFLAGS ""
	var_default SOFLAGS "-fPIC"
	if check_program $CC
	then
		case $(uname -s) in
			*BSD)
				CFLAGS="$CFLAGS -I/usr/local/include"
				LDFLAGS="$LDFLAGS -L/usr/local/lib"
				;;
		esac
		makefile_vars CC LD CFLAGS LDFLAGS SOFLAGS
		check_shared_flags
		return 0
	fi
	return 1
}

# adds CXX, CXXLD, CXXFLAGS and CXXLDFLAGS to config.mk
check_cxx()
{
	var_default CXX ${CROSS}g++
	var_default CXXLD $CXX
	var_default CXXFLAGS "-O2"
	var_default CXXLDFLAGS ""
	if check_program $CXX
	then
		case $(uname -s) in
			*BSD)
				CXXFLAGS="$CXXFLAGS -I/usr/local/include"
				CXXLDFLAGS="$CXXLDFLAGS -L/usr/local/lib"
				;;
		esac
		makefile_vars CXX CXXLD CXXFLAGS CXXLDFLAGS
		return 0
	fi
	return 1
}

# check if CC can generate dependencies (.dep-*.o files)
# always succeeds
check_cc_depgen()
{
	msg_checking "if CC can generate dependency information"
	if cc_supports -MMD -MP -MF /dev/null
	then
		CFLAGS="$CFLAGS -MMD -MP -MF .dep-\$(subst /,-,\$@)"
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
		CXXFLAGS="$CXXFLAGS -MMD -MP -MF .dep-\$(subst /,-,\$@)"
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
	if try_link "$3" >/dev/null 2>&1
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
# @name:    variable prefix (e.g. GLIB -> GLIB_CFLAGS, GLIB_LIBS)
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
#     pkg_config GLIB "glib-2.0 >= 2.2"
#     return $?
#   }
#
#   run_checks check_cc check_glib
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
		__pc_libs="$($PKG_CONFIG --libs ""$2"")"
		msg_result "$__pc_libs"

		msg_checking "for ${1}_CFLAGS (pkg-config)"
		__pc_cflags="$($PKG_CONFIG --cflags ""$2"")"
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

# run <name>-config
#
# @name:    name
# @program: the -config program
#
# example:
#   ---
#   check_cppunit()
#   {
#     app_config CPPUNIT cppunit-config
#     return $?
#   }
#
#   run_checks check_cc check_glib
#   ---
#   CPPUNIT_CFLAGS and CPPUNIT_LIBS are automatically added to config.mk
app_config()
{
	argc app_config $# 2 2
	check_program $2 || return 1

	msg_checking "for ${1}_CFLAGS"
	__ac_cflags="$($2 --cflags)"
	msg_result "$__ac_cflags"

	msg_checking "for ${1}_LIBS"
	__ac_libs="$($2 --libs)"
	msg_result "$__ac_libs"

	makefile_var ${1}_CFLAGS "$__ac_cflags"
	makefile_var ${1}_LIBS "$__ac_libs"
	return 0
}

# @contents:  file contents to compile
try_compile()
{
	argc try_compile $# 1 1
	__src=$(tmp_file prog.c)
	__obj=$(tmp_file prog.o)
	echo "$1" > $__src || exit 1
	$CC -c $__src -o $__obj 2>/dev/null
	return $?
}

# tries to link against a lib
# 
# @ldadd:  something like "-L/usr/X11R6/lib -lX11"
try_link()
{
	argc try_link $# 1 1

	__src=$(tmp_file prog.c)
	__obj=$(tmp_file prog.o)
	__exe=$(tmp_file prog)

	echo "
int main(int argc, char *argv[])
{
	return 0;
}
" > $__src || exit 1

	$CC -c $__src -o $__obj || return 1
	$LD $LDFLAGS $1 -o $__exe $__obj
	return $?
}

# check if the architecture is big-endian
#
# defines WORDS_BIGENDIAN=y/n
check_endianness()
{
	msg_checking "byte order"
	__src=$(tmp_file byteorder.c)
	__obj=$(tmp_file byteorder.o)
	__exe=$(tmp_file byteorder)

	echo "
int main(int argc, char *argv[])
{
	unsigned int i = 1;

	return *(char *)&i;
}
" > $__src || exit 1

	$CC -c $__src -o $__obj 2>/dev/null || exit 1
	$LD -o $__exe $__obj 2>/dev/null || return 1
	if ./$__exe
	then
		msg_result "big-endian"
		WORDS_BIGENDIAN=y
	else
		msg_result "little-endian"
		WORDS_BIGENDIAN=n
	fi
	return 0
}

# check if linking against @ldadd is possible
# use check_library instead if possible
#
# @name:   user visible name
# @ldadd:  arg passed to try_link
check_lib()
{
	argc check_lib $# 2 2
	msg_checking "for $1"
	try_link "$2" > /dev/null 2>&1
	if test $? -eq 0
	then
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
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
	for __libs in "-ldl -Wl,--export-dynamic" "-Wl,--export-dynamic" "-ldl"
	do
		check_library DL "" "$__libs" && return 0
	done
	echo "assuming -ldl is not needed"
	makefile_var DL_LIBS ""
	makefile_var DL_CFLAGS ""
	return 0
}

# check for iconv
#
# adds ICONV_CFLAGS and ICONV_LIBS to config.mk
check_iconv()
{
	if ! check_library ICONV "" "-liconv"
	then
		echo "assuming libc contains iconv"
		makefile_var ICONV_CFLAGS ""
		makefile_var ICONV_LIBS ""
	fi
	return 0
}
