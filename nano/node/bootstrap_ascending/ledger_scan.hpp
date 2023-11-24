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

class ledger_scan final
{
public:
	ledger_scan (nano::bootstrap_ascending::config const &, nano::bootstrap_ascending::service &, nano::ledger &, nano::network_constants &, nano::block_processor &, nano::stats &);
	~ledger_scan ();

	void start ();
	void stop ();

	void cleanup ();

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
	nano::account wait_account ();
	nano::account next_account ();

private:
	nano::bootstrap_ascending::buffered_iterator iterator;

	// Requests for accounts from database have much lower hitrate and could introduce strain on the network
	// A separate (lower) limiter ensures that we always reserve resources for querying accounts from priority queue
	nano::bandwidth_limiter limiter;

	bool stopped{ false };
	mutable nano::mutex mutex;
	mutable nano::condition_variable condition;
	std::thread thread;
};
}