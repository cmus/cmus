#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

# initialization {{{

if test "$CDPATH"
then
	echo "Exporting CDPATH is dangerous and unnecessary!"
	echo
fi
unset CDPATH

. scripts/utils.sh || exit 1
. scripts/checks.sh || exit 1
. scripts/configure-private.sh || exit 1

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

# Add check function(s) that run_checks runs
#
# @check: function(s) to run
#
# NOTE:
#   The @check function takes no arguments and _must_ return 0 on success and
#   non-zero on failure. See checks.sh for more information.
add_check()
{
	before run_checks add_check

	checks="${checks} $*"
}

# Add --enable-FEATURE and --disable-FEATURE flags
#
# @name:          name of the flag (eg. alsa => --enable-alsa)
# @default_value: 'y', 'n' or 'a' (yes, no, auto)
#                 'a' can be used only if check_@name function exists
# @config_var:    name of the variable
# @description:   text shown in --help
#
# defines @config_var=y/n
#
# NOTE:
#   You might want to define check_@name function which will be run by
#   run_checks.  The check_@name function takes no arguments and _must_ return
#   0 on success and non-zero on failure. See checks.sh for more information.
#
# Example:
#   ---
#   check_alsa()
#   {
#     pkg_check_modules alsa "alsa"
#     return $?
#   }
#
#   enable_flag alsa a CONFIG_ALSA "ALSA support"
#   ---
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
		y|n)
			;;
		a)
			# 'auto' looks prettier than 'a' in --help
			value=auto
			if ! is_function "check_${name}"
			then
				die "function \`check_${name}' must be defined if default value for --enable-${name} is 'a'"
			fi
			;;
		*)
			die "default value for an enable flag must be 'y', 'n' or 'a'"
			;;
	esac

	enable_flags="${enable_flags} $name"
	set_var $var $value
	set_var enable_var_${name} $var
	set_var enable_desc_${name} "$desc"
}

# Add an option flag
#
# @flag:          'foo' -> --foo[=ARG]
# @has_arg:       does --@flag take an argument? 'y' or 'n'
# @function:      function to run if --@flag is given
# @description:   text displayed in --help
# @arg_desc:      argument description shown in --help (if @has_arg is 'y')
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
		y|n)
			;;
		*)
			die "argument 2 for add_flag must be 'y' or 'n'"
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

# Set and register variable to be added to config.mk
#
# @name   name of the variable
# @value  value of the variable
makefile_var()
{
	argc makefile_var $# 2 2
	after parse_command_line makefile_var
	before generate_config_mk makefile_var

	set_var $1 "$2"
	makefile_vars $1
}

# Register variables to be added to config.mk
makefile_vars()
{
	before generate_config_mk makefile_vars

	makefile_variables="$makefile_variables $*"
}

# -----------------------------------------------------------------------------
# Config header generation

# Example:
#   config_header_begin config.h
#   config_str PACKAGE VERSION
#   config_bool CONFIG_ALSA
#   config_header_end

config_header_begin()
{
	argc config_header_begin $# 1 1
	after run_checks config_header_begin

	config_header_file="$1"
	config_header_tmp=$(tmp_file config_header)

	local def=$(echo $config_header_file | to_upper | sed 's/[\.-/]/_/g')
	cat <<EOF > "$config_header_tmp"
#ifndef $def
#define $def

EOF
}

config_str()
{
	local i

	for i in $*
	do
		echo "#define $i \"$(get_var $i)\"" >> "$config_header_tmp"
	done
}

config_int()
{
	local i

	for i in $*
	do
		echo "#define $i $(get_var $i)" >> "$config_header_tmp"
	done
}

config_bool()
{
	local i v

	for i in $*
	do
		v=$(get_var $i)
		case $v in
			n)
				echo "/* #define $i */" >> "$config_header_tmp"
				;;
			y)
				echo "#define $i 1" >> "$config_header_tmp"
				;;
			*)
				die "bool '$i' has invalid value '$v'"
				;;
		esac
	done
}

config_header_end()
{
	local dir

	argc config_header_end $# 0 0
	echo "" >> "$config_header_tmp"
	echo "#endif" >> "$config_header_tmp"
	mkdir -p $(dirname "$config_header_file")
	update_file "$config_header_tmp" "$config_header_file"
}
# -----------------------------------------------------------------------------

# Print values for enable flags
print_config()
{
	local flag

	echo
	echo "Configuration:"
	for flag in $enable_flags
	do
		strpad "${flag}: " 21
		echo -n "$strpad_ret"
		get_var $(get_var enable_var_${flag})
	done
}
