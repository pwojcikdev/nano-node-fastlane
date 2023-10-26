#pragma once

#include <nano/lib/logging_enums.hpp>
#include <nano/lib/object_stream.hpp>

#include <initializer_list>
#include <memory>
#include <shared_mutex>
#include <sstream>

#include <spdlog/spdlog.h>

namespace nano
{
spdlog::level::level_enum to_spdlog_level (nano::log::level);

class nlogger
{
public:
	nlogger ();

public:
public: // logging
	template <class... Args>
	void log (nano::log::level level, nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).log (to_spdlog_level (level), fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void debug (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).debug (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void info (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).info (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void warn (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).warn (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void error (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).error (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void critical (nano::log::type tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger (tag).critical (fmt, std::forward<Args> (args)...);
	}

public:
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

	template <typename... Args>
	void trace (nano::log::type tag, nano::log::detail detail, Args &&... args)
	{
		auto logger = get_logger (tag);

		if (!logger.should_log (spdlog::level::trace))
		{
			return;
		}

		std::stringstream ss;

		nano::object_stream obs{ ss };
		(obs.write (args.name, args.value), ...);

		logger.trace ("\"{}\" {}", to_string (detail), ss.str ());
	}

private:
	std::unordered_map<nano::log::type, std::shared_ptr<spdlog::logger>> spd_loggers;
	std::shared_mutex mutex;

private:
	spdlog::logger & get_logger (nano::log::type tag);
	std::shared_ptr<spdlog::logger> make_logger (nano::log::type tag);
};
}

namespace nano::logging
{
class config final
{
public:
public:
	nano::log::level default_level{ nano::log::level::info };

public:
	static config cli_default ();
	static config daemon_default ();
	static config tests_default ();
};

/**
 * Global initialization of logging that all loggers will use
 */
void initialize (nano::logging::config);

/**
 * Cleanly shutdown logging (flush buffers, release file handles, etc)
 */
void release ();
}