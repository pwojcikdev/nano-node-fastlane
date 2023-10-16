#include <nano/lib/blocks.hpp>
#include <nano/lib/config.hpp>
#include <nano/lib/memory.hpp>
#include <nano/lib/stats_enums.hpp>
#include <nano/lib/stream.hpp>
#include <nano/lib/utility.hpp>
#include <nano/lib/work.hpp>
#include <nano/messages/messages.hpp>

#include <boost/asio/ip/address_v6.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/format.hpp>
#include <boost/pool/pool_alloc.hpp>

#include <bitset>
#include <chrono>
#include <cstdint>
#include <memory>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

/*
 * message_header
 */

nano::id_dispenser nano::message_header::id_gen{};

nano::message_header::message_header (nano::network_constants const & constants, nano::message_type type_a) :
	network{ constants.current_network },
	version_max{ constants.protocol_version },
	version_using{ constants.protocol_version },
	version_min{ constants.protocol_version_min },
	type (type_a),
	id{ id_gen.next_id () }
{
}

nano::message_header::message_header (bool & error_a, nano::stream & stream_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void nano::message_header::serialize (nano::stream & stream_a) const
{
	nano::write (stream_a, boost::endian::native_to_big (static_cast<uint16_t> (network)));
	nano::write (stream_a, version_max);
	nano::write (stream_a, version_using);
	nano::write (stream_a, version_min);
	nano::write (stream_a, type);
	nano::write (stream_a, static_cast<uint16_t> (extensions.to_ullong ()));
	nano::write (stream_a, id);
}

bool nano::message_header::deserialize (nano::stream & stream_a)
{
	auto error (false);
	try
	{
		uint16_t network_bytes;
		nano::read (stream_a, network_bytes);
		network = static_cast<nano::networks> (boost::endian::big_to_native (network_bytes));
		nano::read (stream_a, version_max);
		nano::read (stream_a, version_using);
		nano::read (stream_a, version_min);
		nano::read (stream_a, type);
		uint16_t extensions_l;
		nano::read (stream_a, extensions_l);
		extensions = extensions_l;
		nano::read (stream_a, id);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

nano::block_type nano::message_header::block_type () const
{
	return static_cast<nano::block_type> (((extensions & block_type_mask) >> 8).to_ullong ());
}

void nano::message_header::block_type_set (nano::block_type type_a)
{
	extensions &= ~block_type_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (type_a) << 8);
}

uint8_t nano::message_header::count_get () const
{
	return static_cast<uint8_t> (((extensions & count_mask) >> 12).to_ullong ());
}

void nano::message_header::count_set (uint8_t count_a)
{
	debug_assert (count_a < 16);
	extensions &= ~count_mask;
	extensions |= std::bitset<16> (static_cast<unsigned long long> (count_a) << 12);
}

void nano::message_header::flag_set (uint8_t flag_a, bool enable)
{
	// Flags from 8 are block_type & count
	debug_assert (flag_a < 8);
	extensions.set (flag_a, enable);
}

bool nano::message_header::bulk_pull_is_count_present () const
{
	auto result (false);
	if (type == nano::message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_count_present_flag))
		{
			result = true;
		}
	}
	return result;
}

bool nano::message_header::bulk_pull_ascending () const
{
	auto result (false);
	if (type == nano::message_type::bulk_pull)
	{
		if (extensions.test (bulk_pull_ascending_flag))
		{
			result = true;
		}
	}
	return result;
}

bool nano::message_header::frontier_req_is_only_confirmed_present () const
{
	auto result (false);
	if (type == nano::message_type::frontier_req)
	{
		if (extensions.test (frontier_req_only_confirmed))
		{
			result = true;
		}
	}
	return result;
}

std::size_t nano::message_header::payload_length_bytes () const
{
	switch (type)
	{
		case nano::message_type::bulk_pull:
		{
			return nano::bulk_pull::size + (bulk_pull_is_count_present () ? nano::bulk_pull::extended_parameters_size : 0);
		}
		case nano::message_type::bulk_push:
		case nano::message_type::telemetry_req:
		{
			// These don't have a payload
			return 0;
		}
		case nano::message_type::frontier_req:
		{
			return nano::frontier_req::size;
		}
		case nano::message_type::bulk_pull_account:
		{
			return nano::bulk_pull_account::size;
		}
		case nano::message_type::keepalive:
		{
			return nano::keepalive::size;
		}
		case nano::message_type::publish:
		{
			return nano::block::size (block_type ());
		}
		case nano::message_type::confirm_ack:
		{
			return nano::confirm_ack::size (count_get ());
		}
		case nano::message_type::confirm_req:
		{
			return nano::confirm_req::size (block_type (), count_get ());
		}
		case nano::message_type::node_id_handshake:
		{
			return nano::node_id_handshake::size (*this);
		}
		case nano::message_type::telemetry_ack:
		{
			return nano::telemetry_ack::size (*this);
		}
		case nano::message_type::asc_pull_req:
		{
			return nano::asc_pull_req::size (*this);
		}
		case nano::message_type::asc_pull_ack:
		{
			return nano::asc_pull_ack::size (*this);
		}
		default:
		{
			debug_assert (false);
			return 0;
		}
	}
}

bool nano::message_header::is_valid_message_type () const
{
	switch (type)
	{
		case nano::message_type::bulk_pull:
		case nano::message_type::bulk_push:
		case nano::message_type::telemetry_req:
		case nano::message_type::frontier_req:
		case nano::message_type::bulk_pull_account:
		case nano::message_type::keepalive:
		case nano::message_type::publish:
		case nano::message_type::confirm_ack:
		case nano::message_type::confirm_req:
		case nano::message_type::node_id_handshake:
		case nano::message_type::telemetry_ack:
		case nano::message_type::asc_pull_req:
		case nano::message_type::asc_pull_ack:
		{
			return true;
		}
		default:
		{
			return false;
		}
	}
}

