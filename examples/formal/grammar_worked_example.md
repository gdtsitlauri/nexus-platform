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
unary           = [ "!" | "-" ] unary | primary ;
primary         = identifier | integer_literal | "(" expression ")" ;
```

## Why This Is LL-Friendly

- precedence is encoded by a stack of nonterminals rather than by ambiguous binary-expression rules
- left recursion is removed
- each parse routine can decide what to do with one token of lookahead

## LR Discussion

An LR parser could accept a flatter, more ambiguous spelling such as:

```ebnf
expr = expr "+" expr | expr "*" expr | "(" expr ")" | identifier | integer_literal ;
```

That grammar is concise but harder to interpret by hand. Phase 2 chooses the LL-style factoring so
the control flow of the parser is easy to inspect in C++.
