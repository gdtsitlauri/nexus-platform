# The dangling-else ambiguity: one shift/reduce conflict, resolved yacc-style by shifting.
%start S
S -> if c then S
   | if c then S else S
   | other
   ;
