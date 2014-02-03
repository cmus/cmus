// compile with   gcc `pkg-config --cflags dbus-1` dbus3.c `pkg-config --libs dbus-1`
// if you don't want to bother with libdbus you should probably use glib

#include <dbus/dbus.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

typedef DBusConnection cmus;

cmus *cmus_open(void)
{
	return dbus_bus_get(DBUS_BUS_SESSION, NULL);
}

DBusMessage *cmus_message(const char *method_name)
{
	return dbus_message_new_method_call("net.sourceforge.cmus",
		"/net/sourceforge/cmus",
		"net.sourceforge.cmus",
		method_name);
}

DBusMessage *cmus_send(cmus *cmus, DBusMessage *msg)
{
	return dbus_connection_send_with_reply_and_block(cmus,
		msg,
		DBUS_TIMEOUT_INFINITE,
		NULL);
}

long long cmus_get_generic(cmus *cmus, const char *method_name, int type)
{
	// every basic dbus type fits in 64 bits
	long long val = 0;
	DBusMessage *msg, *reply;

	msg = cmus_message(method_name);
	reply = cmus_send(cmus, msg);
	dbus_message_unref(msg);
	if (reply == NULL)
		return 0;
	dbus_message_get_args(reply, NULL, type, &val, DBUS_TYPE_INVALID);
	if (type == DBUS_TYPE_STRING && val != 0)
		val = (long long)strdup((char *)val);
	dbus_message_unref(reply);
	return val;
}

char *cmus_get_str(cmus *cmus, const char *method_name)
{
	return (char *)cmus_get_generic(cmus, method_name, DBUS_TYPE_STRING);
}

int cmus_get_bool(cmus *cmus, const char *method_name)
{
	return (int)cmus_get_generic(cmus, method_name, DBUS_TYPE_BOOLEAN);
}

int cmus_get_int(cmus *cmus, const char *method_name)
{
	return (int)cmus_get_generic(cmus, method_name, DBUS_TYPE_INT32);
}

char  *cmus_title(      cmus *cmus)  {  return  cmus_get_str(cmus,   "title");      }
char  *cmus_artist(     cmus *cmus)  {  return  cmus_get_str(cmus,   "artist");     }
char  *cmus_status(     cmus *cmus)  {  return  cmus_get_str(cmus,   "status");     }
int    cmus_has_track(  cmus *cmus)  {  return  cmus_get_bool(cmus,  "has_track");  }
int    cmus_shuffle(    cmus *cmus)  {  return  cmus_get_bool(cmus,  "shuffle");    }
int    cmus_repeat(     cmus *cmus)  {  return  cmus_get_bool(cmus,  "repeat");     }
int    cmus_pos(        cmus *cmus)  {  return  cmus_get_int(cmus,   "pos");        }
int    cmus_duration(   cmus *cmus)  {  return  cmus_get_int(cmus,   "duration");   }
int    cmus_volume(     cmus *cmus)  {  return  cmus_get_int(cmus,   "volume");     }

int main(void)
{
	char *status = NULL, *title = NULL, *artist = NULL;
	cmus *cmus;

	cmus = cmus_open();
	if (cmus == NULL) {
		printf("can't connect to dbus\n");
		return 1;
	}
	status = cmus_status(cmus);
	printf("the player is %s\n", status);
	if (cmus_has_track(cmus)) {
		title = cmus_title(cmus);
		artist = cmus_artist(cmus);
		printf("the current track is    %s - %s\n", artist, title);
		printf("at position    %d/%d\n", cmus_pos(cmus), cmus_duration(cmus));
	} else {
		printf("no track is selected\n");
	}
	printf("shuffle: %s\n", cmus_shuffle(cmus) ? "true" : "false");
	printf("repeat: %s\n", cmus_repeat(cmus) ? "true" : "false");
	printf("we're playing at a volume of %d\n", cmus_volume(cmus));

	free(status);
	free(title);
	free(artist);
}
