#include "slog-dest.h"
#include "cfg-parser.h"
#include "slog-dest-grammar.h" // generated by lexer

extern int slog_debug;
int dummy_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword slog_keywords[] = {
  { "slog", KW_SLOG },
  { "filename", KW_FILENAME },
  { NULL }
};

CfgParser slog_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &slog_debug,
#endif
  .name = "slog",
  .keywords = slog_keywords,
  .parse = (int (*)(CfgLexer *lexer, gpointer *instance, gpointer)) slog_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(slog_, SLOG_, LogDriver **)
