#pragma once

#include <nano/crypto_lib/random_pool.hpp>
#include <nano/node/messages.hpp>

#include <cstdlib>

namespace nano::bootstrap_ascending
{
using id_t = uint64_t;

static nano::bootstrap_ascending::id_t generate_id ()
{
	return nano::random_pool::generate<nano::bootstrap_ascending::id_t> ();
}

template <typename Self, class Response>
class tag_base
{
public:
	void process_response (nano::asc_pull_ack::payload_variant const & response, auto & service) const
	{
		std::visit ([&] (auto const & payload) { process (payload, service); }, response);
	}

	void process (Response const & response, auto & service) const
	{
		service.process (response, *static_cast<Self const *> (this));
	}

	// Fallback
	void process (auto const & response, auto & service) const
	{
		nano::asc_pull_ack::payload_variant{ response }; // Force compilation error if response is not part of variant
		debug_assert (false, "invalid payload");
	}
};

class pull_blocks_tag : public tag_base<pull_blocks_tag, nano::asc_pull_ack::blocks_payload>
{
public:
	enum class query_type
	{
		blocks_by_hash,
		blocks_by_account,
	};

	const nano::account account{};
	nano::hash_or_account start{};
	query_type type{};

	explicit pull_blocks_tag (nano::account account) :
		account{ account } {};

public: // Verify
	enum class verify_result
	{
		ok,
		nothing_new,
		invalid,
	};

	verify_result verify (nano::asc_pull_ack::blocks_payload const & response) const;
};
}
