#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

. scripts/utils.sh || exit 1
. scripts/checks.sh || exit 1

# Usage: parse_command_line "$@"
# USAGE string must be defined in configure (used for --help)
parse_command_line()
{
	while test $# -gt 0
	do
		case $1 in
		--help)
			show_usage
			;;
		-f)
			shift
			test $# -eq 0 && die "-f requires an argument"
			. "$1"
			;;
		-*)
			die "unrecognized option \`$1'"
			;;
		*=*)
			_var=`echo "$1" | sed "s/=.*//"`
			_val=`echo "$1" | sed "s/${_var}=//"`
			set_var "$_var" "$_val"
			;;
		*)
			die "unrecognized argument \`$1'"
			;;
		esac
		shift
	done
}

# check function [variable]
#
# Example:
# check check_cc
# check check_vorbis CONFIG_VORBIS
check()
{
	argc check $# 1 2
	if test $# -eq 1
	then
		$1 || die "configure failed."
		return
	fi

	# optional feature
	case `get_var $2` in
	n)
		;;
	y)
		$1 || die "configure failed."
		;;
	a|'')
		if $1
		then
			set_var $2 y
		else
			set_var $2 n
		fi
		;;
	*)
		die "invalid value for $2. 'y', 'n', 'a' or '' expected"
		;;
	esac
}

# Set and register variable to be added to config.mk
#
# @name   name of the variable
# @value  value of the variable
makefile_var()
{
	argc makefile_var $# 2 2
	set_var $1 "$2"
	makefile_vars $1
}

# Register variables to be added to config.mk
makefile_vars()
{
	makefile_variables="$makefile_variables $*"
}

# generate config.mk
generate_config_mk()
{
	CFLAGS="$CFLAGS $EXTRA_CFLAGS"
	CXXFLAGS="$CXXFLAGS $EXTRA_CXXFLAGS"
	if test -z "$GINSTALL"
	then
		GINSTALL=`path_find ginstall`
		test "$GINSTALL" || GINSTALL=install
	fi
	# $PWD is useless!
	topdir=`pwd`
	makefile_vars GINSTALL topdir

	__tmp=`tmp_file config.mk`
	for __i in $makefile_variables
	do
		echo "$__i = `get_var $__i`"
	done > $__tmp
	update_file $__tmp config.mk
}

# -----------------------------------------------------------------------------
# Config header generation

# Simple interface
#
# Guesses variable types:
#   y or n        -> bool
#   [0-9]+        -> int
#   anything else -> str
#
# Example:
#   CONFIG_FOO=y  # bool
#   VERSION=2.0.1 # string
#   DEBUG=1       # int
#   config_header config.h CONFIG_FOO VERSION DEBUG
config_header()
{
	argc config_header $# 2
	config_header_begin "$1"
	shift
	while test $# -gt 0
	do
		__var=`get_var $1`
		case "$__var" in
		[yn])
			config_bool $1
			;;
		*)
			if test "$__var" && test "$__var" = "`echo $__var | sed 's/[^0-9]//g'`"
			then
				config_int $1
			else
				config_str $1
			fi
			;;
		esac
		shift
	done
	config_header_end
}

# Low-level interface
#
# Example:
#   config_header_begin config.h
#   config_str PACKAGE VERSION
#   config_bool CONFIG_ALSA
#   config_header_end

config_header_begin()
{
	argc config_header_begin $# 1 1
	config_header_file="$1"
	config_header_tmp=`tmp_file config_header`

	__def=`echo $config_header_file | to_upper | sed 's/[-\.\/]/_/g'`
	cat <<EOF > "$config_header_tmp"
#ifndef $__def
#define $__def

EOF
}

config_str()
{
	while test $# -gt 0
	do
		echo "#define $1 \"`get_var $1`\"" >> "$config_header_tmp"
		shift
	done
}

config_int()
{
	while test $# -gt 0
	do
		echo "#define $1 `get_var $1`" >> "$config_header_tmp"
		shift
	done
}

config_bool()
{
	while test $# -gt 0
	do
		case "`get_var $1`" in
			n)
				echo "/* #define $1 */" >> "$config_header_tmp"
				;;
			y)
				echo "#define $1 1" >> "$config_header_tmp"
				;;
			*)
				die "bool '$1' has invalid value '`get_var $1`'"
				;;
		esac
		shift
	done
}

config_header_end()
{
	argc config_header_end $# 0 0
	echo "" >> "$config_header_tmp"
	echo "#endif" >> "$config_header_tmp"
	mkdir -p `dirname "$config_header_file"`
	update_file "$config_header_tmp" "$config_header_file"
}
