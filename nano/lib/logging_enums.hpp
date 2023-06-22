#pragma once

namespace nano::log
{
enum class type
{
	all = 0,

	generic,
	node,
	daemon,
	network,
	blockprocessor,
};

enum class detail
{
	all = 0,

	// network
	message_received,

	// blockprocessor
	block_processed
};
}
