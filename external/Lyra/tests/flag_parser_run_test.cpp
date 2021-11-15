/*
Copyright 2019 René Ferdinand Rivera Morell
Distributed under the Boost Software License, Version 1.0.
(See accompanying file LICENSE.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
*/

#include "mini_test.hpp"
#include <lyra/lyra.hpp>

int main()
{
	using namespace lyra;
	bfg::mini_test::scope test;

	{
		bool flag = false;
		auto p = cli() | opt(flag)["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f" });
		test
			(REQUIRE(result))
			(REQUIRE(flag));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag)["-f"]("A flag");
		auto result = p.parse({ "TestApp" });
		test
			(REQUIRE(result))
			(REQUIRE(!flag));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "true" });
		test
			(REQUIRE(result))
			(REQUIRE(flag));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "yes" });
		test
			(REQUIRE(result))
			(REQUIRE(flag));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "y" });
		test
			(REQUIRE(result))
			(REQUIRE(flag));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "1" });
		test
			(REQUIRE(result))
			(REQUIRE(flag));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "on" });
		test
			(REQUIRE(result))
			(REQUIRE(flag));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "tRUe" });
		test
			(REQUIRE(result))
			(REQUIRE(flag));
	}
	{
		bool flag = true;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "false" });
		test
			(REQUIRE(result))
			(REQUIRE(flag == false));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f", "what" });
		test
			(REQUIRE(!result))
			(REQUIRE(
				result.errorMessage().find("Unable to convert")
				!= std::string::npos));
	}
	{
		bool flag = false;
		auto p = cli() | opt(flag, "true|false")["-f"]("A flag");
		auto result = p.parse({ "TestApp", "-f" });
		test
			(REQUIRE(!result))
			(REQUIRE(
				result.errorMessage().find("Expected argument following -f")
				!= std::string::npos));
	}

	return test;
}
