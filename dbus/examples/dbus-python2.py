#!/usr/bin/env python2
import dbus
import dbus.mainloop.glib
import gobject

def main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    bus.add_signal_receiver(lambda: track_change_handler(bus), dbus_interface = "net.sourceforge.cmus", signal_name='track_change')
    bus.add_signal_receiver(lambda:   vol_change_handler(bus), dbus_interface = "net.sourceforge.cmus", signal_name='vol_change')
    gobject.MainLoop().run()

def track_change_handler(bus):
    obj = bus.get_object("net.sourceforge.cmus", "/net/sourceforge/cmus")
    cmus = dbus.Interface(obj, "net.sourceforge.cmus")
    print "now playing:", cmus.artist(), "-", cmus.title()

def vol_change_handler(bus):
    obj = bus.get_object("net.sourceforge.cmus", "/net/sourceforge/cmus")
    cmus = dbus.Interface(obj, "net.sourceforge.cmus")
    print "new volume:", cmus.volume()

if __name__ == '__main__':
    main()
