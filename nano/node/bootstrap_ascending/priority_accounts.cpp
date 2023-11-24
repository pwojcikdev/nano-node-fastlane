#include <nano/lib/stats_enums.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/bootstrap_ascending/priority_accounts.hpp>
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

nano::bootstrap_ascending::priority_accounts::priority_accounts (const nano::bootstrap_ascending::config & config_a, nano::bootstrap_ascending::service & service_a, nano::ledger & ledger_a, nano::network_constants & network_consts_a, nano::block_processor & block_processor_a, nano::stats & stats_a) :
	config{ config_a },
	service{ service_a },
	ledger{ ledger_a },
	network_consts{ network_consts_a },
	block_processor{ block_processor_a },
	stats{ stats_a },
	accounts{ config.account_sets, stats }
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

nano::bootstrap_ascending::priority_accounts::~priority_accounts ()
{
	// All threads must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::bootstrap_ascending::priority_accounts::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable_priority)
	{
		return;
	}

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascendboot_account_scan);
		run ();
	});
}

void nano::bootstrap_ascending::priority_accounts::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
}

std::size_t nano::bootstrap_ascending::priority_accounts::priority_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.priority_size ();
}

std::size_t nano::bootstrap_ascending::priority_accounts::blocked_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return accounts.blocked_size ();
}

void nano::bootstrap_ascending::priority_accounts::process (const nano::asc_pull_ack::blocks_payload & response, const pull_blocks_tag & tag, pull_blocks_tag::verify_result const result)
{
	stats.inc (nano::stat::type::ascendboot_priority_accounts, nano::stat::detail::reply);

	if (result == pull_blocks_tag::verify_result::nothing_new)
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		accounts.priority_down (tag.account);
	}
}

void nano::bootstrap_ascending::priority_accounts::cleanup ()
{
}

void nano::bootstrap_ascending::priority_accounts::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::ascendboot_priority_accounts, nano::stat::detail::loop);

		lock.unlock ();
		run_one ();
		lock.lock ();
	}
}

void nano::bootstrap_ascending::priority_accounts::run_one ()
{
	// Ensure there is enough space in blockprocessor for queuing new blocks
	service.wait_block_processor ();

	auto account = wait_account ();
	if (account.is_zero ())
	{
		return;
	}

	service.request_account (account);
}

/** Inspects a block that has been processed by the block processor
- Marks an account as blocked if the result code is gap source as there is no reason request additional blocks for this account until the dependency is resolved
- Marks an account as forwarded if it has been recently referenced by a block that has been inserted.
 */
void nano::bootstrap_ascending::priority_accounts::inspect (store::transaction const & tx, nano::process_return const & result, nano::block const & block)
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

nano::account nano::bootstrap_ascending::priority_accounts::wait_account ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto account = accounts.next ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::ascendboot_priority_accounts, nano::stat::detail::next_priority);
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

std::unique_ptr<nano::container_info_component> nano::bootstrap_ascending::priority_accounts::collect_container_info (const std::string & name)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (accounts.collect_container_info ("accounts"));
	return composite;
}