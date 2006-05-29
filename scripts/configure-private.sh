#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

. scripts/utils.sh || exit 1
. scripts/checks.sh || exit 1

# initialization {{{

export LC_ALL=C
export LANG=C

if test "$CDPATH"
then
	echo "Exporting CDPATH is dangerous and unnecessary!"
	echo
fi
unset CDPATH

__cleanup()
{
	if test "$DEBUG_CONFIGURE" = y
	then
		echo
		echo "DEBUG_CONFIGURE=y, not removing temporary files"
		ls .tmp-[0-9]*-*
	else
		rm -f .tmp-[0-9]*-*
	fi
}

__abort()
{
	# can't use "die" because stderr is often redirected to /dev/null
	# (stdout could also be redirected but it's not so common)
	echo
	echo
	echo "Aborting. configure failed."
	# this executes __cleanup automatically
	exit 1
}

# clean temporary files on exit
trap '__cleanup' 0

# clean temporary files and die with error message if interrupted
trap '__abort' 1 2 3 13 15

# }}}

# locals {{{

# --enable-$NAME flags
# $NAME must contain only [a-z0-9-] characters
# For each --enable-$NAME there are
#   enable_desc_$NAME
#   enable_var_$NAME
# variables and check_$NAME function
enable_flags=""

# option flags
opt_flags=""

# config.mk variable names
makefile_variables=""

# checks added by add_check, DEPRECATED
checks=""

# cross compilation, prefix for CC, LD etc.
CROSS=

did_run()
{
	set_var did_run_$1 yes
}

before()
{
	if test "$(get_var did_run_$1)" = yes
	then
		echo
		echo "Bug in the configure script!"
		echo "Function $2 was called after $1."
		exit 1
	fi
}

after()
{
	if test "$(get_var did_run_$1)" != yes
	then
		echo
		echo "Bug in the configure script!"
		echo "Function $2 was called before $1."
		exit 1
	fi
}

show_help()
{
	cat <<EOT
Usage: ./configure [options] [VARIABLE=VALUE]...

  --cross=MACHINE         cross-compile []
EOT
	for __i in $opt_flags
	do
		__tmp=$(get_var flag_argdesc_${__i})
		strpad "--$(echo $__i | sed 's/_/-/g')${__tmp}" 22
		__tmp=$(get_var flag_desc_${__i})
		echo "  $strpad_ret  ${__tmp}"
	done
	cat <<EOT

Optional Features:
  --disable-FEATURE       do not include FEATURE
  --enable-FEATURE        include FEATURE
EOT
	for __i in $enable_flags
	do
		__tmp=$(get_var enable_var_${__i})
		strpad "--enable-$(echo $__i | sed 's/_/-/g')" 22
		echo "  $strpad_ret  $(get_var enable_desc_${__i}) [$(get_var $__tmp)]"
	done
	exit 0
}

handle_enable()
{
	for __ef in $enable_flags
	do
		test "$__ef" = "$1" || continue

		set_var $(get_var enable_var_${1}) $2
		return 0
	done
	die "invalid option --enable-$1"
}

# @arg: key=val
handle_opt()
{
	__k=${1%%=*}
	__n="$(echo $__k | sed 's/-/_/g')"
	for __i in $opt_flags
	do
		test "$__i" != "$__n" && continue

		if test "$__k" = "$1"
		then
			# '--foo'
			test "$(get_var flag_hasarg_${__n})" = y && \
			die "--${__k} requires an argument (--${__k}$(get_var flag_argdesc_${__n}))"
			$(get_var flag_func_${__n}) ${__k}
		else
			# '--foo=bar'
			test "$(get_var flag_hasarg_${__n})" = n && \
			die "--${__k} must not have an argument"
			$(get_var flag_func_${__n}) ${__k} "${1##*=}"
		fi
		return 0
	done
	die "unrecognized option \`$1'"
}

# }}}

parse_command_line()
{
	add_flag help n show_help "show this help and exit"

	# parse flags (--*)
	while test $# -gt 0
	do
		case $1 in
			--enable-*)
				handle_enable "$(echo ${1##--enable-} | sed 's/-/_/g')" y
				;;
			--disable-*)
				handle_enable "$(echo ${1##--disable-} | sed 's/-/_/g')" n
				;;
			--cross=*)
				CROSS=${1##--cross=}
				;;
			--)
				shift
				break
				;;
			--*)
				handle_opt "${1##--}"
				;;
			*)
				break
				;;
		esac
		shift
	done

	while test $# -gt 0
	do
		case $1 in
			*=*)
				set_var ${1%%=*} "${1#*=}"
				;;
			*)
				die "unrecognized argument \`$1'"
				;;
		esac
		shift
	done

	did_run parse_command_line
}

# args: function(s) to run
#
# NOTE:
#   The functions take no arguments and _must_ return 0 on success and
#   non-zero on failure. See checks.sh for more information.
run_checks()
{
	after parse_command_line run_checks

	for __i in $checks $*
	do
		$__i && continue
		echo
		die "configure failed."
	done
	for __i in $enable_flags
	do
		__var=$(get_var enable_var_${__i})
		__val=$(get_var $__var)
		if test "$__val" != n
		then
			if ! is_function check_${__i}
			then
				continue
			fi

			if check_${__i}
			then
				# check successful
				set_var $__var y
			else
				# check failed
				if test "$__val" = y
				then
					die "configure failed."
				else
					# auto
					set_var $__var n
				fi
			fi
		fi
	done
	did_run run_checks
}

# @tmpfile: temporary file
# @file:    file to update
#
# replace @file with @tmpfile if their contents differ
update_file()
{
	if test -e "$2"
	then
		if cmp "$2" "$1" 2>/dev/null 1>&2
		then
			return 0
		fi
		echo "updating $2"
	else
		echo "creating $2"
	fi
	mv -f "$1" "$2"
}

generate_config_mk()
{
	after run_checks generate_config_mk

	topdir=$(pwd)
	makefile_vars topdir

	__tmp=$(tmp_file config.mk)
	for __i in $makefile_variables
	do
		strpad "$__i" 17
		echo "${strpad_ret} = $(get_var $__i)"
	done > $__tmp
	update_file $__tmp config.mk
	did_run generate_config_mk
}
