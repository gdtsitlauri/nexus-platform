#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "nexus/compiler/types/hindley_milner.hpp"

int main() {
  using nexus::compiler::types::infer_type;
  const std::vector<std::pair<std::string, std::string>> well_typed = {
      {"fun x -> x", "'a -> 'a"},
      {"fun f -> fun g -> fun x -> f (g x)", "('a -> 'b) -> ('c -> 'a) -> 'c -> 'b"},
      {"fun x -> fun y -> x", "'a -> 'b -> 'a"},
      {"let id = fun x -> x in (id 1, id true)", "(int * bool)"},
      {"fun p -> (snd p, fst p)", "('a * 'b) -> ('b * 'a)"},
      {"let rec fact n = if n < 1 then 1 else n * fact (n - 1) in fact", "int -> int"},
      {"let rec map f l = if isnil l then nil else cons (f (head l)) (map f (tail l)) in map",
       "('a -> 'b) -> 'a list -> 'b list"},
      {"let rec fold f acc l = if isnil l then acc else fold f (f acc (head l)) (tail l) in fold",
       "('a -> 'b -> 'a) -> 'a -> 'b list -> 'a"},
      {"fun f -> fun x -> f (f x)", "('a -> 'a) -> 'a -> 'a"},
      {"let twice f x = f (f x) in twice (fun n -> n + 1) 0", "int"},
  };
  const std::vector<std::pair<std::string, std::string>> ill_typed = {
      {"fun x -> x x", "occurs check"},
      {"(fun id -> (id 1, id true)) (fun x -> x)", "mismatch"},  // lambda-bound => monomorphic
      {"if 1 then 2 else 3", "mismatch"},
      {"1 + true", "mismatch"},
      {"y", "unbound"},
  };
  int failures = 0;
  for (const auto& [source, expected] : well_typed) {
    const auto result = infer_type(source);
    if (!result.ok || result.type != expected) {
      std::cerr << "FAIL: " << source << " : expected " << expected << ", got "
                << (result.ok ? result.type : "error " + result.error) << '\n';
      ++failures;
    }
  }
  for (const auto& [source, reason] : ill_typed) {
    const auto result = infer_type(source);
    if (result.ok || result.error.find(reason) == std::string::npos) {
      std::cerr << "FAIL: " << source << " should be rejected (" << reason << "), got "
                << (result.ok ? result.type : result.error) << '\n';
      ++failures;
    }
  }
  if (failures == 0) {
    std::cout << "type_inference_test: all checks passed\n";
  }
  return failures == 0 ? 0 : 1;
}
