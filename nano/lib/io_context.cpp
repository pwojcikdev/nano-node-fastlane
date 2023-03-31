#include <nano/lib/io_context.hpp>

nano::io_context_wrapper::io_context_wrapper (unsigned int num_threads_a, nano::thread_role::name thread_role_a) :
	num_threads{ num_threads_a },
	thread_role{ thread_role_a }
{
	io_ctx = std::make_unique<boost::asio::io_context> ();
}

nano::io_context_wrapper::~io_context_wrapper ()
{
	// Should be stopped before destruction
	debug_assert (io_ctx == nullptr);
	debug_assert (io_runner == nullptr);
}

void nano::io_context_wrapper::start ()
{
	debug_assert (io_ctx);
	debug_assert (io_runner == nullptr);

	io_runner = std::make_unique<nano::thread_runner> (*io_ctx, num_threads, thread_role);
}

void nano::io_context_wrapper::stop ()
{
	debug_assert (io_ctx);
	debug_assert (io_runner);

	io_ctx->stop ();
	io_runner = nullptr; // Stops and waits for threads running the context
	io_ctx = nullptr;
}

boost::asio::io_context & nano::io_context_wrapper::context () const
{
	release_assert (io_ctx);
	return *io_ctx;
}