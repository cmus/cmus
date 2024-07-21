#!/bin/sh

usage() {
    printf "%s <music directory>
    Automatically add/remove files to/from the Library
    when files in the given directory are changed." $0
    exit
}

[ -z "$1" ] && usage

which inotifywait >/dev/null || { printf "This script requires inotify-tools to be installed."; exit; }

# NB. if your files are on removable storage, -e unmount may be useful here.
inotifywait --csv --monitor --recursive -e create -e delete "$1" | \
while IFS=, read -r path event file; do
	case "$event" in
    	CREATE) cmus-remote -C "add ${path}${file}" ;;
    	*) cmus-remote -C "update-cache" ;;
	esac
done
