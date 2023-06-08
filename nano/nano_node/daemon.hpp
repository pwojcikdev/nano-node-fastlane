#include <nano/lib/logging.hpp>

namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace nano
{
class node_flags;

class daemon
{
	nano::nlogger nlogger{ "daemon" };

public:
	void run (boost::filesystem::path const &, nano::node_flags const & flags);
};
}
