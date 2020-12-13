#!/bin/sh
#
# cmus-status-display
#
# Usage:
#   in cmus command ":set status_display_program=cmus-status-display"
#
# This scripts is executed by cmus when status changes:
#   cmus-status-display key1 val1 key2 val2 ...
#
# All keys contain only chars a-z. Values are UTF-8 strings.
#
# Keys: status file url artist album discnumber tracknumber title date
#   - status (stopped, playing, paused) is always given
#   - file or url is given only if track is 'loaded' in cmus
#   - other keys/values are given only if they are available
#

output()
{
	# write status to ~/cmus-status.txt (not very useful though)
	echo "$*" >> ~/cmus-status.txt 2>&1

	# WMI (http://wmi.modprobe.de/)
	#wmiremote -t "$*" &> /dev/null
}

while test $# -ge 2
do
	eval _$1='$2'
	shift
	shift
done

if test -n "$_file"
then
	h=$(($_duration / 3600))
	m=$(($_duration % 3600))

	duration=""
	test $h -gt 0 && dur="$h:"
	duration="$dur$(printf '%02d:%02d' $(($m / 60)) $(($m % 60)))"

	output "[$_status] $_artist - $_album - $_title ($_date) $duration"
elif test -n "$_url"
then
	output "[$_status] $_url - $_title"
else
	output "[$_status]"
fi
