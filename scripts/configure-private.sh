#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

# locals {{{

# --enable-$NAME flags
# $NAME must contain only [a-z0-9-] characters
enable_flags=""

# put config values to config.h and config.mk
enable_use_config_h_val=yes
enable_use_config_mk_val=yes

# option flags
opt_flags=""

# For each --enable-$NAME there are
#   enable_value_$NAME
#   enable_desc_$NAME
#   enable_var_$NAME
# variables and check_$NAME function

# for each $config_vars there are
#   cv_value_$NAME
#   cv_type_$NAME
config_vars=""

# config.mk variable names
mk_env_vars=""

checks=""

PACKAGE=""
VERSION=""
CROSS=

did_run()
{
	set_var did_run_$1 yes
}

before()
{
	if test "$(get_var did_run_$1)" = yes
	then
		echo
		echo "Bug in the configure script!"
		echo "Function $2 was called after $1."
		exit 1
	fi
}

after()
{
	if test "$(get_var did_run_$1)" != yes
	then
		echo
		echo "Bug in the configure script!"
		echo "Function $2 was called before $1."
		exit 1
	fi
}

get_dir_val()
{
	local val

	val=$(get_var $1)
	if test -z "$val"
	then
		echo "$2"
	else
		echo "$val"
	fi
}

show_help()
{
	local i tmp

	cat <<EOT
Usage: ./configure [options] [VARIABLE=VALUE]...

  --cross=MACHINE         cross-compile []
EOT
	for i in $opt_flags
	do
		tmp=$(get_var flag_argdesc_${i})
		strpad "--$(echo $i | sed 's/_/-/g')${tmp}" 22
		tmp=$(get_var flag_desc_${i})
		echo "  $strpad_ret  ${tmp}"
	done
	cat <<EOT

Optional Features:
  --disable-FEATURE       do not include FEATURE (same as --enable-FEATURE=no)
  --enable-FEATURE[=ARG]  include FEATURE (ARG=yes|no|auto) [ARG=yes]
EOT
	for i in $enable_flags
	do
		strpad "--enable-$(echo $i | sed 's/_/-/g')" 22
		echo "  $strpad_ret  $(get_var enable_desc_${i}) [$(get_var enable_value_${i})]"
	done
	exit 0
}

handle_enable()
{
	local flag val i

	flag="$1"
	val="$2"

	for i in $enable_flags
	do
		test "$i" = "$flag" || continue

		case $val in
			yes|no|auto)
				set_var enable_value_${flag} $val
				return 0
				;;
			*)
				die "invalid argument for --enable-${flag}"
				;;
		esac
	done
	die "invalid option --enable-$flag"
}

set_makefile_variables()
{
	local flag i

	for flag in $enable_flags
	do
		local var=$(get_var enable_var_${flag})
		if test -n "$var" && test "$(get_var enable_config_mk_${flag})" = yes
		then
			local v
			if test "$(get_var enable_value_${flag})" = yes
			then
				v=y
			else
				v=n
			fi
			makefile_var $var $v
		fi
	done
}

set_config_h_variables()
{
	local flag name

	for flag in $enable_flags
	do
		local var=$(get_var enable_var_${flag})
		if test -n "$var" && test "$(get_var enable_config_h_${flag})" = yes
		then
			config_var "${var}" "$(get_var enable_value_${flag})" bool
		fi
	done
}

# }}}

