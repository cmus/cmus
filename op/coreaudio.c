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
#include <stdatomic.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include "../debug.h"
#include "../op.h"
#include "../mixer.h"
#include "../sf.h"
#include "../utils.h"
#include "../xmalloc.h"


/////////////////////////////////////////////////////////////////////////////
// Ring buffer utility from the PortAudio project.
// Original licence information is listed below.

/*
 * Portable Audio I/O Library
 * Ring Buffer utility.
 *
 * Author: Phil Burk, http://www.softsynth.com
 * modified for SMP safety on Mac OS X by Bjorn Roche
 * modified for SMP safety on Linux by Leland Lucius
 * also, allowed for const where possible
 * Note that this is safe only for a single-thread reader and a
 * single-thread writer.
 *
 * This program uses the PortAudio Portable Audio Library.
 * For more information see: http://www.portaudio.com
 * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

typedef struct coreaudio_ring_buffer_t {
	size_t buffer_size;
	size_t write_index;
	size_t read_index;
	size_t big_mask;
	size_t small_mask;
	char *buffer;
} coreaudio_ring_buffer_t;

static int coreaudio_ring_buffer_init(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes);
static void coreaudio_ring_buffer_destroy(coreaudio_ring_buffer_t *rbuf);
static void coreaudio_ring_buffer_flush(coreaudio_ring_buffer_t *rbuf);
static size_t coreaudio_ring_buffer_write_available(coreaudio_ring_buffer_t *rbuf);
static size_t coreaudio_ring_buffer_read_available(coreaudio_ring_buffer_t *rbuf);
static size_t coreaudio_ring_buffer_write(coreaudio_ring_buffer_t *rbuf, const char *data, size_t num_of_bytes);
static size_t coreaudio_ring_buffer_read(coreaudio_ring_buffer_t *rbuf, char *data, size_t num_of_bytes);
static size_t coreaudio_ring_buffer_write_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2);
static size_t coreaudio_ring_buffer_advance_write_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes);
static size_t coreaudio_ring_buffer_read_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2);
static size_t coreaudio_ring_buffer_advance_read_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes);

static int coreaudio_ring_buffer_init(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes)
{
	if (((num_of_bytes - 1) & num_of_bytes) != 0)
		return -1;				/*Not Power of two. */
	rbuf->buffer_size = num_of_bytes;
	rbuf->buffer = (char *)malloc(num_of_bytes);
	coreaudio_ring_buffer_flush(rbuf);
	rbuf->big_mask = (num_of_bytes *2) - 1;
	rbuf->small_mask = (num_of_bytes) - 1;
	return 0;
}

static void coreaudio_ring_buffer_destroy(coreaudio_ring_buffer_t *rbuf)
{
	if (rbuf->buffer)
		free(rbuf->buffer);
	rbuf->buffer = NULL;
	rbuf->buffer_size = 0;
	rbuf->write_index = 0;
	rbuf->read_index = 0;
	rbuf->big_mask = 0;
	rbuf->small_mask = 0;
}

static size_t coreaudio_ring_buffer_read_available(coreaudio_ring_buffer_t *rbuf)
{
	atomic_thread_fence(memory_order_seq_cst);
	return ((rbuf->write_index - rbuf->read_index) & rbuf->big_mask);
}

static size_t coreaudio_ring_buffer_write_available(coreaudio_ring_buffer_t *rbuf)
{
	return (rbuf->buffer_size - coreaudio_ring_buffer_read_available(rbuf));
}

static void coreaudio_ring_buffer_flush(coreaudio_ring_buffer_t *rbuf)
{
	rbuf->write_index = rbuf->read_index = 0;
}

static size_t coreaudio_ring_buffer_write_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2)
{
	size_t index;
	size_t available = coreaudio_ring_buffer_write_available(rbuf);
	if (num_of_bytes > available)
		num_of_bytes = available;
	index = rbuf->write_index & rbuf->small_mask;
	if ((index + num_of_bytes) > rbuf->buffer_size) {
		size_t first_half = rbuf->buffer_size - index;
		*data_ptr1 = &rbuf->buffer[index];
		*size_ptr1 = first_half;
		*data_ptr2 = &rbuf->buffer[0];
		*size_ptr2 = num_of_bytes - first_half;
	} else {
		*data_ptr1 = &rbuf->buffer[index];
		*size_ptr1 = num_of_bytes;
		*data_ptr2 = NULL;
		*size_ptr2 = 0;
	}
	return num_of_bytes;
}

