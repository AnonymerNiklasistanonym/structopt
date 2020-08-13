
#pragma once
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <structopt/array_size.hpp>
#include <structopt/is_specialization.hpp>
#include <structopt/third_party/magic_enum/magic_enum.hpp>
#include <structopt/third_party/visit_struct/visit_struct.hpp>
#include <structopt/visitor.hpp>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <algorithm>
#include <iterator>

namespace structopt {

namespace details {

struct parser {
  structopt::details::visitor visitor;
  std::vector<std::string> arguments;
  std::size_t current_index{1};
  std::size_t next_index{1};
  bool double_dash_encountered{false}; // "--" option-argument delimiter

  bool is_optional(const std::string &name) {
    if (double_dash_encountered) {
      return false;
    } else if (name == "--") {
      double_dash_encountered = true;
      return false;
    }

    bool result = false;
    if (name.size() >= 2) {
      // e.g., -b, -v
      if (name[0] == '-') {
        result = true;

        // TODO: check if rest of name is NOT a decimal literal - this could be a negative
        // number if (name is a decimal literal) {
        //   result = false;
        // }

        if (name[1] == '-') {
          result = true;
        }
      }
    }
    return result;
  }

  bool is_optional_field(const std::string &next) {
    if (!is_optional(next)) {
      return false;
    }

    bool result = false;
    for (auto &field_name : visitor.field_names) {
      if (next == "--" + field_name or next == "-" + std::string(1, field_name[0])) {
        // okay `next` matches _a_ field name (which is an optional field)
        result = true;
      }
    }
    return result;
  }

  template <typename T>
  std::pair<T, bool> parse_argument(const char *name) {
    T result;
    bool success = true;
    if constexpr (visit_struct::traits::is_visitable<T>::value) {
      result = parse_nested_struct<T>(name);
    }
    else if constexpr (std::is_enum<T>::value) {
      result = parse_enum_argument<T>(name);
      next_index += 1;
    } else if constexpr (structopt::is_specialization<T, std::pair>::value) {
      result =
          parse_pair_argument<typename T::first_type, typename T::second_type>(name);
    } else if constexpr (structopt::is_specialization<T, std::tuple>::value) {
      result = parse_tuple_argument<T>(name);
    } else if constexpr (!is_stl_container<T>::value) {
      result = parse_single_argument<T>(name);
      next_index += 1;
    } else if constexpr (structopt::is_array<T>::value) {
      constexpr std::size_t N = structopt::array_size<T>::size;
      result = parse_array_argument<typename T::value_type, N>(name);
    } else if constexpr (structopt::is_specialization<T, std::deque>::value) {
      result = parse_deque_argument<typename T::value_type>(name);
    } else if constexpr (structopt::is_specialization<T, std::vector>::value) {
      result = parse_vector_argument<typename T::value_type>(name);
    } else {
      success = false;
    }
    return {result, success};
  }

  template <typename T> std::optional<T> parse_optional_argument(const char *name) {
    next_index += 1;
    std::optional<T> result;
    bool success;
    if (next_index < arguments.size()) {
      auto [value, success] = parse_argument<T>(name);
      if (success) {
        result = value;
      }
    }
    return result;
  }

  // Any field that can be constructed using std::stringstream
  // Not container type
  // Not a visitable type, i.e., a nested struct
  template <typename T>
  inline typename std::enable_if<!visit_struct::traits::is_visitable<T>::value, T>::type
  parse_single_argument(const char *name) {
    // std::cout << "Parsing single argument for field " << name << "\n";
    const std::string argument = arguments[next_index];
    std::istringstream ss(argument);
    T result;
    ss >> result;
    return result;
  }

  // Nested visitable struct
  template <typename T>
  inline typename std::enable_if<visit_struct::traits::is_visitable<T>::value, T>::type
  parse_nested_struct(const char *name) {
    // std::cout << "Parsing nested struct\n";
    T argument_struct;

    // Save struct field names
    structopt::details::visitor nested_visitor;
    visit_struct::for_each(argument_struct, nested_visitor);

    // TODO: pass along program name, version etc. (info from `app` object)
    // to the nested parser so that it can correctly report help() msgs

    structopt::details::parser parser;
    parser.next_index = 0;
    parser.current_index = 0;
    parser.visitor = std::move(nested_visitor);

    std::copy(arguments.begin() + next_index, arguments.end(),
              std::back_inserter(parser.arguments));

    // std::cout << "Nested struct " << name << " arguments:\n";
    // for (auto& v : parser.arguments) {
    //   std::cout << v << " ";
    // }
    // std::cout << "\n";

    // std::cout << "BEFORE: " <<  current_index << " " << next_index << "\n";

    for (std::size_t i = 0; i < parser.arguments.size(); i++) {
      parser.current_index = i;
      visit_struct::for_each(argument_struct, parser);
    }

    // std::cout << "AFTER: " <<  parser.current_index << " " << parser.next_index <<
    // "\n";

    return argument_struct;
  }

