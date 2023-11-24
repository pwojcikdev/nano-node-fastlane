#include <nano/lib/stats_enums.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/bootstrap_ascending/ledger_scan.hpp>
#include <nano/node/bootstrap_ascending/priority_accounts.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/network.hpp>
#include <nano/node/nodeconfig.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/account.hpp>
#include <nano/store/component.hpp>

nano::bootstrap_ascending::ledger_scan::ledger_scan (const nano::bootstrap_ascending::config & config_a, nano::bootstrap_ascending::service & service_a, nano::ledger & ledger_a, nano::network_constants & network_consts_a, nano::block_processor & block_processor_a, nano::stats & stats_a) :
	config{ config_a },
	service{ service_a },
	ledger{ ledger_a },
	network_consts{ network_consts_a },
	block_processor{ block_processor_a },
	stats{ stats_a },
	iterator{ ledger.store },
	limiter{ config.database_rate_limit, 1.0 }
{
}

nano::bootstrap_ascending::ledger_scan::~ledger_scan ()
{
	// All threads must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::bootstrap_ascending::ledger_scan::start ()
{
	debug_assert (!thread.joinable ());

	if (!config.enable_ledger_scan)
	{
		return;
	}

	thread = std::thread ([this] () {
		nano::thread_role::set (nano::thread_role::name::ascendboot_ledger_scan);
		run ();
	});
}

void nano::bootstrap_ascending::ledger_scan::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
}

void nano::bootstrap_ascending::ledger_scan::cleanup ()
{

}

void nano::bootstrap_ascending::ledger_scan::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::ascendboot_ledger_scan, nano::stat::detail::loop);

		lock.unlock ();
		run_one ();
		lock.lock ();
	}
}

void nano::bootstrap_ascending::ledger_scan::run_one ()
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

nano::account nano::bootstrap_ascending::ledger_scan::wait_account ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		auto account = next_account ();
		if (!account.is_zero ())
		{
			stats.inc (nano::stat::type::ascendboot_ledger_scan, nano::stat::detail::next_database);
			return account;
		}
		else
		{
			stats.inc (nano::stat::type::ascendboot_ledger_scan, nano::stat::detail::next_none);
			condition.wait_for (lock, config.throttle_wait);
		}
	}
	return { 0 };
}

nano::account nano::bootstrap_ascending::ledger_scan::next_account ()
{
	debug_assert (!mutex.try_lock ());

	if (limiter.should_pass (1))
	{
		auto account = iterator.next ();
		if (!account.is_zero ())
		{
			return account;
		}
	}
	return { 0 };
}