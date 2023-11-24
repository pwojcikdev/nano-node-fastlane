#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/lib/observer_set.hpp>
#include <nano/lib/timer.hpp>
#include <nano/node/bandwidth_limiter.hpp>
#include <nano/node/bootstrap_ascending/account_scan.hpp>
#include <nano/node/bootstrap_ascending/common.hpp>
#include <nano/node/bootstrap_ascending/peer_scoring.hpp>
#include <nano/node/bootstrap_server.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <thread>

namespace mi = boost::multi_index;

namespace nano
{
class block_processor;
class ledger;
class network;
class node_config;

namespace transport
{
	class channel;
}
}

namespace nano::bootstrap_ascending
{
class lazy_pulling final
{
public:
	class tag : public nano::bootstrap_ascending::tag_base<tag, nano::asc_pull_ack::account_info_payload>
	{
	};
};

class config final
{
public:
	nano::error deserialize (nano::tomlconfig & toml);
	nano::error serialize (nano::tomlconfig & toml) const;

	// Maximum number of un-responded requests per channel
	std::size_t requests_limit{ 64 };
	std::size_t database_rate_limit{ 10 };
	std::size_t pull_count{ nano::bootstrap_server::max_blocks };
	std::chrono::milliseconds timeout{ 1000 * 5 };
	std::chrono::milliseconds throttle_wait{ 100 };

	account_sets_config account_sets;
};

class service final
{
public:
	service (nano::node_config &, nano::block_processor &, nano::ledger &, nano::network &, nano::stats &);
	~service ();

	void start ();
	void stop ();

	/**
	 * Process `asc_pull_ack` message coming from network
	 */
	void process (nano::asc_pull_ack const & message, std::shared_ptr<nano::transport::channel> const & channel);

public: // Info
	std::unique_ptr<nano::container_info_component> collect_container_info (std::string const & name);
	std::size_t score_size () const;

private: // Dependencies
	nano::node_config & config;
	nano::network_constants & network_consts;
	nano::block_processor & block_processor;
	nano::ledger & ledger;
	nano::network & network;
	nano::stats & stats;

public: // Strategies
	nano::bootstrap_ascending::account_scan account_scan;
	nano::bootstrap_ascending::lazy_pulling lazy_pulling;

	using tag_strategy_variant = std::variant<account_scan::tag, lazy_pulling::tag>;

	bool request (tag_strategy_variant const &, nano::asc_pull_req::payload_variant const &);

public: // Tag
	struct async_tag
	{
		tag_strategy_variant strategy;
		nano::bootstrap_ascending::id_t id{ 0 };
		std::chrono::steady_clock::time_point time{};
	};

public: // Events
	nano::observer_set<async_tag const &, std::shared_ptr<nano::transport::channel> &> on_request;
	nano::observer_set<async_tag const &> on_reply;
	nano::observer_set<async_tag const &> on_timeout;

private:
	void run ();
	void track (async_tag const & tag);

	/* Waits for channel with free capacity for bootstrap messages */
	std::shared_ptr<nano::transport::channel> wait_available_channel ();

public:
	void process (nano::asc_pull_ack::blocks_payload const & response, account_scan::tag const &);
	void process (nano::asc_pull_ack::account_info_payload const & response, lazy_pulling::tag const &);

private:
	// clang-format off
	class tag_sequenced {};
	class tag_id {};
	class tag_account {};

	using ordered_tags = boost::multi_index_container<async_tag,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequenced>>,
		mi::hashed_unique<mi::tag<tag_id>,
			mi::member<async_tag, nano::bootstrap_ascending::id_t, &async_tag::id>>
	>>;
	// clang-format on
	ordered_tags tags;

	nano::bootstrap_ascending::peer_scoring scoring;

	bool stopped{ false };
	mutable nano::mutex mutex;
	mutable nano::condition_variable condition;
	std::thread thread;
};
}