void nano::message_header::operator() (nano::object_stream & obs) const
{
	obs.write ("type", type);
	obs.write ("network", nano::to_string (network));
	obs.write ("network_int", static_cast<uint16_t> (network));
	obs.write ("version", static_cast<uint16_t> (version_using));
	obs.write ("version_min", static_cast<uint16_t> (version_min));
	obs.write ("version_max", static_cast<uint16_t> (version_max));
	obs.write ("extensions", static_cast<uint16_t> (extensions.to_ulong ()));
	obs.write ("id", id);
}

/*
 * message
 */

nano::message::message (nano::network_constants const & constants, nano::message_type type_a) :
	header (constants, type_a)
{
}

nano::message::message (nano::message_header const & header_a) :
	header (header_a)
{
}

std::shared_ptr<std::vector<uint8_t>> nano::message::to_bytes () const
{
	auto bytes = std::make_shared<std::vector<uint8_t>> ();
	nano::vectorstream stream (*bytes);
	serialize (stream);
	return bytes;
}

nano::shared_const_buffer nano::message::to_shared_const_buffer () const
{
	return shared_const_buffer (to_bytes ());
}

nano::message_type nano::message::type () const
{
	return header.type;
}

void nano::message::operator() (nano::object_stream & obs) const
{
	obs.write ("header", header);
}

/*
 * keepalive
 */

nano::keepalive::keepalive (nano::network_constants const & constants) :
	message (constants, nano::message_type::keepalive)
{
	nano::endpoint endpoint (boost::asio::ip::address_v6{}, 0);
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i)
	{
		*i = endpoint;
	}
}

nano::keepalive::keepalive (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void nano::keepalive::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.keepalive (*this);
}

void nano::keepalive::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	for (auto i (peers.begin ()), j (peers.end ()); i != j; ++i)
	{
		debug_assert (i->address ().is_v6 ());
		auto bytes (i->address ().to_v6 ().to_bytes ());
		write (stream_a, bytes);
		write (stream_a, i->port ());
	}
}

bool nano::keepalive::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == nano::message_type::keepalive);
	auto error (false);
	for (auto i (peers.begin ()), j (peers.end ()); i != j && !error; ++i)
	{
		std::array<uint8_t, 16> address;
		uint16_t port;
		if (!try_read (stream_a, address) && !try_read (stream_a, port))
		{
			*i = nano::endpoint (boost::asio::ip::address_v6 (address), port);
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool nano::keepalive::operator== (nano::keepalive const & other_a) const
{
	return peers == other_a.peers;
}

void nano::keepalive::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("peers", peers);
}

/*
 * publish
 */

nano::publish::publish (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a, nano::uint128_t const & digest_a, nano::block_uniquer * uniquer_a) :
	message (header_a),
	digest (digest_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

nano::publish::publish (nano::network_constants const & constants, std::shared_ptr<nano::block> const & block_a) :
	message (constants, nano::message_type::publish),
	block (block_a)
{
	header.block_type_set (block->type ());
}

void nano::publish::serialize (nano::stream & stream_a) const
{
	debug_assert (block != nullptr);
	header.serialize (stream_a);
	block->serialize (stream_a);
}

bool nano::publish::deserialize (nano::stream & stream_a, nano::block_uniquer * uniquer_a)
{
	debug_assert (header.type == nano::message_type::publish);
	block = nano::deserialize_block (stream_a, header.block_type (), uniquer_a);
	auto result (block == nullptr);
	return result;
}

void nano::publish::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.publish (*this);
}

bool nano::publish::operator== (nano::publish const & other_a) const
{
	return *block == *other_a.block;
}

void nano::publish::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("block", block);
}

/*
 * confirm_req
 */

nano::confirm_req::confirm_req (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a, nano::block_uniquer * uniquer_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a, uniquer_a);
	}
}

nano::confirm_req::confirm_req (nano::network_constants const & constants, std::shared_ptr<nano::block> const & block_a) :
	message (constants, nano::message_type::confirm_req),
	block (block_a)
{
	header.block_type_set (block->type ());
}

nano::confirm_req::confirm_req (nano::network_constants const & constants, std::vector<std::pair<nano::block_hash, nano::root>> const & roots_hashes_a) :
	message (constants, nano::message_type::confirm_req),
	roots_hashes (roots_hashes_a)
{
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (nano::block_type::not_a_block);
	debug_assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

nano::confirm_req::confirm_req (nano::network_constants const & constants, nano::block_hash const & hash_a, nano::root const & root_a) :
	message (constants, nano::message_type::confirm_req),
	roots_hashes (std::vector<std::pair<nano::block_hash, nano::root>> (1, std::make_pair (hash_a, root_a)))
{
	debug_assert (!roots_hashes.empty ());
	// not_a_block (1) block type for hashes + roots request
	header.block_type_set (nano::block_type::not_a_block);
	debug_assert (roots_hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (roots_hashes.size ()));
}

void nano::confirm_req::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.confirm_req (*this);
}

void nano::confirm_req::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	if (header.block_type () == nano::block_type::not_a_block)
	{
		debug_assert (!roots_hashes.empty ());
		// Write hashes & roots
		for (auto & root_hash : roots_hashes)
		{
			write (stream_a, root_hash.first);
			write (stream_a, root_hash.second);
		}
	}
	else
	{
		debug_assert (block != nullptr);
		block->serialize (stream_a);
	}
}

bool nano::confirm_req::deserialize (nano::stream & stream_a, nano::block_uniquer * uniquer_a)
{
	bool result (false);
	debug_assert (header.type == nano::message_type::confirm_req);
	try
	{
		if (header.block_type () == nano::block_type::not_a_block)
		{
			uint8_t count (header.count_get ());
			for (auto i (0); i != count && !result; ++i)
			{
				nano::block_hash block_hash (0);
				nano::block_hash root (0);
				read (stream_a, block_hash);
				read (stream_a, root);
				if (!block_hash.is_zero () || !root.is_zero ())
				{
					roots_hashes.emplace_back (block_hash, root);
				}
			}

			result = roots_hashes.empty () || (roots_hashes.size () != count);
		}
		else
		{
			block = nano::deserialize_block (stream_a, header.block_type (), uniquer_a);
			result = block == nullptr;
		}
	}
	catch (std::runtime_error const &)
	{
		result = true;
	}

	return result;
}

