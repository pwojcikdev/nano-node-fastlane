#include <nano/messages/errors.hpp>

/*
 * error_messages
 */

std::string nano::error_messages_category::message (int ev) const
{
	switch (static_cast<nano::error_messages> (ev))
	{
		case nano::error_messages::generic:
			return "Unknown error";
		case nano::error_messages::invalid_header:
			return "Invalid message header";
		case nano::error_messages::invalid_network:
			return "Invalid network";
		case nano::error_messages::invalid_type:
			return "Invalid message type";
		case nano::error_messages::outdated_version:
			return "Outdated version";
		case nano::error_messages::size_too_large:
			return "Message size is too large";
	}

	return "Invalid error code";
}