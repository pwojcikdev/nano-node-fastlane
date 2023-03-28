#include <nano/boost/asio/post.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/threading.hpp>

#include <boost/format.hpp>

#include <future>
#include <iostream>
#include <thread>

/*
 * thread_attributes
 */

boost::thread::attributes nano::thread_attributes::get_default ()
{
	boost::thread::attributes attrs;
	attrs.set_stack_size (8000000); // 8MB
	return attrs;
}

/*
 * thread_runner
 */

nano::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned num_threads_a, const nano::thread_role::name thread_role_a) :
	io_ctx{ io_ctx_a },
	num_threads{ num_threads_a },
	role{ thread_role_a },
	io_guard{ boost::asio::make_work_guard (io_ctx_a) }
{
}

nano::thread_runner::~thread_runner ()
{
	// All threads must be stopped before destruction
	debug_assert (threads.empty ());
}

void nano::thread_runner::start ()
{
	for (auto i (0u); i < num_threads; ++i)
	{
		threads.emplace_back (nano::thread_attributes::get_default (), [this] () {
			nano::thread_role::set (role);

			// In a release build, catch and swallow any exceptions,
			// In debug mode let if fall through

#ifndef NDEBUG
			run ();
#else
			try
			{
				run ();
			}
			catch (std::exception const & ex)
			{
				std::cerr << ex.what () << std::endl;
			}
			catch (...)
			{
			}
#endif
		});
	}
}

void nano::thread_runner::stop ()
{
	debug_assert (io_ctx.stopped ()); // Context should be stopped before the runner

	io_guard.reset ();
	for (auto & thread : threads)
	{
		nano::join_or_pass (thread);
	}
	threads.clear ();
}

void nano::thread_runner::join ()
{
	io_guard.reset ();
	for (auto & thread : threads)
	{
		nano::join_or_pass (thread);
	}
	threads.clear ();
}

void nano::thread_runner::run ()
{
#if NANO_ASIO_HANDLER_TRACKING == 0
	io_ctx.run ();
#else
	nano::timer<> timer;
	timer.start ();
	while (true)
	{
		timer.restart ();
		// Run at most 1 completion handler and record the time it took to complete (non-blocking)
		auto count = io_ctx.poll_one ();
		if (count == 1 && timer.since_start ().count () >= NANO_ASIO_HANDLER_TRACKING)
		{
			auto timestamp = std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
			std::cout << (boost::format ("[%1%] io_thread held for %2%ms") % timestamp % timer.since_start ().count ()).str () << std::endl;
		}
		// Sleep for a bit to give more time slices to other threads
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
		std::this_thread::yield ();
	}
#endif
}

/*
 * thread_pool
 */

nano::thread_pool::thread_pool (unsigned num_threads, nano::thread_role::name thread_name) :
	num_threads (num_threads),
	thread_pool_m (std::make_unique<boost::asio::thread_pool> (num_threads))
{
	set_thread_names (num_threads, thread_name);
}

nano::thread_pool::~thread_pool ()
{
	stop ();
}

void nano::thread_pool::stop ()
{
	nano::unique_lock<nano::mutex> lk (mutex);
	if (!stopped)
	{
		stopped = true;
#if defined(BOOST_ASIO_HAS_IOCP)
		// A hack needed for Windows to prevent deadlock during destruction, described here: https://github.com/chriskohlhoff/asio/issues/431
		boost::asio::use_service<boost::asio::detail::win_iocp_io_context> (*thread_pool_m).stop ();
#endif
		lk.unlock ();
		thread_pool_m->stop ();
		thread_pool_m->join ();
		lk.lock ();
		thread_pool_m = nullptr;
	}
}

void nano::thread_pool::push_task (std::function<void ()> task)
{
	++num_tasks;
	nano::lock_guard<nano::mutex> guard (mutex);
	if (!stopped)
	{
		boost::asio::post (*thread_pool_m, [this, task] () {
			task ();
			--num_tasks;
		});
	}
}

void nano::thread_pool::add_timed_task (std::chrono::steady_clock::time_point const & expiry_time, std::function<void ()> task)
{
	nano::lock_guard<nano::mutex> guard (mutex);
	if (!stopped && thread_pool_m)
	{
		auto timer = std::make_shared<boost::asio::steady_timer> (thread_pool_m->get_executor (), expiry_time);
		timer->async_wait ([this, task, timer] (boost::system::error_code const & ec) {
			if (!ec)
			{
				push_task (task);
			}
		});
	}
}

unsigned nano::thread_pool::get_num_threads () const
{
	return num_threads;
}

uint64_t nano::thread_pool::num_queued_tasks () const
{
	return num_tasks;
}

// Set the names of all the threads in the thread pool for easier identification
void nano::thread_pool::set_thread_names (unsigned num_threads, nano::thread_role::name thread_name)
{
	std::vector<std::promise<void>> promises (num_threads);
	std::vector<std::future<void>> futures;
	futures.reserve (num_threads);
	std::transform (promises.begin (), promises.end (), std::back_inserter (futures), [] (auto & promise) {
		return promise.get_future ();
	});

	for (auto i = 0u; i < num_threads; ++i)
	{
		boost::asio::post (*thread_pool_m, [&promise = promises[i], thread_name] () {
			nano::thread_role::set (thread_name);
			promise.set_value ();
		});
	}

	// Wait until all threads have finished
	for (auto & future : futures)
	{
		future.wait ();
	}
}

std::unique_ptr<nano::container_info_component> nano::collect_container_info (thread_pool & thread_pool, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "count", thread_pool.num_queued_tasks (), sizeof (std::function<void ()>) }));
	return composite;
}

unsigned int nano::hardware_concurrency ()
{
	// Try to read overridden value from environment variable
	static int value = nano::get_env_int_or_default ("NANO_HARDWARE_CONCURRENCY", 0);
	if (value <= 0)
	{
		// Not present or invalid, use default
		return std::thread::hardware_concurrency ();
	}
	return value;
}
