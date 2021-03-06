#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "nooc.h"
#include "stack.h"
#include "ir.h"
#include "util.h"

#define ADVANCE(n) \
			start.data += (n) ; \
			start.len -= (n) ; \
			col += (n) ;

struct token *
lex(struct slice start)
{
	size_t line = 1;
	size_t col = 1;
	struct token *const head = xcalloc(1, sizeof(struct token));

	struct token *cur = head;
	while (start.len) {
		if (isblank(*start.data)) {
			ADVANCE(1);
			continue;
		}

		if (*start.data == '\n') {
			ADVANCE(1);
			line += 1;
			col = 1;
			continue;
		}

		cur->line = line;
		cur->col = col;

		if (slice_cmplit(&start, "if") == 0) {
			cur->type = TOK_IF;
			ADVANCE(2);
		} else if (slice_cmplit(&start, "let") == 0) {
			cur->type = TOK_LET;
			ADVANCE(3);
		} else if (slice_cmplit(&start, "else") == 0) {
			cur->type = TOK_ELSE;
			ADVANCE(4);
		} else if (slice_cmplit(&start, "loop") == 0) {
			cur->type = TOK_LOOP;
			ADVANCE(4);
		} else if (slice_cmplit(&start, "return") == 0) {
			cur->type = TOK_RETURN;
			ADVANCE(6);
		} else if (slice_cmplit(&start, "break") == 0) {
			cur->type = TOK_BREAK;
			ADVANCE(5);
		} else if (*start.data == '>') {
			cur->type = TOK_GREATER;
			ADVANCE(1);
		} else if (*start.data == '!') {
			cur->type = TOK_NOT;
			ADVANCE(1);
		} else if (*start.data == '$') {
			cur->type = TOK_DOLLAR;
			ADVANCE(1);
		} else if (*start.data == ',') {
			cur->type = TOK_COMMA;
			ADVANCE(1);
		} else if (*start.data == '(') {
			cur->type = TOK_LPAREN;
			ADVANCE(1);
		} else if (*start.data == ')') {
			cur->type = TOK_RPAREN;
			ADVANCE(1);
		} else if (*start.data == '[') {
			cur->type = TOK_LSQUARE;
			ADVANCE(1);
		} else if (*start.data == ']') {
			cur->type = TOK_RSQUARE;
			ADVANCE(1);
		} else if (*start.data == '{') {
			cur->type = TOK_LCURLY;
			ADVANCE(1);
		} else if (*start.data == '}') {
			cur->type = TOK_RCURLY;
			ADVANCE(1);
		} else if (isdigit(*start.data)) {
			cur->slice.data = start.data;
			cur->slice.len = 1;
			ADVANCE(1);
			cur->type = TOK_NUM;
			while (isdigit(*start.data)) {
				ADVANCE(1);
				cur->slice.len++;
			}
		} else if (*start.data == '"') {
			ADVANCE(1);
			cur->slice.data = start.data;
			cur->type = TOK_STRING;
			while (*start.data != '"') {
				ADVANCE(1);
				cur->slice.len++;
			}

			ADVANCE(1);
		} else if (*start.data == '+') {
			cur->type = TOK_PLUS;
			ADVANCE(1);
		} else if (*start.data == '-') {
			cur->type = TOK_MINUS;
			ADVANCE(1);
		} else if (*start.data == '=') {
			cur->type = TOK_EQUAL;
			ADVANCE(1);
		} else if (isalpha(*start.data)) {
			cur->type = TOK_NAME;
			cur->slice.data = start.data;
			cur->slice.len = 1;
			ADVANCE(1);
			while (isalnum(*start.data)) {
				ADVANCE(1);
				cur->slice.len++;
			}
		} else {
			error(line, col, "invalid token");
		}

		cur->next = xcalloc(1, sizeof(struct token));

		cur = cur->next;
	}

	cur->line = line;
	cur->col = col;

	return head;
}
