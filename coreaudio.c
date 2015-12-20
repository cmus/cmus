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

#include "coreaudio.h"
#include "op.h"
#include "mixer.h"
#include "sf.h"
#include "xmalloc.h"

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>

static int coreaudio_max_volume = 100;
static AudioDeviceID device_id;
static AudioStreamBasicDescription format_description ;
static AudioUnit audio_unit = NULL;

static coreaudio_ringbuffer_t *ring_buffer = NULL;
static int coreaudio_pause(void);

static OSStatus play_callback(
	void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
        UInt32 inNumberFrames, AudioBufferList *ioData)
{
	int amt = coreaudio_ringbuffer_read_space(ring_buffer);
	int req=inNumberFrames * format_description.mBytesPerFrame;
	if(amt>req)
 		amt=req;
	if(amt)
		coreaudio_ringbuffer_read(ring_buffer, ioData->mBuffers[0].mData, amt);
	else
		coreaudio_pause();
	ioData->mBuffers[0].mDataByteSize = amt;
 	return noErr;
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
	AudioOutputUnitStop(audio_unit);
	AudioUnitUninitialize(audio_unit);
	AudioComponentInstanceDispose(audio_unit);
	device_id = kAudioDeviceUnknown;
	AudioHardwareUnload();
	coreaudio_ringbuffer_free(ring_buffer);
	ring_buffer = NULL;
	return OP_ERROR_SUCCESS;
}

static int coreaudio_open(sample_format_t sf, const channel_position_t *channel_map)
{
	AudioObjectPropertyAddress property_address = {
		kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};
  
	// Get the default output device index.
	// TODO: make it possible to talk to specific interface directly.
	// TODO: enable hog mode.
	UInt32 device_id_size = sizeof(device_id);
	OSStatus err = AudioObjectGetPropertyData(
		kAudioObjectSystemObject, &property_address, 0, NULL,
		&device_id_size, &device_id);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	// Initialize Audio Unit.
	AudioComponentDescription desc = {
		kAudioUnitType_Output,
		kAudioUnitSubType_DefaultOutput,
		kAudioUnitManufacturer_Apple,
		0,
		0
	};
	AudioComponent comp = AudioComponentFindNext(0, &desc);
	if (!comp) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	err = AudioComponentInstanceNew(comp, &audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	err = AudioUnitInitialize(audio_unit);
	if (err != noErr)
		return -OP_ERROR_SAMPLE_FORMAT;

	// Set the output stream format.
	format_description.mSampleRate = (Float64)sf_get_rate(sf);
	format_description.mFormatID = kAudioFormatLinearPCM;
	format_description.mFormatFlags = kAudioFormatFlagIsPacked;
	if (sf_get_bigendian(sf))
		format_description.mFormatFlags |= kAudioFormatFlagIsBigEndian;
	if (sf_get_signed(sf))
		format_description.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
	format_description.mBytesPerPacket = sf_get_frame_size(sf);
	format_description.mFramesPerPacket = 1;
	format_description.mChannelsPerFrame = sf_get_channels(sf);
	format_description.mBitsPerChannel = sf_get_bits(sf);
	format_description.mBytesPerFrame = sf_get_frame_size(sf);
	UInt32 desc_size = sizeof(format_description);
	err = AudioUnitSetProperty(
		audio_unit, kAudioUnitProperty_StreamFormat, 
		kAudioUnitScope_Output, 0, &format_description, desc_size);
	// Doesn't matter if it cannot set it.

	err = AudioUnitSetProperty(
		audio_unit, kAudioUnitProperty_StreamFormat, 
		kAudioUnitScope_Input, 0, &format_description, desc_size);
	if (err != noErr) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	// Setup callback.
	AURenderCallbackStruct callback;
	callback.inputProc = play_callback;
	callback.inputProcRefCon = NULL;
	err = AudioUnitSetProperty(
		audio_unit, kAudioUnitProperty_SetRenderCallback,
		kAudioUnitScope_Input, 0, &callback, sizeof(callback));
	if (err != noErr)
		return -OP_ERROR_SAMPLE_FORMAT;

	// Configure the buffer.

	AudioValueRange value_range = { 0, 0 };
	UInt32 value_range_size = sizeof(AudioValueRange);
	err = AudioUnitGetProperty(
		audio_unit, kAudioDevicePropertyBufferFrameSizeRange,
		kAudioUnitScope_Global, 0, &value_range, &value_range_size);
	if (err != noErr) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	UInt32 buffer_frame_size = value_range.mMaximum;
	UInt32 buffer_frame_size_size = sizeof(buffer_frame_size);
	err = AudioUnitSetProperty(
		audio_unit, kAudioDevicePropertyBufferFrameSize,
		kAudioUnitScope_Global, 0, &buffer_frame_size, buffer_frame_size_size);
	// Doesn't matter if it cannot set it.

	err = AudioUnitGetProperty(
		audio_unit, kAudioDevicePropertyBufferFrameSize,
		kAudioUnitScope_Global, 0, &buffer_frame_size, &buffer_frame_size_size);
	if (err != noErr) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	buffer_frame_size *= 10;
	ring_buffer = coreaudio_ringbuffer_create(buffer_frame_size);

	err = AudioOutputUnitStart(audio_unit);
	if (err != noErr) {
	        coreaudio_ringbuffer_free(ring_buffer);
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_write(const char *buf, int cnt)
{
	int wrote=coreaudio_ringbuffer_write(ring_buffer, buf, cnt);
	return wrote;
}

static int coreaudio_mixer_set_volume(int l, int r)
{
	int volume = l > r ? l : r;
	Float32 vol = volume * 1.0f / coreaudio_max_volume;
	if (vol > 1.0f)
		vol = 1.0f;
	if (vol < 0.0f)
		vol = 0.0f;
	OSStatus err = AudioUnitSetParameter(audio_unit, kHALOutputParam_Volume,
		kAudioUnitScope_Global, 0, vol, 0);

	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_get_volume(int *l, int *r)
{
	Float32 vol;
	OSStatus err = AudioUnitGetParameter(audio_unit, kHALOutputParam_Volume,
		kAudioUnitScope_Global, 0, &vol);
	int volume = vol * coreaudio_max_volume;
	if (volume > coreaudio_max_volume)
		volume = coreaudio_max_volume;
	if (volume < 0)
		volume = 0;
	*l = volume;
	*r = volume;
	if (err != noErr) {
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
	OSStatus err = AudioOutputUnitStop(audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_unpause(void)
{
	OSStatus err = AudioOutputUnitStart(audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_buffer_space(void)
{
	return coreaudio_ringbuffer_write_space(ring_buffer);
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
