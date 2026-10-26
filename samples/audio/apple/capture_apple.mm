#include "../capture.h"

#include <ev2/context.h>
#include <ev2/utils/log.h>

#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>

struct Capture::AppleCapture
{
	Capture *owner = nullptr;
	AudioObjectID tap = kAudioObjectUnknown;
	AudioObjectID device = kAudioObjectUnknown;
	AudioDeviceIOProcID proc = nullptr;
	bool started = false;
	std::array<float, 1024> interleaved;

	~AppleCapture()
	{
		if (device != kAudioObjectUnknown) {
			if (proc) {
				if (started)
					AudioDeviceStop(device, proc);
				AudioDeviceDestroyIOProcID(device, proc);
			}
			AudioHardwareDestroyAggregateDevice(device);
		}
		if (@available(macOS 14.2, *)) {
			if (tap != kAudioObjectUnknown)
				AudioHardwareDestroyProcessTap(tap);
		}
	}

	static OSStatus on_data(
		AudioObjectID, const AudioTimeStamp *, const AudioBufferList *input,
		const AudioTimeStamp *, AudioBufferList *output, const AudioTimeStamp *, void *user
	)
	{
		if (output) {
			for (UInt32 i = 0; i < output->mNumberBuffers; ++i) {
				const AudioBuffer &buffer = output->mBuffers[i];
				if (buffer.mData)
					memset(buffer.mData, 0, buffer.mDataByteSize);
			}
		}
		if (!input || !input->mNumberBuffers)
			return noErr;

		auto *state = static_cast<AppleCapture *>(user);
		const AudioBuffer &first = input->mBuffers[0];
		if (!first.mData)
			return noErr;

		// The stereo tap can deliver interleaved or separate channel buffers.
		if (input->mNumberBuffers == 1 && first.mNumberChannels == 2) {
			state->owner->write(static_cast<const float *>(first.mData),
				first.mDataByteSize / (2 * sizeof(float)));
		} else if (input->mNumberBuffers == 2 && first.mNumberChannels == 1) {
			const AudioBuffer &second = input->mBuffers[1];
			if (!second.mData || second.mNumberChannels != 1)
				return noErr;

			const auto *left = static_cast<const float *>(first.mData);
			const auto *right = static_cast<const float *>(second.mData);
			const size_t frames = std::min(first.mDataByteSize, second.mDataByteSize) / sizeof(float);
			for (size_t offset = 0; offset < frames;) {
				const size_t count = std::min(frames - offset, state->interleaved.size() / 2);
				for (size_t i = 0; i < count; ++i) {
					state->interleaved[2 * i] = left[offset + i];
					state->interleaved[2 * i + 1] = right[offset + i];
				}
				state->owner->write(state->interleaved.data(), count);
				offset += count;
			}
		}
		return noErr;
	}
};

int Capture::init_output_apple(uint32_t capacity)
{
	if (@available(macOS 14.2, *)) {
		@autoreleasepool {
			m_apple = new AppleCapture;
			m_apple->owner = this;
			auto fail = [this](const char *operation, OSStatus status) {
				log_error("Capture: %s (CoreAudio status %d)", operation, (int)status);
				destroy();
				return ev2::EINIT_FAILED;
			};

			CATapDescription *description = [[CATapDescription alloc]
				initStereoGlobalTapButExcludeProcesses:@[]];
			description.name = @"ev2 System Audio";
			description.privateTap = YES;
			description.muteBehavior = CATapUnmuted;

			OSStatus status = AudioHardwareCreateProcessTap(description, &m_apple->tap);
			if (status == noErr && m_apple->tap == kAudioObjectUnknown)
				status = kAudioHardwareBadObjectError;
			if (status != noErr)
				return fail("failed to create system audio tap", status);

			AudioStreamBasicDescription format = {};
			AudioObjectPropertyAddress address = {
				kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
			};
			UInt32 size;
			// The HAL may not publish the new tap's properties immediately.
			for (int attempt = 0; attempt < 10; ++attempt) {
				size = sizeof(format);
				status = AudioObjectGetPropertyData(m_apple->tap, &address, 0, nullptr, &size, &format);
				if (status != kAudioHardwareBadObjectError && status != kAudioHardwareNotReadyError)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			if (status != noErr)
				return fail("failed to read system audio format", status);
			if (format.mFormatID != kAudioFormatLinearPCM ||
				!(format.mFormatFlags & kAudioFormatFlagIsFloat) ||
				(format.mFormatFlags & kAudioFormatFlagIsBigEndian) ||
				format.mBitsPerChannel != 32 || format.mChannelsPerFrame != 2 ||
				format.mBytesPerFrame != sizeof(float) *
					((format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) ? 1 : 2) ||
				!(format.mSampleRate > 0 && format.mSampleRate <= UINT32_MAX)) {
				return fail("unsupported system audio format", kAudioDeviceUnsupportedFormatError);
			}

			// A tap-only aggregate avoids opening a hardware microphone input.
			NSDictionary *aggregate = @{
				@kAudioAggregateDeviceNameKey: @"ev2 System Audio",
				@kAudioAggregateDeviceUIDKey: [NSUUID UUID].UUIDString,
				@kAudioAggregateDeviceIsPrivateKey: @YES,
				@kAudioAggregateDeviceTapAutoStartKey: @YES,
				@kAudioAggregateDeviceTapListKey: @[@{
					@kAudioSubTapUIDKey: description.UUID.UUIDString,
					@kAudioSubTapDriftCompensationKey: @YES,
				}],
			};
			status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggregate, &m_apple->device);
			if (status == noErr && m_apple->device == kAudioObjectUnknown)
				status = kAudioHardwareBadObjectError;
			if (status != noErr)
				return fail("failed to create system audio capture device", status);

			// Aggregate devices become ready asynchronously.
			address.mSelector = kAudioDevicePropertyDeviceIsAlive;
			UInt32 alive = 0;
			for (int attempt = 0; attempt < 300; ++attempt) {
				alive = 0;
				size = sizeof(alive);
				status = AudioObjectGetPropertyData(m_apple->device, &address, 0, nullptr, &size, &alive);
				if (status != noErr && status != kAudioHardwareBadObjectError && status != kAudioHardwareNotReadyError)
					return fail("failed to query capture device readiness", status);
				if (status == noErr && alive)
					break;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			if (!alive)
				return fail("system audio capture device did not become ready", kAudioHardwareNotReadyError);

			m_format.sample_rate = (uint32_t)format.mSampleRate;
			m_format.channels = format.mChannelsPerFrame;
			m_capacity = capacity;
			m_written = 0;
			m_ring.assign(m_capacity * m_format.channels, 0.f);

			status = AudioDeviceCreateIOProcID(m_apple->device, AppleCapture::on_data, m_apple, &m_apple->proc);
			if (status != noErr)
				return fail("failed to create system audio callback", status);
			status = AudioDeviceStart(m_apple->device, m_apple->proc);
			if (status != noErr)
				return fail("failed to start capture; allow System Audio Recording in System Settings > Privacy & Security", status);
			m_apple->started = true;

			log_info("Capture: system audio, %u Hz, %u channels", m_format.sample_rate, m_format.channels);
			return ev2::SUCCESS;
		}
	} else {
		log_error("Capture: system audio capture requires macOS 14.2 or later");
		return ev2::EINIT_FAILED;
	}
}

void Capture::destroy_output_apple()
{
	delete m_apple;
	m_apple = nullptr;
}
