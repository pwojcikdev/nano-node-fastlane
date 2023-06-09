#pragma once

#include <nano/lib/object_stream.hpp>

#include <initializer_list>
#include <memory>
#include <sstream>

#include <spdlog/spdlog.h>

namespace nano::log
{
enum class type
{
	_all = 0,
	generic,
	node,
	network,
	messages,
};
}

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
	template <class T>
	struct arg
	{
		std::string_view name;
		T const & value;

		arg (std::string_view name_a, T const & value_a) :
			name{ name_a },
			value{ value_a }
		{
		}
	};

public:
	template <class T>
	static arg<T> field (std::string_view name, T const & value)
	{
		return { name, value };
	}

public:
	template <typename... Args>
	void trace (std::string_view message, Args &&... args)
	{
		if (!spd_logger->should_log (spdlog::level::trace))
		{
			return;
		}

		std::stringstream ss;
		nano::object_stream obs{ ss };
		(obs.write (args.name, args.value), ...);

		spd_logger->trace ("\"{}\" {}", message, ss.str ());
	}

private:
	std::shared_ptr<spdlog::logger> spd_logger;
};

void initialize_logging ();
}