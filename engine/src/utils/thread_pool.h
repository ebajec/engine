#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <functional>
#include <cstdint>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>

class ThreadPool
{
	bool m_terminate = false;
	uint32_t m_num_active = 0;

	std::vector<std::thread> m_threads;

	std::mutex m_sync;
	std::condition_variable m_cv;

	std::queue<std::function<void(void)>> m_queue;
	std::unordered_map<std::thread::id, uint32_t> m_map;

	void loop(); 
	void init(uint32_t count, const char *name);
public:
	bool schedule(std::function<void(void)> &&task); 

	/// @return index of given thread in pool, or UINT32_MAX if not present 
	uint32_t get_pool_index(std::thread::id id) {
		auto it = m_map.find(id);
		return it == m_map.end() ? UINT32_MAX : it->second;
	}

	ThreadPool(); 
	ThreadPool(uint32_t num_threads, const char *name); 
	~ThreadPool();
};

extern void g_schedule_task(std::function<void(void)> &&task);
extern void g_schedule_background(std::function<void(void)> &&task);

#endif // THREAD_POOL_H
