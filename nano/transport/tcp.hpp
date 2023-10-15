#include <nano/messages/messages.hpp>
#include <nano/transport/transport.hpp>

#include <boost/asio.hpp> // TODO: Remove once code is moved into cpp
#include <boost/asio/ip/tcp.hpp>

#include <queue>

namespace nano::transport::v2::tcp
{
class tcp_socket : nano::transport::v2::socket
{
public:
	tcp_socket (boost::asio::io_context & io_ctx, boost::asio::ip::tcp::socket socket_a) :
		socket{ std::move (socket_a) },
		send_timer{ io_ctx }
	{
	}

	boost::asio::awaitable<message_ptr> receive () override
	{
		char buffer[8];

		auto result = co_await boost::asio::async_read (socket, boost::asio::buffer (buffer), boost::asio::use_awaitable);

		std::cout << "socket received: " << result << std::endl;

		co_return nullptr;
	}

	void send (message_ptr message) override
	{
		std::lock_guard guard{ send_mutex };
		send_queue.push (std::move (message));
	}

	boost::asio::awaitable<void> run () override
	{
		while (true)
		{
			co_await send_next ();
		}
	}

	boost::asio::awaitable<void> send_next ()
	{
		if (auto msg = try_pop ())
		{
			auto buffer = (*msg)->to_bytes ();
			auto result = co_await boost::asio::async_write (socket, boost::asio::buffer (*buffer), boost::asio::use_awaitable);

			std::cout << "socket sent: " << result << std::endl;
		}
		else
		{
			send_timer.expires_from_now (100ms);
			co_await send_timer.async_wait (boost::asio::use_awaitable);
		}
	}

	std::optional<message_ptr> try_pop ()
	{
		std::lock_guard guard{ send_mutex };
		if (!send_queue.empty ())
		{
			auto message = std::move (send_queue.front ());
			send_queue.pop ();
			return message;
		}
		else
		{
			return std::nullopt;
		}
	}

private:
	boost::asio::ip::tcp::socket socket;

private:
	std::mutex mutable send_mutex;
	std::queue<message_ptr> send_queue;
	boost::asio::steady_timer send_timer;
};
}