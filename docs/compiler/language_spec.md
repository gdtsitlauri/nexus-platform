# NexusLang Language Specification

NexusLang is a small, statically typed educational language designed for the Nexus frontend and
later compiler/architecture phases. Phase 2 implements a bounded core that is large enough to
exercise lexing, parsing, AST construction, and semantic analysis without pulling in IR or backend
concerns.

## Implemented Phase 2 Subset

- scalar types: `int`, `bool`
- function return type: `int`, `bool`, or `void`
- array types for variables and parameters using postfix extents such as `int[4]`
- top-level function definitions
- local variable declarations with optional initializer
- blocks, `if`/`else`, `while`, and `return`
- integer and boolean literals
- identifiers
- unary operators: `-`, `!`
- binary operators: `*`, `/`, `%`, `+`, `-`, `<`, `<=`, `>`, `>=`, `==`, `!=`, `&&`, `||`
- function calls and array indexing
- assignment statements to identifiers or array elements
- recursion through function calls

## Lexical Structure

### Comments and Whitespace

- whitespace separates tokens and is otherwise ignored
- line comments begin with `//` and continue to end of line

### Identifiers

- pattern: letter or underscore, followed by letters, digits, or underscores
- examples: `main`, `factorial`, `_tmp`, `sum4`

### Keywords

`fn`, `var`, `if`, `else`, `while`, `return`, `true`, `false`, `int`, `bool`, `void`

### Literals

- decimal integer literals: `0`, `1`, `42`
- boolean literals: `true`, `false`

### Operators

- assignment: `=`
- arithmetic: `+`, `-`, `*`, `/`, `%`
- logical: `!`, `&&`, `||`
- comparison: `<`, `<=`, `>`, `>=`, `==`, `!=`
- function return arrow: `->`

### Delimiters and Punctuation

`(` `)` `{` `}` `[` `]` `:` `,` `;`

## Type Syntax

- scalar types: `int`, `bool`, `void`
- array types: scalar base plus one or more postfix extents
  - examples: `int[4]`, `bool[8][2]`
- Phase 2 semantic rule: function return types must be scalar or `void`

## Statements

- variable declaration: `var name: type;`
- initialized declaration: `var name: type = expr;`
- assignment: `target = expr;`
- expression statement: `call();`
- return: `return;` or `return expr;`
- conditional: `if (expr) stmt else stmt`
- loop: `while (expr) stmt`
- block: `{ stmt* }`

## Expressions and Precedence

From highest to lowest:

| Level | Operators | Associativity |
| --- | --- | --- |
| postfix | `call(...)`, `index[...]` | left |
| unary | `-`, `!` | right |
| multiplicative | `*`, `/`, `%` | left |
| additive | `+`, `-` | left |
| comparison | `<`, `<=`, `>`, `>=` | left |
| equality | `==`, `!=` | left |
| logical and | `&&` | left |
| logical or | `||` | left |

Assignment is a statement form in Phase 2, not an expression form.

## Function Definitions

Syntax:

```text
fn name(param: type, ...) -> return_type {
  ...
}
```

Examples:

```nexus
fn main() -> int {
  return 0;
}

fn factorial(n: int) -> int {
  if (n <= 1) {
    return 1;
  }
  return n * factorial(n - 1);
}
```

## EBNF

```ebnf
program         = { function_decl } ;

function_decl   = "fn" identifier "(" [ parameter_list ] ")" "->" type block ;
parameter_list  = parameter { "," parameter } ;
parameter       = identifier ":" type ;

type            = base_type { "[" integer_literal "]" } ;
base_type       = "int" | "bool" | "void" ;

block           = "{" { statement } "}" ;

statement       = block
                | var_decl
                | if_stmt
                | while_stmt
                | return_stmt
                | expr_or_assign_stmt ;

var_decl        = "var" identifier ":" type [ "=" expression ] ";" ;
if_stmt         = "if" "(" expression ")" statement [ "else" statement ] ;
while_stmt      = "while" "(" expression ")" statement ;
return_stmt     = "return" [ expression ] ";" ;
expr_or_assign_stmt
                = expression [ "=" expression ] ";" ;

expression      = logical_or ;
logical_or      = logical_and { "||" logical_and } ;
logical_and     = equality { "&&" equality } ;
equality        = comparison { ( "==" | "!=" ) comparison } ;
comparison      = additive { ( "<" | "<=" | ">" | ">=" ) additive } ;
additive        = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative  = unary { ( "*" | "/" | "%" ) unary } ;
unary           = [ "!" | "-" ] unary | postfix ;
postfix         = primary { call_suffix | index_suffix } ;
call_suffix     = "(" [ argument_list ] ")" ;
argument_list   = expression { "," expression } ;
index_suffix    = "[" expression "]" ;
primary         = identifier | integer_literal | "true" | "false" | "(" expression ")" ;
```

## Valid Examples

See:

- `examples/source_lang/factorial.nx`
- `examples/source_lang/arrays_and_loops.nx`

## Invalid Examples

Syntax error:

```nexus
fn broken() -> int {
  var x: int = 1
  return x;
}
```

Semantic errors:

```nexus
fn bad(flag: bool) -> int {
  var x: int = flag;
  if (x) {
    return true;
  }
  return missing;
}
```

## Phase 2 Deliberate Limits

- no structs
- no global variables
- no array literals
- no `for` loop syntax
- no assignment expressions
- no implicit conversions
- no IR-facing annotations or backend constraints yet
