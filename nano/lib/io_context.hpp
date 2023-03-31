#pragma once

#include <nano/boost/asio/io_context.hpp>
#include <nano/lib/threading.hpp>

#include <memory>

namespace nano
{
/**
 * Encapsulates `boost::asio::io_context` and manages associated thread pool.
 * NOTE: There is no additional locking performed internally when creating/destroying `io_context` as it's mean to be called from within `node::start()/stop()` when no other threads are running, therefore this class should be the first one that is started and the last one that is stopped.
 */
class io_context_wrapper
{
public:
	io_context_wrapper (unsigned num_threads, nano::thread_role::name thread_role);
	~io_context_wrapper ();

	/**
	 * Creates the underlying `io_context` and starts threads driving it
	 */
	void start ();

	/**
	 * Stops all threads and destroys the underlying `io_context`.
	 * All the components using a reference to the `io_context` should be stopped beforehand.
	 */
	void stop ();

	/**
	 * Get the underlying `io_context` for doing the actual IO
	 */
	boost::asio::io_context & context () const;

private:
	unsigned const num_threads;
	nano::thread_role::name const thread_role;

	/// Keep `io_context` as unique_ptr, so we can control its lifetime
	std::unique_ptr<boost::asio::io_context> io_ctx;
	std::unique_ptr<nano::thread_runner> io_runner;
};
}