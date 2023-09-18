#include <nano/node/node.hpp>
#include <nano/node/vote_cache.hpp>

/*
 * entry
 */

nano::vote_cache::entry::entry (const nano::block_hash & hash) :
	hash{ hash }
{
}

bool nano::vote_cache::entry::vote (const nano::account & representative, const uint64_t & timestamp, const nano::uint128_t & rep_weight)
{
	auto existing = std::find_if (voters.begin (), voters.end (), [&representative] (auto const & item) { return item.representative == representative; });
	if (existing != voters.end ())
	{
		// We already have a vote from this rep
		// Update timestamp if newer but tally remains unchanged as we already counted this rep weight
		// It is not essential to keep tally up to date if rep voting weight changes, elections do tally calculations independently, so in the worst case scenario only our queue ordering will be a bit off
		if (timestamp > existing->timestamp)
		{
			existing->timestamp = timestamp;
			recalculate_tally ();
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		// Vote from an unseen representative, add to list and update tally
		if (voters.size () < max_voters)
		{
			voters.push_back ({ representative, timestamp, rep_weight });
			recalculate_tally ();
			return true;
		}
		else
		{
			return false;
		}
	}
}

std::size_t nano::vote_cache::entry::fill (std::shared_ptr<nano::election> election) const
{
	std::size_t inserted = 0;
	for (const auto & entry : voters)
	{
		auto [is_replay, processed] = election->vote (entry.representative, entry.timestamp, hash, nano::election::vote_source::cache);
		if (processed)
		{
			inserted++;
		}
	}
	return inserted;
}

std::size_t nano::vote_cache::entry::size () const
{
	return voters.size ();
}

void nano::vote_cache::entry::recalculate_tally ()
{
	tally = 0;
	final_tally = 0;

	for (auto const & entry : voters)
	{
		tally += entry.weight;
		if (nano::vote::timestamp_is_final (entry.timestamp))
		{
			final_tally += entry.weight;
		}
	}
}

bool nano::vote_cache::entry::cooldown (std::chrono::milliseconds cooldown_time) const
{
	if (last_cooldown + cooldown_time < std::chrono::steady_clock::now ())
	{
		last_cooldown = std::chrono::steady_clock::now ();
		return true;
	}
	else
	{
		return false;
	}
}

/*
 * vote_cache
 */

nano::vote_cache::vote_cache (const config config_a) :
	max_size{ config_a.max_size }
{
}

void nano::vote_cache::vote (const nano::block_hash & hash, const std::shared_ptr<nano::vote> vote)
{
	auto weight = rep_weight_query (vote->account);
	vote_impl (hash, vote->account, vote->timestamp (), weight);
}

void nano::vote_cache::vote_impl (const nano::block_hash & hash, const nano::account & representative, uint64_t const & timestamp, const nano::uint128_t & rep_weight)
{
	nano::unique_lock<nano::mutex> lock{ mutex };

	/**
	 * If there is no cache entry for the block hash, create a new entry for both cache and queue.
	 * Otherwise update existing cache entry and, if queue contains entry for the block hash, update the queue entry
	 */
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		bool modified = false;
		bool success = cache_by_hash.modify (existing, [&representative, &timestamp, &rep_weight, &modified] (entry & ent) {
			modified = ent.vote (representative, timestamp, rep_weight);
		});
		release_assert (success); // Ensure iterator `existing` is valid

		if (modified)
		{
			auto & queue_by_hash = queue.get<tag_hash> ();
			if (auto queue_existing = queue_by_hash.find (hash); queue_existing != queue_by_hash.end ())
			{
				queue_by_hash.modify (queue_existing, [&existing] (queue_entry & ent) {
					ent.tally = existing->tally;
					ent.final_tally = existing->final_tally;
				});
			}
		}
	}
	else
	{
		entry cache_entry{ hash };
		cache_entry.vote (representative, timestamp, rep_weight);

		cache.get<tag_hash> ().insert (cache_entry);

		// If a stale entry for the same hash already exists in queue, replace it by a new entry with fresh tally
		auto & queue_by_hash = queue.get<tag_hash> ();
		if (auto queue_existing = queue_by_hash.find (hash); queue_existing != queue_by_hash.end ())
		{
			queue_by_hash.erase (queue_existing);
		}
		queue_by_hash.insert ({ hash, cache_entry.tally, cache_entry.final_tally });

		trim_overflow_locked ();
	}
}

bool nano::vote_cache::cache_empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return cache.empty ();
}

bool nano::vote_cache::queue_empty () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.empty ();
}

std::size_t nano::vote_cache::cache_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return cache.size ();
}

std::size_t nano::vote_cache::queue_size () const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return queue.size ();
}