static size_t coreaudio_ring_buffer_advance_write_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes)
{
	atomic_thread_fence(memory_order_seq_cst);
	return rbuf->write_index = (rbuf->write_index + num_of_bytes) & rbuf->big_mask;
}

static size_t coreaudio_ring_buffer_read_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2)
{
	size_t index;
	size_t available = coreaudio_ring_buffer_read_available(rbuf);
	if (num_of_bytes > available)
		num_of_bytes = available;
	index = rbuf->read_index & rbuf->small_mask;
	if ((index + num_of_bytes) > rbuf->buffer_size) {
		size_t first_half = rbuf->buffer_size - index;
		*data_ptr1 = &rbuf->buffer[index];
		*size_ptr1 = first_half;
		*data_ptr2 = &rbuf->buffer[0];
		*size_ptr2 = num_of_bytes - first_half;
	} else {
		*data_ptr1 = &rbuf->buffer[index];
		*size_ptr1 = num_of_bytes;
		*data_ptr2 = NULL;
		*size_ptr2 = 0;
	}
	return num_of_bytes;
}

static size_t coreaudio_ring_buffer_advance_read_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes)
{
	atomic_thread_fence(memory_order_seq_cst);
	return rbuf->read_index = (rbuf->read_index + num_of_bytes) & rbuf->big_mask;
}

static size_t coreaudio_ring_buffer_write(coreaudio_ring_buffer_t *rbuf, const char *data, size_t num_of_bytes)
{
	size_t size1, size2, num_write;
	char *data1, *data2;
	num_write = coreaudio_ring_buffer_write_regions(rbuf, num_of_bytes, &data1, &size1, &data2, &size2);
	if (size2 > 0) {
		memcpy(data1, data, size1);
		data = ((char *) data) + size1;
		memcpy(data2, data, size2);
	} else {
		memcpy(data1, data, size1);
	}
	coreaudio_ring_buffer_advance_write_index(rbuf, num_write);
	return num_write;
}

static size_t coreaudio_ring_buffer_read(coreaudio_ring_buffer_t *rbuf, char *data, size_t num_of_bytes)
{
	size_t size1, size2, num_read;
	char *data1, *data2;
	num_read = coreaudio_ring_buffer_read_regions(rbuf, num_of_bytes, &data1, &size1, &data2, &size2);
	if (size2 > 0) {
		memcpy(data, data1, size1);
		data = ((char *) data) + size1;
		memcpy(data, data2, size2);
	} else {
		memcpy(data, data1, size1);
	}
	coreaudio_ring_buffer_advance_read_index(rbuf, num_read);
	return num_read;
}

// End of ring buffer utility from the PortAudio project.
/////////////////////////////////////////////////////////////////////////////

static char *coreaudio_opt_device_name     = NULL;
static bool  coreaudio_opt_enable_hog_mode = false;
static bool  coreaudio_opt_sync_rate       = false;

static int coreaudio_max_volume = 100;
static AudioDeviceID coreaudio_device_id = kAudioDeviceUnknown;
static AudioStreamBasicDescription coreaudio_format_description;
static AudioUnit coreaudio_audio_unit = NULL;
static UInt32 coreaudio_buffer_frame_size = 1;
static coreaudio_ring_buffer_t coreaudio_ring_buffer = {0, 0, 0, 0, 0, NULL};
static UInt32 coreaudio_stereo_channels[2];
static int coreaudio_mixer_pipe_in = 0;
static int coreaudio_mixer_pipe_out = 0;

static OSStatus coreaudio_device_volume_change_listener(AudioObjectID inObjectID,
							UInt32 inNumberAddresses,
							const AudioObjectPropertyAddress inAddresses[],
							void *inClientData)
{
	notify_via_pipe(coreaudio_mixer_pipe_in);
	return noErr;
}

static OSStatus coreaudio_play_callback(void *user_data,
					AudioUnitRenderActionFlags *flags,
					const AudioTimeStamp *ts,
					UInt32 busnum,
					UInt32 nframes,
					AudioBufferList *buflist)
{
	int count = nframes * coreaudio_format_description.mBytesPerFrame;
	buflist->mBuffers[0].mDataByteSize = coreaudio_ring_buffer_read(&coreaudio_ring_buffer,
									buflist->mBuffers[0].mData,
									count);
 	return noErr;
}

