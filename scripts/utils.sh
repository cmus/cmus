#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

# initialization {{{

LC_ALL=C
LANG=C

export LC_ALL LANG

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
# CROSS=

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
		__tmp_file_counter=`expr $__tmp_file_counter + 1`
		test -f "$__tmp_filename" || break
	done
	echo "$__tmp_filename"
}

# get variable value
#
# @name:  name of the variable
get_var()
{
	eval echo '"$'${1}'"'
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
	test "`get_var $1`" || set_var $1 "$2"
}

# usage: echo $foo | to_upper
to_upper()
{
	# stupid solaris tr doesn't understand ranges
	tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ
}

# portable which command
path_find()
{
	case $1 in
	*/*)
		if test -x "$1"
		then
			echo "$1"
			return 0
		fi
		return 1
		;;
	esac

	_ifs="$IFS"
	IFS=:
	for __pf_i in $PATH
	do
		if test -x "$__pf_i/$1"
		then
			IFS="$_ifs"
			echo "$__pf_i/$1"
			return 0
		fi
	done
	IFS="$_ifs"
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
	if test -f "$2"
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