bool nano::confirm_req::operator== (nano::confirm_req const & other_a) const
{
	bool equal (false);
	if (block != nullptr && other_a.block != nullptr)
	{
		equal = *block == *other_a.block;
	}
	else if (!roots_hashes.empty () && !other_a.roots_hashes.empty ())
	{
		equal = roots_hashes == other_a.roots_hashes;
	}
	return equal;
}

std::size_t nano::confirm_req::size (nano::block_type type_a, std::size_t count)
{
	std::size_t result (0);
	if (type_a != nano::block_type::invalid && type_a != nano::block_type::not_a_block)
	{
		result = nano::block::size (type_a);
	}
	else if (type_a == nano::block_type::not_a_block)
	{
		result = count * (sizeof (nano::uint256_union) + sizeof (nano::block_hash));
	}
	return result;
}

void nano::confirm_req::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("block", block);

	// Write roots as: [ { root: ##, hash: ## } ,...]
	obs.write ("roots", std::views::transform (roots_hashes, [] (auto const & root_hash) {
		return [&root_hash] (nano::object_stream & obs) {
			auto [root, hash] = root_hash;
			obs.write ("root", root);
			obs.write ("hash", hash);
		};
	}));
}

/*
 * confirm_ack
 */

nano::confirm_ack::confirm_ack (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a, nano::vote_uniquer * uniquer_a) :
	message (header_a),
	vote (nano::make_shared<nano::vote> (error_a, stream_a))
{
	if (!error_a && uniquer_a)
	{
		vote = uniquer_a->unique (vote);
	}
}

nano::confirm_ack::confirm_ack (nano::network_constants const & constants, std::shared_ptr<nano::vote> const & vote_a) :
	message (constants, nano::message_type::confirm_ack),
	vote (vote_a)
{
	header.block_type_set (nano::block_type::not_a_block);
	debug_assert (vote_a->hashes.size () < 16);
	header.count_set (static_cast<uint8_t> (vote_a->hashes.size ()));
}

void nano::confirm_ack::serialize (nano::stream & stream_a) const
{
	debug_assert (header.block_type () == nano::block_type::not_a_block || header.block_type () == nano::block_type::send || header.block_type () == nano::block_type::receive || header.block_type () == nano::block_type::open || header.block_type () == nano::block_type::change || header.block_type () == nano::block_type::state);
	header.serialize (stream_a);
	vote->serialize (stream_a);
}

bool nano::confirm_ack::operator== (nano::confirm_ack const & other_a) const
{
	auto result (*vote == *other_a.vote);
	return result;
}

void nano::confirm_ack::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.confirm_ack (*this);
}

std::size_t nano::confirm_ack::size (std::size_t count)
{
	std::size_t result = sizeof (nano::account) + sizeof (nano::signature) + sizeof (uint64_t) + count * sizeof (nano::block_hash);
	return result;
}

void nano::confirm_ack::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("vote", vote);
}

/*
 * frontier_req
 */

nano::frontier_req::frontier_req (nano::network_constants const & constants) :
	message (constants, nano::message_type::frontier_req)
{
}

nano::frontier_req::frontier_req (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void nano::frontier_req::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, start.bytes);
	write (stream_a, age);
	write (stream_a, count);
}

bool nano::frontier_req::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == nano::message_type::frontier_req);
	auto error (false);
	try
	{
		nano::read (stream_a, start.bytes);
		nano::read (stream_a, age);
		nano::read (stream_a, count);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void nano::frontier_req::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.frontier_req (*this);
}

bool nano::frontier_req::operator== (nano::frontier_req const & other_a) const
{
	return start == other_a.start && age == other_a.age && count == other_a.count;
}

void nano::frontier_req::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("start", start);
	obs.write ("age", age);
	obs.write ("count", count);
}

/*
 * bulk_pull
 */

nano::bulk_pull::bulk_pull (nano::network_constants const & constants) :
	message (constants, nano::message_type::bulk_pull)
{
}

nano::bulk_pull::bulk_pull (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void nano::bulk_pull::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull (*this);
}

void nano::bulk_pull::serialize (nano::stream & stream_a) const
{
	/*
	 * Ensure the "count_present" flag is set if there
	 * is a limit specifed.  Additionally, do not allow
	 * the "count_present" flag with a value of 0, since
	 * that is a sentinel which we use to mean "all blocks"
	 * and that is the behavior of not having the flag set
	 * so it is wasteful to do this.
	 */
	debug_assert ((count == 0 && !is_count_present ()) || (count != 0 && is_count_present ()));

	header.serialize (stream_a);
	write (stream_a, start);
	write (stream_a, end);

	if (is_count_present ())
	{
		std::array<uint8_t, extended_parameters_size> count_buffer{ { 0 } };
		decltype (count) count_little_endian;
		static_assert (sizeof (count_little_endian) < (count_buffer.size () - 1), "count must fit within buffer");

		count_little_endian = boost::endian::native_to_little (count);
		memcpy (count_buffer.data () + 1, &count_little_endian, sizeof (count_little_endian));

		write (stream_a, count_buffer);
	}
}

bool nano::bulk_pull::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == nano::message_type::bulk_pull);
	auto error (false);
	try
	{
		nano::read (stream_a, start);
		nano::read (stream_a, end);

		if (is_count_present ())
		{
			std::array<uint8_t, extended_parameters_size> extended_parameters_buffers;
			static_assert (sizeof (count) < (extended_parameters_buffers.size () - 1), "count must fit within buffer");

			nano::read (stream_a, extended_parameters_buffers);
			if (extended_parameters_buffers.front () != 0)
			{
				error = true;
			}
			else
			{
				memcpy (&count, extended_parameters_buffers.data () + 1, sizeof (count));
				boost::endian::little_to_native_inplace (count);
			}
		}
		else
		{
			count = 0;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool nano::bulk_pull::is_count_present () const
{
	return header.extensions.test (count_present_flag);
}

void nano::bulk_pull::set_count_present (bool value_a)
{
	header.extensions.set (count_present_flag, value_a);
}

void nano::bulk_pull::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("start", start);
	obs.write ("end", end);
	obs.write ("count", count);
}

/*
 * bulk_pull_account
 */

nano::bulk_pull_account::bulk_pull_account (nano::network_constants const & constants) :
	message (constants, nano::message_type::bulk_pull_account)
{
}

nano::bulk_pull_account::bulk_pull_account (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a) :
	message (header_a)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

void nano::bulk_pull_account::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.bulk_pull_account (*this);
}

