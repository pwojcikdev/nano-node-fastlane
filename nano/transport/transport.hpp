#pragma once

#include <boost/asio/awaitable.hpp>

#include <memory>

namespace nano
{
class message;
}

namespace nano::transport::v2
{
class channel
{
public:
	using sink_callback = std::function<void (std::unique_ptr<nano::message>)>;

public:
	virtual void send (nano::message const & message) = 0;

	virtual void start ();
	//	virtual void stop ();
};
}