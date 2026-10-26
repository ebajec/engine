#include "capture.h"

#include <ev2/context.h>
#include <ev2/utils/log.h>

#include <miniaudio.h>

#include <algorithm>
#include <cassert>
#include <cstring>

Capture::~Capture()
{
	destroy();
}

int Capture::init(CaptureMode mode, uint32_t capacity)
{
	destroy();

	if (!capacity) {
		log_error("Capture: buffer capacity must be nonzero");
		return ev2::EINIT_FAILED;
	}

#if defined(__APPLE__)
	if (mode == CAPTURE_MODE_OUTPUT)
		return init_output_apple(capacity);
#endif

	ma_context_config context_config = ma_context_config_init();
	context_config.pulse.pApplicationName = "ev2";

#if defined(_WIN32)
	const ma_backend backend = ma_backend_wasapi;
#elif defined(__APPLE__)
	const ma_backend backend = ma_backend_coreaudio;
#else
	const ma_backend backend = ma_backend_pulseaudio;
#endif

	// A null device id is the default input device.
	ma_device_config config = ma_device_config_init(ma_device_type_capture);
#if !defined(_WIN32) && !defined(__APPLE__)
	ma_device_id device_id = {};
#endif

	// Windows uses WASAPI loopback; Linux uses the default sink's monitor.
	// macOS output capture is handled by a Core Audio tap above.
	if (mode == CAPTURE_MODE_OUTPUT) {
#if defined(_WIN32)
		config = ma_device_config_init(ma_device_type_loopback);
#elif !defined(__APPLE__)
		strncpy(device_id.pulse, "@DEFAULT_MONITOR@", sizeof(device_id.pulse) - 1);
		config.capture.pDeviceID = &device_id;
#endif
	}

	// 0 keeps the device's native channel count and sample rate, so nothing
	// is resampled.
	config.capture.format = ma_format_f32;
	config.capture.channels = 0;
	config.sampleRate = 0;
	config.dataCallback = on_data;
	config.pUserData = this;

	m_context = new ma_context;
	if (ma_context_init(&backend, 1, &context_config, m_context) != MA_SUCCESS) {
		delete m_context;
		m_context = nullptr;
		log_error("Capture: failed to initialize audio backend");
		return ev2::EINIT_FAILED;
	}

	m_device = new ma_device;
	if (ma_device_init(m_context, &config, m_device) != MA_SUCCESS) {
		delete m_device;
		m_device = nullptr;
		destroy();
		log_error("Capture: failed to open capture device");
		return ev2::EINIT_FAILED;
	}

	m_format.sample_rate = m_device->sampleRate;
	m_format.channels = m_device->capture.channels;

	m_capacity = capacity;
	m_written = 0;
	m_ring.assign(m_capacity * m_format.channels, 0);

	if (ma_device_start(m_device) != MA_SUCCESS) {
		destroy();
		log_error("Capture: failed to start capture device");
		return ev2::EINIT_FAILED;
	}

	log_info("Capture: '%s', %u Hz, %u channels",
		m_device->capture.name, m_format.sample_rate, m_format.channels);

	return ev2::SUCCESS;
}

void Capture::destroy()
{
#if defined(__APPLE__)
	destroy_output_apple();
#endif

	if (m_device) {
		ma_device_uninit(m_device);
		delete m_device;
		m_device = nullptr;
	}
	if (m_context) {
		ma_context_uninit(m_context);
		delete m_context;
		m_context = nullptr;
	}
	m_format = {};
	m_ring.clear();
	m_capacity = 0;
	m_written = 0;
}

void Capture::on_data(ma_device *device, void *out, const void *in, uint32_t frames)
{
	(void)out;
	static_cast<Capture *>(device->pUserData)->write(
		static_cast<const float *>(in), frames);
}

void Capture::write(const float *src, uint64_t frames)
{
	std::lock_guard lock(m_mutex);

	const uint64_t C = m_format.channels;

	// Only the last m_capacity frames would survive anyway.
	if (frames > m_capacity) {
		src += (frames - m_capacity) * C;
		m_written += frames - m_capacity;
		frames = m_capacity;
	}

	const uint64_t begin = m_written % m_capacity;
	const uint64_t n0 = std::min(frames, m_capacity - begin);

	memcpy(m_ring.data() + begin * C, src, n0 * C * sizeof(float));
	memcpy(m_ring.data(), src + n0 * C, (frames - n0) * C * sizeof(float));

	m_written += frames;
}

// Caller holds m_mutex and has checked [start, start + frames) is in the ring.
void Capture::copy(uint64_t start, float *dst, uint64_t frames) const
{
	const uint64_t C = m_format.channels;

	const uint64_t begin = start % m_capacity;
	const uint64_t n0 = std::min(frames, m_capacity - begin);

	memcpy(dst, m_ring.data() + begin * C, n0 * C * sizeof(float));
	memcpy(dst + n0 * C, m_ring.data(), (frames - n0) * C * sizeof(float));
}

uint64_t Capture::position() const
{
	std::lock_guard lock(m_mutex);
	return m_written;
}

bool Capture::read(uint64_t start, std::span<float> out) const
{
	if (!m_format.channels)
		return false;

	const uint64_t frames = out.size() / m_format.channels;

	std::lock_guard lock(m_mutex);

	if (start + frames > m_written || m_written - start > m_capacity)
		return false;

	copy(start, out.data(), frames);
	return true;
}

uint64_t Capture::read_latest(std::span<float> out) const
{
	if (!m_format.channels)
		return 0;

	const uint64_t C = m_format.channels;
	const uint64_t frames = out.size() / C;
	assert(frames <= m_capacity);

	std::lock_guard lock(m_mutex);

	const uint64_t avail = std::min(frames, m_written);
	const uint64_t pad = frames - avail;

	std::fill_n(out.data(), pad * C, 0.f);
	copy(m_written - avail, out.data() + pad * C, avail);

	return m_written;
}
