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
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "debug.h"
#include "op.h"
#include "mixer.h"
#include "sf.h"
#include "xmalloc.h"

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>

static int coreaudio_max_volume = 100;
static AudioDeviceID coreaudio_device_id;
static AudioStreamBasicDescription coreaudio_format_description ;
static AudioUnit coreaudio_audio_unit = NULL;
static char *coreaudio_device_name = NULL;
static bool coreaudio_enable_hog_mode = false;
static UInt32 coreaudio_buffer_frame_size = 0;
static int (^write_block)(char* buffer, int count) = NULL;

static OSStatus play_callback(
	void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
        UInt32 inNumberFrames, AudioBufferList *ioData)
{
//	int count = inNumberFrames * coreaudio_format_description.mBytesPerFrame;
	int count = ioData->mBuffers[0].mDataByteSize;
	if (write_block) {
		ioData->mBuffers[0].mDataByteSize = write_block(ioData->mBuffers[0].mData, count);
	}
 	return noErr;
}

static int coreaudio_init(void)
{
	coreaudio_device_id = kAudioDeviceUnknown;
	return OP_ERROR_SUCCESS;
}

static int coreaudio_exit(void)
{
	AudioHardwareUnload();
	return OP_ERROR_SUCCESS;
}

static int coreaudio_close(void)
{
	AudioOutputUnitStop(coreaudio_audio_unit);
	AudioUnitUninitialize(coreaudio_audio_unit);
	AudioComponentInstanceDispose(coreaudio_audio_unit);
	coreaudio_device_id = kAudioDeviceUnknown;
	return OP_ERROR_SUCCESS;
}

