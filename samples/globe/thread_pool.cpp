#include "thread_pool.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

class ThreadPool
{
	size_t m_count;
	std::vector<std::thread> m_threads;

	std::mutex m_sync;
	std::condition_variable m_cv;

	std::atomic_bool m_terminate;

	std::queue<std::function<void(void)>> m_queue;

	void thread_func() 
	{
		std::unique_lock<std::mutex> lock(m_sync, std::defer_lock);
		while (!m_terminate) {

			m_cv.wait(lock, [&](){
				return m_terminate || !m_queue.empty();
			});
		}
	}

	void enqueue(std::function<void(void)> &&task) 
	{

	}

	ThreadPool() 
	{
		size_t count = std::thread::hardware_concurrency();
		m_count = count;
		m_threads.resize(count);

		for (size_t i = 0; i < count; ++i) {
			m_threads[i] = std::thread(&ThreadPool::thread_func, this);
		}
	}

	~ThreadPool()
	{
		m_terminate = true;	
	}
};

void g_schedule_task(std::function<void(void*)>&& task)
{

}

