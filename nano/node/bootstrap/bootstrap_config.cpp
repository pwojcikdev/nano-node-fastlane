#include <nano/lib/tomlconfig.hpp>
#include <nano/node/bootstrap/bootstrap_config.hpp>

/*
 * account_sets_config
 */
nano::error nano::account_sets_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("consideration_count", consideration_count);
	toml.get ("priorities_max", priorities_max);
	toml.get ("blocking_max", blocking_max);

	auto cooldown_l = cooldown.count ();
	toml.get ("cooldown", cooldown_l);
	cooldown = std::chrono::milliseconds{ cooldown_l };

	return toml.get_error ();
}

nano::error nano::account_sets_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("consideration_count", consideration_count, "Limit the number of account candidates to consider and also the number of iterations.\ntype:uint64");
	toml.put ("priorities_max", priorities_max, "Cutoff size limit for the priority list.\ntype:uint64");
	toml.put ("blocking_max", blocking_max, "Cutoff size limit for the blocked accounts from the priority list.\ntype:uint64");
	toml.put ("cooldown", cooldown.count (), "Waiting time for an account to become available.\ntype:milliseconds");

	return toml.get_error ();
}

/*
 * bootstrap_ascending_config
 */
nano::error nano::bootstrap_ascending_config::deserialize (nano::tomlconfig & toml)
{
	toml.get ("requests_limit", requests_limit);
	toml.get ("database_requests_limit", database_requests_limit);
	toml.get ("pull_count", pull_count);

	auto timeout_l = timeout.count ();
	toml.get ("timeout", timeout_l);
	timeout = std::chrono::milliseconds{ timeout_l };

	toml.get ("throttle_coefficient", throttle_coefficient);

	auto throttle_wait_l = throttle_wait.count ();
	toml.get ("throttle_wait", throttle_wait_l);
	throttle_wait = std::chrono::milliseconds{ throttle_wait_l };

	if (toml.has_key ("account_sets"))
	{
		auto config_l = toml.get_required_child ("account_sets");
		account_sets.deserialize (config_l);
	}

	return toml.get_error ();
}

nano::error nano::bootstrap_ascending_config::serialize (nano::tomlconfig & toml) const
{
	toml.put ("requests_limit", requests_limit, "Request limit to ascending bootstrap after which requests will be dropped.\nNote: changing to unlimited (0) is not recommended.\ntype:uint64");
	toml.put ("database_requests_limit", database_requests_limit, "Request limit for accounts from database after which requests will be dropped.\nNote: changing to unlimited (0) is not recommended as this operation competes for resources on querying the database.\ntype:uint64");
	toml.put ("pull_count", pull_count, "Number of requested blocks for ascending bootstrap request.\ntype:uint64");
	toml.put ("timeout", timeout.count (), "Timeout in milliseconds for incoming ascending bootstrap messages to be processed.\ntype:milliseconds");
	toml.put ("throttle_coefficient", throttle_coefficient, "Scales the number of samples to track for bootstrap throttling.\ntype:uint64");
	toml.put ("throttle_wait", throttle_wait.count (), "Length of time to wait between requests when throttled.\ntype:milliseconds");

	nano::tomlconfig account_sets_l;
	account_sets.serialize (account_sets_l);
	toml.put_child ("account_sets", account_sets_l);

	return toml.get_error ();
}