void nano::bulk_pull_account::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	write (stream_a, account);
	write (stream_a, minimum_amount);
	write (stream_a, flags);
}

bool nano::bulk_pull_account::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == nano::message_type::bulk_pull_account);
	auto error (false);
	try
	{
		nano::read (stream_a, account);
		nano::read (stream_a, minimum_amount);
		nano::read (stream_a, flags);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void nano::bulk_pull_account::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("account", account);
	obs.write ("minimum_amount", minimum_amount);
	obs.write ("flags", static_cast<uint8_t> (flags)); // TODO: Prettier flag printing
}

/*
 * bulk_push
 */

nano::bulk_push::bulk_push (nano::network_constants const & constants) :
	message (constants, nano::message_type::bulk_push)
{
}

nano::bulk_push::bulk_push (nano::message_header const & header_a) :
	message (header_a)
{
}

bool nano::bulk_push::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == nano::message_type::bulk_push);
	return false;
}

void nano::bulk_push::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
}

void nano::bulk_push::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.bulk_push (*this);
}

void nano::bulk_push::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data
}

/*
 * telemetry_req
 */

nano::telemetry_req::telemetry_req (nano::network_constants const & constants) :
	message (constants, nano::message_type::telemetry_req)
{
}

nano::telemetry_req::telemetry_req (nano::message_header const & header_a) :
	message (header_a)
{
}

bool nano::telemetry_req::deserialize (nano::stream & stream_a)
{
	debug_assert (header.type == nano::message_type::telemetry_req);
	return false;
}

void nano::telemetry_req::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
}

void nano::telemetry_req::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.telemetry_req (*this);
}

void nano::telemetry_req::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data
}

/*
 * telemetry_ack
 */

nano::telemetry_ack::telemetry_ack (nano::network_constants const & constants) :
	message (constants, nano::message_type::telemetry_ack)
{
}

nano::telemetry_ack::telemetry_ack (bool & error_a, nano::stream & stream_a, nano::message_header const & message_header) :
	message (message_header)
{
	if (!error_a)
	{
		error_a = deserialize (stream_a);
	}
}

nano::telemetry_ack::telemetry_ack (nano::network_constants const & constants, nano::telemetry_data const & telemetry_data_a) :
	message (constants, nano::message_type::telemetry_ack),
	data (telemetry_data_a)
{
	debug_assert (telemetry_data::size + telemetry_data_a.unknown_data.size () <= message_header::telemetry_size_mask.to_ulong ()); // Maximum size the mask allows
	header.extensions &= ~message_header::telemetry_size_mask;
	header.extensions |= std::bitset<16> (static_cast<unsigned long long> (telemetry_data::size) + telemetry_data_a.unknown_data.size ());
}

void nano::telemetry_ack::serialize (nano::stream & stream_a) const
{
	header.serialize (stream_a);
	if (!is_empty_payload ())
	{
		data.serialize (stream_a);
	}
}

