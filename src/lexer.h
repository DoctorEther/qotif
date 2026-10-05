/*
 * lexer.h - small tokenizer used for .map, .fgd and .def files
 */
#ifndef QOTIF_LEXER_H
#define QOTIF_LEXER_H

#include <stddef.h>

typedef struct lexer_s {
    const char *p;
    int line;
    const char *punct;   /* single character tokens */
    int quoted;          /* last token was a "quoted string" */
    char tok[2048];
} lexer_t;

void lex_init(lexer_t *lx, const char *text, const char *punct);
/* reads the next token; returns 0 at end of input */
int lex_next(lexer_t *lx);
/* reads a run of non-blank characters, ignoring punctuation (texture names) */
int lex_word(lexer_t *lx);
/* looks at the next token without consuming it */
int lex_peek(const lexer_t *lx, char *out, size_t size, int *quoted);
/* true if the next token is the unquoted string s */
int lex_peek_is(const lexer_t *lx, const char *s);
/* true if the last token read is the unquoted string s */
int lex_is(const lexer_t *lx, const char *s);
/* true if another token follows on the current line */
int lex_more_on_line(const lexer_t *lx);

#endif