static AudioDeviceID coreaudio_get_default_device()
{
	AudioObjectPropertyAddress aopa = {
		kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};

	AudioDeviceID dev_id = kAudioDeviceUnknown;
	UInt32 dev_id_size = sizeof(dev_id);
	AudioObjectGetPropertyData(kAudioObjectSystemObject,
				   &aopa,
				   0,
				   NULL,
				   &dev_id_size,
				   &dev_id);
	return dev_id;
}

static AudioDeviceID coreaudio_find_device(const char *dev_name)
{
	AudioObjectPropertyAddress aopa = {
		kAudioHardwarePropertyDevices,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};

	UInt32 property_size = 0;
	OSStatus err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,
						      &aopa,
						      0,
						      NULL,
						      &property_size);
	if (err != noErr)
		return kAudioDeviceUnknown;

	aopa.mSelector = kAudioHardwarePropertyDevices;
	int device_count = property_size / sizeof(AudioDeviceID);
	AudioDeviceID devices[device_count];
	property_size = sizeof(devices);

	err = AudioObjectGetPropertyData(kAudioObjectSystemObject,
					 &aopa,
					 0,
					 NULL,
					 &property_size,
					 devices);
	if (err != noErr)
		return kAudioDeviceUnknown;

	aopa.mSelector = kAudioDevicePropertyDeviceName;
	for (int i = 0; i < device_count; i++) {
		char name[256] = {0};
		property_size = sizeof(name);
		err = AudioObjectGetPropertyData(devices[i],
						 &aopa,
						 0,
						 NULL,
						 &property_size,
						 name);
		if (err == noErr && strcmp(name, dev_name) == 0) {
			return devices[i];
		}
	}

	return kAudioDeviceUnknown;
}

static const struct {
	channel_position_t pos;
	const AudioChannelLabel label;
} coreaudio_channel_mapping[] = {
	{ CHANNEL_POSITION_LEFT,                        kAudioChannelLabel_Left },
	{ CHANNEL_POSITION_RIGHT,                       kAudioChannelLabel_Right },
	{ CHANNEL_POSITION_CENTER,                      kAudioChannelLabel_Center },
	{ CHANNEL_POSITION_LFE,                         kAudioChannelLabel_LFEScreen },
	{ CHANNEL_POSITION_SIDE_LEFT,                   kAudioChannelLabel_LeftSurround },
	{ CHANNEL_POSITION_SIDE_RIGHT,                  kAudioChannelLabel_RightSurround },
	{ CHANNEL_POSITION_MONO,                        kAudioChannelLabel_Mono },
	{ CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,        kAudioChannelLabel_LeftCenter },
	{ CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,       kAudioChannelLabel_RightCenter },
	{ CHANNEL_POSITION_REAR_LEFT,                   kAudioChannelLabel_LeftSurroundDirect },
	{ CHANNEL_POSITION_REAR_RIGHT,                  kAudioChannelLabel_RightSurroundDirect },
	{ CHANNEL_POSITION_REAR_CENTER,                 kAudioChannelLabel_CenterSurround },
	{ CHANNEL_POSITION_INVALID,                     kAudioChannelLabel_Unknown },
};

static void coreaudio_set_channel_position(AudioDeviceID dev_id,
					    int channels,
					    const channel_position_t *map)
{
	AudioObjectPropertyAddress aopa = {
		kAudioDevicePropertyPreferredChannelLayout,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};
	AudioChannelLayout *layout = NULL;
	size_t layout_size = (size_t) &layout->mChannelDescriptions[channels];
	layout = (AudioChannelLayout*)malloc(layout_size);
	layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions ;
	layout->mChannelBitmap = 0;
	layout->mNumberChannelDescriptions = channels;
	AudioChannelDescription *descriptions = layout->mChannelDescriptions;
	for (int i = 0; i < channels; i++) {
		const channel_position_t pos = map[i];
		AudioChannelLabel label = kAudioChannelLabel_Mono;
		for (int j = 0; j < N_ELEMENTS(coreaudio_channel_mapping); j++) {
			if (pos == coreaudio_channel_mapping[j].pos) {
				label = coreaudio_channel_mapping[j].label;
				break;
			}
		}
		descriptions[channels - 1 - i].mChannelLabel = label;
		descriptions[i].mChannelFlags = kAudioChannelFlags_AllOff;
		descriptions[i].mCoordinates[0] = 0;
		descriptions[i].mCoordinates[1] = 0;
		descriptions[i].mCoordinates[2] = 0;
	}
	OSStatus err =
		AudioObjectSetPropertyData(dev_id,
				&aopa,
			 0, NULL, layout_size, layout);
	if (err != noErr)
		d_print("Cannot set the channel layout successfully.\n");
	free(layout);
}