bool nano::telemetry_ack::deserialize (nano::stream & stream_a)
{
	auto error (false);
	debug_assert (header.type == nano::message_type::telemetry_ack);
	try
	{
		if (!is_empty_payload ())
		{
			data.deserialize (stream_a, nano::narrow_cast<uint16_t> (header.extensions.to_ulong ()));
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void nano::telemetry_ack::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.telemetry_ack (*this);
}

uint16_t nano::telemetry_ack::size () const
{
	return size (header);
}

uint16_t nano::telemetry_ack::size (nano::message_header const & message_header_a)
{
	return static_cast<uint16_t> ((message_header_a.extensions & message_header::telemetry_size_mask).to_ullong ());
}

bool nano::telemetry_ack::is_empty_payload () const
{
	return size () == 0;
}

void nano::telemetry_ack::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	if (!is_empty_payload ())
	{
		obs.write ("data", data);
	}
}

/*
 * telemetry_data
 */

void nano::telemetry_data::deserialize (nano::stream & stream_a, uint16_t payload_length_a)
{
	read (stream_a, signature);
	read (stream_a, node_id);
	read (stream_a, block_count);
	boost::endian::big_to_native_inplace (block_count);
	read (stream_a, cemented_count);
	boost::endian::big_to_native_inplace (cemented_count);
	read (stream_a, unchecked_count);
	boost::endian::big_to_native_inplace (unchecked_count);
	read (stream_a, account_count);
	boost::endian::big_to_native_inplace (account_count);
	read (stream_a, bandwidth_cap);
	boost::endian::big_to_native_inplace (bandwidth_cap);
	read (stream_a, peer_count);
	boost::endian::big_to_native_inplace (peer_count);
	read (stream_a, protocol_version);
	read (stream_a, uptime);
	boost::endian::big_to_native_inplace (uptime);
	read (stream_a, genesis_block.bytes);
	read (stream_a, major_version);
	read (stream_a, minor_version);
	read (stream_a, patch_version);
	read (stream_a, pre_release_version);
	read (stream_a, maker);

	uint64_t timestamp_l;
	read (stream_a, timestamp_l);
	boost::endian::big_to_native_inplace (timestamp_l);
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	read (stream_a, active_difficulty);
	boost::endian::big_to_native_inplace (active_difficulty);
	if (payload_length_a > latest_size)
	{
		read (stream_a, unknown_data, payload_length_a - latest_size);
	}
}

void nano::telemetry_data::serialize_without_signature (nano::stream & stream_a) const
{
	// All values should be serialized in big endian
	write (stream_a, node_id);
	write (stream_a, boost::endian::native_to_big (block_count));
	write (stream_a, boost::endian::native_to_big (cemented_count));
	write (stream_a, boost::endian::native_to_big (unchecked_count));
	write (stream_a, boost::endian::native_to_big (account_count));
	write (stream_a, boost::endian::native_to_big (bandwidth_cap));
	write (stream_a, boost::endian::native_to_big (peer_count));
	write (stream_a, protocol_version);
	write (stream_a, boost::endian::native_to_big (uptime));
	write (stream_a, genesis_block.bytes);
	write (stream_a, major_version);
	write (stream_a, minor_version);
	write (stream_a, patch_version);
	write (stream_a, pre_release_version);
	write (stream_a, maker);
	write (stream_a, boost::endian::native_to_big (std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ()));
	write (stream_a, boost::endian::native_to_big (active_difficulty));
	write (stream_a, unknown_data);
}

void nano::telemetry_data::serialize (nano::stream & stream_a) const
{
	write (stream_a, signature);
	serialize_without_signature (stream_a);
}

nano::error nano::telemetry_data::serialize_json (nano::jsonconfig & json, bool ignore_identification_metrics_a) const
{
	json.put ("block_count", block_count);
	json.put ("cemented_count", cemented_count);
	json.put ("unchecked_count", unchecked_count);
	json.put ("account_count", account_count);
	json.put ("bandwidth_cap", bandwidth_cap);
	json.put ("peer_count", peer_count);
	json.put ("protocol_version", protocol_version);
	json.put ("uptime", uptime);
	json.put ("genesis_block", genesis_block.to_string ());
	json.put ("major_version", major_version);
	json.put ("minor_version", minor_version);
	json.put ("patch_version", patch_version);
	json.put ("pre_release_version", pre_release_version);
	json.put ("maker", maker);
	json.put ("timestamp", std::chrono::duration_cast<std::chrono::milliseconds> (timestamp.time_since_epoch ()).count ());
	json.put ("active_difficulty", nano::to_string_hex (active_difficulty));
	// Keep these last for UI purposes
	if (!ignore_identification_metrics_a)
	{
		json.put ("node_id", node_id.to_node_id ());
		json.put ("signature", signature.to_string ());
	}
	return json.get_error ();
}

nano::error nano::telemetry_data::deserialize_json (nano::jsonconfig & json, bool ignore_identification_metrics_a)
{
	if (!ignore_identification_metrics_a)
	{
		std::string signature_l;
		json.get ("signature", signature_l);
		if (!json.get_error ())
		{
			if (signature.decode_hex (signature_l))
			{
				json.get_error ().set ("Could not deserialize signature");
			}
		}

		std::string node_id_l;
		json.get ("node_id", node_id_l);
		if (!json.get_error ())
		{
			if (node_id.decode_node_id (node_id_l))
			{
				json.get_error ().set ("Could not deserialize node id");
			}
		}
	}

	json.get ("block_count", block_count);
	json.get ("cemented_count", cemented_count);
	json.get ("unchecked_count", unchecked_count);
	json.get ("account_count", account_count);
	json.get ("bandwidth_cap", bandwidth_cap);
	json.get ("peer_count", peer_count);
	json.get ("protocol_version", protocol_version);
	json.get ("uptime", uptime);
	std::string genesis_block_l;
	json.get ("genesis_block", genesis_block_l);
	if (!json.get_error ())
	{
		if (genesis_block.decode_hex (genesis_block_l))
		{
			json.get_error ().set ("Could not deserialize genesis block");
		}
	}
	json.get ("major_version", major_version);
	json.get ("minor_version", minor_version);
	json.get ("patch_version", patch_version);
	json.get ("pre_release_version", pre_release_version);
	json.get ("maker", maker);
	auto timestamp_l = json.get<uint64_t> ("timestamp");
	timestamp = std::chrono::system_clock::time_point (std::chrono::milliseconds (timestamp_l));
	auto current_active_difficulty_text = json.get<std::string> ("active_difficulty");
	auto ec = nano::from_string_hex (current_active_difficulty_text, active_difficulty);
	debug_assert (!ec);
	return json.get_error ();
}

std::string nano::telemetry_data::to_string () const
{
	nano::jsonconfig jc;
	serialize_json (jc, true);
	std::stringstream ss;
	jc.write (ss);
	return ss.str ();
}

bool nano::telemetry_data::operator== (nano::telemetry_data const & data_a) const
{
	return (signature == data_a.signature && node_id == data_a.node_id && block_count == data_a.block_count && cemented_count == data_a.cemented_count && unchecked_count == data_a.unchecked_count && account_count == data_a.account_count && bandwidth_cap == data_a.bandwidth_cap && uptime == data_a.uptime && peer_count == data_a.peer_count && protocol_version == data_a.protocol_version && genesis_block == data_a.genesis_block && major_version == data_a.major_version && minor_version == data_a.minor_version && patch_version == data_a.patch_version && pre_release_version == data_a.pre_release_version && maker == data_a.maker && timestamp == data_a.timestamp && active_difficulty == data_a.active_difficulty && unknown_data == data_a.unknown_data);
}

bool nano::telemetry_data::operator!= (nano::telemetry_data const & data_a) const
{
	return !(*this == data_a);
}

void nano::telemetry_data::sign (nano::keypair const & node_id_a)
{
	debug_assert (node_id == node_id_a.pub);
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	signature = nano::sign_message (node_id_a.prv, node_id_a.pub, bytes.data (), bytes.size ());
}

bool nano::telemetry_data::validate_signature () const
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream (bytes);
		serialize_without_signature (stream);
	}

	return nano::validate_message (node_id, bytes.data (), bytes.size (), signature);
}

void nano::telemetry_data::operator() (nano::object_stream & obs) const
{
	// TODO: Implement writing data
}

/*
 * node_id_handshake
 */

nano::node_id_handshake::node_id_handshake (bool & error_a, nano::stream & stream_a, nano::message_header const & header_a) :
	message (header_a)
{
	error_a = deserialize (stream_a);
}

