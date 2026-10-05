# Ambiguous expression grammar: no LR(k) or LL(k) parser exists, Earley counts every parse tree.
%start E
E -> E + E { $$ = $1 + $3 }
   | E * E { $$ = $1 * $3 }
   | num
   ;
