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
#include <libkern/OSAtomic.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#include "debug.h"
#include "op.h"
#include "mixer.h"
#include "sf.h"
#include "xmalloc.h"


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

int coreaudio_ring_buffer_init(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes);
void coreaudio_ring_buffer_destroy(coreaudio_ring_buffer_t *rbuf);
void coreaudio_ring_buffer_flush(coreaudio_ring_buffer_t *rbuf);
size_t coreaudio_ring_buffer_write_available(coreaudio_ring_buffer_t *rbuf);
size_t coreaudio_ring_buffer_read_available(coreaudio_ring_buffer_t *rbuf);
size_t coreaudio_ring_buffer_write(coreaudio_ring_buffer_t *rbuf, const char *data, size_t num_of_bytes);
size_t coreaudio_ring_buffer_read(coreaudio_ring_buffer_t *rbuf, char *data, size_t num_of_bytes);
size_t coreaudio_ring_buffer_write_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2);
size_t coreaudio_ring_buffer_advance_write_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes);
size_t coreaudio_ring_buffer_read_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2);
size_t coreaudio_ring_buffer_advance_read_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes);

int coreaudio_ring_buffer_init(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes)
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

void coreaudio_ring_buffer_destroy(coreaudio_ring_buffer_t *rbuf)
{
	if (rbuf->buffer)
		free(rbuf->buffer);
	rbuf->buffer = NULL;
}

size_t coreaudio_ring_buffer_read_available(coreaudio_ring_buffer_t *rbuf)
{
	OSMemoryBarrier();
	return ((rbuf->write_index - rbuf->read_index) & rbuf->big_mask);
}

size_t coreaudio_ring_buffer_write_available(coreaudio_ring_buffer_t *rbuf)
{
	return (rbuf->buffer_size - coreaudio_ring_buffer_read_available(rbuf));
}

void coreaudio_ring_buffer_flush(coreaudio_ring_buffer_t *rbuf)
{
	rbuf->write_index = rbuf->read_index = 0;
}

size_t coreaudio_ring_buffer_write_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2)
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

size_t coreaudio_ring_buffer_advance_write_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes)
{
	OSMemoryBarrier();
	return rbuf->write_index = (rbuf->write_index + num_of_bytes) & rbuf->big_mask;
}

size_t coreaudio_ring_buffer_read_regions(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes, char **data_ptr1, size_t *size_ptr1, char **data_ptr2, size_t *size_ptr2)
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

size_t coreaudio_ring_buffer_advance_read_index(coreaudio_ring_buffer_t *rbuf, size_t num_of_bytes)
{
	OSMemoryBarrier();
	return rbuf->read_index = (rbuf->read_index + num_of_bytes) & rbuf->big_mask;
}

size_t coreaudio_ring_buffer_write(coreaudio_ring_buffer_t *rbuf, const char *data, size_t num_of_bytes)
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

size_t coreaudio_ring_buffer_read(coreaudio_ring_buffer_t *rbuf, char *data, size_t num_of_bytes)
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

// Plugin settings.
static int coreaudio_max_volume = 100;
static AudioDeviceID coreaudio_device_id;
static AudioDeviceIOProcID coreaudio_proc_id = NULL;
static AudioStreamBasicDescription coreaudio_format_description ;
static AudioUnit coreaudio_audio_unit = NULL;
static char *coreaudio_device_name = NULL;
static bool coreaudio_enable_hog_mode = false;
static bool coreaudio_use_digital = false;
static UInt32 coreaudio_buffer_frame_size = 1;
static coreaudio_ring_buffer_t coreaudio_ring_buffer;


static OSStatus play_callback(
	void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
        UInt32 inNumberFrames, AudioBufferList *ioData)
{
	int count = inNumberFrames * coreaudio_format_description.mBytesPerFrame;
	ioData->mBuffers[0].mDataByteSize = coreaudio_ring_buffer_read(&coreaudio_ring_buffer, ioData->mBuffers[0].mData, count);
 	return noErr;
}

