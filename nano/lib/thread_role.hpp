#pragma once

#include <string>

/*
 * Functions for understanding the role of the current thread
 */
namespace nano::thread_role
{
enum class name
{
	unknown,
	io,
	work,
	packet_processing,
	vote_processing,
	block_processing,
	request_loop,
	wallet_actions,
	bootstrap_initiator,
	bootstrap_connections,
	voting,
	signature_checking,
	rpc_request_processor,
	rpc_process_container,
	confirmation_height_processing,
	worker,
	bootstrap_worker,
	request_aggregator,
	state_block_signature_verification,
	epoch_upgrader,
	db_parallel_traversal,
	election_scheduler,
	unchecked,
	backlog_population,
	election_hinting,
	vote_generator_queue,
	bootstrap_server,
	telemetry,
	optimistic_scheduler,
	ascending_bootstrap,
	bootstrap_server_requests,
	bootstrap_server_responses,
};

/*
 * Get/Set the identifier for the current thread
 */
nano::thread_role::name get ();
void set (nano::thread_role::name);

/*
 * Get the thread name as a string from enum
 */
std::string get_string (nano::thread_role::name);

/*
 * Get the current thread's role as a string
 */
std::string get_string ();

/*
 * Internal only, should not be called directly
 */
void set_os_name (std::string const &);
}