std::optional<nano::vote_cache::entry> nano::vote_cache::find (const nano::block_hash & hash) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };
	return find_locked (hash);
}

bool nano::vote_cache::erase (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	bool result = false;
	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		cache_by_hash.erase (existing);
		result = true;
	}
	auto & queue_by_hash = queue.get<tag_hash> ();
	if (auto existing = queue_by_hash.find (hash); existing != queue_by_hash.end ())
	{
		queue_by_hash.erase (existing);
	}
	return result;
}

std::optional<nano::vote_cache::entry> nano::vote_cache::pop (nano::uint128_t const & min_tally)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	if (!queue.empty ())
	{
		auto & queue_by_tally = queue.get<tag_tally> ();
		auto top = std::prev (queue_by_tally.end ()); // Iterator to element with the highest tally
		if (auto maybe_cache_entry = find_locked (top->hash); maybe_cache_entry)
		{
			// Here we check whether our best candidate passes the minimum vote tally threshold
			// If yes, erase it from the queue (but still keep the votes in cache)
			if (maybe_cache_entry->tally >= min_tally)
			{
				queue_by_tally.erase (top);
				return maybe_cache_entry.value ();
			}
		}
	}
	return {};
}

std::optional<nano::vote_cache::entry> nano::vote_cache::peek (nano::uint128_t const & min_tally) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	if (!queue.empty ())
	{
		auto & queue_by_tally = queue.get<tag_tally> ();
		auto top = std::prev (queue_by_tally.end ()); // Iterator to element with the highest tally
		if (auto maybe_cache_entry = find_locked (top->hash); maybe_cache_entry)
		{
			if (maybe_cache_entry->tally >= min_tally)
			{
				return maybe_cache_entry.value ();
			}
		}
	}
	return {};
}

std::optional<nano::vote_cache::entry> nano::vote_cache::peek_final (nano::uint128_t const & min_final_tally) const
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	if (!queue.empty ())
	{
		auto & queue_by_tally = queue.get<tag_final_tally> ();
		auto top = std::prev (queue_by_tally.end ()); // Iterator to element with the highest tally
		if (auto maybe_cache_entry = find_locked (top->hash); maybe_cache_entry)
		{
			if (maybe_cache_entry->final_tally >= min_final_tally)
			{
				return maybe_cache_entry.value ();
			}
		}
	}
	return {};
}

void nano::vote_cache::trigger (const nano::block_hash & hash)
{
	nano::lock_guard<nano::mutex> lock{ mutex };

	// Only reinsert to queue if it is not already in queue and there are votes in passive cache
	auto & queue_by_hash = queue.get<tag_hash> ();
	if (auto existing_queue = queue_by_hash.find (hash); existing_queue == queue_by_hash.end ())
	{
		if (auto maybe_cache_entry = find_locked (hash); maybe_cache_entry)
		{
			queue_by_hash.insert ({ hash, maybe_cache_entry->tally, maybe_cache_entry->final_tally });

			trim_overflow_locked ();
		}
	}
}

std::optional<nano::vote_cache::entry> nano::vote_cache::find_locked (const nano::block_hash & hash) const
{
	debug_assert (!mutex.try_lock ());

	auto & cache_by_hash = cache.get<tag_hash> ();
	if (auto existing = cache_by_hash.find (hash); existing != cache_by_hash.end ())
	{
		return *existing;
	}
	return {};
}

void nano::vote_cache::trim_overflow_locked ()
{
	debug_assert (!mutex.try_lock ());

	// When cache overflown remove the oldest entry
	if (cache.size () > max_size)
	{
		cache.get<tag_sequenced> ().pop_front ();
	}
	if (queue.size () > max_size)
	{
		queue.get<tag_sequenced> ().pop_front ();
	}
}

void nano::vote_cache::iterate (const nano::uint128_t & min_tally, const nano::uint128_t & min_final_tally, const std::function<void (entry const &)> & action) const
{
	std::vector<entry> to_process;
	{
		// TODO: Iterate without holding a lock
		nano::lock_guard<nano::mutex> lock{ mutex };

		for (auto & entry : cache.get<tag_final_tally> ())
		{
//			if (entry.final_tally < min_final_tally)
//			{
//				break;
//			}
			to_process.push_back (entry);
		}

		for (auto & entry : cache.get<tag_tally> ())
		{
//			if (entry.tally < min_tally)
//			{
//				break;
//			}
			to_process.push_back (entry);
		}
	}

	for (auto & entry : to_process)
	{
		action (entry);
	}
}

std::unique_ptr<nano::container_info_component> nano::vote_cache::collect_container_info (const std::string & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "cache", cache_size (), sizeof (ordered_cache::value_type) }));
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "queue", queue_size (), sizeof (ordered_queue::value_type) }));
	return composite;
}