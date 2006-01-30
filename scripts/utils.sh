#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

# FIXME: not sure if every /bin/sh supports this
is_function()
{
	local line

	line=$(type "$1" 2>/dev/null | head -n 1)
	test "${line##* }" = "function"
}

# argc function_name $# min [max]
argc()
{
	local max

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
	local filename

	if test -z "$__tmp_file_counter"
	then
		__tmp_file_counter=0
	fi

	while true
	do
		filename=.tmp-${__tmp_file_counter}-${1}
		__tmp_file_counter=$(($__tmp_file_counter + 1))
		test -e "$filename" || break
	done
	echo "$filename"
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
	local len

	strpad_ret="$1"
	len="$2"
	len=$(($len - ${#strpad_ret}))
	while test $len -gt 0
	do
		strpad_ret="${strpad_ret} "
		len=$(($len - 1))
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
	for i in $(echo $PATH | sed 's/:/ /g')
	do
		if test -x "$i/$1"
		then
			echo "$i/$1"
			return 0
		fi
	done
	return 1
}
