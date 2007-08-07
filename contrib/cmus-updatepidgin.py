#!/usr/bin/python
# -*- coding: utf-8 -*-

import dbus
import sys

args = {}

for n in range(1, len(sys.argv) - 1, 2):
	args[sys.argv[n]] = sys.argv[n + 1]

print args

bus = dbus.SessionBus()

obj = bus.get_object("im.pidgin.purple.PurpleService", "/im/pidgin/purple/PurpleObject")
pidgin = dbus.Interface(obj, "im.pidgin.purple.PurpleInterface")

current = pidgin.PurpleSavedstatusGetCurrent()
status_type = pidgin.PurpleSavedstatusGetType(current)
saved = pidgin.PurpleSavedstatusNew("", status_type)
pidgin.PurpleSavedstatusSetMessage(saved, "♪ %s - %s" % (args["artist"], args["title"]))
pidgin.PurpleSavedstatusActivate(saved)


