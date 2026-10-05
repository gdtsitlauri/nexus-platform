# Formal Languages Toolkit: Automata, LL, LR, Earley, Attributes

`src/compiler/formal/` implements the language-theory part of the compiler courses as executable
tools (`nexusc regex`, `nexusc grammar`).

## Regular expressions and finite automata

`regex -> Thompson NFA -> subset-construction DFA -> minimal DFA (Moore partition refinement)`.
Syntax: literals, escapes, `.`, classes `[a-z_]`, `[^0-9]`, `( )`, `|`, `*`, `+`, `?`.

```bash
./build/bin/nexusc regex '(a|b)*abb' --match abb abab
# NFA states: 20, DFA states: 5, minimal DFA states: 4   (Dragon book examples 3.36 / 3.40)
```

`nexus_formal_test` checks NFA, DFA and minimal DFA against `std::regex` on 3,200 random strings over
eight patterns.

## Grammars

A small grammar format (`examples/grammars/*.g`):

```text
%start E
E -> E + T { $$ = $1 + $3 } | T ;
T -> T * F { $$ = $1 * $3 } | F ;
F -> ( E ) { $$ = $2 } | num ;
```

| command | what it does |
| --- | --- |
| `nexusc grammar g first-follow` | nullable, FIRST and FOLLOW sets |
| `nexusc grammar g transform` | left-recursion elimination (Aho et al. algorithm 4.19) + left factoring, then an LL(1) check |
| `nexusc grammar g ll1 "tokens"` | LL(1) table with conflicts, predictive parse trace, parse tree |
| `nexusc grammar g lr0/slr/lr1/lalr "tokens" [--items]` | item sets, ACTION/GOTO tables, conflict counts, shift-reduce trace |
| `nexusc grammar g earley "tokens"` | Earley chart and the number of distinct parse trees |

## Results on the example grammars

| grammar | result |
| --- | --- |
| `expr.g` (left recursive) | not LL(1) (6 conflicting cells), not LR(0) (6 shift/reduce), SLR(1) = LALR(1) = LR(1) conflict-free; `transform` makes it LL(1) |
| `lvalue.g` (Dragon book 4.49) | one SLR(1) shift/reduce conflict; LALR(1) with 10 states and LR(1) with 14 states are conflict-free |
| `expr_ll1.g` | LL(1); evaluates `2 + 3 * 4 + ( 1 + 1 ) * 5 = 24` with S-attributed actions |
| `ambiguous.g` | not LR(1); Earley counts 2 trees for `1 + 2 * 3` and Catalan(5) = 42 for six operands |
| `dangling_else.g` | exactly one shift/reduce conflict, resolved like yacc (shift), and 2 Earley trees |

## Attribute grammars

Each alternative may carry an S-attributed action over `$1..$n` with `+ - * / %` and parentheses.
Both the LL(1) and the LR parsers build parse trees and evaluate the synthesized attribute bottom-up
(`synthesized value = ...`).
