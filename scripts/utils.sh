#!/bin/bash
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

PROGRAM_NAME=${0##*/}

#
# Don't use __FOO functions directly. use FOO alias instead!
#

shopt -s expand_aliases

# usage: 'argc min' or 'argc min max'
# check argument count
# e.g. 'argc 3 4' defines that minimum argument count is 3 and maximum 4
alias argc='__argc $FUNCNAME $#'

# usage: 'did_run'
# mark function to have been run
alias did_run='__did_run $FUNCNAME'

# usage: 'only_once'
# allow function to be run only once
alias only_once='__only_once $FUNCNAME'

# usage: 'before function_name'
# this function must be run before function 'function_name'
alias before='__before $FUNCNAME'

# usage: 'after function_name'
# this function must be run after function 'function_name'
alias after='__after $FUNCNAME'

# usage: 'deprecated'
# mark function deprecated
alias deprecated='__deprecated $FUNCNAME'

is_function()
{
	argc 1
	[[ $(type -t "$1") = function ]]
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

# check argument count
#
# @func:  name of the function
# @given: number of arguments given to the function
# @min:   minimum number of arguments for the function
# @max:   maximum number of arguments for the function (optional)
# 
# if @max not given then @max is set to @min
#
# NOTE: use argc alias instead!
__argc()
{
	local func given min max

	[[ $# -lt 3 ]] && die "not enough arguments for $FUNCNAME"
	[[ $# -gt 4 ]] && die "too many arguments for $FUNCNAME"
	func="$1"
	given="$2"
	min="$3"
	if [[ $# -eq 3 ]]
	then
		max="$min"
	else
		max="$4"
	fi
	[[ $given -lt $min ]] && die "not enough arguments for $func"
	[[ $given -gt $max ]] && die "too many arguments for $func"
}

__did_run()
{
	argc 1
	set_var did_run_$1 1
}

__only_once()
{
	argc 1
	[[ $(get_var did_run_$1) -eq 1 ]] && die "run \`$1' only once"
}

__before()
{
	argc 2
	[[ $(get_var did_run_$2) -ne 1 ]] || die "\`$1' must be run before \`$2'"
}

__after()
{
	argc 2
	[[ $(get_var did_run_$2) -eq 1 ]] || die "\`$2' must be run before \`$1'"
}

__deprecated()
{
	argc 1
	warn "function \`$1' is deprecated"
}


# usage: 'tmp_file .c'
# get filename for temporary file
tmp_file()
{
	local filename

	argc 1
	if [[ -z $__tmp_file_counter ]]
	then
		__tmp_file_counter=0
	fi

	while true
	do
		filename=.tmp-${__tmp_file_counter}-${1}
		__tmp_file_counter=$(($__tmp_file_counter + 1))
		[[ -e $filename ]] || break
	done
	echo "$filename"
}

# get variable value
#
# @name:  name of the variable
get_var()
{
	argc 1
	eval echo \$${1}
}

# set variable by name
#
# @name:  name of the variable
# @value: value of the variable
set_var()
{
	argc 2
	eval $1='$2'
}

# set variable @name to @default IF NOT ALREADY SET
#
# NOTE: to set variable if it was not already set or
#       was empty use `var_default' instead
set_unset_var()
{
	local tmp

	argc 2
	eval "tmp=\${$1-_VAR_NOT_SET_}"
	if [[ $tmp = _VAR_NOT_SET_ ]]
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
	argc 2
	[[ -z $(get_var $1) ]] && set_var $1 "$2"
}

# @list:  list of strings
# @value: value to search
list_contains()
{
	local i

	argc 2
	for i in $1
	do
		[[ $i = $2 ]] && return 0
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
	argc 1
	OUT_FILE="$1"
	[[ $OUT_FILE = "stdout" ]] && return
	[[ $OUT_FILE = "stderr" ]] && return
	if [[ -f $OUT_FILE ]]
	then
		rm "$OUT_FILE" || exit 1
	fi
}

# like `echo' but output file is set by `output_file' function
out()
{
	[[ -z $OUT_FILE ]] && die "OUT_FILE not set. run output_file first"
	if [[ $OUT_FILE = "stdout" ]]
	then
		echo "$@"
	elif [[ $OUT_FILE = "stderr" ]]
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

	argc 2
	strpad_ret="$1"
	len="$2"
	len=$(($len - ${#strpad_ret}))
	while [[ $len -gt 0 ]]
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

	[[ -z $(get_var $name) ]] && pdie "variable \`$name' must be set"
}

# portable which command
path_find()
{
	local prog

	argc 1
	prog=$(type -p "$1")
	if [[ $? -eq 0 ]] && [[ -x $prog ]]
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
	local abspath i

	argc 1
	abspath="$1"
	[[ ${abspath##/} = ${abspath} ]] && die "path must be absolute"

	cd /
	for i in ${abspath//\// }
	do
		if [[ -L $i ]]
		then
			i=$(readlink $i)
			[[ -z $i ]] && die "readlink failed"
		fi
		cd $i || die "couldn't cd to '$i'"
	done
	pwd
}
