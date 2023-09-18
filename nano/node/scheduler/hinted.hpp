#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>

#include <condition_variable>
#include <thread>

namespace nano
{
class node;
class node_config;
class active_transactions;
class vote_cache;
class online_reps;
}
namespace nano::scheduler
{
/*
 * Monitors inactive vote cache and schedules elections with the highest observed vote tally.
 */
class hinted final
{
public: // Config
	struct config final
	{
		explicit config (node_config const & config);
		// Interval of wakeup to check inactive vote cache when idle
		uint64_t vote_cache_check_interval_ms;
	};

public:
	hinted (config const &, nano::node &, nano::vote_cache &, nano::active_transactions &, nano::online_reps &, nano::stats &);
	~hinted ();

	void start ();
	void stop ();

	/*
	 * Notify about changes in AEC vacancy
	 */
	void notify ();

private:
	bool predicate () const;
	void run ();
	void run_iterative ();
	bool activate (nano::store::transaction const &, nano::block_hash const & hash, bool check_dependents);
	void activate_dependents (nano::store::transaction const &, nano::block const & block);

	nano::uint128_t tally_threshold () const;
	nano::uint128_t final_tally_threshold () const;

private: // Dependencies
	nano::node & node;
	nano::vote_cache & vote_cache;
	nano::active_transactions & active;
	nano::online_reps & online_reps;
	nano::stats & stats;

private:
	config const config_m;

	bool stopped{ false };
	nano::condition_variable condition;
	mutable nano::mutex mutex;
	std::thread thread;
};
}
