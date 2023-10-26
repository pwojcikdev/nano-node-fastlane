#include <nano/lib/numbers.hpp>
#include <nano/lib/object_stream.hpp>
#include <nano/secure/utility.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

TEST (object_stream, primitive)
{
	// Spacing
	std::cout << std::endl;

	{
		nano::object_stream obs{ std::cout };
		obs.write ("field_name", "field_value");

		std::cout << std::endl;
	}

	{
		nano::object_stream obs{ std::cout };
		obs.write ("bool_field", true);
		obs.write ("bool_field", false);

		std::cout << std::endl;
	}

	{
		nano::object_stream obs{ std::cout };
		obs.write ("int_field", 1234);
		obs.write ("int_field", -1234);
		obs.write ("int_field", std::numeric_limits<int>::max ());
		obs.write ("int_field", std::numeric_limits<int>::min ());

		std::cout << std::endl;
	}

	{
		nano::object_stream obs{ std::cout };
		obs.write ("uint64_field", (uint64_t)1234);
		obs.write ("uint64_field", (uint64_t)-1234);
		obs.write ("uint64_field", std::numeric_limits<uint64_t>::max ());
		obs.write ("uint64_field", std::numeric_limits<uint64_t>::min ());

		std::cout << std::endl;
	}

	{
		nano::object_stream obs{ std::cout };
		obs.write ("float_field", 1234.5678f);
		obs.write ("float_field", -1234.5678f);
		obs.write ("float_field", std::numeric_limits<float>::max ());
		obs.write ("float_field", std::numeric_limits<float>::min ());
		obs.write ("float_field", std::numeric_limits<float>::lowest ());

		std::cout << std::endl;
	}

	{
		nano::object_stream obs{ std::cout };
		obs.write ("double_field", 1234.5678f);
		obs.write ("double_field", -1234.5678f);
		obs.write ("double_field", std::numeric_limits<double>::max ());
		obs.write ("double_field", std::numeric_limits<double>::min ());
		obs.write ("double_field", std::numeric_limits<double>::lowest ());

		std::cout << std::endl;
	}

	// Spacing
	std::cout << std::endl;
}

TEST (object_stream, object_writer_basic)
{
	// Spacing
	std::cout << std::endl;

	{
		nano::object_stream obs{ std::cout };
		obs.write ("object_field", [] (nano::object_stream & obs) {
			obs.write ("field1", "value1");
			obs.write ("field2", "value2");
			obs.write ("field3", true);
			obs.write ("field4", 1234);
		});

		std::cout << std::endl;
	}

	// Spacing
	std::cout << std::endl;
}

TEST (object_stream, object_writer_nested)
{
	// Spacing
	std::cout << std::endl;

	{
		nano::object_stream obs{ std::cout };
		obs.write ("object_field", [] (nano::object_stream & obs) {
			obs.write ("field1", "value1");

			obs.write ("nested_object", [] (nano::object_stream & obs) {
				obs.write ("nested_field1", "nested_value1");
				obs.write ("nested_field2", false);
				obs.write ("nested_field3", -1234);
			});

			obs.write ("field2", "value2");
			obs.write ("field3", true);
			obs.write ("field4", 1234);
		});

		std::cout << std::endl;
	}

	// Spacing
	std::cout << std::endl;
}