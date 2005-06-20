#!/bin/bash
#
# Copyright 2005 Timo Hirvonen <tihirvon@ee.oulu.fi>
#
# This file is licensed under the GPLv2.

# locals {{{

module_names=()
module_cflags=()
module_libs=()

# --enable-$NAME flags
# $NAME must contain only [a-z0-9-] characters
enable_flags=""

# For each --enable-$NAME there are
#   enable_value_$NAME
#   enable_desc_$NAME
#   enable_var_$NAME
# variables and check_$NAME function

config_var_names=()
config_var_values=()
config_var_descriptions=()
config_var_types=()

mk_var_names=()
mk_var_values=()

mk_env_vars=""

# these are environment variables
install_dir_vars="prefix exec_prefix bindir sbindir libexecdir datadir sysconfdir sharedstatedir localstatedir libdir includedir infodir mandir"
set_install_dir_vars=""

checks=""

print_enables()
{
	local flag

	for flag in $enable_flags
	do
		lprint "  --enable-${flag//_/-}" 24
		echo "  $(get_var enable_desc_${flag}) [$(get_var enable_value_${flag})]"
	done
}

get_dir_val()
{
	local val

	argc 2
	val=$(get_var $1)
	if [[ -z $val ]]
	then
		echo "$2"
	else
		echo "$val"
	fi
}

