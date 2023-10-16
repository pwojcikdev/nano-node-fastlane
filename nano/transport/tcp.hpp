#include <nano/messages/messages.hpp>
#include <nano/transport/transport.hpp>

#include <boost/asio.hpp> // TODO: Remove once code is moved into cpp
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <future>
#include <queue>

namespace nano::transport::v2::tcp
{
/*
 * Lean wrapper around socket that provides async send/receive operations for nano messages
 */
class tcp_socket
{
public:
	explicit tcp_socket (boost::asio::ip::tcp::socket socket_a) :
		socket{ std::move (socket_a) },
		remote_endpoint{ socket.remote_endpoint () },
		local_endpoint{ socket.local_endpoint () }
	{
	}

public:
	boost::asio::awaitable<std::unique_ptr<nano::message>> receive ()
	{
		nano::message_deserializer deserializer;

		uint8_t buffer[8]; // TODO: Replace with vector buffer
		auto result = co_await boost::asio::async_read (socket, boost::asio::buffer (buffer), boost::asio::use_awaitable);

		nano::bufferstream header_stream{ buffer, sizeof (buffer) };
		auto header = deserializer.deserialize_header (header_stream);

		std::cout << "socket received: " << result << std::endl;

		co_return nullptr;
	}

	boost::asio::awaitable<void> send (nano::message const & message)
	{
		auto buffer = message.to_bytes ();
		auto result = co_await boost::asio::async_write (socket, boost::asio::buffer (*buffer), boost::asio::use_awaitable);

		std::cout << "socket sent: " << result << std::endl;
	}

public:
	boost::asio::any_io_executor get_executor ()
	{
		return socket.get_executor ();
	}

private:
	boost::asio::ip::tcp::socket socket;

public:
	using endpoint_type = decltype (socket)::endpoint_type;

	// Cached endpoints
	const endpoint_type remote_endpoint;
	const endpoint_type local_endpoint;
};

/*
 * Manages traffic over a single socket
 */
class tcp_channel : nano::transport::v2::channel, std::enable_shared_from_this<tcp_channel>
{
public:
	explicit tcp_channel (std::shared_ptr<nano::transport::v2::tcp::tcp_socket> socket_a) :
		socket{ std::move (socket_a) }
	{
	}

	void start () override
	{
		sending_coro = boost::asio::co_spawn (
		socket->get_executor (), [this_l = shared_from_this ()] () mutable -> boost::asio::awaitable<void> {
			co_await this_l->run_sending ();
		},
		boost::asio::use_awaitable);

		receiving_coro = boost::asio::co_spawn (
		socket->get_executor (), [this_l = shared_from_this ()] () mutable -> boost::asio::awaitable<void> {
			co_await this_l->run_receiving ();
		},
		boost::asio::use_awaitable);
	}

	boost::asio::awaitable<void> stop ()
	{
		debug_assert (!stopping);
		debug_assert (!stopped);

		stopping = true;

		co_await std::move (sending_coro);
		co_await std::move (receiving_coro);

		stopped = true;
	}

	void send (nano::message const & message) override
	{
	}

private:
	boost::asio::awaitable<void> sending_coro;
	boost::asio::awaitable<void> receiving_coro;
	boost::asio::awaitable<void> watchdog_coro;

	std::atomic<bool> stopping{ false };
	std::atomic<bool> stopped{ false };

private: // Sending
	boost::asio::awaitable<void> run_sending ()
	{
		boost::asio::cancellation_state cs = co_await boost::asio::this_coro::cancellation_state;

		while (!cs.cancelled ())
		{
			//			co_await send_next ();
		}
	}

	boost::asio::awaitable<void> run_receiving ()
	{
		while (true)
		{
		}
	}

	//	boost::asio::awaitable<void> send (message_ptr message)
	//	{
	//		std::lock_guard guard{ send_mutex };
	//		send_queue.push (std::move (message));
	//	}

	//	boost::asio::awaitable<void> send_next ()
	//	{
	//		if (auto msg = try_pop ())
	//		{
	//			auto buffer = (*msg)->to_bytes ();
	//			auto result = co_await boost::asio::async_write (socket, boost::asio::buffer (*buffer), boost::asio::use_awaitable);
	//
	//			std::cout << "socket sent: " << result << std::endl;
	//		}
	//		else
	//		{
	//			send_timer.expires_from_now (100ms);
	//			co_await send_timer.async_wait (boost::asio::use_awaitable);
	//		}
	//	}
	//
	//	std::optional<message_ptr> try_pop ()
	//	{
	//		std::lock_guard guard{ send_mutex };
	//		if (!send_queue.empty ())
	//		{
	//			auto message = std::move (send_queue.front ());
	//			send_queue.pop ();
	//			return message;
	//		}
	//		else
	//		{
	//			return std::nullopt;
	//		}
	//	}

public:
	using endpoint_type = nano::transport::v2::tcp::tcp_socket::endpoint_type;

	endpoint_type remote_endpoint () const noexcept
	{
		return socket->remote_endpoint;
	}

	endpoint_type local_endpoint () const noexcept
	{
		return socket->local_endpoint;
	}

private:
	std::shared_ptr<nano::transport::v2::tcp::tcp_socket> socket;
};
}