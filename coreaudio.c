/*
 * Copyright (C) 2015 Yue Wang <yuleopen@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "op.h"
#include "mixer.h"
#include "sf.h"
#include "xmalloc.h"

#include <CoreAudio/CoreAudio.h>

static int coreaudio_max_volume = 100;
static AudioDeviceID device_id;
static AudioStreamBasicDescription format_description ;
static AudioDeviceIOProcID proc_id = NULL;

/* This is large, but best (maybe it should be even larger).
 * CoreAudio supposedly has an internal latency in the order of 2ms */
#define NUM_BUFS 16


  /* Ring-buffer */
static pthread_mutex_t buffer_mutex; /* mutex covering buffer variables */

static unsigned char *buffer[NUM_BUFS];
static unsigned int buffer_len = 0;
  
static unsigned int buf_read = 0;
static unsigned int buf_write = 0;
static unsigned int buf_read_pos = 0;
static unsigned int buf_write_pos = 0;
static int full_buffers = 0;
static int buffered_bytes = 0;

/* General purpose Ring-buffering routines */
static int write_buffer(const char* data,int len){
	int len2=0;

	while(len>0){
		if(full_buffers==NUM_BUFS) {
		printf("Buffer overrun\n");
		break;
	}

	int x=buffer_len-buf_write_pos;
	if(x>len) x=len;
	memcpy(buffer[buf_write]+buf_write_pos,data+len2,x);

	/* accessing common variables, locking mutex */
	pthread_mutex_lock(&buffer_mutex);
	len2+=x; len-=x;
	buffered_bytes+=x; buf_write_pos+=x;
		if(buf_write_pos>=buffer_len) {
			/* block is full, find next! */
			buf_write=(buf_write+1)%NUM_BUFS;
			++full_buffers;
			buf_write_pos=0;
		}
		pthread_mutex_unlock(&buffer_mutex);
	}

	return len2;
}

static int read_buffer(unsigned char* data,int len){
	int len2=0;

	while(len>0){
		if(full_buffers==0) {
			printf("Buffer overrun\n");
			break;
		}

		int x=buffer_len-buf_read_pos;
		if(x>len) x=len;
		memcpy(data+len2,buffer[buf_read]+buf_read_pos,x);
		len2+=x; len-=x;

		/* accessing common variables, locking mutex */
		pthread_mutex_lock(&buffer_mutex);
		buffered_bytes-=x; buf_read_pos+=x;
		if(buf_read_pos>=buffer_len){
			/* block is empty, find next! */
			buf_read=(buf_read+1)%NUM_BUFS;
			--full_buffers;
			buf_read_pos=0;
		}
		pthread_mutex_unlock(&buffer_mutex);
	}
	return len2;
}

static OSStatus play_callback(AudioDeviceID inDevice,
	const AudioTimeStamp *inNow,
	const AudioBufferList *inInputData,
	const AudioTimeStamp *inInputTime,
	AudioBufferList *outOutputData,
	const AudioTimeStamp *inOutputTime,
	void *inClientData)
{
	outOutputData->mBuffers[0].mDataByteSize =
		read_buffer((char *)outOutputData->mBuffers[0].mData, buffer_len);

  return 0;
}

static int coreaudio_init(void)
{
	device_id = kAudioDeviceUnknown;
	return OP_ERROR_SUCCESS;
}

static int coreaudio_exit(void)
{
	AudioHardwareUnload();
	return OP_ERROR_SUCCESS;
}

static int coreaudio_close(void)
{
	OSStatus status;
	status = AudioDeviceStop(device_id, play_callback); 
	if ( status != kAudioHardwareNoError ) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	status = AudioDeviceDestroyIOProcID(device_id, proc_id);
	if ( status != kAudioHardwareNoError ) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	device_id = kAudioDeviceUnknown;
	AudioHardwareUnload();

	for(int i=0;i<NUM_BUFS;i++) free(buffer[i]);
	pthread_mutex_destroy(&buffer_mutex);

	return OP_ERROR_SUCCESS;
}

