#include <nano/lib/utility.hpp>
#include <nano/node/bootstrap_server.hpp>
#include <nano/node/transport/channel.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/account.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/confirmation_height.hpp>

// TODO: Make threads configurable
nano::bootstrap_server::bootstrap_server (nano::store::component & store_a, nano::ledger & ledger_a, nano::network_constants const & network_constants_a, nano::stats & stats_a) :
	store{ store_a },
	ledger{ ledger_a },
	network_constants{ network_constants_a },
	stats{ stats_a },
	request_queue{ stats, nano::stat::type::bootstrap_server, nano::thread_role::name::bootstrap_server, /* threads */ 1, /* max size */ 1024 * 16, /* max batch */ 128 }
{
	request_queue.process_batch = [this] (auto & batch) {
		process_batch (batch);
	};
}

nano::bootstrap_server::~bootstrap_server ()
{
}

void nano::bootstrap_server::start ()
{
	request_queue.start ();
}

void nano::bootstrap_server::stop ()
{
	request_queue.stop ();
}

bool nano::bootstrap_server::verify_request_type (nano::asc_pull_type type) const
{
	switch (type)
	{
		case asc_pull_type::invalid:
			return false;
		case asc_pull_type::blocks:
		case asc_pull_type::account_info:
		case asc_pull_type::frontiers:
			return true;
	}
	return false;
}

bool nano::bootstrap_server::verify (const nano::asc_pull_req & message) const
{
	if (!verify_request_type (message.type))
	{
		return false;
	}

	struct verify_visitor
	{
		bool operator() (nano::empty_payload const &) const
		{
			return false;
		}
		bool operator() (nano::asc_pull_req::blocks_payload const & pld) const
		{
			return pld.count > 0 && pld.count <= max_blocks;
		}
		bool operator() (nano::asc_pull_req::account_info_payload const & pld) const
		{
			return !pld.target.is_zero ();
		}
		bool operator() (nano::asc_pull_req::frontiers_payload const & pld) const
		{
			return pld.count > 0 && pld.count <= max_frontiers;
		}
	};

	return std::visit (verify_visitor{}, message.payload);
}

bool nano::bootstrap_server::request (nano::asc_pull_req const & message, std::shared_ptr<nano::transport::channel> channel)
{
	if (!verify (message))
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::invalid);
		return false;
	}

	// If channel is full our response will be dropped anyway, so filter that early
	// TODO: Add per channel limits (this ideally should be done on the channel message processing side)
	if (channel->max (nano::transport::traffic_type::bootstrap))
	{
		stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::channel_full, nano::stat::dir::in);
		return false;
	}

	request_queue.add (std::make_pair (message, channel));
	return true;
}

void nano::bootstrap_server::respond (nano::asc_pull_ack & response, std::shared_ptr<nano::transport::channel> & channel)
{
	stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response, nano::stat::dir::out);

	// Increase relevant stats depending on payload type
	struct stat_visitor
	{
		nano::stats & stats;

		void operator() (nano::empty_payload const &)
		{
			debug_assert (false, "missing payload");
		}
		void operator() (nano::asc_pull_ack::blocks_payload const & pld)
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response_blocks, nano::stat::dir::out);
			stats.add (nano::stat::type::bootstrap_server, nano::stat::detail::blocks, nano::stat::dir::out, pld.blocks.size ());
		}
		void operator() (nano::asc_pull_ack::account_info_payload const & pld)
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response_account_info, nano::stat::dir::out);
		}
		void operator() (nano::asc_pull_ack::frontiers_payload const & pld)
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::response_frontiers, nano::stat::dir::out);
			stats.add (nano::stat::type::bootstrap_server, nano::stat::detail::frontiers, nano::stat::dir::out, pld.frontiers.size ());
		}
	};
	std::visit (stat_visitor{ stats }, response.payload);

	on_response.notify (response, channel);

	channel->send (
	response, [this] (auto & ec, auto size) {
		if (ec)
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::write_error, nano::stat::dir::out);
		}
	},
	nano::transport::buffer_drop_policy::limiter, nano::transport::traffic_type::bootstrap);
}

void nano::bootstrap_server::process_batch (std::deque<request_t> & batch)
{
	auto transaction = store.tx_begin_read ();

	for (auto & [request, channel] : batch)
	{
		transaction.refresh_if_needed ();

		if (!channel->max (nano::transport::traffic_type::bootstrap))
		{
			auto response = process (transaction, request);
			respond (response, channel);
		}
		else
		{
			stats.inc (nano::stat::type::bootstrap_server, nano::stat::detail::channel_full, nano::stat::dir::out);
		}
	}
}

