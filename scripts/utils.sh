#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

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

# config.mk variable names
makefile_variables=""

# cross compilation, prefix for CC, LD etc.
CROSS=

# argc function_name $# min [max]
argc()
{
	if test $# -lt 3 || test $# -gt 4
	then
		die "argc: expecting 3-4 arguments (got $*)"
	fi

	if test $# -eq 3
	then
		if test $2 -lt $3
		then
			die "$1: expecting at least $3 arguments"
		fi
	else
		if test $2 -lt $3 || test $2 -gt $4
		then
			die "$1: expecting $3-$4 arguments"
		fi
	fi
}

# print warning message (all parameters)
warn()
{
	echo "$@" >&2
}

# print error message (all parameters) and exit
die()
{
	warn "$@"
	exit 1
}

# usage: 'tmp_file .c'
# get filename for temporary file
tmp_file()
{
	if test -z "$__tmp_file_counter"
	then
		__tmp_file_counter=0
	fi

	while true
	do
		__tmp_filename=.tmp-${__tmp_file_counter}-${1}
		__tmp_file_counter=$(($__tmp_file_counter + 1))
		test -e "$__tmp_filename" || break
	done
	echo "$__tmp_filename"
}

# get variable value
#
# @name:  name of the variable
get_var()
{
	eval echo \"\$${1}\"
}

# set variable by name
#
# @name:  name of the variable
# @value: value of the variable
set_var()
{
	eval $1='$2'
}

# set variable @name to @default IF NOT SET OR EMPTY
#
# @name:  name of the variable
# @value: value of the variable
var_default()
{
	test -z "$(get_var $1)" && set_var $1 "$2"
}

# usage: echo $foo | to_upper
to_upper()
{
	# stupid solaris tr doesn't understand ranges
	tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ
}

# @str: string to pad with spaces
# @len: minimum length of the string
#
# returned string is $strpad_ret
strpad()
{
	strpad_ret="$1"
	__sp_len="$2"
	__sp_len=$(($__sp_len - ${#strpad_ret}))
	while test $__sp_len -gt 0
	do
		strpad_ret="${strpad_ret} "
		__sp_len=$(($__sp_len - 1))
	done
}

# portable which command
path_find()
{
	if test -x "$1"
	then
		echo "$1"
		return 0
	fi

	# _NEVER_ trust 'which' or 'type'!
	for __pf_i in $(echo $PATH | sed 's/:/ /g')
	do
		if test -x "$__pf_i/$1"
		then
			echo "$__pf_i/$1"
			return 0
		fi
	done
	return 1
}

show_usage()
{
	cat <<EOF
Usage ./configure [-f FILE] [OPTION=VALUE]...

  -f FILE         Read OPTION=VALUE list from FILE (sh script)
$USAGE
EOF
	exit 0
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

# optionally used after try_compile
__try_link()
{
	__exe=$(tmp_file prog)
	__cmd="$LD $LDFLAGS $@ -o $__exe $__obj"
	$LD $LDFLAGS "$@" -o $__exe $__obj 2>/dev/null
	return $?
}

# optionally used after try_compile
__compile_failed()
{
	warn
	warn "Failed to compile simple program:"
	warn "---"
	cat $__src >&2
	warn "---"
	warn "Command: $__cmd"
	warn "Make sure your CC and CFLAGS are sane."
	exit 1
}

# optionally used after __try_link or try_link
__link_failed()
{
	warn
	warn "Failed to link simple program."
	warn "Command: $__cmd"
	warn "Make sure your LD (CC?) and LDFLAGS are sane."
	exit 1
}

__choose_shell()
{
	case $SHELL in
	*bash|*ksh|*posh|*dash|*ash)
		# should work
		;;
	*)
		case $(uname -s) in
		SunOS)
			# never trust SunOS
			for SHELL in bash ksh dash ash posh
			do
				SHELL=$(path_find $SHELL) && return
			done
			warn "Could not find working SHELL"
			die "Try ./configure SHELL=/path/to/shell"
			;;
		*)
			# /bin/sh works on sane systems
			SHELL=/bin/sh
			;;
		esac
		;;
	esac
}
