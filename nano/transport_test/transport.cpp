#include <nano/test_common/testutil.hpp>
#include <nano/transport/tcp.hpp>
#include <nano/transport/transport.hpp>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <future>
#include <thread>

TEST (transport, basic)
{
	boost::asio::io_context io_ctx;

	std::thread ctx_thread{ [&io_ctx] () {
		io_ctx.run ();
	} };

	{
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{ io_ctx.get_executor () };

		// Create two sockets, one for each end of the connection, use only boost standard code
		boost::asio::ip::tcp::socket socket1{ io_ctx };
		boost::asio::ip::tcp::socket socket2{ io_ctx };
		// Make socket1 listen on ephermeral port, and socket2 connect to it
		boost::asio::ip::tcp::acceptor acceptor{ io_ctx, boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v4::any (), 0 }, /* reuse address */ true };

		std::thread server_thread{ [&acceptor, &socket2] () {
			acceptor.accept (socket2);
		} };

		std::thread client_thread{ [&socket1, &acceptor] () {
			socket1.connect (acceptor.local_endpoint ());
		} };

		server_thread.join ();
		client_thread.join ();

		std::cout << "done connecting" << std::endl;

		nano::transport::v2::tcp::tcp_socket nano_socket_1{ io_ctx, std::move (socket1) };
		nano::transport::v2::tcp::tcp_socket nano_socket_2{ io_ctx, std::move (socket2) };

		auto receive_done = boost::asio::co_spawn (
		io_ctx,
		[&nano_socket_1] () mutable -> boost::asio::awaitable<void> {
			auto msg = co_await nano_socket_1.receive ();

			std::cout << "received" << std::endl;
		},
		boost::asio::use_future);

		auto run_done = boost::asio::co_spawn (
		io_ctx,
		[&nano_socket_2] () mutable -> boost::asio::awaitable<void> {
			co_await nano_socket_2.run ();
		},
		boost::asio::use_future);

		nano::keepalive message{ nano::dev::network_params.network };
		nano_socket_2.send (std::make_unique<nano::keepalive> (message));

		std::cout << "sent" << std::endl;

		receive_done.wait ();

		std::cout << "done message" << std::endl;
	}

	ctx_thread.join ();
}