  // Pair argument
  template <typename T1, typename T2>
  std::pair<T1, T2> parse_pair_argument(const char *name) {
    std::pair<T1, T2> result;
    bool success;
    {
      // Pair first
      auto [value, success] = parse_argument<T1>(name);
      if (success) {
        result.first = value;
      }
    }
    {
      // Pair second
      auto [value, success] = parse_argument<T2>(name);
      if (success) {
        result.second = value;
      }
    }
    return result;
  }

  // Array argument
  template <typename T, std::size_t N>
  std::array<T, N> parse_array_argument(const char *name) {
    std::array<T, N> result;
    bool success;
    for (std::size_t i = 0; i < N; i++) {
      auto [value, success] = parse_argument<T>(name);
      if (success) {
        result[i] = value;
      }
    }
    return result;
  }

  template <class Tuple, class F, std::size_t... I>
  constexpr F for_each_impl(Tuple&& t, F&& f, std::index_sequence<I...>) {
      return (void)std::initializer_list<int>{(std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))),0)...}, f;
  }

  template <class Tuple, class F>
  constexpr F for_each(Tuple&& t, F&& f) {
      return for_each_impl(std::forward<Tuple>(t), std::forward<F>(f),
                          std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
  }

  // Parse single tuple element
  template <typename T>
  void parse_tuple_element(const char *name, T&& result) {
    auto [value, success] = parse_argument<typename std::remove_reference<T>::type>(name);
    if (success) {
      result = value;
    }
  }

  // Tuple argument
  template<typename Tuple>
  Tuple parse_tuple_argument(const char *name) {
    Tuple result;
    for_each(result, [&](auto&& arg) { 
      parse_tuple_element(name, arg);
    });
    return result;
  }

  // Deque argument
  template <typename T> std::deque<T> parse_deque_argument(const char *name) {
    std::deque<T> result;
    // Parse from current till end
    for (std::size_t i = next_index; i < arguments.size(); i++) {
      const auto next = arguments[next_index];
      if (is_optional_field(next)) {
        // this marks the end of the vector (break here)
        break;
      }
      auto [value, success] = parse_argument<T>(name);
      if (success) {
        result.push_back(value);
      }
    }
    return result;
  }

  // Vector argument
  template <typename T> std::vector<T> parse_vector_argument(const char *name) {
    std::vector<T> result;
    // Parse from current till end
    for (std::size_t i = next_index; i < arguments.size(); i++) {
      const auto next = arguments[next_index];
      if (is_optional_field(next)) {
        // this marks the end of the vector (break here)
        break;
      }
      auto [value, success] = parse_argument<T>(name);
      if (success) {
        result.push_back(value);
      }
    }
    return result;
  }

  // Enum class
  template <typename T> T parse_enum_argument(const char *name) {
    T result;
    auto maybe_enum_value = magic_enum::enum_cast<T>(arguments[next_index]);
    if (maybe_enum_value.has_value()) {
      result = maybe_enum_value.value();
    } else {
      // TODO: Throw error invalid enum option
    }
    return result;
  }

  // Visitor function for nested struct
  template <typename T>
  inline typename std::enable_if<visit_struct::traits::is_visitable<T>::value, void>::type
  operator()(const char *name, T &value) {
    // std::cout << "Parssing nested struct" << std::endl;
    if (next_index > current_index) {
      current_index = next_index;
    }

    if (current_index < arguments.size()) {
      const auto next = arguments[current_index];
      const auto field_name = std::string{name};

      // std::cout << "Next: " << next << "; Name: " << name << "\n";

      // Check if `next` is the start of a subcommand
      if (visitor.is_field_name(next) && next == field_name) {
        next_index += 1;
        value = parse_nested_struct<T>(name);
      }
    }
  }

  // Visitor function for any positional field (not std::optional)
  template <typename T>
  inline typename std::enable_if<!structopt::is_specialization<T, std::optional>::value &&
                                     !visit_struct::traits::is_visitable<T>::value,
                                 void>::type
  operator()(const char *name, T &result) {
    // std::cout << "Parsing positional: " << name << std::endl;
    if (next_index > current_index) {
      current_index = next_index;
    }

    // std::cout << "current_index: " << current_index << "; next_index: " << next_index
    // << "\n";

    if (current_index < arguments.size()) {
      const auto next = arguments[current_index];

      // TODO: Deal with negative numbers - these are not optional arguments
      if (is_optional(next)) {
        return;
      }

      if (visitor.positional_field_names.empty()) {
        // We're not looking to save any more positional fields
        // all of them already have a value
        // TODO: Report error, unexpected argument
        return;
      }

      const auto field_name = visitor.positional_field_names.front();

      // // This will be parsed as a subcommand (nested struct)
      // if (visitor.is_field_name(next) && next == field_name) {
      //   return;
      // }

      if (field_name != std::string{name}) {
        // current field is not the one we want to parse 
        return;
      }

      // Remove from the positional field list as it is about to be parsed
      visitor.positional_field_names.pop_front();

      // std::cout << "Next: " << next << "; Name: " << field_name << "\n";

      auto [value, success] = parse_argument<T>(field_name.c_str());
      if (success) {
        result = value;
      } else {
        // positional field does not yet have a value
        visitor.positional_field_names.push_front(field_name);
      }
    }
  }

  // Visitor function for std::optional field
  template <typename T>
  inline typename std::enable_if<structopt::is_specialization<T, std::optional>::value,
                                 void>::type
  operator()(const char *name, T &value) {
    // std::cout << "Parsing optional " << name << std::endl;
    if (next_index > current_index) {
      current_index = next_index;
    }

    if (current_index < arguments.size()) {
      const auto next = arguments[current_index];
      const auto field_name = std::string{name};

      if (next == "--" and double_dash_encountered == false) {
        double_dash_encountered = true;
        next_index += 1;
        return;
      }

      // Remove special characters from argument
      // e.g., --verbose => verbose
      // e.g., -v => v
      // e.g., --input-file => inputfile
      auto next_alpha = next;
      next_alpha.erase(std::remove_if(next_alpha.begin(), next_alpha.end(),
                                      [](char c) { return !std::isalpha(c); }),
                       next_alpha.end());

      // Remove special characters from field name
      // e.g., verbose => verbose
      // e.g., input_file => inputfile
      auto field_name_alpha = field_name;
      field_name_alpha.erase(std::remove_if(field_name_alpha.begin(),
                                            field_name_alpha.end(),
                                            [](char c) { return !std::isalpha(c); }),
                             field_name_alpha.end());

      // std::cout << "Trying to parse optional: " << field_name << " " << next << "\n";

      // if `next` looks like an optional argument
      // i.e., starts with `-` or `--`
      // see if you can find an optional field in the struct with a matching name

      // check if the current argument looks like it could be this optional field
      if ((double_dash_encountered == false) and
          ((next == "--" + field_name or next == "-" + std::string(1, field_name[0])) or
           (next_alpha == field_name_alpha))) {

        // std::cout << "Parsing optional: " << field_name << " " << next << "\n";

        // this is an optional argument matching the current struct field
        if constexpr (std::is_same<typename T::value_type, bool>::value) {
          // It is a boolean optional argument
          // Does it have a default value?
          // If yes, this is a FLAG argument, e.g,, "--verbose" will set it to true if the
          // default value is false No need to write "--verbose true"
          if (value.has_value()) {
            // The field already has a default value!
            value = !value.value(); // simply toggle it
            next_index += 1;
          } else {
            // boolean optional argument doesn't have a default value
            // expect one
            value = parse_optional_argument<typename T::value_type>(name);
          }
        } else {
          // Not std::optional<bool>
          // Parse the argument type <T>
          value = parse_optional_argument<typename T::value_type>(name);
        }
      }
    }
  }
};

// Specialization for std::string
template <>
inline std::string parser::parse_single_argument<std::string>(const char *name) {
  return arguments[next_index];
}

// Specialization for bool
// yes, YES, on, 1, true, TRUE, etc. = true
// no, NO, off, 0, false, FALSE, etc. = false
// Converts argument to lower case before check
template <> inline bool parser::parse_single_argument<bool>(const char *name) {
  if (next_index > current_index) {
    current_index = next_index;
  }

  if (current_index < arguments.size()) {
    const std::vector<std::string> true_strings{"on", "yes", "1", "true"};
    const std::vector<std::string> false_strings{"off", "no", "0", "false"};
    std::string current_argument = arguments[current_index];

    // Convert argument to lower case
    std::transform(current_argument.begin(), current_argument.end(),
                   current_argument.begin(), ::tolower);

    // Detect if argument is true or false
    if (std::find(true_strings.begin(), true_strings.end(), current_argument) !=
        true_strings.end()) {
      return true;
    } else if (std::find(false_strings.begin(), false_strings.end(), current_argument) !=
               false_strings.end()) {
      return false;
    } else {
      // TODO: report error? Invalid argument, bool expected
      return false;
    }
  } else {
    return false;
  }
}

} // namespace details

} // namespace structopt
