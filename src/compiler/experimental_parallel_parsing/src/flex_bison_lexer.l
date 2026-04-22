%option noyywrap nounput noinput yylineno nodefault bison-bridge bison-locations
%option prefix="nexus_fb_"

%{
#include "flex_bison_parser.hpp"

#include <cstdlib>

#include "bison_lr_driver.hpp"

#define YYSTYPE NEXUS_FB_STYPE
#define YYLTYPE NEXUS_FB_LTYPE

static int nexus_fb_column = 1;

void nexus_fb_reset_column(void) {
  nexus_fb_column = 1;
}

#define YY_DECL int nexus_fb_lex( \
    NEXUS_FB_STYPE* yylval_param, \
    NEXUS_FB_LTYPE* yylloc_param, \
    nexus::compiler::experimental_parallel_parsing::ParseState* state)

#define YY_USER_INIT do { (void)yylval_param; (void)state; } while (0)

#define YY_USER_ACTION                                                          \
  yylloc_param->first_line = yylineno;                                          \
  yylloc_param->last_line = yylineno;                                           \
  yylloc_param->first_column = nexus_fb_column;                                 \
  yylloc_param->last_column = nexus_fb_column + static_cast<int>(yyleng) - 1;   \
  nexus_fb_column += static_cast<int>(yyleng);
%}

%%

[ \t\r]+          { (void)state; /* ignore horizontal whitespace */ }
"//".*            { (void)state; /* ignore line comments */ }
\n+               { (void)state; nexus_fb_column = 1; }

"fn"              { return FN; }
"var"             { return VAR; }
"return"          { return RETURN; }
"if"              { return IF; }
"else"            { return ELSE; }
"while"           { return WHILE; }
"true"            { return TRUE; }
"false"           { return FALSE; }
"int"             { return INT; }
"bool"            { return BOOL; }
"->"              { return ARROW; }
"<="              { return LE; }
">="              { return GE; }
"=="              { return EQ; }
"!="              { return NE; }
[0-9]+            { return INTEGER; }
[A-Za-z_][A-Za-z0-9_]* { return IDENT; }
.                 { return yytext[0]; }

%%
