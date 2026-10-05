# The same language after left-recursion elimination: LL(1).  The S-attributed actions still
# evaluate sums of products because + and * are associative.
%start E
E  -> T E2       { $$ = $1 + $2 } ;
E2 -> + T E2     { $$ = $2 + $3 }
    | %empty     { $$ = 0 }
    ;
T  -> F T2       { $$ = $1 * $2 } ;
T2 -> * F T2     { $$ = $2 * $3 }
    | %empty     { $$ = 1 }
    ;
F  -> ( E )      { $$ = $2 }
    | num
    ;