show_help()
{
	local _bindir _sbindir _libexecdir _datadir _sysconfdir _sharedstatedir _localstatedir _libdir _includedir _infodir _mandir

	argc 0

	_bindir=$(get_dir_val bindir EPREFIX/bin)
	_sbindir=$(get_dir_val sbindir EPREFIX/sbin)
	_libexecdir=$(get_dir_val libexecdir EPREFIX/libexec)
	_datadir=$(get_dir_val datadir PREFIX/share)
	_sysconfdir=$(get_dir_val sysconfdir PREFIX/etc)
	_sharedstatedir=$(get_dir_val sharedstatedir PREFIX/com)
	_localstatedir=$(get_dir_val localstatedir PREFIX/var)
	_libdir=$(get_dir_val libdir EPREFIX/lib)
	_includedir=$(get_dir_val includedir PREFIX/include)
	_infodir=$(get_dir_val infodir PREFIX/info)
	_mandir=$(get_dir_val mandir PREFIX/share/man)

	cat <<EOT
Usage: ./configure [options] [VARIABLE=VALUE]...
Installation directories:
  --prefix=PREFIX         install architecture-independent files in PREFIX
                          [/usr/local]
  --exec-prefix=EPREFIX   install architecture-dependent files in EPREFIX
                          [PREFIX]

By default, \`make install' will install all the files in
\`/usr/local/bin', \`/usr/local/lib' etc.  You can specify
an installation prefix other than \`/usr/local' using \`--prefix',
for instance \`--prefix=\$HOME'.

For better control, use the options below.

Fine tuning of the installation directories:
  --bindir=DIR           user executables [$_bindir]
  --sbindir=DIR          system admin executables [$_sbindir]
  --libexecdir=DIR       program executables [$_libexecdir]
  --datadir=DIR          read-only architecture-independent data [$_datadir]
  --sysconfdir=DIR       read-only single-machine data [$_sysconfdir]
  --sharedstatedir=DIR   modifiable architecture-independent data [$_sharedstatedir]
  --localstatedir=DIR    modifiable single-machine data [$_localstatedir]
  --libdir=DIR           object code libraries [$_libdir]
  --includedir=DIR       C header files [$_includedir]
  --infodir=DIR          info documentation [$_infodir]
  --mandir=DIR           man documentation [$_mandir]

Optional Features:
  --disable-FEATURE       do not include FEATURE (same as --enable-FEATURE=no)
  --enable-FEATURE[=ARG]  include FEATURE (ARG=yes|no|auto) [ARG=yes]
EOT
	print_enables
	echo
	echo "Some influential environment variables:"
	echo "  CC CFLAGS LD LDFLAGS SOFLAGS"
	echo "  CXX CXXFLAGS CXXLD CXXLDFLAGS"
}

is_enable_flag()
{
	argc 1
	list_contains "$enable_flags" "$1"
}

handle_enable()
{
	local flag val

	argc 2
	flag="$1"
	val="$2"
	is_enable_flag "$flag" || die "invalid option --enable-$key"
	case $val in
		yes|no|auto)
			set_var enable_value_${flag} $val
			;;
		*)
			die "invalid argument for --enable-${flag}"
			;;
	esac
}

reset_vars()
{
	local name

	argc 0
	for name in $install_dir_vars PACKAGE VERSION PACKAGE_NAME PACKAGE_BUGREPORT
	do
		set_var $name ''
	done
}

set_unset_install_dir_vars()
{
	argc 0
	var_default prefix "/usr/local"
	var_default exec_prefix "$prefix"
	var_default bindir "$exec_prefix/bin"
	var_default sbindir "$exec_prefix/sbin"
	var_default libexecdir "$exec_prefix/libexec"
	var_default datadir "$prefix/share"
	var_default sysconfdir "$prefix/etc"
	var_default sharedstatedir "$prefix/com"
	var_default localstatedir "$prefix/var"
	var_default libdir "$exec_prefix/lib"
	var_default includedir "$prefix/include"
	var_default infodir "$prefix/info"
	var_default mandir "$prefix/share/man"
}

set_makefile_variables()
{
	local flag i

	argc 0
	for flag in $enable_flags
	do
		local var=$(get_var enable_var_${flag})
		if [[ -n $var ]]
		then
			local v
			if [[ $(get_var enable_value_${flag}) = yes ]]
			then
				v=y
			else
				v=n
			fi
			makefile_var $var $v
		fi
	done

	i=0
	while [[ $i -lt ${#module_names[@]} ]]
	do
		local ucname=$(echo ${module_names[$i]} | to_upper)
		makefile_var ${ucname}_CFLAGS "${module_cflags[$i]}"
		makefile_var ${ucname}_LIBS "${module_libs[$i]}"
		i=$(($i + 1))
	done

	makefile_env_vars $install_dir_vars
}

set_config_h_variables()
{
	local flag name

	argc 0
	config_str PACKAGE "$PACKAGE" "package name (short)"
	config_str VERSION "$VERSION" "packege version"
	[[ -n $PACKAGE_NAME ]] && config_str PACKAGE_NAME "$PACKAGE_NAME" "package name (full)"
	[[ -n $PACKAGE_BUGREPORT ]] && config_str PACKAGE_BUGREPORT "$PACKAGE_BUGREPORT" "address where bug reports should be sent"

	for flag in $enable_flags
	do
		local var=$(get_var enable_var_${flag})
		if [[ -n $var ]]
		then
			config_var "${var}" "$(get_var enable_value_${flag})" "$(get_var enable_desc_${flag})" bool
		fi
	done

	for name in $install_dir_vars
	do
		config_str $(echo $name | to_upper) "$(get_var $name)"
	done
}

# }}}

parse_command_line()
{
	local kv key var val
	local name

	only_once

	for name in PACKAGE VERSION
	do
		[[ -z $(get_var $name) ]] && die "$name must be defined in 'configure'"
	done

	# parse flags (--*)
	while [[ $# -gt 0 ]]
	do
		case $1 in
			--enable-*)
				kv=${1##--enable-}
				key=${kv%%=*}
				if [[ $key = $kv ]]
				then
					# '--enable-foo'
					val=yes
				else
					# '--enable-foo=bar'
					val=${kv##*=}
				fi
				handle_enable "${key//-/_}" "$val"
				;;
			--disable-*)
				key=${1##--disable-}
				handle_enable "${key//-/_}" "no"
				;;
			--h|--he|--hel|--help)
				show_help
				exit 0
				;;
			--prefix=*|--exec-prefix=*|--bindir=*|--sbindir=*|--libexecdir=*|--datadir=*|--sysconfdir=*|--sharedstatedir=*|--localstatedir=*|--libdir=*|--includedir=*|--infodir=*|--mandir=*)
				kv=${1##--}
				key=${kv%%=*}
				val=${kv##*=}
				var=${key/-/_}
				set_var ${var} "$val"
				set_install_dir_vars="${set_install_dir_vars} ${var}"
				;;
			--)
				shift
				break
				;;
			--*)
				die "unrecognized option \`$1'"
				;;
			*)
				break
				;;
		esac
		shift
	done

	while [[ $# -gt 0 ]]
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

	set_unset_install_dir_vars
	did_run

	top_srcdir=$(follow_links $srcdir)
	[[ -z $top_srcdir ]] && exit 1
	top_builddir=$(follow_links $PWD)
	[[ -z $top_builddir ]] && exit 1
	makefile_env_vars PACKAGE VERSION top_builddir top_srcdir scriptdir

	for i in PACKAGE VERSION PACKAGE_NAME PACKAGE_BUGREPORT top_builddir top_srcdir $install_dir_vars
	do
		export $i
	done
}

run_checks()
{
	local check flag

	argc 0
	after parse_command_line
	only_once

	trap 'rm -f .tmp-*' 0 1 2 3 13 15
	for check in $checks
	do
		$check || die -e "\nconfigure failed."
	done
	for flag in $enable_flags
	do
		local val=$(get_var enable_value_${flag})
		if [[ $val != no ]]
		then
			if ! is_function check_${flag}
			then
# 				[[ $val = auto ]] && die ""
				continue
			fi

			if check_${flag}
			then
				# check successful
				set_var enable_value_${flag} yes
			else
				# check failed
				if [[ $val = yes ]]
				then
					die "\nconfigure failed."
				else
					# auto
					set_var enable_value_${flag} no
				fi
			fi
		fi
	done

	# .distclean has to be removed before calling generated_file()
	# but not before configure has succeeded
	rm -f .distclean
	did_run
}

var_print()
{
	argc 1
	lprint "$1:" 20
	echo " $(get_var $1)"
}

config_var()
{
	argc 4
	after parse_command_line
	before generate_config_h

	local i=${#config_var_names[@]}
	config_var_names[$i]="$1"
	config_var_values[$i]="$2"
	config_var_descriptions[$i]="$3"
	config_var_types[$i]="$4"
}

generated_file()
{
	argc 1
	after run_checks

	echo "$1" >> .distclean
}

update_file()
{
	local old new

	argc 2
	new="$1"
	old="$2"
	if [[ -e $old ]]
	then
		cmp "$old" "$new" 2>/dev/null 1>&2 && return 0
	fi
	mv -f "$new" "$old"
}

generate_config_h()
{
	local tmp i

	argc 0
	after run_checks
	only_once

	set_config_h_variables
	echo "Generating config.h"
	tmp=$(tmp_file config.h)
	output_file $tmp
	out "#ifndef _CONFIG_H"
	out "#define _CONFIG_H"
	i=0
	while [[ $i -lt ${#config_var_names[@]} ]]
	do
		out
		if [[ -n ${config_var_descriptions[$i]} ]]
		then
			out "/* ${config_var_descriptions[$i]} */"
		fi
		if [[ ${config_var_types[$i]} = bool ]]
		then
			case ${config_var_values[$i]} in
				no)
					out "/* #define ${config_var_names[$i]} */"
					;;
				yes)
					out "#define ${config_var_names[$i]} 1"
					;;
				*)
					die "invalid value \`${config_var_values[$i]}' for boolean ${config_var_names[$i]}"
					;;
			esac
		else
			out -n "#define ${config_var_names[$i]} "
			case ${config_var_types[$i]} in
				int)
					out "${config_var_values[$i]}"
					;;
				str)
					out "\"${config_var_values[$i]}\""
					;;
				*)
					die "invalid config variable type \`${config_var_types[$i]}'"
					;;
			esac
		fi
		i=$(($i + 1))
	done
	out
	out "#endif"
	update_file $tmp config.h
	did_run
}

generate_config_mk()
{
	local i s c tmp

	argc 0
	after run_checks
	only_once

	set_makefile_variables
	echo "Generating config.mk"
	tmp=$(tmp_file config.mk)
	output_file $tmp
	out '# run "make help" for usage information'
	out
	for i in $mk_env_vars
	do
		s="export ${i}"
		c=$((24 - ${#s}))
		while [[ $c -gt 0 ]]
		do
			s="${s} "
			c=$(($c - 1))
		done
		out "${s} := $(get_var $i)"
	done
	i=0
	while [[ $i -lt ${#mk_var_names[@]} ]]
	do
		s="export ${mk_var_names[$i]}"
		c=$((24 - ${#s}))
		while [[ $c -gt 0 ]]
		do
			s="${s} "
			c=$(($c - 1))
		done
		out "${s} := ${mk_var_values[$i]}"
		i=$(($i + 1))
	done
	out
	out 'include $(scriptdir)/main.mk'
	update_file $tmp config.mk
	did_run
}

reset_vars
