# Dragon-book grammar 4.49: LALR(1) and LR(1) but not SLR(1) (shift/reduce conflict on '=').
%start S
S -> L = R | R ;
L -> * R | id ;
R -> L ;
