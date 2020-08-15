
#pragma once
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <structopt/is_stl_container.hpp>
#include <structopt/parser.hpp>
#include <structopt/third_party/visit_struct/visit_struct.hpp>
#include <type_traits>
#include <vector>

#define STRUCTOPT VISITABLE_STRUCT

namespace structopt {

class app {
  details::visitor visitor;

public:
  explicit app(const std::string name, const std::string version = "")
      : visitor(name, version) {}

  template <typename T> T parse(const std::vector<std::string> &arguments) {
    T argument_struct;

    // Visit the struct and save flag, optional and positional field names
    visit_struct::for_each(argument_struct, visitor);

    // add `help` and `version` optional arguments
    visitor.optional_field_names.push_back("help");
    visitor.optional_field_names.push_back("version");

    // Construct the argument parser
    structopt::details::parser parser;
    parser.visitor = visitor;
    parser.arguments = arguments;

    for (std::size_t i = 1; i < parser.arguments.size(); i++) {
      parser.current_index = i;
      visit_struct::for_each(argument_struct, parser);
    }

    // directly call the parser to check for `help` and `version` flags
    std::optional<bool> help = false, version = false;
    for (std::size_t i = 1; i < parser.arguments.size(); i++) {
      parser.operator()("help", help);
      parser.operator()("version", version);

      if (help == true) {
        // if help is requested, print help and exit
        visitor.print_help(std::cout);
        exit(EXIT_SUCCESS);
      } else if (version == true) {
        // if version is requested, print version and exit
        std::cout << visitor.version << "\n";
        exit(EXIT_SUCCESS);
      }
    }

    if (!parser.visitor.positional_field_names.empty()) {
      // if all positional arguments were provided
      // this list would be empty
      auto front = parser.visitor.positional_field_names.front();
      if (std::find(parser.visitor.vector_like_positional_field_names.begin(),
                    parser.visitor.vector_like_positional_field_names.end(),
                    front) == parser.visitor.vector_like_positional_field_names.end()) {
        // this positional argument is not a vector-like argument
        // it expects values
        throw structopt::exception("Error: expected value for positional argument `" +
                                       front + "`.",
                                   parser.visitor);
      }
    }

    return argument_struct;
  }

  template <typename T> T parse(int argc, char *argv[]) {
    std::vector<std::string> arguments;
    std::copy(argv, argv + argc, std::back_inserter(arguments));
    return parse<T>(arguments);
  }
};

} // namespace structopt