static int coreaudio_open(sample_format_t sf, const channel_position_t *channel_map)
{
	OSStatus err;
	UInt32 property_size;
	Boolean using_auhal = false;

	AudioObjectPropertyAddress property_address = {
		kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};

  	// Get the default output device index as a fallback.
	UInt32 coreaudio_device_id_size = sizeof(coreaudio_device_id);
	err = AudioObjectGetPropertyData(
		kAudioObjectSystemObject, &property_address, 0, NULL,
		&coreaudio_device_id_size, &coreaudio_device_id);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	if (coreaudio_device_name)
	{
		property_address.mSelector = kAudioHardwarePropertyDevices;
		err = AudioObjectGetPropertyDataSize(
			kAudioObjectSystemObject,
			&property_address, 0, NULL, &property_size);
		if (err == noErr)
		{
			property_address.mSelector = kAudioHardwarePropertyDevices;
			int device_count = property_size / sizeof(AudioDeviceID);
			AudioDeviceID devices[property_size];
			property_size = sizeof(devices);
			err = AudioObjectGetPropertyData(
				kAudioObjectSystemObject,
				&property_address, 0, NULL, 
				&property_size, devices);

			property_address.mSelector = kAudioDevicePropertyDeviceName;
			for (int i = 0; i < device_count; i++)
			{
				char name[256];
				property_size = sizeof(name);
				err = AudioObjectGetPropertyData(
					devices[i], &property_address, 0, NULL,
					&property_size,name);
				if (err == noErr && strncmp(name,coreaudio_device_name, strlen(coreaudio_device_name)) == 0)
				{
					coreaudio_device_id = devices[i];
					using_auhal = true;
					break;
				}
			}

		}

	}
	// Initialize Audio Unit.
	AudioComponentDescription desc = {
		kAudioUnitType_Output,
		kAudioUnitSubType_DefaultOutput,
		kAudioUnitManufacturer_Apple,
		0,
		0
	};
	if (using_auhal) 
		desc.componentSubType = kAudioUnitSubType_HALOutput;

	AudioComponent comp = AudioComponentFindNext(0, &desc);
	if (!comp) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	err = AudioComponentInstanceNew(comp, &coreaudio_audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	if (using_auhal)
	{
		err = AudioUnitSetProperty(
			coreaudio_audio_unit,
			kAudioOutputUnitProperty_CurrentDevice,
			kAudioUnitScope_Global,
			0,
			&coreaudio_device_id,
			sizeof(coreaudio_device_id));
		if (err != noErr) {
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}

		// Disable input on the AUHAL.
		UInt32 enableIO = 0;
		err = AudioUnitSetProperty(
			coreaudio_audio_unit, kAudioOutputUnitProperty_EnableIO,
			kAudioUnitScope_Input,
			1,          // input element 1
			&enableIO,  // disable
			sizeof(enableIO));
		if (err != noErr) {
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}

		// Enable output on the AUHAL.
		enableIO = 1;
		err = AudioUnitSetProperty(
			coreaudio_audio_unit, kAudioOutputUnitProperty_EnableIO,
			kAudioUnitScope_Output,
			0,          // output element 0
			&enableIO,  // enable
			sizeof(enableIO));
		if (err != noErr) {
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}
	}
		
	// Set the output stream format.
	coreaudio_format_description.mSampleRate = (Float64)sf_get_rate(sf);
	coreaudio_format_description.mFormatID = kAudioFormatLinearPCM;
	coreaudio_format_description.mFormatFlags = kAudioFormatFlagIsPacked;
	if (sf_get_bigendian(sf))
		coreaudio_format_description.mFormatFlags |= kAudioFormatFlagIsBigEndian;
	if (sf_get_signed(sf))
		coreaudio_format_description.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;
	coreaudio_format_description.mBytesPerPacket = sf_get_frame_size(sf);
	coreaudio_format_description.mFramesPerPacket = 1;
	coreaudio_format_description.mChannelsPerFrame = sf_get_channels(sf);
	coreaudio_format_description.mBitsPerChannel = sf_get_bits(sf);
	coreaudio_format_description.mBytesPerFrame = sf_get_frame_size(sf);
	UInt32 desc_size = sizeof(coreaudio_format_description);
	err = AudioUnitSetProperty(
		coreaudio_audio_unit, kAudioUnitProperty_StreamFormat, 
		kAudioUnitScope_Output, 0, &coreaudio_format_description, desc_size);
	// Doesn't matter if it cannot set it.

	err = AudioUnitSetProperty(
		coreaudio_audio_unit, kAudioUnitProperty_StreamFormat, 
		kAudioUnitScope_Input, 0, &coreaudio_format_description, desc_size);
	if (err != noErr) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	// Setup callback.
	AURenderCallbackStruct callback;
	callback.inputProc = play_callback;
	callback.inputProcRefCon = NULL;
	err = AudioUnitSetProperty(
		coreaudio_audio_unit, kAudioUnitProperty_SetRenderCallback,
		kAudioUnitScope_Input, 0, &callback, sizeof(callback));
	if (err != noErr)
		return -OP_ERROR_SAMPLE_FORMAT;

	// Configure the buffer.

	AudioValueRange value_range = { 0, 0 };
	property_size = sizeof(AudioValueRange);
	err = AudioUnitGetProperty(
		coreaudio_audio_unit, kAudioDevicePropertyBufferFrameSizeRange,
		kAudioUnitScope_Global, 0, &value_range, &property_size);
	if (err != noErr) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	UInt32 buffer_frame_size = value_range.mMaximum;
	property_size = sizeof(buffer_frame_size);
	err = AudioUnitSetProperty(
		coreaudio_audio_unit, kAudioDevicePropertyBufferFrameSize,
		kAudioUnitScope_Global, 0, &buffer_frame_size, property_size);
	// Doesn't matter if it cannot set it.

	err = AudioUnitGetProperty(
		coreaudio_audio_unit, kAudioDevicePropertyBufferFrameSize,
		kAudioUnitScope_Global, 0, &buffer_frame_size, &property_size);
	if (err != noErr) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}
	buffer_frame_size *= coreaudio_format_description.mBytesPerFrame;
	coreaudio_buffer_frame_size = buffer_frame_size;

	if (coreaudio_enable_hog_mode)
	{
		pid_t hog_pid = getpid();
		property_size = sizeof(hog_pid);
		property_address.mSelector = kAudioDevicePropertyHogMode;
		err = AudioObjectSetPropertyData(
			coreaudio_device_id, &property_address, 0, NULL,
			property_size, &hog_pid);
		if (err != noErr)
		{
			d_print("Cannot get hog mode information.\n");
		}
	}

	err = AudioUnitInitialize(coreaudio_audio_unit);
	if (err != noErr)
		return -OP_ERROR_SAMPLE_FORMAT;


	err = AudioOutputUnitStart(coreaudio_audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	return OP_ERROR_SUCCESS;
}

static int coreaudio_write(const char *buf, int cnt)
{
	__block int written = 0;
	__block int count = cnt;
	__block const char *src = buf;
	dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
	write_block = ^(char *out_buf, int out_cnt) {
		written = count > cnt ? cnt : count;
		if (written)
			memcpy(out_buf, src, written);
		count -= written;
		src += written;
		dispatch_semaphore_signal(semaphore);
		return written;
	};
	dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
	dispatch_release(semaphore);
	return written;
}

static int coreaudio_mixer_set_volume(int l, int r)
{
	int volume = l > r ? l : r;
	Float32 vol = volume * 1.0f / coreaudio_max_volume;
	if (vol > 1.0f)
		vol = 1.0f;
	if (vol < 0.0f)
		vol = 0.0f;
	OSStatus err = AudioUnitSetParameter(coreaudio_audio_unit, kHALOutputParam_Volume,
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
	OSStatus err = AudioUnitGetParameter(coreaudio_audio_unit, kHALOutputParam_Volume,
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
	switch (key) {
 	case 0:
		free(coreaudio_device_name);
		coreaudio_device_name = NULL;
		if (val[0])
			coreaudio_device_name = xstrdup(val);
		break;
	case 1:
		coreaudio_enable_hog_mode = strcmp(val, "true") ? false : true;
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int op_coreaudio_get_option(int key, char **val)
{
	switch (key) {
 	case 0:
		if (coreaudio_device_name)
			*val = xstrdup(coreaudio_device_name);
		break;
	case 1:
		*val = xstrdup(coreaudio_enable_hog_mode ? "true" : "false");
		break;
 	default:
 		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int coreaudio_pause(void)
{
	OSStatus err = AudioOutputUnitStop(coreaudio_audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_unpause(void)
{
	OSStatus err = AudioOutputUnitStart(coreaudio_audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_buffer_space(void)
{
	return coreaudio_buffer_frame_size;
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
	"device",
	"enable_hog_mode",
	NULL
};

const char * const op_mixer_options[] = {
	NULL
};

const int op_priority = 2;
