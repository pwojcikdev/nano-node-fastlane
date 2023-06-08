#pragma once

#include <cstdint>
#include <string_view>

namespace nano::log
{
enum class tag : uint8_t
{
	_all = 0, // reserved

	generic,
	node,
	daemon,
	network,
	blockprocessor,
	rpc_callbacks,
	prunning,
	wallet,
};

enum class detail
{
	_all = 0, // reserved

	// blockprocessor
	block_processed,

	// network
	message_received
};
}

namespace nano
{
std::string_view to_string (nano::log::tag);
std::string_view to_string (nano::log::detail);
}
