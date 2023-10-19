#pragma once

#include <nano/lib/errors.hpp>

namespace nano
{
enum class error_messages
{
	generic = 1,
	invalid_header,
	invalid_network,
	invalid_type,
	outdated_version,
	size_too_large,
};
}

REGISTER_ERROR_CODES (nano, error_messages);