static AudioStreamBasicDescription coreaudio_fill_format_description(sample_format_t sf)
{
	AudioStreamBasicDescription desc = {
		.mSampleRate       = (Float64)sf_get_rate(sf),
		.mFormatID         = kAudioFormatLinearPCM,
		.mFormatFlags      = kAudioFormatFlagIsPacked,
		.mBytesPerPacket   = sf_get_frame_size(sf),
		.mFramesPerPacket  = 1,
		.mChannelsPerFrame = sf_get_channels(sf),
		.mBitsPerChannel   = sf_get_bits(sf),
		.mBytesPerFrame    = sf_get_frame_size(sf),
	};

	d_print("Bits:%d\n", sf_get_bits(sf));
	if (sf_get_bigendian(sf))
		desc.mFormatFlags |= kAudioFormatFlagIsBigEndian;
	if (sf_get_signed(sf))
		desc.mFormatFlags |= kLinearPCMFormatFlagIsSignedInteger;

	return desc;
}

static void coreaudio_sync_device_sample_rate(AudioDeviceID dev_id, AudioStreamBasicDescription desc)
{
	AudioObjectPropertyAddress aopa = {
		kAudioDevicePropertyAvailableNominalSampleRates,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};

	UInt32 property_size;
	OSStatus err = AudioObjectGetPropertyDataSize(dev_id,
						      &aopa,
						      0,
						      NULL,
						      &property_size);

	int count = property_size/sizeof(AudioValueRange);
	AudioValueRange ranges[count];
	property_size = sizeof(ranges);
	err = AudioObjectGetPropertyData(dev_id,
					 &aopa,
					 0,
					 NULL,
					 &property_size,
					 &ranges);
	// Get the maximum sample rate as fallback.
	Float64 sample_rate = .0;
	for (int i = 0; i < count; i++) {
		if (ranges[i].mMaximum > sample_rate)
			sample_rate = ranges[i].mMaximum;
	}

	// Now try to see if the device support our format sample rate.
	// For some high quality media samples, the frame rate may exceed
	// device capability. In this case, we let CoreAudio downsample
	// by decimation with an integer factor ranging from 1 to 4.
	for (int f = 4; f > 0; f--) {
		Float64 rate = desc.mSampleRate / f;
		for (int i = 0; i < count; i++) {
			if (ranges[i].mMinimum <= rate
			   && rate <= ranges[i].mMaximum) {
				sample_rate = rate;
				break;
			}
		}
	}

	aopa.mSelector = kAudioDevicePropertyNominalSampleRate,

	err = AudioObjectSetPropertyData(dev_id,
					 &aopa,
					 0,
					 NULL,
					 sizeof(&desc.mSampleRate),
					 &sample_rate);
	if (err != noErr)
		d_print("Failed to synchronize the sample rate: %d\n", err);
}

static void coreaudio_hog_device(AudioDeviceID dev_id, bool hog)
{
	pid_t hog_pid;
	AudioObjectPropertyAddress aopa = {
		kAudioDevicePropertyHogMode,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};
	UInt32 size = sizeof(hog_pid);
	OSStatus err = AudioObjectGetPropertyData(dev_id,
						  &aopa,
						  0,
						  NULL,
						  &size,
						  &hog_pid);
	if (err != noErr) {
		d_print("Cannot get hog information: %d\n", err);
		return;
	}
	if (hog) {
		if (hog_pid != -1) {
			d_print("Device is already hogged.\n");
			return;
		}
	} else {
		if (hog_pid != getpid()) {
			d_print("Device is not owned by this process.\n");
			return;
		}
	}
	hog_pid = hog ? getpid() : -1;
	size = sizeof(hog_pid);
	err = AudioObjectSetPropertyData(dev_id,
					 &aopa,
					 0,
					 NULL,
					 size,
					 &hog_pid);
	if (err != noErr)
		d_print("Cannot hog the device: %d\n", err);
}

