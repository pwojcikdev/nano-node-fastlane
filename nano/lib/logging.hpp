#pragma once

#include <nano/lib/logging_enums.hpp>
#include <nano/lib/object_stream.hpp>

#include <initializer_list>
#include <memory>
#include <sstream>

#include <spdlog/spdlog.h>

namespace nano
{
class nlogger
{
public:
	explicit nlogger (std::string name)
	{
		auto const & sinks = spdlog::default_logger ()->sinks ();
		spd_logger = std::make_shared<spdlog::logger> ("logger", sinks.begin (), sinks.end ());
		spdlog::initialize_logger (spd_logger);
	}

public:
public: // logging
	template <class... Args>
	void debug (nano::log::tag tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger_impl (tag).debug (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void info (nano::log::tag tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger_impl (tag).info (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void warn (nano::log::tag tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger_impl (tag).warn (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void error (nano::log::tag tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger_impl (tag).error (fmt, std::forward<Args> (args)...);
	}

	template <class... Args>
	void critical (nano::log::tag tag, spdlog::format_string_t<Args...> fmt, Args &&... args)
	{
		get_logger_impl (tag).critical (fmt, std::forward<Args> (args)...);
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

public: // trace
	template <class T>
	static arg<T> field (std::string_view name, T const & value)
	{
		return { name, value };
	}

	template <typename... Args>
	void trace (nano::log::tag tag, nano::log::detail detail, Args &&... args)
	{
		auto logger = get_logger_impl (tag);

		if (!logger.should_log (spdlog::level::trace))
		{
			return;
		}

		std::stringstream ss;
		nano::object_stream obs{ ss };
		(obs.write (args.name, args.value), ...);

		logger.trace ("\"{}\" {}", nano::to_string (detail), ss.str ());
	}

private:
	spdlog::logger & get_logger_impl (nano::log::tag tag);

private:
	std::shared_ptr<spdlog::logger> spd_logger;
};

void initialize_logging ();
}