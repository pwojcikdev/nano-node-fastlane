#include <nano/lib/stats_enums.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/bootstrap_ascending/account_scan.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>

using namespace std::chrono_literals;

/*
 * account_scan
 */

nano::bootstrap_ascending::account_scan::account_scan (const nano::bootstrap_ascending::config & config_a, nano::bootstrap_ascending::service & service_a, nano::ledger & ledger_a, nano::network_constants & network_consts_a, nano::block_processor & block_processor_a, nano::stats & stats_a) :
	config{ config_a },
	service{ service_a },
	ledger{ ledger_a },
	network_consts{ network_consts_a },
	block_processor{ block_processor_a },
	stats{ stats_a },
	accounts{ config.account_sets, stats },
	iterator{ ledger.store },
	throttle{ compute_throttle_size () },
	database_limiter{ config.database_rate_limit, 1.0 }
{
	block_processor.batch_processed.add ([this] (auto const & batch) {
		bool should_notify = false;
		{
			nano::lock_guard<nano::mutex> lock{ mutex };
			auto transaction = ledger.store.tx_begin_read ();
			for (auto const & [result, block, context] : batch)
			{
				// Do not try to unnecessarily bootstrap live traffic chains
				if (context.source == nano::block_processor::block_source::bootstrap)
				{
					inspect (transaction, result, *block);
					should_notify = true;
				}
			}
		}
		if (should_notify)
		{
			condition.notify_all ();
		}
	});
}

nano::bootstrap_ascending::account_scan::~account_scan ()
{
	// All threads must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::bootstrap_ascending::account_scan::start ()
{
	debug_assert (!thread.joinable ());

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascendboot_account_scan);
		run ();
	});
}

void nano::bootstrap_ascending::account_scan::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
}

std::size_t nano::bootstrap_ascending::account_scan::priority_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.priority_size ();
}

std::size_t nano::bootstrap_ascending::account_scan::blocked_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.blocked_size ();
}

void nano::bootstrap_ascending::account_scan::process (const nano::asc_pull_ack::blocks_payload & response, const account_scan::tag & tag)
{
	stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::reply);

	auto result = tag.verify (response);
	switch (result)
	{
		using enum account_scan::tag::verify_result;

		case ok:
		{
			stats.add (nano::stat::type::ascendboot_account_scan, nano::stat::detail::blocks, nano::stat::dir::in, response.blocks.size ());

			for (auto & block : response.blocks)
			{
				block_processor.add (block, nano::block_processor::block_source::bootstrap);
			}

			nano::lock_guard<nano::mutex> lock{ mutex };
			throttle.add (true);
		}
		break;
		case nothing_new:
		{
			stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::nothing_new);

			nano::lock_guard<nano::mutex> lock{ mutex };
			accounts.priority_down (tag.account);
			throttle.add (false);
		}
		break;
		case invalid:
		{
			stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::invalid);
			// TODO: Log
		}
		break;
	}
}

void nano::bootstrap_ascending::account_scan::cleanup ()
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	throttle.resize (compute_throttle_size ());
}

void nano::bootstrap_ascending::account_scan::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::loop);

		lock.unlock ();
		run_one ();
		lock.lock ();
		throttle_if_needed (lock);
	}
}

auto nano::bootstrap_ascending::account_scan::prepare_request (const nano::account account) -> std::pair<tag, nano::asc_pull_req::blocks_payload>
{
	tag tag{};
	nano::asc_pull_req::blocks_payload request{};
	request.count = config.pull_count;

	// Check if the account picked has blocks, if it does, start the pull from the highest block
	auto info = ledger.store.account.get (ledger.store.tx_begin_read (), account);
	if (info)
	{
		tag.type = account_scan::tag::query_type::blocks_by_hash;
		tag.start = request.start = info->head;
		request.start_type = nano::asc_pull_req::hash_type::block;
	}
	else
	{
		tag.type = account_scan::tag::query_type::blocks_by_account;
		tag.start = request.start = account;
		request.start_type = nano::asc_pull_req::hash_type::account;
	}

	return { tag, request };
}