nano::asc_pull_ack nano::bootstrap_server::process (store::transaction const & transaction, const nano::asc_pull_req & message)
{
	auto payload = std::visit ([this, &transaction] (auto && request) { return process (transaction, request); }, message.payload);

	nano::asc_pull_ack response{ network_constants };
	response.id = message.id;
	response.set_payload (payload);
	response.update_header ();

	return response;
}

nano::asc_pull_ack::payload_variant nano::bootstrap_server::process (const store::transaction &, const nano::empty_payload & request)
{
	// Empty payload should never be possible, but return empty response anyway
	debug_assert (false, "missing payload");
	return nano::empty_payload{};
}

/*
 * Blocks request
 */
nano::asc_pull_ack::payload_variant nano::bootstrap_server::process (store::transaction const & transaction, nano::asc_pull_req::blocks_payload const & request)
{
	const std::size_t count = std::min (static_cast<std::size_t> (request.count), max_blocks);

	switch (request.start_type)
	{
		case asc_pull_req::hash_type::block:
		{
			if (store.block.exists (transaction, request.start.as_block_hash ()))
			{
				return prepare_blocks_response (transaction, request.start.as_block_hash (), count);
			}
		}
		break;
		case asc_pull_req::hash_type::account:
		{
			auto info = ledger.account_info (transaction, request.start.as_account ());
			if (info)
			{
				// Start from open block if pulling by account
				return prepare_blocks_response (transaction, info->open_block, count);
			}
		}
		break;
	}

	// Neither block nor account found, send empty response to indicate that
	return nano::asc_pull_ack::blocks_payload{};
}

nano::asc_pull_ack::payload_variant nano::bootstrap_server::prepare_blocks_response (store::transaction const & transaction, nano::block_hash start_block, std::size_t count)
{
	debug_assert (count <= max_blocks); // Should be filtered out earlier

	auto blocks = prepare_blocks (transaction, start_block, count);
	debug_assert (blocks.size () <= count);

	nano::asc_pull_ack::blocks_payload response{};
	response.blocks = blocks;
	return response;
}

std::vector<std::shared_ptr<nano::block>> nano::bootstrap_server::prepare_blocks (store::transaction const & transaction, nano::block_hash start_block, std::size_t count) const
{
	debug_assert (count <= max_blocks); // Should be filtered out earlier

	std::vector<std::shared_ptr<nano::block>> result;
	if (!start_block.is_zero ())
	{
		std::shared_ptr<nano::block> current = store.block.get (transaction, start_block);
		while (current && result.size () < count)
		{
			result.push_back (current);

			auto successor = current->sideband ().successor;
			current = store.block.get (transaction, successor);
		}
	}

	return result;
}

/*
 * Account info request
 */
nano::asc_pull_ack::payload_variant nano::bootstrap_server::process (const store::transaction & transaction, const nano::asc_pull_req::account_info_payload & request)
{
	nano::account target{ 0 };
	switch (request.target_type)
	{
		case asc_pull_req::hash_type::account:
		{
			target = request.target.as_account ();
		}
		break;
		case asc_pull_req::hash_type::block:
		{
			// Try to lookup account assuming target is block hash
			target = ledger.account_safe (transaction, request.target.as_block_hash ());
		}
		break;
	}

	nano::asc_pull_ack::account_info_payload response{};
	response.account = target;

	auto account_info = ledger.account_info (transaction, target);
	if (account_info)
	{
		response.open = account_info->open_block;
		response.head = account_info->head;
		response.block_count = account_info->block_count;

		auto conf_info = store.confirmation_height.get (transaction, target);
		if (conf_info)
		{
			response.conf_frontier = conf_info->frontier;
			response.conf_height = conf_info->height;
		}
	}
	// If account is missing the response payload will contain all 0 fields, except for the target

	return response;
}

/*
 * Frontiers request
 */
nano::asc_pull_ack::payload_variant nano::bootstrap_server::process (const store::transaction & transaction, const nano::asc_pull_req::frontiers_payload & request)
{
	debug_assert (request.count <= max_frontiers); // Should be filtered out earlier

	nano::asc_pull_ack::frontiers_payload response{};

	for (auto it = store.account.begin (transaction, request.start), end = store.account.end (); it != end && response.frontiers.size () < request.count; ++it)
	{
		response.frontiers.emplace_back (it->first, it->second.head);
	}

	return response;
}