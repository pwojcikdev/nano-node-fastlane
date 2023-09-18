#include <nano/lib/stats.hpp>
#include <nano/node/node.hpp>
#include <nano/node/scheduler/hinted.hpp>

nano::scheduler::hinted::config::config (nano::node_config const & config) :
	vote_cache_check_interval_ms{ config.network_params.network.is_dev_network () ? 100u : 1000u }
{
}

nano::scheduler::hinted::hinted (config const & config_a, nano::node & node_a, nano::vote_cache & vote_cache_a, nano::active_transactions & active_a, nano::online_reps & online_reps_a, nano::stats & stats_a) :
	config_m{ config_a },
	node{ node_a },
	vote_cache{ vote_cache_a },
	active{ active_a },
	online_reps{ online_reps_a },
	stats{ stats_a }
{
}

nano::scheduler::hinted::~hinted ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::scheduler::hinted::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::scheduler_hinted);
		run ();
	} };
}

void nano::scheduler::hinted::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	notify ();
	nano::join_or_pass (thread);
}

void nano::scheduler::hinted::notify ()
{
	condition.notify_all ();
}

bool nano::scheduler::hinted::predicate () const
{
	// Check if there is space inside AEC for a new hinted election
	return active.vacancy (nano::election_behavior::hinted) > 0;
}

bool nano::scheduler::hinted::activate (const nano::store::transaction & transaction, const nano::block_hash & hash, bool check_dependents)
{
	// Check if block exists
	if (auto block = node.store.block.get (transaction, hash); block != nullptr)
	{
		// Ensure block is not already confirmed
		if (node.block_confirmed_or_being_confirmed (transaction, hash))
		{
			stats.inc (nano::stat::type::hinting, nano::stat::detail::already_confirmed);
			return false;
		}
		if (check_dependents && !node.ledger.dependents_confirmed (transaction, *block))
		{
			stats.inc (nano::stat::type::hinting, nano::stat::detail::dependent_unconfirmed);
			activate_dependents (transaction, *block);
			return false;
		}

		// Try to insert it into AEC as hinted election
		// We check for AEC vacancy inside the predicate
		auto result = node.active.insert (block, nano::election_behavior::hinted);
		stats.inc (nano::stat::type::hinting, result.inserted ? nano::stat::detail::insert : nano::stat::detail::insert_failed);
		return true;
	}
	else
	{
		// Missing block in ledger to start an election
		stats.inc (nano::stat::type::hinting, nano::stat::detail::missing_block);
		node.bootstrap_block (hash);
	}

	return false;
}

void nano::scheduler::hinted::activate_dependents (const nano::store::transaction & transaction, const nano::block & block)
{
	auto dependents = node.ledger.dependent_blocks (transaction, block);
	for (auto const & hash : dependents)
	{
		if (!hash.is_zero ())
		{
			bool activated = activate (transaction, hash, /* check dependents */ true);
			if (activated)
			{
				stats.inc (nano::stat::type::hinting, nano::stat::detail::dependent_activated);
			}
		}
	}
}

void nano::scheduler::hinted::run_iterative ()
{
	const auto minimum_tally = tally_threshold ();
	const auto minimum_final_tally = final_tally_threshold ();

	auto transaction = node.store.tx_begin_read ();

	vote_cache.iterate (minimum_tally, minimum_final_tally, [this, &transaction, minimum_tally, minimum_final_tally] (auto & entry) {
		if (!predicate ())
		{
			return;
		}

		if (entry.final_tally () >= minimum_final_tally)
		{
			stats.inc (nano::stat::type::hinting, nano::stat::detail::activate_final);
			activate (transaction, entry.hash (), /* activate regardless of dependents */ false);
			return;
		}
		if (entry.tally () >= minimum_tally)
		{
			stats.inc (nano::stat::type::hinting, nano::stat::detail::activate_normal);
			activate (transaction, entry.hash (), /* ensure previous confirmed */ true);
			return;
		}
	});
}

void nano::scheduler::hinted::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::hinting, nano::stat::detail::loop);

		// Periodically wakeup for condition checking
		// We are not notified every time new vote arrives in inactive vote cache as that happens too often
		condition.wait_for (lock, std::chrono::milliseconds (config_m.vote_cache_check_interval_ms), [this] () {
			return stopped || predicate ();
		});

		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped)
		{
			lock.unlock ();

			if (predicate ())
			{
				run_iterative ();
			}

			lock.lock ();
		}
	}
}

nano::uint128_t nano::scheduler::hinted::tally_threshold () const
{
	//	auto min_tally = (online_reps.trended () / 100) * node.config.election_hint_weight_percent;
	//	return min_tally;

	return 0;
}

nano::uint128_t nano::scheduler::hinted::final_tally_threshold () const
{
	auto quorum = online_reps.delta ();
	return quorum;
}