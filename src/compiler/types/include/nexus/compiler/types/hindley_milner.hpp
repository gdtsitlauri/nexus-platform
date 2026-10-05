#pragma once

#include <string>
#include <vector>

namespace nexus::compiler::types {

// Hindley-Milner type inference (Damas-Milner Algorithm W) for a small ML-like language.
//
//   e ::= n | true | false | x | fun x -> e | e e | let x = e in e | let rec f = e in e
//       | if e then e else e | e + e | e - e | e * e | e < e | e = e | (e, e) | (e)
//
// Built-in polymorphic constants: fst, snd, nil ('a list), cons, head, tail, isnil.
// Inference uses unification with the occurs check and generalises let-bound definitions, so
// `let id = fun x -> x in (id 1, id true)` is well typed while `fun x -> x x` is rejected.

struct InferenceResult {
  bool ok = false;
  std::string type;   // principal type, e.g. "('a -> 'b) -> 'a list -> 'b list"
  std::string error;
  std::vector<std::string> steps;  // unification trace
};

InferenceResult infer_type(const std::string& source);

}  // namespace nexus::compiler::types
