#pragma once

#include <boost/asio/awaitable.hpp>

#include <memory>

namespace nano
{
class message;
}

namespace nano::transport::v2
{
using message_ptr = std::unique_ptr<nano::message>;

class socket
{
public:
	virtual boost::asio::awaitable<message_ptr> receive () = 0;
	virtual void send (message_ptr message) = 0;
	virtual boost::asio::awaitable<void> run () = 0;
};
}