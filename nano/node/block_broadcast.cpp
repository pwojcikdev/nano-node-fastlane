#include <nano/lib/threading.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/block_broadcast.hpp>
#include <nano/node/blockprocessor.hpp>
#include <nano/node/network.hpp>
#include <nano/node/node.hpp>

nano::block_broadcast::block_broadcast (nano::node & node_a, nano::block_processor & block_processor_a, nano::network & network_a, nano::stats & stats_a, bool enabled_a) :
	node{ node_a },
	block_processor{ block_processor_a },
	network{ network_a },
	stats{ stats_a },
	enabled{ enabled_a }
{
	if (!enabled)
	{
		return;
	}

	block_processor.batch_processed.add ([this] (auto const & batch) {
		bool should_notify = false;
		for (auto const & [result, block, context] : batch)
		{
			// Only rebroadcast local blocks that were successfully processed (no forks or gaps)
			if (result.code == nano::process_result::progress && context.source == nano::block_processor::block_source::local)
			{
				nano::lock_guard<nano::mutex> guard{ mutex };
				local_blocks.emplace_back (local_entry{ block, std::chrono::steady_clock::now () });
				stats.inc (nano::stat::type::block_broadcaster, nano::stat::detail::insert);

				// Erase oldest blocks if the queue gets too big
				while (local_blocks.size () > local_max_size)
				{
					stats.inc (nano::stat::type::block_broadcaster, nano::stat::detail::overfill);
					local_blocks.pop_front ();
				}

				should_notify = true;
			}
		}
		if (should_notify)
		{
			condition.notify_all ();
		}
	});

	block_processor.rolled_back.add ([this] (auto const & block) {
		nano::lock_guard<nano::mutex> guard{ mutex };
		auto erased = local_blocks.get<tag_hash> ().erase (block->hash ());
		stats.add (nano::stat::type::block_broadcaster, nano::stat::detail::rollback, erased);
	});
}

nano::block_broadcast::~block_broadcast ()
{
	// Thread must be stopped before destruction
	debug_assert (!thread.joinable ());
}

void nano::block_broadcast::start ()
{
	if (!enabled)
	{
		return;
	}

	debug_assert (!thread.joinable ());

	thread = std::thread{ [this] () {
		nano::thread_role::set (nano::thread_role::name::block_broadcasting);
		run ();
	} };
}

void nano::block_broadcast::stop ()
{
	{
		nano::lock_guard<nano::mutex> lock{ mutex };
		stopped = true;
	}
	condition.notify_all ();
	nano::join_or_pass (thread);
}

void nano::block_broadcast::run ()
{
	nano::unique_lock<nano::mutex> lock{ mutex };
	while (!stopped)
	{
		stats.inc (nano::stat::type::block_broadcaster, nano::stat::detail::loop);

		condition.wait_for (lock, local_check_interval);
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds

		if (!stopped)
		{
			cleanup ();
			run_once (lock);
			debug_assert (lock.owns_lock ());
		}
	}
}

void nano::block_broadcast::run_once (nano::unique_lock<nano::mutex> & lock)
{
	debug_assert (lock.owns_lock ());

	std::vector<std::shared_ptr<nano::block>> to_broadcast;

	auto const now = std::chrono::steady_clock::now ();
	for (auto & entry : local_blocks)
	{
		if (entry.last_broadcast + local_broadcast_interval < now)
		{
			to_broadcast.push_back (entry.block);
			entry.last_broadcast = now;
		}
	}

	lock.unlock ();

	for (auto const & block : to_broadcast)
	{
		stats.inc (nano::stat::type::block_broadcaster, nano::stat::detail::broadcast, nano::stat::dir::out);
		network.flood_block_initial (block);
	}

	lock.lock ();
}

void nano::block_broadcast::cleanup ()
{
	debug_assert (!mutex.try_lock ());

	// TODO: Mutex is held during IO, it is possible to improve this further
	auto transaction = node.store.tx_begin_read ();
	erase_if (local_blocks, [this, &transaction] (auto const & entry) {
		transaction.refresh_if_needed ();

		if (entry.last_broadcast == std::chrono::steady_clock::time_point{})
		{
			// This block has never been broadcasted, keep it so it's broadcasted at least once
			return false;
		}
		if (entry.arrival + local_age_cutoff < std::chrono::steady_clock::now ())
		{
			stats.inc (nano::stat::type::block_broadcaster, nano::stat::detail::erase_old);
			return true;
		}
		if (node.block_confirmed_or_being_confirmed (transaction, entry.block->hash ()))
		{
			stats.inc (nano::stat::type::block_broadcaster, nano::stat::detail::erase_confirmed);
			return true;
		}

		return false;
	});
}

std::unique_ptr<nano::container_info_component> nano::block_broadcast::collect_container_info (const std::string & name) const
{
	nano::lock_guard<nano::mutex> guard{ mutex };

	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "local", local_blocks.size (), sizeof (decltype (local_blocks)::value_type) }));
	return composite;
}