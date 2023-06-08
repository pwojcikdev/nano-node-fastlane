#include <nano/lib/logging.hpp>

#include <spdlog/cfg/env.h>

void nano::initialize_logging ()
{
	spdlog::cfg::load_env_levels ();
	spdlog::set_automatic_registration (false);
}

spdlog::logger & nano::nlogger::get_logger_impl (nano::log::tag tag)
{
	return *spd_logger;
}