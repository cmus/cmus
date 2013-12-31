/*
 * Copyright 2013-2014 Various Authors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SD_INTROSPECT_H_
#define _SD_INTROSPECT_H_

#define MULTILINE(...) #__VA_ARGS__

const char *SD_INTROSPECT = MULTILINE(
<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN" "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node><interface name="net.sourceforge.cmus">
	<method name="album">         <arg type="s" direction="out"/></method>
	<method name="albumartist">   <arg type="s" direction="out"/></method>
	<method name="artist">        <arg type="s" direction="out"/></method>
	<method name="compilation">   <arg type="b" direction="out"/></method>
	<method name="date">          <arg type="i" direction="out"/></method>
	<method name="discnumber">    <arg type="i" direction="out"/></method>
	<method name="duration">      <arg type="i" direction="out"/></method>
	<method name="filename">      <arg type="s" direction="out"/></method>
	<method name="has_track">     <arg type="b" direction="out"/></method>
	<method name="list_artists">  <arg type="s" direction="out"/></method>
	<method name="original_date"> <arg type="i" direction="out"/></method>
	<method name="pos">           <arg type="i" direction="out"/></method>
	<method name="query_old">     <arg type="s" direction="out"/></method>
	<method name="repeat">        <arg type="b" direction="out"/></method>
	<method name="shuffle">       <arg type="b" direction="out"/></method>
	<method name="status">        <arg type="s" direction="out"/></method>
	<method name="title">         <arg type="s" direction="out"/></method>
	<method name="tracknumber">   <arg type="i" direction="out"/></method>
	<method name="version">       <arg type="s" direction="out"/></method>
	<method name="volume">        <arg type="i" direction="out"/></method>
	<method name="clear_library">  </method>
	<method name="clear_playlist"> </method>
	<method name="clear_queue">    </method>
	<method name="hello">          </method>
	<method name="next">           </method>
	<method name="pause">          </method>
	<method name="play">           </method>
	<method name="prev">           </method>
	<method name="stop">           </method>
	<method name="toggle_repeat">  </method>
	<method name="toggle_shuffle"> </method>
	<method name="add_to_library">  <arg type="s"/></method>
	<method name="add_to_playlist"> <arg type="s"/></method>
	<method name="add_to_queue">    <arg type="s"/></method>
	<method name="cmd">             <arg type="s"/></method>
	<method name="load">            <arg type="s"/></method>
	<method name="play_file">       <arg type="s"/></method>
	<method name="seek">            <arg type="s"/></method>
	<method name="set_volume">      <arg type="s"/></method>

	<signal name="exit">          </signal>
	<signal name="status_change"> </signal>
	<signal name="track_change">  </signal>
	<signal name="vol_change">    </signal>
</interface></node>
);

#endif