parse_command_line()
{
	local kv key var val
	local name

	for name in PACKAGE VERSION
	do
		test -z "$(get_var $name)" && die "$name must be defined in 'configure'"
	done

	add_flag help no show_help "show this help and exit"

	# parse flags (--*)
	while test $# -gt 0
	do
		case $1 in
			--enable-*)
				kv=${1##--enable-}
				key=${kv%%=*}
				if test "$key" = "$kv"
				then
					# '--enable-foo'
					val=yes
				else
					# '--enable-foo=bar'
					val=${kv##*=}
				fi
				handle_enable "$(echo $key | sed 's/-/_/g')" "$val"
				;;
			--disable-*)
				key=${1##--disable-}
				handle_enable "$(echo $key | sed 's/-/_/g')" "no"
				;;
			--cross=*)
				CROSS=${1##--cross=}
				;;
			--)
				shift
				break
				;;
			--*)
				local name found f
				kv=${1##--}
				key=${kv%%=*}
				name="$(echo $key | sed 's/-/_/g')"
				found=false
				for f in $opt_flags
				do
					if test "$f" = "$name"
					then
						if test "$key" = "$kv"
						then
							# '--foo'
							test "$(get_var flag_hasarg_${name})" = yes && die "--${key} requires an argument (--${key}$(get_var flag_argdesc_${name}))"
							$(get_var flag_func_${name}) ${key}
						else
							# '--foo=bar'
							test "$(get_var flag_hasarg_${name})" = no && die "--${key} must not have an argument"
							$(get_var flag_func_${name}) ${key} "${kv##*=}"
						fi
						found=true
						break
					fi
				done
				$found || die "unrecognized option \`$1'"
				;;
			*)
				break
				;;
		esac
		shift
	done

	while test $# -gt 0
	do
		case $1 in
			*=*)
				key=${1%%=*}
				val=${1#*=}
				set_var $key "$val"
				;;
			*)
				die "unrecognized argument \`$1'"
				;;
		esac
		shift
	done

	did_run parse_command_line

	makefile_env_vars PACKAGE VERSION
}

run_checks()
{
	local check flag

	after parse_command_line run_checks

	trap 'rm -f .tmp-[0-9]*-*' 0 1 2 3 13 15
	for check in $checks
	do
		$check || die -e "\nconfigure failed."
	done
	for flag in $enable_flags
	do
		local val=$(get_var enable_value_${flag})
		if test "$val" != no
		then
			if ! is_function check_${flag}
			then
# 				test "$val" = auto && die ""
				continue
			fi

			if check_${flag}
			then
				# check successful
				set_var enable_value_${flag} yes
			else
				# check failed
				if test "$val" = yes
				then
					die "configure failed."
				else
					# auto
					set_var enable_value_${flag} no
				fi
			fi
		fi
	done
	did_run run_checks
}

var_print()
{
	strpad "$1:" 20
	echo "${strpad_ret} $(get_var $1)"
}

config_var()
{
	after parse_command_line config_var
	before generate_config_h config_var

	config_vars="$config_vars $1"
	set_var cv_value_$1 "$2"
	set_var cv_type_$1 "$3"
}

update_file()
{
	local old new

	new="$1"
	old="$2"
	if test -e "$old"
	then
		cmp "$old" "$new" 2>/dev/null 1>&2 && return 0
	fi
	mv -f "$new" "$old"
}

redirect_stdout()
{
	exec 8>&1
	exec > "$1"
}

restore_stdout()
{
	exec >&8
	exec 8>&-
}

generate_config_h()
{
	local tmp i

	after run_checks generate_config_h

	set_config_h_variables
	echo "Generating config.h"
	tmp=$(tmp_file config.h)
	redirect_stdout $tmp

	echo "#ifndef CONFIG_H"
	echo "#define CONFIG_H"
	echo
	for i in $config_vars
	do
		local v t
		v=$(get_var cv_value_${i})
		t=$(get_var cv_type_${i})
		case $t in
			bool)
				case $v in
					no)
						echo "/* #define $i */"
						;;
					yes)
						echo "#define $i 1"
						;;
				esac
				;;
			int)
				echo "#define $i $v"
				;;
			str)
				echo "#define $i \"$v\""
				;;
		esac
	done
	echo
	echo "#endif"

	restore_stdout
	update_file $tmp config.h
	did_run generate_config_h
}

generate_config_mk()
{
	local i tmp

	after run_checks generate_config_mk

	set_makefile_variables
	echo "Generating config.mk"
	tmp=$(tmp_file config.mk)
	redirect_stdout $tmp

	for i in $mk_env_vars
	do
		strpad "$i" 17
		echo "${strpad_ret} := $(get_var $i)"
	done

	restore_stdout
	update_file $tmp config.mk
	did_run generate_config_mk
}
