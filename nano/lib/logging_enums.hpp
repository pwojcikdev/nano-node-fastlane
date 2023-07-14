#pragma once

#include <cstdint>
#include <string_view>

namespace nano::log
{
enum class level
{
	trace,
	debug,
	info,
	warn,
	error,
	critical,
	off,
};

enum class tag : uint32_t
{
	all = 0, // reserved

	generic,
	node,
	daemon,
	wallet,
	rpc,
	rpc_connection,
	rpc_callbacks,
	rpc_request,
	active_transactions,
	blockprocessor,
	network,
	tcp,
	prunning,
	bulk_pull_client,
	bulk_pull_server,
	bulk_pull_account_client,
	bulk_pull_account_server,
	bulk_push_client,
	bulk_push_server,
	frontier_req_client,
	frontier_req_server,
	bootstrap,
	bootstrap_lazy,
};

enum class detail
{
	all = 0, // reserved

	// node
	process_confirmed,

	// active_transactions
	active_started,
	active_stopped,

	// blockprocessor
	block_processed,

	// network
	message_received,

	// bulk pull/push
	pulled_block,
	sending_block,
	sending_pending,
	sending_frontier,
	requesting_account_or_head,
	requesting_pending,

};
}

namespace nano
{
std::string_view to_string (nano::log::tag);
std::string_view to_string (nano::log::detail);
}
