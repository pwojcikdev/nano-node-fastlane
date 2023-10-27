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
class logging_config final
{
public:
public:
	nano::log::level default_level{ nano::log::level::info };

public:
	static logging_config cli_default ();
	static logging_config daemon_default ();
	static logging_config tests_default ();
};

class nlogger final
{
public:
	nlogger ();

public:
	static void initialize (nano::logging_config const &);
	static void release ();

private:
	static bool initialized;
	static std::mutex initialization_mutex;

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

	static spdlog::level::level_enum to_spdlog_level (nano::log::level);
};
}

namespace nano
{
class logging_interval final
{
public:
	explicit logging_interval (std::chrono::seconds target_a) :
		target{ target_a }
	{
	}

	bool should_log ()
	{
		std::lock_guard guard{ mutex };

		auto now = std::chrono::steady_clock::now ();
		if (now - last_log > target)
		{
			last_log = now;
			return true;
		}
		return false;
	}

private:
	const std::chrono::seconds target;
	std::chrono::steady_clock::time_point last_log;
	mutable std::mutex mutex;
};
}