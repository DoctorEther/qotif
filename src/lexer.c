/*
 * lexer.c - small tokenizer used for .map, .fgd and .def files
 */
#include "lexer.h"
#include "common.h"

#include <ctype.h>

void lex_init(lexer_t *lx, const char *text, const char *punct)
{
    lx->p = text;
    lx->line = 1;
    lx->punct = punct;
    lx->quoted = 0;
    lx->tok[0] = 0;
}

static void skip_ws(lexer_t *lx)
{
    for (;;) {
        while (*lx->p && isspace((unsigned char)*lx->p)) {
            if (*lx->p == '\n')
                lx->line++;
            lx->p++;
        }
        if (lx->p[0] == '/' && lx->p[1] == '/') {
            while (*lx->p && *lx->p != '\n')
                lx->p++;
            continue;
        }
        break;
    }
}

static int read_quoted(lexer_t *lx)
{
    size_t n = 0;

    lx->p++;
    while (*lx->p && *lx->p != '"') {
        if (*lx->p == '\n')
            lx->line++;
        if (n < sizeof(lx->tok) - 1)
            lx->tok[n++] = *lx->p;
        lx->p++;
    }
    if (*lx->p == '"')
        lx->p++;
    lx->tok[n] = 0;
    lx->quoted = 1;
    return 1;
}

int lex_next(lexer_t *lx)
{
    size_t n = 0;

    skip_ws(lx);
    lx->quoted = 0;
    lx->tok[0] = 0;
    if (!*lx->p)
        return 0;
    if (*lx->p == '"')
        return read_quoted(lx);
    if (strchr(lx->punct, *lx->p)) {
        lx->tok[0] = *lx->p++;
        lx->tok[1] = 0;
        return 1;
    }
    while (*lx->p && !isspace((unsigned char)*lx->p) && *lx->p != '"'
           && !strchr(lx->punct, *lx->p)) {
        if (lx->p[0] == '/' && lx->p[1] == '/')
            break;
        if (n < sizeof(lx->tok) - 1)
            lx->tok[n++] = *lx->p;
        lx->p++;
    }
    lx->tok[n] = 0;
    return 1;
}

int lex_word(lexer_t *lx)
{
    size_t n = 0;

    skip_ws(lx);
    lx->quoted = 0;
    lx->tok[0] = 0;
    if (!*lx->p)
        return 0;
    if (*lx->p == '"')
        return read_quoted(lx);
    while (*lx->p && !isspace((unsigned char)*lx->p)) {
        if (n < sizeof(lx->tok) - 1)
            lx->tok[n++] = *lx->p;
        lx->p++;
    }
    lx->tok[n] = 0;
    return 1;
}

int lex_peek(const lexer_t *lx, char *out, size_t size, int *quoted)
{
    lexer_t copy = *lx;
    int r = lex_next(&copy);

    if (out)
        str_copy(out, copy.tok, size);
    if (quoted)
        *quoted = copy.quoted;
    return r;
}

int lex_peek_is(const lexer_t *lx, const char *s)
{
    char buf[64];
    int q;

    if (!lex_peek(lx, buf, sizeof(buf), &q))
        return 0;
    return !q && strcmp(buf, s) == 0;
}

int lex_is(const lexer_t *lx, const char *s)
{
    return !lx->quoted && strcmp(lx->tok, s) == 0;
}

int lex_more_on_line(const lexer_t *lx)
{
    const char *p = lx->p;

    while (*p == ' ' || *p == '\t' || *p == '\r')
        p++;
    if (!*p || *p == '\n')
        return 0;
    if (p[0] == '/' && p[1] == '/')
        return 0;
    return 1;
}
