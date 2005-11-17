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

. scripts/utils.sh || exit 1
. scripts/checks.sh || exit 1
. scripts/configure-private.sh || exit 1

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

	argc enable_flag $# 4 4
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
			die "parameter for enable_use_config_h must be 'yes' or 'no'"
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
			die "parameter for enable_use_config_mk must be 'yes' or 'no'"
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

	argc add_flag $# 4 5
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
			die "argument 2 for add_flag must be 'yes' or 'no'"
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

# Add variable to Makefile
#
# @name   name of the variable
# @value  value of the variable
makefile_var()
{
	argc makefile_var $# 2 2
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
	argc config_str $# 2 2
	after run_checks config_str
	before generate_config_h config_str

	config_var "$1" "$2" "str"
}

# Add integer variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_int()
{
	argc config_int $# 2 2
	after run_checks config_int
	before generate_config_h config_int

	config_var "$1" "$2" "int"
}

# Add boolean variable to config.h
#
# @name         name of the variable
# @value        value of the variable
# @description  OPTIONAL
config_bool()
{
	argc config_bool $# 2 2
	after run_checks config_bool
	before generate_config_h config_bool

	config_var "$1" "$2" "bool"
}

# Print configuration
# Useful at end of configure script.
print_config()
{
	local flag

	after generate_config_mk print_config

	echo
	echo "Configuration:"
	for flag in $enable_flags
	do
		strpad "${flag}: " 21
		echo -n "$strpad_ret"
		get_var enable_value_${flag}
	done
}
