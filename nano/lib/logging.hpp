#pragma once

#include <nano/lib/object_stream.hpp>

#include <initializer_list>
#include <memory>

#include <spdlog/spdlog.h>

namespace nano
{
enum class logtag
{
	all = 0,
	generic,

	timing,
	lifetime_tracking,
	rpc_callback,
	ledger,
	ledger_rollback,
	network_messages,
	network_handshake,
};

class nlogger
{
public:
	explicit nlogger (std::string name)
	{
		auto const & sinks = spdlog::default_logger ()->sinks ();
		spd_logger = std::make_shared<spdlog::logger> (name, sinks.begin (), sinks.end ());
		spdlog::initialize_logger (spd_logger);
	}

public:
	template <class... Args>
	void debug (spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		spd_logger->debug (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void info (spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		spd_logger->info (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void warn (spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		spd_logger->warn (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void error (spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		spd_logger->error (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void critical (spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		spd_logger->critical (fmt, std::forward<Args> (args)...);
	}

private:
	std::shared_ptr<spdlog::logger> spd_logger;
};

void initialize_logging ();
}