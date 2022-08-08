/*
Copyright 2019-2022 René Ferdinand Rivera Morell
Distributed under the Boost Software License, Version 1.0.
(See accompanying file LICENSE.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
*/

// tag::part1[]
#include <cstdlib>
#include <iostream>
#include <lyra/lyra.hpp>

int main(int argc, const char** argv)
{
	// Where we read in the argument value:
	int width = 0;

	// The parser with the one option argument:
	auto cli = lyra::cli()
		| lyra::opt(width, "width")
			  ["-w"]["--width"]("How wide should it be?");

	// ...
	// end::part1[]
	// tag::part2[]
	// ...

	// Parse the program arguments:
	auto result = cli.parse({ argc, argv });

	// Check that the arguments where valid:
	if (!result)
	{
		std::cerr << "Error in command line: " << result.message() << std::endl;
		return 1;
	}

	std::cout << "width = " << width << "\n";
	return 0;
}
// end::part2[]
