# Grammar Worked Example

Consider the expression fragment:

```ebnf
expression      = logical_or ;
logical_or      = logical_and { "||" logical_and } ;
logical_and     = equality { "&&" equality } ;
equality        = comparison { ( "==" | "!=" ) comparison } ;
comparison      = additive { ( "<" | "<=" | ">" | ">=" ) additive } ;
additive        = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative  = unary { ( "*" | "/" | "%" ) unary } ;
unary           = "!" unary | "-" unary | primary ;
primary         = identifier | integer_literal | "(" expression ")" ;
```

## Why This Is LL-Friendly

- precedence is encoded by a stack of nonterminals rather than by ambiguous binary-expression rules
- left recursion is removed
- each parse routine can decide what to do with one token of lookahead

## LR Discussion

A parser generator can accept a flatter spelling with explicit precedence/associativity rules such as:

```ebnf
expr = expr "+" expr | expr "*" expr | "(" expr ")" | identifier | integer_literal ;
```

That grammar is ambiguous as written: an LR table has conflicts unless a policy resolves them.
Left recursion alone is acceptable to LR; ambiguity is a separate issue. The NEXUS parser chooses LL-style factoring so
the control flow of the parser is easy to inspect in C++.