static OSStatus coreaudio_set_buffer_size(AudioUnit au, AudioStreamBasicDescription desc, int *frame_size)
{
	AudioValueRange value_range = {0, 0};
	UInt32 property_size = sizeof(AudioValueRange);
	OSStatus err = AudioUnitGetProperty(au,
					    kAudioDevicePropertyBufferFrameSizeRange,
					    kAudioUnitScope_Global,
					    0,
					    &value_range,
					    &property_size);
	if (err != noErr)
		return err;

	UInt32 buffer_frame_size = value_range.mMaximum;
	err = AudioUnitSetProperty(au,
				   kAudioDevicePropertyBufferFrameSize,
				   kAudioUnitScope_Global,
				   0,
				   &buffer_frame_size,
				   sizeof(buffer_frame_size));
	if (err != noErr)
		d_print("Failed to set maximum buffer size: %d\n", err);

	property_size = sizeof(buffer_frame_size);
	err = AudioUnitGetProperty(au,
				   kAudioDevicePropertyBufferFrameSize,
				   kAudioUnitScope_Global,
				   0,
				   &buffer_frame_size,
				   &property_size);
	if (err != noErr) {
		d_print("Cannot get the buffer frame size: %d\n", err);
		return err;
	}

	buffer_frame_size *= desc.mBytesPerFrame;

	// We set the frame size to a power of two integer that
	// is larger than buffer_frame_size.
	while (*frame_size < buffer_frame_size + 1) {
		*frame_size <<= 1;
	}

	return noErr;
}

static OSStatus coreaudio_init_audio_unit(AudioUnit *au,
					  OSType os_type,
					  AudioDeviceID dev_id)
{
	OSStatus err;
	AudioComponentDescription comp_desc = {
		kAudioUnitType_Output,
		os_type,
		kAudioUnitManufacturer_Apple,
		0,
		0
	};

	AudioComponent comp = AudioComponentFindNext(0, &comp_desc);
	if (!comp) {
		return -1;
	}

	err = AudioComponentInstanceNew(comp, au);
	if (err != noErr)
		return err;

	if (os_type == kAudioUnitSubType_HALOutput) {
		err = AudioUnitSetProperty(*au,
					   kAudioOutputUnitProperty_CurrentDevice,
					   kAudioUnitScope_Global,
					   0,
					   &dev_id,
					   sizeof(dev_id));
		if (err != noErr)
			return err;
	}

	return err;
}

static OSStatus coreaudio_start_audio_unit(AudioUnit *au,
					   int *frame_size,
					   AudioStreamBasicDescription desc)
{

	OSStatus err;
	err = AudioUnitSetProperty(*au,
				   kAudioUnitProperty_StreamFormat,
				   kAudioUnitScope_Input,
				   0,
				   &desc,
				   sizeof(desc));
	if (err != noErr)
		return err;

	AURenderCallbackStruct cb = {
		.inputProc = coreaudio_play_callback,
		.inputProcRefCon = NULL,
	};
	err = AudioUnitSetProperty(*au,
				   kAudioUnitProperty_SetRenderCallback,
				   kAudioUnitScope_Input,
				   0,
				   &cb,
				   sizeof(cb));
	if (err != noErr)
		return err;

	err = AudioUnitInitialize(*au);
	if (err != noErr)
		return err;

	err = coreaudio_set_buffer_size(*au, desc, frame_size);
	if (err != noErr)
		return err;

	return AudioOutputUnitStart(*au);
}