void nano::bootstrap_ascending::account_scan::run_one ()
{
	// Ensure there is enough space in blockprocessor for queuing new blocks
	wait_blockprocessor ();

	// Waits for account either from priority queue or database
	auto account = wait_available_account ();
	if (account.is_zero ())
	{
		return;
	}

	auto [tag, request] = prepare_request (account);
	service.request (tag, request);
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void nano::bootstrap_ascending::account_scan::inspect (store::transaction const & tx, nano::process_return const & result, nano::block const & block)
{
	debug_assert (!mutex.try_lock ());

	auto const hash = block.hash ();
	switch (result.code)
	{
		case nano::process_result::progress:
		{
			const auto account = ledger.account (tx, hash);
			const auto is_send = ledger.is_send (tx, block);

			// If we've inserted any block in to an account, unmark it as blocked
			accounts.unblock (account);
			accounts.priority_up (account);
			accounts.timestamp (account, /* reset timestamp */ true);

			if (is_send)
			{
				// TODO: Encapsulate this as a helper somewhere
				nano::account destination{ 0 };
				switch (block.type ())
				{
					case nano::block_type::send:
						destination = block.destination ();
						break;
					case nano::block_type::state:
						destination = block.link ().as_account ();
						break;
					default:
						debug_assert (false, "unexpected block type");
						break;
				}
				if (!destination.is_zero ())
				{
					accounts.unblock (destination, hash); // Unblocking automatically inserts account into priority set
					accounts.priority_up (destination);
				}
			}
		}
		break;
		case nano::process_result::gap_source:
		{
			const auto account = block.previous ().is_zero () ? block.account () : ledger.account (tx, block.previous ());
			const auto source = block.source ().is_zero () ? block.link ().as_block_hash () : block.source ();

			// Mark account as blocked because it is missing the source block
			accounts.block (account, source);

			// TODO: Track stats
		}
		break;
		case nano::process_result::old:
		{
			// TODO: Track stats
		}
		break;
		case nano::process_result::gap_previous:
		{
			// TODO: Track stats
		}
		break;
		default: // No need to handle other cases
			break;
	}
}

nano::account nano::bootstrap_ascending::account_scan::available_account ()
{
	debug_assert (!mutex.try_lock ());

	{
		auto account = accounts.next ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::next_priority);
			return account;
		}
	}

	if (database_limiter.should_pass (1))
	{
		auto account = iterator.next ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::next_database);
			return account;
		}
	}

	stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::next_none);
	return { 0 };
}

nano::account nano::bootstrap_ascending::account_scan::wait_available_account ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto account = available_account ();
		if (!account.is_zero ())
		{
			accounts.timestamp (account);
			return account;
		}
		else
		{
			condition.wait_for (lock, config.throttle_wait); // We will be woken up if a new account is ready
		}
	}
	return { 0 };
}

void nano::bootstrap_ascending::account_scan::wait_blockprocessor ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		lock.unlock ();

		// Do not check blockprocessor when holding a lock as it may cause a deadlock
		if (block_processor.size () > 1024)
		{
			lock.lock ();
			condition.wait_for (lock, config.throttle_wait, [this] () { return stopped; });
		}
		else
		{
			return;
		}
	}
}

void nano::bootstrap_ascending::account_scan::throttle_if_needed (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());
	if (!iterator.warmup () && throttle.throttled ())
	{
		stats.inc (nano::stat::type::ascendboot_account_scan, nano::stat::detail::throttled);
		condition.wait_for (lock, config.throttle_wait, [this] () { return stopped; });
	}
}

std::size_t nano::bootstrap_ascending::account_scan::compute_throttle_size () const
{
	// Scales logarithmically with ledger block
	// Returns: config.throttle_coefficient * sqrt(block_count)
	std::size_t size_new = config.throttle_coefficient * std::sqrt (ledger.cache.block_count.load ());
	return size_new < 16 ? 16 : size_new;
}

/**
 * Verifies whether the received response is valid. Returns:
 * - invalid: when received blocks do not correspond to requested hash/account or they do not make a valid chain
 * - nothing_new: when received response indicates that the account chain does not have more blocks
 * - ok: otherwise, if all checks pass
 */
auto nano::bootstrap_ascending::account_scan::tag::verify (const nano::asc_pull_ack::blocks_payload & response) const -> verify_result
{
	auto const & blocks = response.blocks;

	if (blocks.empty ())
	{
		return verify_result::nothing_new;
	}
	if (blocks.size () == 1 && blocks.front ()->hash () == start.as_block_hash ())
	{
		return verify_result::nothing_new;
	}

	auto const & first = blocks.front ();
	switch (type)
	{
		case query_type::blocks_by_hash:
		{
			if (first->hash () != start.as_block_hash ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		case query_type::blocks_by_account:
		{
			// Open & state blocks always contain account field
			if (first->account () != start.as_account ())
			{
				// TODO: Stat & log
				return verify_result::invalid;
			}
		}
		break;
		default:
			debug_assert (false, "invalid type");
			return verify_result::invalid;
	}

	// Verify blocks make a valid chain
	nano::block_hash previous_hash = blocks.front ()->hash ();
	for (int n = 1; n < blocks.size (); ++n)
	{
		auto & block = blocks[n];
		if (block->previous () != previous_hash)
		{
			// TODO: Stat & log
			return verify_result::invalid; // Blocks do not make a chain
		}
		previous_hash = block->hash ();
	}

	return verify_result::ok;
}

std::unique_ptr<nano::container_info_component> nano::bootstrap_ascending::account_scan::collect_container_info (const std::string & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle", throttle.size (), 0 }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "throttle_successes", throttle.successes (), 0 }));
	composite->add_component (accounts.collect_container_info ("accounts"));
	return composite;
}