#include <nano/lib/logging.hpp>
#include <nano/lib/utility.hpp>

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

/*
 * nlogger
 */

std::mutex nano::nlogger::initialization_mutex;
bool nano::nlogger::initialized{ false };

nano::nlogger::nlogger ()
{
}

void nano::nlogger::initialize (const nano::logging_config & config)
{
	std::lock_guard guard{ initialization_mutex };

	spdlog::set_automatic_registration (false);
	spdlog::set_level (to_spdlog_level (config.default_level));
	spdlog::cfg::load_env_levels ();

	//	auto logger = spdlog::basic_logger_mt ("default", "nano_log.txt");
	auto logger = spdlog::stdout_color_mt ("default");
	spdlog::set_default_logger (logger);

	initialized = true;
}

void nano::nlogger::release ()
{
}

spdlog::logger & nano::nlogger::get_logger (nano::log::type tag)
{
	// This is a two-step process to avoid locking the mutex in the common case
	{
		std::shared_lock slock{ mutex };

		if (auto it = spd_loggers.find (tag); it != spd_loggers.end ())
		{
			return *it->second;
		}
	}
	// Not found, create a new logger
	{
		std::unique_lock lock{ mutex };

		auto [it2, inserted] = spd_loggers.emplace (tag, make_logger (tag));
		return *it2->second;
	}
}

std::shared_ptr<spdlog::logger> nano::nlogger::make_logger (nano::log::type tag)
{
	std::lock_guard guard{ initialization_mutex };

	debug_assert (initialized, "logging must be initialized before using nlogger");

	auto const default_logger = spdlog::default_logger ();
	auto const & sinks = default_logger->sinks ();

	auto spd_logger = std::make_shared<spdlog::logger> (std::string{ nano::to_string (tag) }, sinks.begin (), sinks.end ());
	spdlog::initialize_logger (spd_logger);
	return spd_logger;
}

spdlog::level::level_enum nano::nlogger::to_spdlog_level (nano::log::level level)
{
	switch (level)
	{
		case nano::log::level::off:
			return spdlog::level::off;
		case nano::log::level::critical:
			return spdlog::level::critical;
		case nano::log::level::error:
			return spdlog::level::err;
		case nano::log::level::warn:
			return spdlog::level::warn;
		case nano::log::level::info:
			return spdlog::level::info;
		case nano::log::level::debug:
			return spdlog::level::debug;
		case nano::log::level::trace:
			return spdlog::level::trace;
	}
	debug_assert (false);
	return spdlog::level::off;
}

/*
 * logging_config
 */

nano::logging_config nano::logging_config::cli_default ()
{
	logging_config config;
	config.default_level = nano::log::level::critical;
	return config;
}

nano::logging_config nano::logging_config::daemon_default ()
{
	logging_config config;
	config.default_level = nano::log::level::info;
	return config;
}

nano::logging_config nano::logging_config::tests_default ()
{
	logging_config config;
	config.default_level = nano::log::level::critical;
	return config;
}