nano::node_id_handshake::node_id_handshake (nano::network_constants const & constants, std::optional<query_payload> query_a, std::optional<response_payload> response_a) :
	message (constants, nano::message_type::node_id_handshake),
	query{ query_a },
	response{ response_a }
{
	if (query)
	{
		header.flag_set (query_flag);
		header.flag_set (v2_flag); // Always indicate support for V2 handshake when querying, old peers will just ignore it
	}
	if (response)
	{
		header.flag_set (response_flag);
		header.flag_set (v2_flag, response->v2.has_value ()); // We only use V2 handshake when replying to peers that indicated support for it
	}
}

void nano::node_id_handshake::serialize (nano::stream & stream) const
{
	header.serialize (stream);
	if (query)
	{
		query->serialize (stream);
	}
	if (response)
	{
		response->serialize (stream);
	}
}

bool nano::node_id_handshake::deserialize (nano::stream & stream)
{
	debug_assert (header.type == nano::message_type::node_id_handshake);
	bool error = false;
	try
	{
		if (is_query (header))
		{
			query_payload pld{};
			pld.deserialize (stream);
			query = pld;
		}

		if (is_response (header))
		{
			response_payload pld{};
			pld.deserialize (stream, header);
			response = pld;
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

bool nano::node_id_handshake::is_query (nano::message_header const & header)
{
	debug_assert (header.type == nano::message_type::node_id_handshake);
	bool result = header.extensions.test (query_flag);
	return result;
}

bool nano::node_id_handshake::is_response (nano::message_header const & header)
{
	debug_assert (header.type == nano::message_type::node_id_handshake);
	bool result = header.extensions.test (response_flag);
	return result;
}

bool nano::node_id_handshake::is_v2 (nano::message_header const & header)
{
	debug_assert (header.type == nano::message_type::node_id_handshake);
	bool result = header.extensions.test (v2_flag);
	return result;
}

bool nano::node_id_handshake::is_v2 () const
{
	return is_v2 (header);
}

void nano::node_id_handshake::visit (nano::message_visitor & visitor_a) const
{
	visitor_a.node_id_handshake (*this);
}

std::size_t nano::node_id_handshake::size () const
{
	return size (header);
}

std::size_t nano::node_id_handshake::size (nano::message_header const & header)
{
	std::size_t result = 0;
	if (is_query (header))
	{
		result += query_payload::size;
	}
	if (is_response (header))
	{
		result += response_payload::size (header);
	}
	return result;
}

void nano::node_id_handshake::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("query", query);
	obs.write ("response", response);
}

/*
 * node_id_handshake::query_payload
 */

void nano::node_id_handshake::query_payload::serialize (nano::stream & stream) const
{
	nano::write (stream, cookie);
}

void nano::node_id_handshake::query_payload::deserialize (nano::stream & stream)
{
	nano::read (stream, cookie);
}

void nano::node_id_handshake::query_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("cookie", cookie);
}

/*
 * node_id_handshake::response_payload
 */

void nano::node_id_handshake::response_payload::serialize (nano::stream & stream) const
{
	if (v2)
	{
		nano::write (stream, node_id);
		nano::write (stream, v2->salt);
		nano::write (stream, v2->genesis);
		nano::write (stream, signature);
	}
	// TODO: Remove legacy handshake
	else
	{
		nano::write (stream, node_id);
		nano::write (stream, signature);
	}
}

void nano::node_id_handshake::response_payload::deserialize (nano::stream & stream, nano::message_header const & header)
{
	if (is_v2 (header))
	{
		nano::read (stream, node_id);
		v2_payload pld{};
		nano::read (stream, pld.salt);
		nano::read (stream, pld.genesis);
		v2 = pld;
		nano::read (stream, signature);
	}
	else
	{
		nano::read (stream, node_id);
		nano::read (stream, signature);
	}
}

std::size_t nano::node_id_handshake::response_payload::size (const nano::message_header & header)
{
	return is_v2 (header) ? size_v2 : size_v1;
}

std::vector<uint8_t> nano::node_id_handshake::response_payload::data_to_sign (const nano::uint256_union & cookie) const
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream stream{ bytes };

		if (v2)
		{
			nano::write (stream, cookie);
			nano::write (stream, v2->salt);
			nano::write (stream, v2->genesis);
		}
		// TODO: Remove legacy handshake
		else
		{
			nano::write (stream, cookie);
		}
	}
	return bytes;
}

void nano::node_id_handshake::response_payload::sign (const nano::uint256_union & cookie, nano::keypair const & key)
{
	debug_assert (key.pub == node_id);
	auto data = data_to_sign (cookie);
	signature = nano::sign_message (key.prv, key.pub, data.data (), data.size ());
	debug_assert (validate (cookie));
}

bool nano::node_id_handshake::response_payload::validate (const nano::uint256_union & cookie) const
{
	auto data = data_to_sign (cookie);
	if (nano::validate_message (node_id, data.data (), data.size (), signature)) // true => error
	{
		return false; // Fail
	}
	return true; // OK
}

void nano::node_id_handshake::response_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("node_id", node_id);
	obs.write ("signature", signature);

	obs.write ("v2", v2.has_value ());
	if (v2)
	{
		obs.write ("salt", v2->salt);
		obs.write ("genesis", v2->genesis);
	}
}

/*
 * asc_pull_req
 */

nano::asc_pull_req::asc_pull_req (const nano::network_constants & constants) :
	message (constants, nano::message_type::asc_pull_req)
{
}

nano::asc_pull_req::asc_pull_req (bool & error, nano::stream & stream, const nano::message_header & header) :
	message (header)
{
	error = deserialize (stream);
}

void nano::asc_pull_req::visit (nano::message_visitor & visitor) const
{
	visitor.asc_pull_req (*this);
}

void nano::asc_pull_req::serialize (nano::stream & stream) const
{
	header.serialize (stream);
	nano::write (stream, type);
	nano::write_big_endian (stream, id);

	serialize_payload (stream);
}