static OSStatus direct_callback(AudioDeviceID inDevice,
	const AudioTimeStamp *inNow,
	const AudioBufferList *inInputData,
	const AudioTimeStamp *inInputTime,
	AudioBufferList *outOutputData,
	const AudioTimeStamp *inOutputTime,
	void *inClientData)
{
	int count = outOutputData->mBuffers[0].mDataByteSize;
	outOutputData->mBuffers[0].mDataByteSize = coreaudio_ring_buffer_read(&coreaudio_ring_buffer, outOutputData->mBuffers[0].mData, count);
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
	if (coreaudio_use_digital)
	{
		AudioOutputUnitStop(coreaudio_audio_unit);
		AudioUnitUninitialize(coreaudio_audio_unit);
		AudioComponentInstanceDispose(coreaudio_audio_unit);
	} else {
		AudioDeviceDestroyIOProcID(coreaudio_device_id, coreaudio_proc_id);
	}
	AudioObjectPropertyAddress property_address = {
		kAudioDevicePropertyHogMode,
		kAudioObjectPropertyScopeOutput,
		kAudioObjectPropertyElementMaster
	};

	// unhog the device.
	pid_t hog_pid = -1;
	UInt32 property_size = sizeof(hog_pid);
	AudioObjectSetPropertyData(
		coreaudio_device_id, &property_address, 0, NULL,
		property_size, &hog_pid);
	// Doesn't matter if we don't own the access.

	coreaudio_proc_id = NULL;
	coreaudio_device_id = kAudioDeviceUnknown;
	coreaudio_ring_buffer_destroy(&coreaudio_ring_buffer);
	return OP_ERROR_SUCCESS;
}

