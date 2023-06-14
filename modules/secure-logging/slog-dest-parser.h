#ifndef SLOG_PARSER_H_INCLUDED
#define SLOG_PARSER_H_INCLUDED

#include "cfg-parser.h"
#include "cfg-lexer.h"
#include "slog-dest.h"

extern CfgParser slog_parser;

CFG_PARSER_DECLARE_LEXER_BINDING(slog_, SLOG_, LogDriver **)

#endif
