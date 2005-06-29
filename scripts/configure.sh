#!/bin/bash
#
# Copyright 2005 Timo Hirvonen <tihirvon@ee.oulu.fi>
#
# This file is licensed under the GPLv2.

# misc {{{

check_bash_version()
{
	local tmp

	[[ ${BASH_VERSINFO[0]} -gt 2 ]] && return 0
	tmp=${BASH_VERSINFO[1]}
	tmp=${tmp:0:2}
	[[ ${BASH_VERSINFO[0]} -eq 2 ]] && [[ "${tmp}" = "05" ]] && return 0
	echo "bash 2.05 or newer required." >&2
	exit 1
}

check_bash_version

srcdir="$(dirname $0)"
srcdir=$(cd $srcdir && pwd)
[[ -z $srcdir ]] && exit 1

[[ -z $scriptdir ]] && scriptdir="${srcdir}/scripts"

if [[ ! -d $srcdir ]]
then
	echo "srcdir ($srcdir) is not directory"
	exit 1
fi

if [[ ! -d $scriptdir ]]
then
	echo "scriptdir ($scriptdir) is not directory"
	exit 1
fi

source ${scriptdir}/utils.sh || exit 1
source ${scriptdir}/checks.sh || exit 1
source ${scriptdir}/configure-private.sh || exit 1

check_source_dir()
{
	local src bld

	src=$(follow_links $srcdir)
	bld=$(follow_links $(pwd))
	if [[ $src != $bld ]] && [[ -e $src/Makefile ]]
	then
		echo "Source directory already configured. Please run \`make distclean' in there first."
		exit 1
	fi
	return 0
}

check_source_dir

# }}}

# Add check function that will be run by run_checks
#
# @check: function to run
#
# NOTE:
#   The @check function takes no arguments and _must_ return 0 on success and
#   non-zero on failure. See checks.sh for more information.
add_check()
{
	argc 1
	before run_checks

	checks="${checks} $1"
}

# Add --enable-FEATURE=ARG flag
#
# @name:          name of the flag (eg. alsa => --enable-alsa)
# @default_value: 'yes', 'no' or 'auto'
#                 'auto' can be used only if check_@name function exists
# @config_var:    name of the variable written to Makefile and config.h
# @description:   help text
#
# NOTE:
#   You might want to define check_@name function which will be run by
#   run_checks.  The check_@name function takes no arguments and _must_ return
#   0 on success and non-zero on failure. See checks.sh for more information.
#
# E.g. if @config_var is CONFIG_ALSA then
#   "CONFIG_ALSA := y" or
#   "CONFIG_ALSA := n" will be written to Makefile
# and
#   "#define CONFIG_ALSA" or
#   "/* #define CONFIG_ALSA */" will be written to config.h
enable_flag()
{
	local name value var desc

	argc 4
	before parse_command_line

	name="$1"
	value="$2"
	var="$3"
	desc="$4"

	case $value in
		yes|no)
			;;
		auto)
			if ! is_function "check_${name}"
			then
				die "function \`check_${name}' must be defined if default value for --enable-${name} is 'auto'"
			fi
			;;
		*)
			die "default value for an enable flag must be 'yes', 'no' or 'auto'"
			;;
	esac

	enable_flags="${enable_flags} $name"
	set_var enable_value_${name} "$value"
	set_var enable_var_${name} "$var"
	set_var enable_desc_${name} "$desc"

	set_var enable_config_h_${name} $enable_use_config_h_val
	set_var enable_config_mk_${name} $enable_use_config_mk_val
}

enable_use_config_h()
{
	argc 1
	case $1 in
		yes|no)
			;;
		*)
			die "parameter for $FUNCNAME must be 'yes' or 'no'"
			;;
	esac
	enable_use_config_h_val=$1
}

enable_use_config_mk()
{
	argc 1
	case $1 in
		yes|no)
			;;
		*)
			die "parameter for $FUNCNAME must be 'yes' or 'no'"
			;;
	esac
	enable_use_config_mk_val=$1
}

