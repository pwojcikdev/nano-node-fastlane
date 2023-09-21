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

enum class tag
{
	all = 0, // reserved

	generic,
	node,
	daemon,
	wallet,
	qt,
	rpc,
	rpc_connection,
	rpc_callbacks,
	rpc_request,
	ipc,
	ipc_server,
	websocket,
	tls,
	active_transactions,
	election,
	blockprocessor,
	network,
	network_processed,
	channel,
	channel_send,
	channel_send_result,
	socket,
	socket_server,
	tcp,
	tcp_server,
	prunning,
	conf_processor_bounded,
	conf_processor_unbounded,
	distributed_work,
	epoch_upgrader,
	opencl_work,
	upnp,
	repcrawler,
	lmdb,
	rocksdb,
	txn_tracker,
	gap_cache,
	vote_processor,
	election_scheduler,
	vote_generator,

	// bootstrap
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
	bootstrap_legacy,
};

enum class category
{
	all = 0, // reserved

	work_generation,
};

enum class detail
{
	all = 0, // reserved

	// node
	process_confirmed,

	// active_transactions
	active_started,
	active_stopped,

	// election
	election_confirmed,
	election_expired,
	broadcast_vote,

	// blockprocessor
	block_processed,

	// vote_processor
	vote_processed,

	// network
	message_processed,
	message_sent,
	message_dropped,

	// election_scheduler
	block_activated,

	// vote_generator
	candidate_processed,

	// bulk pull/push
	pulled_block,
	sending_block,
	sending_pending,
	sending_frontier,
	requesting_account_or_head,
	requesting_pending,

	// message types
	not_a_type,
	invalid,
	keepalive,
	publish,
	republish_vote,
	confirm_req,
	confirm_ack,
	node_id_handshake,
	telemetry_req,
	telemetry_ack,
	asc_pull_req,
	asc_pull_ack,
	bulk_pull,
	bulk_push,
	frontier_req,
	bulk_pull_account,

};

enum class preset
{
	cli,
	daemon,
	tests,
};
}

namespace nano
{
std::string_view to_string (nano::log::tag);
std::string_view to_string (nano::log::detail);
}
