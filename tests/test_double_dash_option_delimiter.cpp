#include <doctest.hpp>
#include <structopt/app.hpp>

using doctest::test_suite;

struct GrepOptions {
  // reverse the matching
  std::optional<bool> v = false;
  
  // positional arguments
  std::string search;
  std::string pathspec;
};
STRUCTOPT(GrepOptions, v, search, pathspec);

TEST_CASE("structopt can parse the '--' delimiter as end of optional arguments" * test_suite("single_optional")) {
  {
    auto arguments = structopt::app("test").parse<GrepOptions>(std::vector<std::string>{"grep", "--", "-v", "file.csv"});
    REQUIRE(arguments.v == false);
    REQUIRE(arguments.search == "-v");
    REQUIRE(arguments.pathspec == "file.csv");
  }
}