# usage: 'configure_simple "$@"'
# 
#  o parse command line
#  o run checks
#  o generate Makefile
#  o generate config.h
# 
# If you need to add variables to Makefile/config.h you can run
#   parse_command_line,
#   run_checks,
#   generate_config_mk and
#   generate_config_h
# manually.  generate_config_h is optional. Add variables to Makefile or
# config.h with config_{bool,int,str} or makefile_var / makefile_env_vars.
configure_simple()
{
	parse_command_line "$@"
	run_checks
	generate_config_h
	generate_config_mk
	generate_makefiles .
}

generate_makefiles()
{
	local dir file i count

	after run_checks
	for dir in "$@"
	do
		dir=${dir//\/\//\/}
		if [[ $dir = "." ]]
		then
			count=0
			file="Makefile"
		else
			local tmp=${dir//\//}
			count=$((${#dir} - ${#tmp} + 1))
			file="${dir}/Makefile"
		fi
		echo "Generating ${file}"
		mkdir -p $dir || exit 1
		output_file ${file}
		out -n "include "
		i=0
		while [[ $i -lt $count ]]
		do
			out -n "../"
			i=$(($i + 1))
		done
		out "config.mk"
	done
}

# Add variable to Makefile
#
# @name   name of the variable
# @value  value of the variable
makefile_var()
{
	local i

	argc 2
	after parse_command_line
	before generate_config_mk

	i=${#mk_var_names[@]}
	mk_var_names[$i]="$1"
	mk_var_values[$i]="$2"
}

# Add environment variables to Makefile
#
# @var...  environment variable names
makefile_env_vars()
{
	after parse_command_line
	before generate_config_mk

	mk_env_vars="$mk_env_vars $@"
}

# Add string variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_str()
{
	argc 2 3
	after run_checks
	before generate_config_h

	config_var "$1" "$2" "$3" "str"
}

# Add integer variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_int()
{
	argc 2 3
	after run_checks
	before generate_config_h

	config_var "$1" "$2" "$3" "int"
}

# Add boolean variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_bool()
{
	argc 2 3
	after run_checks
	before generate_config_h

	config_var "$1" "$2" "$3" "bool"
}

filter_in_files()
{
	local i o

	after run_checks
	for o in "$@"
	do
		i=${srcdir}/${o}.in
		[[ -f ${i} ]] || die "${i} doesn't exist"
		echo "Generating $o"
		generated_file $o
		mkdir -p $(dirname $o) || exit 1
		$scriptdir/sedin $i $o || exit 1
	done
}

# Print configuration
# Useful at end of configure script.
print_config()
{
	local flag

	argc 0
	after generate_config_mk

	echo
	echo "Configuration:"
	for flag in $enable_flags
	do
		lprint "${flag}: " 21
		get_var enable_value_${flag}
	done
}

# Print compiler settings (CC CFLAGS etc)
# Useful at end of configure script.
print_compiler_settings()
{
	argc 0
	after generate_config_mk

	echo
	echo "Compiler Settings:"

	if list_contains "$checks" check_cc
	then
		var_print CC
		var_print LD
		var_print CFLAGS
		var_print LDFLAGS
		var_print SOFLAGS
	fi

	if list_contains "$checks" check_cxx
	then
		var_print CXX
		var_print CXXLD
		var_print CXXFLAGS
		var_print CXXLDFLAGS
	fi

	if list_contains "$checks" check_ar
	then
		var_print AR
		var_print ARFLAGS
	fi

	if list_contains "$checks" check_as
	then
		var_print AS
	fi
}

# Print install dir variables (prefix, bindir...)
# Useful at end of configure script.
#
# NOTE:  This does not print variables that were not set by user.
print_install_dirs()
{
	local names name

	argc 0
	after generate_config_mk

	echo
	echo "Installation Directories:"
	names=$(for name in prefix $set_install_dir_vars; do echo $name; done | sort | uniq)
	for name in $names
	do
		lprint "$name:" 20
		echo " $(get_var $name)"
	done
}