bool nano::asc_pull_req::deserialize (nano::stream & stream)
{
	debug_assert (header.type == nano::message_type::asc_pull_req);
	bool error = false;
	try
	{
		nano::read (stream, type);
		nano::read_big_endian (stream, id);

		deserialize_payload (stream);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void nano::asc_pull_req::serialize_payload (nano::stream & stream) const
{
	debug_assert (verify_consistency ());

	std::visit ([&stream] (auto && pld) { pld.serialize (stream); }, payload);
}

void nano::asc_pull_req::deserialize_payload (nano::stream & stream)
{
	switch (type)
	{
		case asc_pull_type::blocks:
		{
			blocks_payload pld;
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::account_info:
		{
			account_info_payload pld;
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		default:
			throw std::runtime_error ("Unknown asc_pull_type");
	}
}

void nano::asc_pull_req::update_header ()
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream payload_stream (bytes);
		serialize_payload (payload_stream);
	}
	debug_assert (bytes.size () <= 65535u); // Max int16 for storing size
	debug_assert (bytes.size () >= 1);
	header.extensions = std::bitset<16> (bytes.size ());
}

std::size_t nano::asc_pull_req::size (const nano::message_header & header)
{
	uint16_t payload_length = nano::narrow_cast<uint16_t> (header.extensions.to_ulong ());
	return partial_size + payload_length;
}

bool nano::asc_pull_req::verify_consistency () const
{
	struct consistency_visitor
	{
		nano::asc_pull_type type;

		void operator() (empty_payload) const
		{
			debug_assert (false, "missing payload");
		}
		void operator() (blocks_payload) const
		{
			debug_assert (type == asc_pull_type::blocks);
		}
		void operator() (account_info_payload) const
		{
			debug_assert (type == asc_pull_type::account_info);
		}
	};
	std::visit (consistency_visitor{ type }, payload);
	return true; // Just for convenience of calling from asserts
}

void nano::asc_pull_req::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("type", type);
	obs.write ("id", id);

	std::visit ([&obs] (auto && pld) { pld (obs); }, payload); // Log payload
}

/*
 * asc_pull_req::blocks_payload
 */

void nano::asc_pull_req::blocks_payload::serialize (nano::stream & stream) const
{
	nano::write (stream, start);
	nano::write (stream, count);
	nano::write (stream, start_type);
}

void nano::asc_pull_req::blocks_payload::deserialize (nano::stream & stream)
{
	nano::read (stream, start);
	nano::read (stream, count);
	nano::read (stream, start_type);
}

void nano::asc_pull_req::blocks_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("start", start);
	obs.write ("start_type", start_type);
	obs.write ("count", count);
}

/*
 * asc_pull_req::account_info_payload
 */

void nano::asc_pull_req::account_info_payload::serialize (stream & stream) const
{
	nano::write (stream, target);
	nano::write (stream, target_type);
}

void nano::asc_pull_req::account_info_payload::deserialize (stream & stream)
{
	nano::read (stream, target);
	nano::read (stream, target_type);
}

void nano::asc_pull_req::account_info_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("target", target);
	obs.write ("target_type", target_type);
}

/*
 * asc_pull_ack
 */

nano::asc_pull_ack::asc_pull_ack (const nano::network_constants & constants) :
	message (constants, nano::message_type::asc_pull_ack)
{
}

nano::asc_pull_ack::asc_pull_ack (bool & error, nano::stream & stream, const nano::message_header & header) :
	message (header)
{
	error = deserialize (stream);
}

void nano::asc_pull_ack::visit (nano::message_visitor & visitor) const
{
	visitor.asc_pull_ack (*this);
}

void nano::asc_pull_ack::serialize (nano::stream & stream) const
{
	debug_assert (header.extensions.to_ulong () > 0); // Block payload must have least `not_a_block` terminator
	header.serialize (stream);
	nano::write (stream, type);
	nano::write_big_endian (stream, id);

	serialize_payload (stream);
}

