#include "utils/thread_pool.h"
#include "utils/thread_pool.h"

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
//#include <atomic>

struct ThreadPool
{
	bool m_terminate = false;
	uint32_t m_num_active = 0;

	std::vector<std::thread> m_threads;

	std::mutex m_sync;
	std::condition_variable m_cv;

	std::queue<std::function<void(void)>> m_queue;

	void thread_func() 
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
public:
	bool schedule(std::function<void(void)> &&task) 
	{
		std::unique_lock<std::mutex> lock(m_sync);
		if (m_terminate)
			return false;
		m_queue.push(std::move(task));
		lock.unlock();

		m_cv.notify_one();

		return true;
	}

	ThreadPool() 
	{
		size_t count = (size_t)std::max(std::thread::hardware_concurrency(),1U);
		m_threads.resize(count);

		for (size_t i = 0; i < count; ++i) {
			m_threads[i] = std::thread(&ThreadPool::thread_func, this);
		}
	}

	~ThreadPool()
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
};

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

