#pragma once

#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/bootstrap_ascending/account_sets.hpp>
#include <nano/node/bootstrap_ascending/common.hpp>
#include <nano/node/bootstrap_ascending/iterators.hpp>
#include <nano/node/bootstrap_ascending/throttle.hpp>
#include <nano/node/messages.hpp>

#include <thread>

namespace nano
{
class block_processor;
class ledger;
class network;
}

namespace nano::bootstrap_ascending
{
class service;
class config;

class priority_accounts final
{
public:
	class tag : public nano::bootstrap_ascending::tag_base<tag, nano::asc_pull_ack::blocks_payload>
	{
	public:
		enum class query_type
		{
			blocks_by_hash,
			blocks_by_account,
		};

		const nano::account account{};
		nano::hash_or_account start{};
		query_type type{};

		enum class verify_result
		{
			ok,
			nothing_new,
			invalid,
		};

		verify_result verify (nano::asc_pull_ack::blocks_payload const & response) const;
	};

public:
	priority_accounts (nano::bootstrap_ascending::config const &, nano::bootstrap_ascending::service &, nano::ledger &, nano::network_constants &, nano::block_processor &, nano::stats &);
	~priority_accounts ();

	void start ();
	void stop ();

	void process (nano::asc_pull_ack::blocks_payload const & response, tag const &);
	void cleanup ();

	std::size_t blocked_size () const;
	std::size_t priority_size () const;

	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);

private: // Dependencies
	nano::bootstrap_ascending::config const & config;
	nano::bootstrap_ascending::service & service;
	nano::ledger & ledger;
	nano::network_constants & network_consts;
	nano::block_processor & block_processor;
	nano::stats & stats;

private:
	void run ();
	void run_one ();
	std::pair<tag, nano::asc_pull_req::blocks_payload> prepare_request (nano::account account);

	/* Inspects a block that has been processed by the block processor */
	void inspect (store::transaction const &, nano::process_return const & result, nano::block const & block);

	/* Waits until a suitable account outside of cool down period is available */
	nano::account available_account ();
	nano::account wait_available_account ();

	/* Throttles requesting new blocks, not to overwhelm blockprocessor */
	void wait_blockprocessor ();

private:
	nano::bootstrap_ascending::account_sets accounts;
	nano::bootstrap_ascending::buffered_iterator iterator;

	// Requests for accounts from database have much lower hitrate and could introduce strain on the network
	// A separate (lower) limiter ensures that we always reserve resources for querying accounts from priority queue
	nano::bandwidth_limiter database_limiter;

	bool stopped{ false };
	mutable nano::mutex mutex;
	mutable nano::condition_variable condition;
	std::thread thread;
};
}