static int coreaudio_init(void)
{
	AudioDeviceID default_dev_id = coreaudio_get_default_device();
	if (default_dev_id == kAudioDeviceUnknown) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	AudioDeviceID named_dev_id = kAudioDeviceUnknown;
	if (coreaudio_opt_device_name)
		named_dev_id = coreaudio_find_device(coreaudio_opt_device_name);

	coreaudio_device_id = named_dev_id != kAudioDeviceUnknown ? named_dev_id : default_dev_id;

	if (named_dev_id != kAudioDeviceUnknown && coreaudio_opt_enable_hog_mode)
		coreaudio_hog_device(coreaudio_device_id, true);

	OSType unit_subtype = named_dev_id != kAudioDeviceUnknown ?
					kAudioUnitSubType_HALOutput :
					kAudioUnitSubType_DefaultOutput;
	OSStatus err = coreaudio_init_audio_unit(&coreaudio_audio_unit,
						 unit_subtype,
						 coreaudio_device_id);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_exit(void)
{
	AudioComponentInstanceDispose(coreaudio_audio_unit);
	coreaudio_audio_unit = NULL;
	coreaudio_hog_device(coreaudio_device_id, false);
	AudioHardwareUnload();
	coreaudio_device_id = kAudioDeviceUnknown;
	return OP_ERROR_SUCCESS;
}

static int coreaudio_open(sample_format_t sf, const channel_position_t *channel_map)
{

	coreaudio_format_description = coreaudio_fill_format_description(sf);
	if (coreaudio_opt_sync_rate)
		coreaudio_sync_device_sample_rate(coreaudio_device_id, coreaudio_format_description);
	if (channel_map)
		coreaudio_set_channel_position(coreaudio_device_id,
					       coreaudio_format_description.mChannelsPerFrame,
					       channel_map);
	OSStatus err = coreaudio_start_audio_unit(&coreaudio_audio_unit,
						  &coreaudio_buffer_frame_size,
						  coreaudio_format_description);
	if (err)
		return -OP_ERROR_SAMPLE_FORMAT;
	coreaudio_ring_buffer_init(&coreaudio_ring_buffer, coreaudio_buffer_frame_size);
	return OP_ERROR_SUCCESS;
}

static int coreaudio_close(void)
{
	AudioOutputUnitStop(coreaudio_audio_unit);
	AudioUnitUninitialize(coreaudio_audio_unit);
	coreaudio_ring_buffer_destroy(&coreaudio_ring_buffer);
	return OP_ERROR_SUCCESS;
}

static int coreaudio_drop(void)
{
	coreaudio_ring_buffer_flush(&coreaudio_ring_buffer);
	return OP_ERROR_SUCCESS;
}

static int coreaudio_write(const char *buf, int cnt)
{
	return coreaudio_ring_buffer_write(&coreaudio_ring_buffer, buf, cnt);
}

static OSStatus coreaudio_get_device_stereo_channels(AudioDeviceID dev_id, UInt32 *channels) {
	AudioObjectPropertyAddress aopa = {
		kAudioDevicePropertyPreferredChannelsForStereo,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};
	UInt32 size = sizeof(UInt32[2]);
	OSStatus err = AudioObjectGetPropertyData(dev_id,
						  &aopa,
						  0,
						  NULL,
						  &size,
						  channels);
	return err;
}

static int coreaudio_mixer_set_volume(int l, int r)
{
	Float32 vol[2];
	OSStatus err = 0;
	for (int i = 0; i < 2; i++) {
		vol[i]  = (i == 0 ? l : r) * 1.0f / coreaudio_max_volume;
		if (vol[i] > 1.0f)
			vol[i] = 1.0f;
		if (vol[i] < 0.0f)
			vol[i] = 0.0f;
		AudioObjectPropertyAddress aopa = {
			.mSelector	= kAudioDevicePropertyVolumeScalar,
			.mScope		= kAudioObjectPropertyScopeOutput,
			.mElement	= coreaudio_stereo_channels[i]
		};

		UInt32 size = sizeof(vol[i]);
		err |= AudioObjectSetPropertyData(coreaudio_device_id,
						  &aopa,
						  0,
						  NULL,
						  size,
						  vol + i);
	}
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_get_volume(int *l, int *r)
{
	clear_pipe(coreaudio_mixer_pipe_out, -1);
	Float32 vol[2] = {.0, .0};
	OSStatus err = 0;
	for (int i = 0; i < 2; i++) {
		AudioObjectPropertyAddress aopa = {
			.mSelector	= kAudioDevicePropertyVolumeScalar,
			.mScope		= kAudioObjectPropertyScopeOutput,
			.mElement	= coreaudio_stereo_channels[i]
		};
		UInt32 size = sizeof(vol[i]);
		err |= AudioObjectGetPropertyData(coreaudio_device_id,
						  &aopa,
						  0,
						  NULL,
						  &size,
						  vol + i);
		int volume = vol[i] * coreaudio_max_volume;
		if (volume > coreaudio_max_volume)
			volume = coreaudio_max_volume;
		if (volume < 0)
			volume = 0;
		if (i == 0) {
			*l = volume;
		} else {
			*r = volume;
		}
	}
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_open(int *volume_max)
{
	*volume_max = coreaudio_max_volume;
	OSStatus err = coreaudio_get_device_stereo_channels(coreaudio_device_id, coreaudio_stereo_channels);
	if (err != noErr) {
		d_print("Cannot get channel information: %d\n", err);
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	for (int i = 0; i < 2; i++) {
		AudioObjectPropertyAddress aopa = {
			.mSelector	= kAudioDevicePropertyVolumeScalar,
			.mScope		= kAudioObjectPropertyScopeOutput,
			.mElement	= coreaudio_stereo_channels[i]
		};
		err |= AudioObjectAddPropertyListener(coreaudio_device_id,
						      &aopa,
						      coreaudio_device_volume_change_listener,
						      NULL);
	}
	if (err != noErr) {
		d_print("Cannot add property listener: %d\n", err);
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	init_pipes(&coreaudio_mixer_pipe_out, &coreaudio_mixer_pipe_in);
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_close(void)
{
	OSStatus err = noErr;
	for (int i = 0; i < 2; i++) {
		AudioObjectPropertyAddress aopa = {
			.mSelector	= kAudioDevicePropertyVolumeScalar,
			.mScope		= kAudioObjectPropertyScopeOutput,
			.mElement	= coreaudio_stereo_channels[i]
		};

		err |= AudioObjectRemovePropertyListener(coreaudio_device_id,
							 &aopa,
							 coreaudio_device_volume_change_listener,
							 NULL);
	}
	if (err != noErr) {
		d_print("Cannot remove property listener: %d\n", err);
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	close(coreaudio_mixer_pipe_out);
	close(coreaudio_mixer_pipe_in);
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_dummy(void)
{
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_get_fds(int *fds)
{
	fds[0] = coreaudio_mixer_pipe_out;
	return 1;
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
	return coreaudio_ring_buffer_write_available(&coreaudio_ring_buffer);
}

static int coreaudio_set_sync_sample_rate(const char *val)
{
	coreaudio_opt_sync_rate = strcmp(val, "true") ? false : true;
	if (coreaudio_opt_sync_rate)
		coreaudio_sync_device_sample_rate(coreaudio_device_id, coreaudio_format_description);
	return 0;
}

static int coreaudio_get_sync_sample_rate(char **val)
{
	*val = xstrdup(coreaudio_opt_sync_rate ? "true" : "false");
	return 0;
}

static int coreaudio_set_enable_hog_mode(const char *val)
{
	coreaudio_opt_enable_hog_mode = strcmp(val, "true") ? false : true;
	coreaudio_hog_device(coreaudio_device_id, coreaudio_opt_enable_hog_mode);
	return 0;
}

static int coreaudio_get_enable_hog_mode(char **val)
{
	*val = xstrdup(coreaudio_opt_enable_hog_mode ? "true" : "false");
	return 0;
}

static int coreaudio_set_device(const char *val)
{
	free(coreaudio_opt_device_name);
	coreaudio_opt_device_name = NULL;
	if (val[0])
		coreaudio_opt_device_name = xstrdup(val);
	return 0;
}

static int coreaudio_get_device(char **val)
{
	if (coreaudio_opt_device_name)
		*val = xstrdup(coreaudio_opt_device_name);
	return 0;
}

const struct output_plugin_ops op_pcm_ops = {
	.init         = coreaudio_init,
	.exit         = coreaudio_exit,
	.open         = coreaudio_open,
	.close        = coreaudio_close,
	.drop         = coreaudio_drop,
	.write        = coreaudio_write,
	.pause        = coreaudio_pause,
	.unpause      = coreaudio_unpause,
	.buffer_space = coreaudio_buffer_space,
};


const struct mixer_plugin_ops op_mixer_ops = {
	.init       = coreaudio_mixer_dummy,
	.exit       = coreaudio_mixer_dummy,
	.open       = coreaudio_mixer_open,
	.close      = coreaudio_mixer_close,
	.get_fds    = coreaudio_mixer_get_fds,
	.set_volume = coreaudio_mixer_set_volume,
	.get_volume = coreaudio_mixer_get_volume,
};

const struct output_plugin_opt op_pcm_options[] = {
	OPT(coreaudio, device),
	OPT(coreaudio, enable_hog_mode),
	OPT(coreaudio, sync_sample_rate),
	{ NULL },
};

const struct mixer_plugin_opt op_mixer_options[] = {
	{ NULL },
};

const int op_priority = 1;
const unsigned op_abi_version = OP_ABI_VERSION;