static int coreaudio_open(sample_format_t sf, const channel_position_t *channel_map)
{
	/* initialise mutex */
	pthread_mutex_init(&buffer_mutex, NULL);
	pthread_mutex_unlock(&buffer_mutex);

	AudioObjectPropertyAddress property_address = {
		kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};
  
	// Get the default output device index.
	UInt32 device_id_size = sizeof(device_id);
	OSStatus err = AudioObjectGetPropertyData(
		kAudioObjectSystemObject, &property_address, 0, NULL,
		&device_id_size, &device_id);
	if (err != kAudioHardwareNoError) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	// Set the output device format.
	format_description.mSampleRate = (Float64)sf_get_rate(sf);
	format_description.mFormatID = kAudioFormatLinearPCM;
	format_description.mFormatFlags = 0;
	format_description.mFormatFlags |= kAudioFormatFlagIsPacked;
	format_description.mFormatFlags &= ~kAudioFormatFlagIsBigEndian;
	if (sf_get_bigendian(sf))
		format_description.mFormatFlags |= kAudioFormatFlagIsBigEndian;

	format_description.mFormatFlags &= ~kLinearPCMFormatFlagIsFloat;
	format_description.mFormatFlags &= ~kLinearPCMFormatFlagIsSignedInteger;
	if (sf_get_signed(sf))
		format_description.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
	format_description.mBytesPerPacket = sf_get_frame_size(sf);
	format_description.mFramesPerPacket = 1;
	format_description.mChannelsPerFrame = sf_get_channels(sf);
	format_description.mBitsPerChannel = sf_get_bits(sf);
	format_description.mBytesPerFrame = sf_get_frame_size(sf);
//	property_address.mScope = kAudioObjectPropertyScopeInput;
	property_address.mSelector = kAudioDevicePropertyStreamFormat;
	err = AudioObjectSetPropertyData(
		device_id, &property_address, 0, 0,
		sizeof(format_description), &format_description);
	if (err != kAudioHardwareNoError) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	// Get a description of the data format used by the default device
	UInt32 desc_size = sizeof(format_description);
	property_address.mSelector = kAudioDevicePropertyStreamFormat;
	err = AudioObjectGetPropertyData(
		device_id, &property_address, 0, 0,
		&desc_size, &format_description);
	if (err != kAudioHardwareNoError) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	// Configure the buffer.
	// Set up buffer.
	property_address.mSelector = kAudioDevicePropertyBufferFrameSize;
	UInt32 buffer_frame_size;
	UInt32 buffer_frame_size_size = sizeof(buffer_frame_size);
	err = AudioObjectGetPropertyData(
		device_id, &property_address, 0, 0,
		&buffer_frame_size_size, &buffer_frame_size);
	if (err != kAudioHardwareNoError) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}
	buffer_len = buffer_frame_size;

	/* Allocate ring-buffer memory */
	buf_read=0;
	buf_write=0;
	buf_read_pos=0;
	buf_write_pos=0;

	full_buffers=0;
	buffered_bytes=0;
	for(int i=0;i<NUM_BUFS;i++) 
		buffer[i]=(unsigned char *) malloc(buffer_len);
	err = AudioDeviceCreateIOProcID(device_id, play_callback, NULL, &proc_id);
	if (err != kAudioHardwareNoError || !proc_id) {
		proc_id = NULL;
  		for(int i=0;i<NUM_BUFS;i++)
			free(buffer[i]);
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	err = AudioDeviceStart(device_id, play_callback);
	if (err != kAudioHardwareNoError) {
  		for(int i=0;i<NUM_BUFS;i++)
			free(buffer[i]);
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	return OP_ERROR_SUCCESS;
}

static int coreaudio_write(const char *buf, int cnt)
{
	return write_buffer(buf, cnt);
}

static int coreaudio_mixer_set_volume(int l, int r)
{
	AudioObjectPropertyAddress property_address = {
		kAudioDevicePropertyVolumeScalar,
		kAudioDevicePropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};
	int volume = l > r ? l : r;
	Float32 vol = volume * 1.0f / coreaudio_max_volume;
	if (vol > 1.0f)
		vol = 1.0f;
	UInt32 size = sizeof(vol);
	OSStatus err = AudioObjectSetPropertyData(device_id,
		&property_address, 0, NULL, size, &vol);
	if (err != kAudioHardwareNoError) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_get_volume(int *l, int *r)
{
	AudioObjectPropertyAddress property_address = {
		kAudioDevicePropertyVolumeScalar,
		kAudioDevicePropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};
	Float32 vol;
	UInt32 size = sizeof(vol);
	OSStatus err = AudioObjectGetPropertyData(device_id,
		&property_address, 0, NULL, &size, &vol);
	int volume = vol * coreaudio_max_volume;
	if (volume > coreaudio_max_volume)
		volume = coreaudio_max_volume;
	*l = volume;
	*r = volume;
	if (err != kAudioHardwareNoError) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_set_option(int key, const char *val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int coreaudio_mixer_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int coreaudio_mixer_open(int *volume_max)
{
	*volume_max = coreaudio_max_volume;
	return 0;
}

static int coreaudio_mixer_dummy(void)
{
	return 0;
}

static int op_coreaudio_set_option(int key, const char *val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int op_coreaudio_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int coreaudio_pause(void)
{
	OSStatus err = AudioDeviceStop(device_id, play_callback);
	if (err != kAudioHardwareNoError) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_unpause(void)
{
	OSStatus err = AudioDeviceStart(device_id, play_callback);
	if (err != kAudioHardwareNoError) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_buffer_space(void)
{
	return (NUM_BUFS - full_buffers) * buffer_len - buf_write_pos;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = coreaudio_init,
	.exit = coreaudio_exit,
	.open = coreaudio_open,
	.close = coreaudio_close,
	.write = coreaudio_write,
	.pause = coreaudio_pause,
	.unpause = coreaudio_unpause,
	.buffer_space = coreaudio_buffer_space,
	.set_option = op_coreaudio_set_option,
	.get_option = op_coreaudio_get_option
};

const struct mixer_plugin_ops op_mixer_ops = {
	.init = coreaudio_mixer_dummy,
	.exit = coreaudio_mixer_dummy,
	.open = coreaudio_mixer_open,
	.close = coreaudio_mixer_dummy,
	.set_volume = coreaudio_mixer_set_volume,
	.get_volume = coreaudio_mixer_get_volume,
	.set_option = coreaudio_mixer_set_option,
	.get_option = coreaudio_mixer_get_option
};

const char * const op_pcm_options[] = {
	NULL
};

const char * const op_mixer_options[] = {
	NULL
};

const int op_priority = 2;
