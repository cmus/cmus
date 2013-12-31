#!/usr/bin/env ruby
require 'dbus'

bus = DBus::SessionBus.instance
cmus = bus.service("net.sourceforge.cmus")
          .object("/net/sourceforge/cmus")
cmus.introspect
cmus = cmus["net.sourceforge.cmus"]

puts "#{cmus.artist[0]} - #{cmus.title[0]}"

cmus.on_signal("track_change") do
    puts "#{cmus.artist[0]} - #{cmus.title[0]}"
end

loop = DBus::Main.new
loop << bus
loop.run
