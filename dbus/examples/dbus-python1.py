#!/usr/bin/env python2
import dbus

def main():
    try:
        bus = dbus.SessionBus()
        obj = bus.get_object("net.sourceforge.cmus", "/net/sourceforge/cmus")
        cmus = dbus.Interface(obj, "net.sourceforge.cmus")
    except:
        return

    print "the player is", cmus.status()
    if cmus.has_track():
        print "the current track is  ", cmus.artist(), "-", cmus.title()
        print "at position   %d/%d" % (cmus.pos(), cmus.duration())
    else:
        print "no track is selected"
    print "shuffle:", bool(cmus.shuffle())
    print "repeat:", bool(cmus.repeat())
    print "we're playing at a volume of", cmus.volume()

if __name__ == '__main__':
    main()
