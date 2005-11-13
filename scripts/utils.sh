#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

PROGRAM_NAME=${0##*/}

# FIXME: not sure if every /bin/sh supports this
is_function()
{
	local line

	line=$(type "$1" 2>/dev/null | head -n 1)
	test "${line##* }" = "function"
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

# set variable @name to @default IF NOT ALREADY SET
#
# NOTE: to set variable if it was not already set or
#       was empty use `var_default' instead
set_unset_var()
{
	local tmp

	eval "tmp=\${$1-_VAR_NOT_SET_}"
	if test "$tmp" = _VAR_NOT_SET_
	then
		set_var $1 "$2"
	fi
}

# set variable @name to @default IF NOT SET OR EMPTY
#
# @name:  name of the variable
# @value: value of the variable
var_default()
{
	test -z "$(get_var $1)" && set_var $1 "$2"
}

# @list:  list of strings
# @value: value to search
list_contains()
{
	local i

	for i in $1
	do
		test "$i" = "$2" && return 0
	done
	return 1
}

# usage: echo $foo | to_upper
to_upper()
{
	# stupid solaris tr doesn't understand ranges
	tr abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ
}

# sets the output file for `out' function
#
# @out_file: filename, "stdout" or "stderr"
output_file()
{
	OUT_FILE="$1"
	test "$OUT_FILE" = "stdout" && return
	test "$OUT_FILE" = "stderr" && return
	if test -f "$OUT_FILE"
	then
		rm "$OUT_FILE" || exit 1
	fi
}

# like `echo' but output file is set by `output_file' function
out()
{
	test -z "$OUT_FILE" && die "OUT_FILE not set. run output_file first"
	if test "$OUT_FILE" = "stdout"
	then
		echo "$@"
	elif test "$OUT_FILE" = "stderr"
	then
		echo "$@" >&2
	else
		echo "$@" >>${OUT_FILE} || exit 1
	fi
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

pwarn()
{
	echo "${PROGRAM_NAME}: Warning: $@" >&2
}

pdie()
{
	echo "${PROGRAM_NAME}: Error: $@" >&2
	exit 1
}

run_verbose()
{
	echo "$@"
	"$@"
}

var_assert()
{
	local name=$1

	test -z "$(get_var $name)" && pdie "variable \`$name' must be set"
}

# portable which command
path_find()
{
	local prog

	prog=$(type "$1" 2>/dev/null | sed 's:[^/]*::')
	if test -x "$prog"
	then
		echo "$prog"
		return 0
	fi
	return 1
}

# cd to @path following symbolic links and print $PWD
#
# @path:  absolute path
follow_links()
{
	cd -P "$1"
	pwd
}
