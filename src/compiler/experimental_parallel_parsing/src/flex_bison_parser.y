%require "3.0"
%defines
%define api.prefix {nexus_fb_}
%define api.pure full
%define parse.error verbose
%locations

%code requires {
namespace nexus::compiler::experimental_parallel_parsing {
struct ParseState;
}
}

%parse-param { nexus::compiler::experimental_parallel_parsing::ParseState* state }
%lex-param { nexus::compiler::experimental_parallel_parsing::ParseState* state }

%code {
#include "bison_lr_driver.hpp"

int nexus_fb_lex(NEXUS_FB_STYPE* yylval_param, NEXUS_FB_LTYPE* yylloc_param, nexus::compiler::experimental_parallel_parsing::ParseState* state);
void nexus_fb_error(NEXUS_FB_LTYPE* loc, nexus::compiler::experimental_parallel_parsing::ParseState* state, const char* msg);
}

%token FN VAR RETURN IF ELSE WHILE TRUE FALSE INT BOOL
%token IDENT INTEGER
%token ARROW LE GE EQ NE

%left EQ NE
%left '<' '>' LE GE
%left '+' '-'
%left '*' '/'
%right UMINUS '!'

%%

program
  : top_level_list
  ;

top_level_list
  : /* empty */
  | top_level_list function_decl
  ;

function_decl
  : FN IDENT '(' parameter_list_opt ')' ARROW type block
    {
      state->result.summary.function_count += 1;
    }
  ;

parameter_list_opt
  : /* empty */
  | parameter_list
  ;

parameter_list
  : parameter
  | parameter_list ',' parameter
  ;

parameter
  : IDENT ':' type
  ;

type
  : INT
  | BOOL
  | INT '[' INTEGER ']'
  | BOOL '[' INTEGER ']'
  ;

block
  : '{' statement_list '}'
  ;

statement_list
  : /* empty */
  | statement_list statement
  ;

statement
  : VAR IDENT ':' type '=' expression ';'
    {
      state->result.summary.variable_decl_count += 1;
    }
  | RETURN expression ';'
    {
      state->result.summary.return_count += 1;
    }
  | IF '(' expression ')' block else_clause_opt
    {
      state->result.summary.if_count += 1;
    }
  | WHILE '(' expression ')' block
    {
      state->result.summary.while_count += 1;
    }
  | lvalue '=' expression ';'
  | expression ';'
  | block
  ;

else_clause_opt
  : /* empty */
  | ELSE block
  ;

lvalue
  : IDENT
  | IDENT '[' expression ']'
  ;

expression
  : equality_expr
  ;

equality_expr
  : relational_expr
  | equality_expr EQ relational_expr
  | equality_expr NE relational_expr
  ;

relational_expr
  : additive_expr
  | relational_expr '<' additive_expr
  | relational_expr '>' additive_expr
  | relational_expr LE additive_expr
  | relational_expr GE additive_expr
  ;

additive_expr
  : multiplicative_expr
  | additive_expr '+' multiplicative_expr
  | additive_expr '-' multiplicative_expr
  ;

multiplicative_expr
  : unary_expr
  | multiplicative_expr '*' unary_expr
  | multiplicative_expr '/' unary_expr
  ;

unary_expr
  : primary
  | '-' unary_expr %prec UMINUS
  | '!' unary_expr
  ;

primary
  : IDENT
  | INTEGER
  | TRUE
  | FALSE
  | IDENT '(' argument_list_opt ')'
  | IDENT '[' expression ']'
  | '(' expression ')'
  ;

argument_list_opt
  : /* empty */
  | argument_list
  ;

argument_list
  : expression
  | argument_list ',' expression
  ;

%%
