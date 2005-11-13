#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

# misc {{{

if test "$CDPATH"
then
	echo "Exporting CDPATH is dangerous and unnecessary!"
	echo
fi
unset CDPATH

srcdir="$(dirname $0)"
srcdir=$(cd $srcdir && pwd)
test -z "$srcdir" && exit 1

test -z "$scriptdir" && scriptdir="${srcdir}/scripts"

if ! test -d "$srcdir"
then
	echo "srcdir ($srcdir) is not a directory"
	exit 1
fi

if test ! -d "$scriptdir"
then
	echo "scriptdir ($scriptdir) is not directory"
	exit 1
fi

. ${scriptdir}/utils.sh || exit 1
. ${scriptdir}/checks.sh || exit 1
. ${scriptdir}/configure-private.sh || exit 1

check_source_dir()
{
	local src bld

	src=$(follow_links $srcdir)
	bld=$(follow_links $(pwd))
	if test "$src" != "$bld" && test -e "$src/Makefile"
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
	before run_checks add_check

	checks="${checks} $*"
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

	test $# -eq 4 || die "enable_flag: expecting 4 arguments $FUNCNAME"
	before parse_command_line enable_flag

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
	case $1 in
		yes|no)
			;;
		*)
			die "parameter for $FUNCNAME must be 'yes' or 'no'"
			;;
	esac
	enable_use_config_mk_val=$1
}

# Add an option flag
#
# @flag:          'foo' -> --foo[=ARG]
# @has_arg:       does --@flag take an argument? 'yes' or 'no'
# @function:      function to run if --@flag is given
# @description:   help text
# @arg_desc:      argument description shown in --help
add_flag()
{
	local flag hasarg func desc name

	if test $# -lt 4 || test $# -gt 5
	then
		die "add_flag: expecting 4-5 arguments"
	fi
	before parse_command_line add_flag

	flag="$1"
	hasarg="$2"
	func="$3"
	desc="$4"
	argdesc="$5"
	case $hasarg in
		yes|no)
			;;
		*)
			die "argument 2 for $FUNCNAME flag must be 'yes' or 'no'"
			;;
	esac
	is_function "${func}" || die "function \`${func}' not defined"
	name="$(echo $flag | sed 's/-/_/g')"
	opt_flags="$opt_flags $name"
	set_var flag_hasarg_${name} "${hasarg}"
	set_var flag_func_${name} "${func}"
	set_var flag_desc_${name} "${desc}"
	set_var flag_argdesc_${name} "${argdesc}"
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
		dir=$(echo $dir | sed 's://:/:g')
		if test "$dir" = "."
		then
			count=0
			file="Makefile"
		else
			local tmp=$(echo $dir | sed 's:/::g')
			count=$((${#dir} - ${#tmp} + 1))
			file="${dir}/Makefile"
		fi
		echo "Generating ${file}"
		mkdir -p $dir || exit 1
		output_file ${file}
		out -n "include "
		i=0
		while test $i -lt $count
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
	test $# -eq 2 || die "makefile_var: expecting 2 arguments"
	after parse_command_line makefile_var
	before generate_config_mk makefile_var

	set_var $1 "$2"
	makefile_env_vars $1
}

# Add environment variables to Makefile
#
# @var...  environment variable names
makefile_env_vars()
{
	after parse_command_line makefile_env_vars
	before generate_config_mk makefile_env_vars

	mk_env_vars="$mk_env_vars $@"
}

# Add string variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_str()
{
	if test $# -lt 2 || test $# -gt 3
	then
		die "config_str: expecting 2-3 arguments"
	fi
	after run_checks config_str
	before generate_config_h config_str

	config_var "$1" "$2" "$3" "str"
}

# Add integer variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_int()
{
	if test $# -lt 2 || test $# -gt 3
	then
		die "config_int: expecting 2-3 arguments"
	fi
	after run_checks config_int
	before generate_config_h config_int

	config_var "$1" "$2" "$3" "int"
}

# Add boolean variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_bool()
{
	if test $# -lt 2 || test $# -gt 3
	then
		die "config_bool: expecting 2-3 arguments"
	fi
	after run_checks config_bool
	before generate_config_h config_bool

	config_var "$1" "$2" "$3" "bool"
}

# Print configuration
# Useful at end of configure script.
print_config()
{
	local flag

	after generate_config_mk

	echo
	echo "Configuration:"
	for flag in $enable_flags
	do
		strpad "${flag}: " 21
		echo -n "$strpad_ret"
		get_var enable_value_${flag}
	done
}

# Print compiler settings (CC CFLAGS etc)
# Useful at end of configure script.
print_compiler_settings()
{
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

	after generate_config_mk

	echo
	echo "Installation Directories:"
	names=$(for name in prefix $set_install_dir_vars; do echo $name; done | sort | uniq)
	for name in $names
	do
		strpad "$name: " 21
		echo -n "$strpad_ret"
		get_var $name
	done
}