bool nano::asc_pull_ack::deserialize (nano::stream & stream)
{
	debug_assert (header.type == nano::message_type::asc_pull_ack);
	bool error = false;
	try
	{
		nano::read (stream, type);
		nano::read_big_endian (stream, id);

		deserialize_payload (stream);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void nano::asc_pull_ack::serialize_payload (nano::stream & stream) const
{
	debug_assert (verify_consistency ());

	std::visit ([&stream] (auto && pld) { pld.serialize (stream); }, payload);
}

void nano::asc_pull_ack::deserialize_payload (nano::stream & stream)
{
	switch (type)
	{
		case asc_pull_type::blocks:
		{
			blocks_payload pld;
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		case asc_pull_type::account_info:
		{
			account_info_payload pld;
			pld.deserialize (stream);
			payload = pld;
			break;
		}
		default:
			throw std::runtime_error ("Unknown asc_pull_type");
	}
}

void nano::asc_pull_ack::update_header ()
{
	std::vector<uint8_t> bytes;
	{
		nano::vectorstream payload_stream (bytes);
		serialize_payload (payload_stream);
	}
	debug_assert (bytes.size () <= 65535u); // Max int16 for storing size
	debug_assert (bytes.size () >= 1);
	header.extensions = std::bitset<16> (bytes.size ());
}

std::size_t nano::asc_pull_ack::size (const nano::message_header & header)
{
	uint16_t payload_length = nano::narrow_cast<uint16_t> (header.extensions.to_ulong ());
	return partial_size + payload_length;
}

bool nano::asc_pull_ack::verify_consistency () const
{
	struct consistency_visitor
	{
		nano::asc_pull_type type;

		void operator() (empty_payload) const
		{
			debug_assert (false, "missing payload");
		}
		void operator() (blocks_payload) const
		{
			debug_assert (type == asc_pull_type::blocks);
		}
		void operator() (account_info_payload) const
		{
			debug_assert (type == asc_pull_type::account_info);
		}
	};
	std::visit (consistency_visitor{ type }, payload);
	return true; // Just for convenience of calling from asserts
}

void nano::asc_pull_ack::operator() (nano::object_stream & obs) const
{
	nano::message::operator() (obs); // Write common data

	obs.write ("type", type);
	obs.write ("id", id);

	std::visit ([&obs] (auto && pld) { pld (obs); }, payload); // Log payload
}

/*
 * asc_pull_ack::blocks_payload
 */

void nano::asc_pull_ack::blocks_payload::serialize (nano::stream & stream) const
{
	debug_assert (blocks.size () <= max_blocks);
	for (auto & block : blocks)
	{
		debug_assert (block != nullptr);
		nano::serialize_block (stream, *block);
	}
	// For convenience, end with null block terminator
	nano::serialize_block_type (stream, nano::block_type::not_a_block);
}

void nano::asc_pull_ack::blocks_payload::deserialize (nano::stream & stream)
{
	auto current = nano::deserialize_block (stream);
	while (current && blocks.size () < max_blocks)
	{
		blocks.push_back (current);
		current = nano::deserialize_block (stream);
	}
}

void nano::asc_pull_ack::blocks_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("blocks", blocks);
}

/*
 * asc_pull_ack::account_info_payload
 */

void nano::asc_pull_ack::account_info_payload::serialize (nano::stream & stream) const
{
	nano::write (stream, account);
	nano::write (stream, open);
	nano::write (stream, head);
	nano::write_big_endian (stream, block_count);
	nano::write (stream, conf_frontier);
	nano::write_big_endian (stream, conf_height);
}

void nano::asc_pull_ack::account_info_payload::deserialize (nano::stream & stream)
{
	nano::read (stream, account);
	nano::read (stream, open);
	nano::read (stream, head);
	nano::read_big_endian (stream, block_count);
	nano::read (stream, conf_frontier);
	nano::read_big_endian (stream, conf_height);
}

void nano::asc_pull_ack::account_info_payload::operator() (nano::object_stream & obs) const
{
	obs.write ("account", account);
	obs.write ("open", open);
	obs.write ("head", head);
	obs.write ("block_count", block_count);
	obs.write ("conf_frontier", conf_frontier);
	obs.write ("conf_height", conf_height);
}

/*
 *
 */

std::string_view nano::to_string (nano::message_type type)
{
	switch (type)
	{
		case nano::message_type::invalid:
			return "invalid";
		case nano::message_type::not_a_type:
			return "not_a_type";
		case nano::message_type::keepalive:
			return "keepalive";
		case nano::message_type::publish:
			return "publish";
		case nano::message_type::confirm_req:
			return "confirm_req";
		case nano::message_type::confirm_ack:
			return "confirm_ack";
		case nano::message_type::bulk_pull:
			return "bulk_pull";
		case nano::message_type::bulk_push:
			return "bulk_push";
		case nano::message_type::frontier_req:
			return "frontier_req";
		case nano::message_type::node_id_handshake:
			return "node_id_handshake";
		case nano::message_type::bulk_pull_account:
			return "bulk_pull_account";
		case nano::message_type::telemetry_req:
			return "telemetry_req";
		case nano::message_type::telemetry_ack:
			return "telemetry_ack";
		case nano::message_type::asc_pull_req:
			return "asc_pull_req";
		case nano::message_type::asc_pull_ack:
			return "asc_pull_ack";
			// default case intentionally omitted to cause warnings for unhandled enums
	}

	return "n/a";
}

nano::stat::detail nano::to_stat_detail (nano::message_type type)
{
	switch (type)
	{
		case nano::message_type::invalid:
			return nano::stat::detail::invalid;
		case nano::message_type::not_a_type:
			return nano::stat::detail::not_a_type;
		case nano::message_type::keepalive:
			return nano::stat::detail::keepalive;
		case nano::message_type::publish:
			return nano::stat::detail::publish;
		case nano::message_type::confirm_req:
			return nano::stat::detail::confirm_req;
		case nano::message_type::confirm_ack:
			return nano::stat::detail::confirm_ack;
		case nano::message_type::bulk_pull:
			return nano::stat::detail::bulk_pull;
		case nano::message_type::bulk_push:
			return nano::stat::detail::bulk_push;
		case nano::message_type::frontier_req:
			return nano::stat::detail::frontier_req;
		case nano::message_type::node_id_handshake:
			return nano::stat::detail::node_id_handshake;
		case nano::message_type::bulk_pull_account:
			return nano::stat::detail::bulk_pull_account;
		case nano::message_type::telemetry_req:
			return nano::stat::detail::telemetry_req;
		case nano::message_type::telemetry_ack:
			return nano::stat::detail::telemetry_ack;
		case nano::message_type::asc_pull_req:
			return nano::stat::detail::asc_pull_req;
		case nano::message_type::asc_pull_ack:
			return nano::stat::detail::asc_pull_ack;
			// default case intentionally omitted to cause warnings for unhandled enums
	}
	debug_assert (false);
	return {};
}

nano::log::detail nano::to_log_detail (nano::message_type type)
{
	switch (type)
	{
		case nano::message_type::invalid:
			return nano::log::detail::invalid;
		case nano::message_type::not_a_type:
			return nano::log::detail::not_a_type;
		case nano::message_type::keepalive:
			return nano::log::detail::keepalive;
		case nano::message_type::publish:
			return nano::log::detail::publish;
		case nano::message_type::confirm_req:
			return nano::log::detail::confirm_req;
		case nano::message_type::confirm_ack:
			return nano::log::detail::confirm_ack;
		case nano::message_type::bulk_pull:
			return nano::log::detail::bulk_pull;
		case nano::message_type::bulk_push:
			return nano::log::detail::bulk_push;
		case nano::message_type::frontier_req:
			return nano::log::detail::frontier_req;
		case nano::message_type::node_id_handshake:
			return nano::log::detail::node_id_handshake;
		case nano::message_type::bulk_pull_account:
			return nano::log::detail::bulk_pull_account;
		case nano::message_type::telemetry_req:
			return nano::log::detail::telemetry_req;
		case nano::message_type::telemetry_ack:
			return nano::log::detail::telemetry_ack;
		case nano::message_type::asc_pull_req:
			return nano::log::detail::asc_pull_req;
		case nano::message_type::asc_pull_ack:
			return nano::log::detail::asc_pull_ack;
			// default case intentionally omitted to cause warnings for unhandled enums
	}
	debug_assert (false);
	return {};
}