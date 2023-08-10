#include <nano/lib/logging_enums.hpp>

#define MAGIC_ENUM_RANGE_MIN 0
#define MAGIC_ENUM_RANGE_MAX 256

#include <magic_enum.hpp>

std::string_view nano::to_string (nano::log::tag tag)
{
	return magic_enum::enum_name (tag);
}

std::string_view nano::to_string (nano::log::detail detail)
{
	return magic_enum::enum_name (detail);
}