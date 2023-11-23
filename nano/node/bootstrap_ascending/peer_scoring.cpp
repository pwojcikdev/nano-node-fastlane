#include <nano/node/bootstrap_ascending/peer_scoring.hpp>
#include <nano/node/bootstrap_ascending/service.hpp>
#include <nano/node/transport/channel.hpp>

/*
 * peer_scoring
 */

nano::bootstrap_ascending::peer_scoring::peer_scoring (nano::bootstrap_ascending::config & config, nano::network_constants const & network_constants) :
	config{ config },
	network_constants{ network_constants }
{
}

bool nano::bootstrap_ascending::peer_scoring::try_send_message (std::shared_ptr<nano::transport::channel> const & channel)
{
	auto & index = scoring.get<tag_channel> ();
	auto existing = index.find (channel.get ());
	if (existing == index.end ())
	{
		index.emplace (channel);
		return true; // Success
	}
	else
	{
		// Limit = 0 indicates no limit
		if (config.requests_limit == 0 || existing->outstanding < config.requests_limit)
		{
			bool success = index.modify (existing, [] (auto & score) {
				++score.outstanding;
				++score.request_count_total;
			});
			release_assert (success);
			return true; // Success
		}
		else
		{
			return false;
		}
	}
	return false;
}

void nano::bootstrap_ascending::peer_scoring::received_message (std::shared_ptr<nano::transport::channel> const & channel)
{
	auto & index = scoring.get<tag_channel> ();
	auto existing = index.find (channel.get ());
	if (existing != index.end ())
	{
		if (existing->outstanding > 1)
		{
			bool success = index.modify (existing, [] (auto & score) {
				--score.outstanding;
				++score.response_count_total;
			});
			release_assert (success);
		}
	}
}

std::shared_ptr<nano::transport::channel> nano::bootstrap_ascending::peer_scoring::channel (uint8_t const min_protocol_version)
{
	auto & index = scoring.get<tag_outstanding> ();
	for (auto const & score : index)
	{
		if (auto channel = score.shared ())
		{
			if (min_protocol_version == 0 || channel->get_network_version () >= min_protocol_version)
			{
				if (!channel->max (nano::transport::traffic_type::bootstrap))
				{
					if (try_send_message (channel))
					{
						return channel;
					}
				}
			}
		}
	}
	return nullptr;
}

std::size_t nano::bootstrap_ascending::peer_scoring::size () const
{
	return scoring.size ();
}

void nano::bootstrap_ascending::peer_scoring::timeout ()
{
	erase_if (scoring, [] (auto const & score) {
		if (auto channel = score.shared ())
		{
			if (channel->alive ())
			{
				return false;
			}
		}
		return true;
	});

	modify_all (scoring, [] (auto & score) {
		score.decay ();
	});
}

void nano::bootstrap_ascending::peer_scoring::sync (std::deque<std::shared_ptr<nano::transport::channel>> const & list)
{
	auto & index = scoring.get<tag_channel> ();
	for (auto const & channel : list)
	{
		if (channel->get_network_version () >= network_constants.bootstrap_protocol_version_min)
		{
			if (index.find (channel.get ()) == index.end ())
			{
				index.emplace (channel);
			}
		}
	}
}

/*
 * peer_score
 */

nano::bootstrap_ascending::peer_scoring::peer_score::peer_score (
std::shared_ptr<nano::transport::channel> const & channel_a) :
	channel{ channel_a },
	channel_ptr{ channel_a.get () }
{
}