static int coreaudio_open(sample_format_t sf, const channel_position_t *channel_map)
{
	OSStatus err;
	UInt32 property_size;
	Boolean found_device = false;

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
			AudioDeviceID devices[device_count];
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
					found_device = true;
					break;
				}
			}

		}

	}

	if (!found_device)
	{
		coreaudio_enable_hog_mode = false;
	}

	// setting the format description.
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
	d_print("format: %f, %d, %d, %d, %d, %d, %d, %d\n",
		coreaudio_format_description.mSampleRate,
		coreaudio_format_description.mFormatID,
		coreaudio_format_description.mFormatFlags,
		coreaudio_format_description.mBytesPerPacket,
		coreaudio_format_description.mFramesPerPacket,
		coreaudio_format_description.mChannelsPerFrame,
		coreaudio_format_description.mBitsPerChannel,
		coreaudio_format_description.mBytesPerFrame);

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
			coreaudio_use_digital = false;
			d_print("Cannot hog the device.\n");
		}
	} else {
		coreaudio_use_digital = false;
		d_print("Direct output requires hogging the device.\n");
	}

	if (coreaudio_use_digital)
	{
		property_address.mSelector = kAudioDevicePropertyStreams;
		AudioObjectGetPropertyDataSize(coreaudio_device_id, &property_address, 0, NULL, &property_size);
		int stream_count = property_size/sizeof(AudioStreamID);
		AudioStreamID streams[stream_count];
		property_size = sizeof(streams);
		AudioObjectGetPropertyData(coreaudio_device_id,
		    &property_address, 0, NULL, &property_size, &streams);
			if (stream_count)
		for (int i = 0; i < stream_count; i++)
		{
			bool stream_support_direct_output = false;
			AudioStreamID stream = streams[i];
			property_address.mSelector = kAudioStreamPropertyPhysicalFormats;
			AudioObjectGetPropertyDataSize(stream, &property_address, 0, NULL, &property_size);
			int format_count = property_size / sizeof (AudioStreamBasicDescription);
			AudioStreamBasicDescription descs[format_count];
			property_size = sizeof(descs);
			AudioObjectGetPropertyData(stream, &property_address, 0, NULL, &property_size, &descs);
			for (int j = 0; j < format_count; j++)
			{
				if (fabs(coreaudio_format_description.mSampleRate - descs[j].mSampleRate) < 1.0
					&& coreaudio_format_description.mFormatID == descs[j].mFormatID
					&& coreaudio_format_description.mFormatFlags == descs[j].mFormatFlags
					&& coreaudio_format_description.mBytesPerPacket == descs[j].mBytesPerPacket
					&& coreaudio_format_description.mFramesPerPacket == descs[j].mFramesPerPacket
					&& coreaudio_format_description.mChannelsPerFrame == descs[j].mChannelsPerFrame
					&& coreaudio_format_description.mBitsPerChannel == descs[j].mBitsPerChannel
					&& coreaudio_format_description.mBytesPerFrame == descs[j].mBytesPerFrame)
				{
					stream_support_direct_output = true;
				}
			}
			if (!stream_support_direct_output)
			{
				d_print("Stream does not support direct output.\n");
				coreaudio_use_digital = false;
				break;
			}
		}
		for (int i = 0; i < stream_count; i++)
		{
			AudioStreamID stream = streams[i];
			property_address.mSelector = kAudioStreamPropertyPhysicalFormat;
			property_size = sizeof(coreaudio_format_description);
			err = AudioObjectSetPropertyData(
				stream, &property_address, 0, NULL,
				property_size, &coreaudio_format_description);
			if (err) {
				coreaudio_use_digital = false;
				break;
			}
			AudioStreamBasicDescription desc;
			property_size = sizeof(desc);
			err = AudioObjectGetPropertyData(
				stream, &property_address, 0, NULL,
				&property_size, &desc);
			if (err) {
				coreaudio_use_digital = false;
				break;
			}
			d_print("current physical stream [%d]: %f, %d, %d, %d, %d, %d, %d, %d\n",
				i,
				desc.mSampleRate,
				desc.mFormatID,
				desc.mFormatFlags,
				desc.mBytesPerPacket,
				desc.mFramesPerPacket,
				desc.mChannelsPerFrame,
				desc.mBitsPerChannel,
				desc.mBytesPerFrame);
			if (!(fabs(coreaudio_format_description.mSampleRate - desc.mSampleRate) < 1.0
				&& coreaudio_format_description.mFormatID == desc.mFormatID
				&& coreaudio_format_description.mFormatFlags == desc.mFormatFlags
				&& coreaudio_format_description.mBytesPerPacket == desc.mBytesPerPacket
				&& coreaudio_format_description.mFramesPerPacket == desc.mFramesPerPacket
				&& coreaudio_format_description.mChannelsPerFrame == desc.mChannelsPerFrame
				&& coreaudio_format_description.mBitsPerChannel == desc.mBitsPerChannel
				&& coreaudio_format_description.mBytesPerFrame == desc.mBytesPerFrame))
			{
				coreaudio_use_digital = false;
				break;
			}
		}
	}

	if (coreaudio_use_digital)
	{
		property_address.mSelector = kAudioDevicePropertySupportsMixing;
		Boolean settable;
		Boolean mixing;
		AudioObjectGetPropertyData(coreaudio_device_id, &property_address, 0, 0,
			&property_size, &mixing);
		if (mixing) {
			AudioObjectIsPropertySettable(coreaudio_device_id, &property_address, &settable);
			if (!settable || err != noErr) {
				d_print("Disable mixing is not supported, Err: %d", noErr);
				coreaudio_use_digital = false;
			} else {
				mixing = 0;
				err = AudioObjectSetPropertyData(
					coreaudio_device_id, &property_address, 0, 0,
					sizeof(mixing), &mixing);
				if (err != noErr)
				{
					d_print("Failed to disable mixing. Error %d\n", err);
					coreaudio_use_digital = false;
				}
			}
		}
	}

	if (coreaudio_use_digital)
	{
		AudioValueRange value_range = { 0, 0 };
		property_size = sizeof(AudioValueRange);
		property_address.mSelector = kAudioDevicePropertyBufferFrameSizeRange;
		err = AudioObjectGetPropertyData(
			coreaudio_device_id, &property_address, 0, 0,
			&property_size, &value_range);
		if (err != noErr) {
			return -OP_ERROR_SAMPLE_FORMAT;
		}

		property_address.mSelector = kAudioDevicePropertyBufferFrameSize;
		UInt32 buffer_frame_size = value_range.mMaximum;
		property_size = sizeof(buffer_frame_size);
		err = AudioObjectSetPropertyData(
			coreaudio_device_id, &property_address, 0, 0,
			property_size, &buffer_frame_size);
		// Doesn't matter if it cannot set it.
		d_print("Cannot set the buffer frame size.\n");

		property_size = sizeof(buffer_frame_size);
		err = AudioObjectGetPropertyData(
			coreaudio_device_id, &property_address, 0, 0,
			&property_size, &buffer_frame_size);
		if (err != kAudioHardwareNoError) {
			return -OP_ERROR_SAMPLE_FORMAT;
		}

		buffer_frame_size *= coreaudio_format_description.mBytesPerFrame;

		// We set the coreaudio_buffer_frame_size to a power of two integer that
		// is larger than buffer_frame_size. This way we can have the optimal
		// ring buffer.
		while (coreaudio_buffer_frame_size < buffer_frame_size + 1)
		{
			coreaudio_buffer_frame_size <<= 1;
		}

		coreaudio_ring_buffer_init(&coreaudio_ring_buffer, coreaudio_buffer_frame_size);
		err = AudioDeviceCreateIOProcID(coreaudio_device_id, direct_callback, NULL, &coreaudio_proc_id);
		if (err != kAudioHardwareNoError || !coreaudio_proc_id) {
			errno = ENODEV;
			coreaudio_ring_buffer_destroy(&coreaudio_ring_buffer);
			return -OP_ERROR_ERRNO;
		}
		err = AudioDeviceStart(coreaudio_device_id, direct_callback);
		if (err != kAudioHardwareNoError) {
			coreaudio_ring_buffer_destroy(&coreaudio_ring_buffer);
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}
		d_print("Great! We support direct output.\n");
		return OP_ERROR_SUCCESS;
	}


	d_print("Use AUHAL\n");
	// Initialize Audio Unit.

	AudioComponentDescription desc = {
		kAudioUnitType_Output,
		kAudioUnitSubType_DefaultOutput,
		kAudioUnitManufacturer_Apple,
		0,
		0
	};
	if (found_device) 
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

	if (found_device)
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
	}
		
	// Set the output stream format.
	property_size = sizeof(coreaudio_format_description);

	err = AudioUnitSetProperty(
		coreaudio_audio_unit, kAudioUnitProperty_StreamFormat, 
		kAudioUnitScope_Input, 0, &coreaudio_format_description, property_size);
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

	// Get the default buffer size.
	AudioValueRange value_range = { 0, 0 };
	property_size = sizeof(AudioValueRange);
	err = AudioUnitGetProperty(
		coreaudio_audio_unit, kAudioDevicePropertyBufferFrameSizeRange,
		kAudioUnitScope_Global, 0, &value_range, &property_size);
	if (err != noErr) {
		d_print("Error %d\n", err);
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	UInt32 buffer_frame_size = value_range.mMaximum;
	property_size = sizeof(buffer_frame_size);
	err = AudioUnitSetProperty(
		coreaudio_audio_unit, kAudioDevicePropertyBufferFrameSize,
		kAudioUnitScope_Global, 0, &buffer_frame_size, property_size);
	// Doesn't matter if it cannot set it.

	property_size = sizeof(buffer_frame_size);
	err = AudioUnitGetProperty(
		coreaudio_audio_unit, kAudioDevicePropertyBufferFrameSize,
		kAudioUnitScope_Global, 0, &buffer_frame_size, &property_size);
	if (err != noErr) {
		d_print("Cannot get the buffer frame size.\n");
		return -OP_ERROR_SAMPLE_FORMAT;
	}
	
	buffer_frame_size *= coreaudio_format_description.mBytesPerFrame;

	// We set the coreaudio_buffer_frame_size to a power of two integer that
	// is larger than buffer_frame_size. This way we can have the optimal
	// ring buffer.
	while (coreaudio_buffer_frame_size < buffer_frame_size + 1)
	{
		coreaudio_buffer_frame_size <<= 1;
	}

	err = AudioUnitInitialize(coreaudio_audio_unit);
	if (err != noErr)
		return -OP_ERROR_SAMPLE_FORMAT;
	d_print("buffer frame size %d", coreaudio_buffer_frame_size);

	coreaudio_ring_buffer_init(&coreaudio_ring_buffer, coreaudio_buffer_frame_size);

	err = AudioOutputUnitStart(coreaudio_audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		coreaudio_ring_buffer_destroy(&coreaudio_ring_buffer);
		return -OP_ERROR_ERRNO;
	}

	return OP_ERROR_SUCCESS;
}

