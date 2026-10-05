# Classic left-recursive expression grammar with an S-attributed evaluator.
# Not LL(1) (left recursion) and not LR(0); it is SLR(1), LALR(1) and LR(1).
%start E
E -> E + T { $$ = $1 + $3 }
   | E - T { $$ = $1 - $3 }
   | T
   ;
T -> T * F { $$ = $1 * $3 }
   | T / F { $$ = $1 / $3 }
   | F
   ;
F -> ( E ) { $$ = $2 }
   | - F   { $$ = 0 - $2 }
   | num
   ;
