#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

msg_checking()
{
	echo -n "checking $@... "
}

msg_result()
{
	echo "$@"
}

msg_error()
{
	echo "$@"
}

# @program: program to check
# @name:    name of variable where to store the full program name (optional)
#
# returns 0 on success and 1 on failure
check_program()
{
	local program varname filename

	if test $# -lt 1 || test $# -gt 2
	then
		die "check_program: expecting 1-2 arguments"
	fi
	program="$1"
	varname="$2"

	msg_checking "for program ${program}"
	filename=$(path_find "${program}")
	if test $? -eq 0
	then
		msg_result $filename
		test $# -eq 2 && set_var $varname $filename
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# @flag: option flag(s) to check
#
# check if $CC supports @flag
check_cc_flag()
{
	test $# -lt 1 && die "check_cc_flag: expecting at least 1 argument"

	test -z "$CC" && die "$FUNCNAME: CC not set"
	msg_checking "for CC flag $@"
	if $CC "$@" -S -o /dev/null -x c /dev/null &> /dev/null
	then
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# @flag: option flag(s) to check
#
# check if $CXX supports @flag
check_cxx_flag()
{
	test $# -lt 1 && die "check_cxx_flag: expecting at least 1 argument"

	test -z "$CXX" && die "$FUNCNAME: CXX not set"
	msg_checking "for CXX flag $@"
	if $CXX "$@" -S -o /dev/null -x c /dev/null &> /dev/null
	then
		msg_result "yes"
		return 0
	else
		msg_result "no"
		return 1
	fi
}

# c compiler
check_cc()
{
	var_default CC ${CROSS}gcc
	var_default LD $CC
	var_default CFLAGS "-O2 -Wall -g"
	var_default LDFLAGS ""
	var_default SOFLAGS "-fPIC"
	if check_program $CC
	then
		makefile_env_vars CC LD CFLAGS LDFLAGS SOFLAGS
		if check_cc_flag -MT /dev/null -MD -MP -MF /dev/null
		then
			makefile_var CC_GENERATE_DEPS y
		else
			makefile_var CC_GENERATE_DEPS n
		fi
		return 0
	fi
	return 1
}

# c++ compiler
check_cxx()
{
	var_default CXX ${CROSS}g++
	var_default CXXLD $CXX
	var_default CXXFLAGS "-O2 -Wall -g"
	var_default CXXLDFLAGS ""
	if check_program $CXX
	then
		makefile_env_vars CXX CXXLD CXXFLAGS CXXLDFLAGS
		if check_cxx_flag -MT /dev/null -MD -MP -MF /dev/null
		then
			makefile_var CXX_GENERATE_DEPS y
		else
			makefile_var CXX_GENERATE_DEPS n
		fi
		return 0
	fi
	return 1
}

check_ar()
{
	var_default AR ${CROSS}ar
	var_default ARFLAGS "-cr"
	if check_program $AR
	then
		makefile_env_vars AR ARFLAGS
		return 0
	fi
	return 1
}

check_as()
{
	var_default AS ${CROSS}gcc
	if check_program $AS
	then
		makefile_env_vars AS
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
			makefile_env_vars PKG_CONFIG
		else
			# don't check again
			PKG_CONFIG="no"
			return 1
		fi
	fi
	return 0
}

# run pkg-config
#
# @name:    name
# @modules: 
#
# example:
#   ---
#   check_glib()
#   {
#     pkg_check_modules glib "glib-2.0 >= 2.2"
#     return $?
#   }
#
#   add_check check_glib
#   ---
#   GLIB_CFLAGS and GLIB_LIBS are automatically added to Makefile
pkg_check_modules()
{
	local name modules

	test $# -eq 2 || die "pkg_check_modules: expecting 2 arguments"
	name="$1"
	modules="$2"
	
	check_pkgconfig
	if test "$PKG_CONFIG" = "no"
	then
		msg_error "*** The pkg-config script could not be found. Make sure it is"
		msg_error "*** in your path, or set the PKG_CONFIG environment variable"
		msg_error "*** to the full path to pkg-config."
		msg_error "*** Or see http://www.freedesktop.org/software/pkgconfig to get pkg-config."
		return 1
	fi

	msg_checking "$modules"
	if $PKG_CONFIG --exists "$modules"
	then
		local uc

		msg_result "yes"
		uc=$(echo $name | to_upper)

		msg_checking "CFLAGS for $name"
		set_var ${uc}_CFLAGS "$($PKG_CONFIG --cflags ""$modules"")"
		msg_result $(get_var ${uc}_CFLAGS)

		msg_checking "LIBS for $name"
		set_var ${uc}_LIBS "$($PKG_CONFIG --libs ""$modules"")"
		msg_result $(get_var ${uc}_LIBS)

		module_names="$module_names $uc"
		return 0
	else
		msg_result "no"

		$PKG_CONFIG --errors-to-stdout --print-errors "$modules"
		msg_error "Library requirements (${modules}) not met; consider adjusting the PKG_CONFIG_PATH environment variable if your libraries are in a nonstandard prefix so pkg-config can find them."
		return 1
	fi
}

# run <name>-config
#
# @name:    name
# @program: the -config program, default is ${name}-config
#
# example:
#   ---
#   check_cppunit()
#   {
#     app_config cppunit
#     return $?
#   }
#
#   add_check check_cppunit
#   ---
#   CPPUNIT_CFLAGS and CPPUNIT_LIBS are automatically added to Makefile
app_config()
{
	local name program uc

	name="$1"
	if test $# -eq 1
	then
		program="${name}-config"
	elif test $# -eq 2
	then
		program="$2"
	else
		die "app_config: expecting 1-2 arguments"
	fi

	msg_checking "$name"
	program=$(path_find "$program")
	if test $? -ne 0
	then
		msg_error "no"
		return 1
	fi

	msg_result "yes"
	uc=$(echo $name | to_upper)

	msg_checking "CFLAGS for $name"
	set_var ${uc}_CFLAGS "$($program --cflags)"
	msg_result $(get_var ${uc}_CFLAGS)

	msg_checking "LIBS for $name"
	set_var ${uc}_LIBS "$($program --libs)"
	msg_result $(get_var ${uc}_LIBS)

	module_names="$module_names $uc"
	return 0
}

try_compile()
{
	local file src obj exe

	test $# -eq 1 || die "try_compile: expecting 1 argument"
	file="$1"
	src=$(tmp_file prog.c)
	obj=$(tmp_file prog.o)
	exe=$(tmp_file prog)
	echo "$file" > $src || exit 1
	$CC -c $src -o $obj 2>/dev/null || exit 1
	$LD -o $exe $obj 2>/dev/null
	return $?
}

# tries to link against a lib
# 
# @ldadd:  something like "-L/usr/X11R6/lib -lX11"
try_link()
{
	local ldadd
	local file src obj exe

	test $# -eq 1 || die "try_link: expecting 1 argument"
	ldadd="$1"
	file="
int main(int argc, char *argv[])
{
	return 0;
}
"
	src=$(tmp_file prog.c)
	obj=$(tmp_file prog.o)
	exe=$(tmp_file prog)

	echo "$file" > $src || exit 1
	$CC -c $src -o $obj || return 1
	$LD $LDFLAGS $ldadd -o $exe $obj
	return $?
}

check_endianness()
{
	local file src obj exe

	file="
int main(int argc, char *argv[])
{
	unsigned int i = 1;

	return *(char *)&i;
}
"
	msg_checking "byte order"
	src=$(tmp_file prog.c)
	obj=$(tmp_file prog.o)
	exe=$(tmp_file prog)
	echo "$file" > $src || exit 1
	$CC -c $src -o $obj 2>/dev/null || exit 1
	$LD -o $exe $obj 2>/dev/null || return 1
	if ./$exe
	then
		msg_result "big-endian"
		WORDS_BIGENDIAN=1
	else
		msg_result "little-endian"
		WORDS_BIGENDIAN=0
	fi
	return 0
}

# @name:   user visible name
# @ldadd:  arg passed to try_link
check_lib()
{
	local name ldadd
	local output

	test $# -eq 2 || die "check_lib: expecting 2 arguments"
	name="$1"
	ldadd="$2"
	msg_checking "$name"
	output=$(try_link "$ldadd" 2>&1)
	if test $? -eq 0
	then
		msg_result "yes"
		return 0
	else
		msg_result "no"
		#msg_error "$output"
		return 1
	fi
}

# check X11 libs
#
# defines X11_LIBS in config.mk
check_x11()
{
	local libs

	for libs in "-lX11" "-L/usr/X11R6/lib -lX11"
	do
		if check_lib "X11 ($libs)" "$libs"
		then
			makefile_var X11_LIBS "$libs"
			return 0
		fi
	done
	return $?
}

# check posix threads
#
# defines PTHREAD_CFLAGS in config.mk
# defines PTHREAD_LIBS in config.mk
check_pthread()
{
	local libs

	for libs in "$PTHREAD_LIBS" -lpthread -lc_r -lkse
	do
		test -z "$libs" && continue
		if check_lib "POSIX Threads ($libs)" "$libs"
		then
			makefile_var PTHREAD_CFLAGS "-D_REENTRANT"
			makefile_var PTHREAD_LIBS "$libs"
			return 0
		fi
	done
	echo "using -pthread gcc option"
	makefile_var PTHREAD_CFLAGS "-pthread -D_THREAD_SAFE"
	makefile_var PTHREAD_LIBS "-pthread"
	return 0
}

# check dynamic linking loader
#
# defines DL_LIBS in config.mk
check_dl()
{
	local libs="-ldl -rdynamic"

	msg_checking "dynamic linking loader"
	if ! try_link "$libs" 2>/dev/null
	then
		libs="-rdynamic"
	fi
	msg_result "$libs"
	makefile_var DL_LIBS "$libs"
	return 0
}

check_iconv()
{
	local libs=-liconv

	if check_lib "iconv ($libs)" "$libs"
	then
		makefile_var ICONV_CFLAGS ""
		makefile_var ICONV_LIBS "$libs"
		return 0
	fi
	echo "assuming libc contains iconv"
	makefile_var ICONV_CFLAGS ""
	makefile_var ICONV_LIBS ""
	return 0
}