static int coreaudio_write(const char *buf, int cnt)
{
	int written = coreaudio_ring_buffer_write(&coreaudio_ring_buffer, buf, cnt);
	return written;
}

static int coreaudio_mixer_set_volume(int l, int r)
{
	Float32 vol_l = l * 1.0f / coreaudio_max_volume;
	if (vol_l > 1.0f)
		vol_l = 1.0f;
	if (vol_l < 0.0f)
		vol_l = 0.0f;
	Float32 vol_r = r * 1.0f / coreaudio_max_volume;
	if (vol_r > 1.0f)
		vol_r = 1.0f;
	if (vol_r < 0.0f)
		vol_r = 0.0f;

	OSStatus err;
	if (coreaudio_use_digital)
	{
		AudioObjectPropertyAddress property_address = {
			kAudioDevicePropertyVolumeScalar,
			kAudioDevicePropertyScopeOutput,
			1
		}; 
		Boolean settable;
		if (!AudioObjectHasProperty(coreaudio_device_id, &property_address)
			|| AudioObjectIsPropertySettable(coreaudio_device_id, &property_address, &settable)
			|| !settable)
		{
			d_print("The device cannot set volume.");
			return -OP_ERROR_NOT_OPTION;
		}
		UInt32 size = sizeof(vol_l);
		err = AudioObjectSetPropertyData(coreaudio_device_id, &property_address, 0, NULL, size, &vol_l);

		property_address.mElement = 2;
		if (!AudioObjectHasProperty(coreaudio_device_id, &property_address)
			|| AudioObjectIsPropertySettable(coreaudio_device_id, &property_address, &settable)
			|| !settable)
		{
			d_print("The device cannot set volume.");
			return -OP_ERROR_NOT_OPTION;
		}
	
		size = sizeof(vol_r);
		err = AudioObjectSetPropertyData(coreaudio_device_id, &property_address, 0, NULL, size, &vol_r);
	} else {
		Float32 vol = vol_l > vol_r ? vol_r : vol_l;
		err = AudioUnitSetParameter(coreaudio_audio_unit, kHALOutputParam_Volume,
			kAudioUnitScope_Global, 0, vol, 0);
	}

	if (err != noErr) {
		d_print("Error on set volume.");
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_mixer_get_volume(int *l, int *r)
{
	OSStatus err;
	if (coreaudio_use_digital)
	{
		Float32 vol_l, vol_r;
		AudioObjectPropertyAddress property_address = {
			kAudioDevicePropertyVolumeScalar,
			kAudioDevicePropertyScopeOutput,
			1
		}; 
		if (!AudioObjectHasProperty(coreaudio_device_id, &property_address))
		{
			d_print("The device cannot get volume.\n");
			return -OP_ERROR_NOT_OPTION;
		}

		UInt32 size = sizeof(vol_l);
		err = AudioObjectGetPropertyData(coreaudio_device_id, &property_address, 0, NULL, &size, &vol_l);
		if (err != noErr) {
			d_print("Error on get volume. Error: %d", err);
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}


		property_address.mElement = 2;
		if (!AudioObjectHasProperty(coreaudio_device_id, &property_address))
		{
			d_print("The device cannot get volume.\n");
			return -OP_ERROR_NOT_OPTION;
		}
		size = sizeof(vol_r);
		err = AudioObjectGetPropertyData(coreaudio_device_id, &property_address, 0, NULL, &size, &vol_r);
		if (err != noErr) {
			d_print("Error on get volume. Error: %d", err);
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}


		*l = vol_l * coreaudio_max_volume;
		*r = vol_r * coreaudio_max_volume;
		if (*l > coreaudio_max_volume)
			*l = coreaudio_max_volume;
		if (*l < 0)
			*l = 0;
		if (*r > coreaudio_max_volume)
			*r = coreaudio_max_volume;
		if (*r < 0)
			*r = 0;
	}
	else
	{
		Float32 vol;
		err = AudioUnitGetParameter(coreaudio_audio_unit, kHALOutputParam_Volume,
			kAudioUnitScope_Global, 0, &vol);
		int volume = vol * coreaudio_max_volume;
		if (volume > coreaudio_max_volume)
			volume = coreaudio_max_volume;
		if (volume < 0)
			volume = 0;
		*l = volume;
		*r = volume;
		if (err != noErr) {
			d_print("Error on get volume. Error: %d", err);
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}

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
	case 2:
		coreaudio_use_digital = strcmp(val, "true") ? false : true;
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
	case 2:
		*val = xstrdup(coreaudio_use_digital ? "true" : "false");
		break;
 	default:
 		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int coreaudio_pause(void)
{
        if (coreaudio_use_digital)
	{
		OSStatus err = AudioDeviceStop(coreaudio_device_id, direct_callback);
		if (err != kAudioHardwareNoError) {
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}
		return OP_ERROR_SUCCESS;
	}
	OSStatus err = AudioOutputUnitStop(coreaudio_audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_unpause(void)
{
        if (coreaudio_use_digital)
	{
		OSStatus err = AudioDeviceStart(coreaudio_device_id, direct_callback);
		if (err != kAudioHardwareNoError) {
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		}
		return OP_ERROR_SUCCESS;
	}
	OSStatus err = AudioOutputUnitStart(coreaudio_audio_unit);
	if (err != noErr) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int coreaudio_buffer_space(void)
{
	int space = coreaudio_ring_buffer_write_available(&coreaudio_ring_buffer);
	return space;
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
	"use_digital",
	NULL
};

const char * const op_mixer_options[] = {
	NULL
};

const int op_priority = 2;
