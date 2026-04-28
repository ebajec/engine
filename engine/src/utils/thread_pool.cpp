#include "utils/thread_pool.h"

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

void ThreadPool::loop() 
{
	std::function<void(void)> task;

	for (;;) {
		std::unique_lock<std::mutex> lock(m_sync);
		--m_num_active;
		m_cv.wait(lock, [&](){
			return m_terminate || !m_queue.empty();
		});

		if (m_terminate && m_queue.empty()) return;

		task = std::move(m_queue.front());
		m_queue.pop();
		++m_num_active;
		lock.unlock();

		task();
	}
}

void ThreadPool::init(uint32_t count, const char *name)
{
	count = std::max(count, 1U);
	m_threads.resize(count);

	for (uint32_t i = 0; i < count; ++i) {
		m_threads[i] = std::thread(&ThreadPool::loop, this);

		m_map[m_threads[i].get_id()] = i;

	  std::string thread_name = std::string(name) + "_" + std::to_string(i);
#ifdef __linux__
		pthread_setname_np(m_threads[i].native_handle(), thread_name.c_str()); 
#endif
	}
}

bool ThreadPool::schedule(std::function<void(void)> &&task) 
{
	std::unique_lock<std::mutex> lock(m_sync);
	if (m_terminate)
		return false;
	m_queue.push(std::move(task));
	lock.unlock();

	m_cv.notify_one();

	return true;
}

ThreadPool::ThreadPool(uint32_t num_threads, const char *name) 
{
	init(num_threads, name);
}

ThreadPool::ThreadPool()
{
	static std::atomic_uint32_t pool_ctr;

	std::string name = "pool" + std::to_string(pool_ctr++);
	init(std::thread::hardware_concurrency(), name.c_str());
}

ThreadPool::~ThreadPool()
{
	std::unique_lock<std::mutex> lock(m_sync);
	m_terminate = true;	
	lock.unlock();
	m_cv.notify_all();

	for (std::thread& thread : m_threads) {
		if (thread.joinable())
			thread.join();
	}
}

static ThreadPool g_pool;
static ThreadPool g_block_pool;

void g_schedule_task(std::function<void(void)>&& task)
{
	g_pool.schedule(std::move(task));
}

void g_schedule_background(std::function<void(void)> &&task)
{
	g_block_pool.schedule(std::move(task));
}

