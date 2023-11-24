#include <nano/node/bootstrap_ascending/common.hpp>

/*
 * pull_blocks_tag
 */

/**
 * Verifies whether the received response is valid. Returns:
 * - invalid: when received blocks do not correspond to requested hash/account or they do not make a valid chain
 * - nothing_new: when received response indicates that the account chain does not have more blocks
 * - ok: otherwise, if all checks pass
 */
auto nano::bootstrap_ascending::pull_blocks_tag::verify (const nano::asc_pull_ack::blocks_payload & response) const -> verify_result
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