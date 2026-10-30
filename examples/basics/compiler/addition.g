# Small enough to construct FIRST/FOLLOW and the LL(1) table by hand.
%start E
E -> num Tail { $$ = $1 + $2 } ;
Tail -> + num Tail { $$ = $2 + $3 }
      | %empty { $$ = 0 }
      ;
