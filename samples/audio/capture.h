#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

struct ma_context;
struct ma_device;

struct CaptureFormat
{
	uint32_t sample_rate = 0; // Fs, in Hz
	uint32_t channels = 0;    // samples are interleaved, one per channel per frame
};

enum CaptureMode
{
	// what is being sent to the default output device
	CAPTURE_MODE_OUTPUT,
	// the default input device
	CAPTURE_MODE_MIC,
};

// Records audio (see CaptureMode) into a ring buffer of float samples in
// [-1, 1]. Positions count frames since init(), so a
// reader can either sample the latest window each frame or consume the stream
// without gaps by advancing its own position.
class Capture
{
	ma_context *m_context = nullptr;
	ma_device *m_device = nullptr;

	CaptureFormat m_format;

	mutable std::mutex m_mutex;
	std::vector<float> m_ring;
	uint64_t m_capacity = 0; // frames
	uint64_t m_written = 0;  // frames

	static void on_data(ma_device *device, void *out, const void *in, uint32_t frames);

	void write(const float *src, uint64_t frames);
	void copy(uint64_t start, float *dst, uint64_t frames) const;
public:
	Capture() = default;
	~Capture();

	Capture(const Capture &) = delete;
	Capture &operator=(const Capture &) = delete;

	// @param capacity frames of history to keep
	int init(CaptureMode mode = CAPTURE_MODE_OUTPUT, uint32_t capacity = 1 << 16);
	void destroy();

	const CaptureFormat &format() const { return m_format; }
	uint64_t capacity() const { return m_capacity; }

	// Frames captured since init().
	uint64_t position() const;

	// Copies frames [start, start + out.size()/channels). Fails if any of them
	// have not been captured yet or have already been overwritten.
	bool read(uint64_t start, std::span<float> out) const;

	// Copies the most recent out.size()/channels frames, zero padded at the
	// front if fewer have been captured, and returns the position just past
	// the last one.
	uint64_t read_latest(std::span<float> out) const;
};

#endif // AUDIO_CAPTURE_H
