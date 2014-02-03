#!/usr/bin/env python2
import dbus
import dbus.mainloop.glib
from gi.repository import (GObject, Notify)

def main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    Notify.init("Hello world");
    bus = dbus.SessionBus()
    bus.add_signal_receiver(lambda: track_change_handler(bus), dbus_interface = "net.sourceforge.cmus", signal_name='track_change')
    GObject.MainLoop().run()

def track_change_handler(bus):
    obj = bus.get_object("net.sourceforge.cmus", "/net/sourceforge/cmus")
    cmus = dbus.Interface(obj, "net.sourceforge.cmus")
    notify = Notify.Notification.new("cmus", "%s - %s" % (cmus.artist(), cmus.title()), "")
    notify.show()

if __name__ == '__main__':
    main()
