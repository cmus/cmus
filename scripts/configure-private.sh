#!/bin/sh
#
# Copyright 2005 Timo Hirvonen
#
# This file is licensed under the GPLv2.

# locals {{{

# --enable-$NAME flags
# $NAME must contain only [a-z0-9-] characters
# For each --enable-$NAME there are
#   enable_desc_$NAME
#   enable_var_$NAME
# variables and check_$NAME function
enable_flags=""

# option flags
opt_flags=""

# config.mk variable names
makefile_variables=""

# checks added by add_check
checks=""

# cross compilation, prefix for CC, LD etc.
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
  --disable-FEATURE       do not include FEATURE
  --enable-FEATURE        include FEATURE
EOT
	for i in $enable_flags
	do
		local var=$(get_var enable_var_${i})
		strpad "--enable-$(echo $i | sed 's/_/-/g')" 22
		echo "  $strpad_ret  $(get_var enable_desc_${i}) [$(get_var $var)]"
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

		set_var $(get_var enable_var_${flag}) $val
		return 0
	done
	die "invalid option --enable-$flag"
}

# }}}

parse_command_line()
{
	local kv key var val
	local name

	add_flag help n show_help "show this help and exit"

	# parse flags (--*)
	while test $# -gt 0
	do
		case $1 in
			--enable-*)
				key=${1##--enable-}
				handle_enable "$(echo $key | sed 's/-/_/g')" y
				;;
			--disable-*)
				key=${1##--disable-}
				handle_enable "$(echo $key | sed 's/-/_/g')" n
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
							test "$(get_var flag_hasarg_${name})" = y && die "--${key} requires an argument (--${key}$(get_var flag_argdesc_${name}))"
							$(get_var flag_func_${name}) ${key}
						else
							# '--foo=bar'
							test "$(get_var flag_hasarg_${name})" = n && die "--${key} must not have an argument"
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
}

run_checks()
{
	local check flag

	after parse_command_line run_checks

	for check in $checks
	do
		$check || die -e "\nconfigure failed."
	done
	for flag in $enable_flags
	do
		local var=$(get_var enable_var_${flag})
		local val=$(get_var $var)
		if test "$val" != n
		then
			if ! is_function check_${flag}
			then
				continue
			fi

			if check_${flag}
			then
				# check successful
				set_var $var y
			else
				# check failed
				if test "$val" = y
				then
					die "configure failed."
				else
					# auto
					set_var $var n
				fi
			fi
		fi
	done
	did_run run_checks
}

update_file()
{
	local tmp f

	tmp="$1"
	f="$2"
	if test -e "$f"
	then
		if cmp "$f" "$tmp" 2>/dev/null 1>&2
		then
			return 0
		fi
		echo "updating $f"
	else
		echo "creating $f"
	fi
	mv -f "$tmp" "$f"
}

generate_config_mk()
{
	local tmp i

	after run_checks generate_config_mk

	tmp=$(tmp_file config.mk)
	for i in $makefile_variables
	do
		strpad "$i" 17
		echo "${strpad_ret} = $(get_var $i)"
	done > $tmp
	update_file $tmp config.mk
	did_run generate_config_mk
}
