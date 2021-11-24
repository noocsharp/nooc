#define DATA_OFFSET 0x2000

enum tokentype {
	TOK_NONE = 0,
	TOK_NAME,

	TOK_LPAREN,
	TOK_RPAREN,

	TOK_PLUS,
	TOK_MINUS,

	TOK_COMMA,
	TOK_EQUAL,

	TOK_NUM,
	TOK_STRING,
};

struct slice {
	char *ptr;
	size_t len;
};

struct token {
	enum tokentype type;
	struct slice slice;
	struct token *next;
};

struct fparams {
	size_t cap;
	size_t len;
	size_t *data;
};

struct fcall {
	struct slice s;
	struct fparams params;
};

struct decl {
	struct slice s;
	size_t val; // struct exprs
	size_t addr;
};

struct data {
	size_t cap;
	size_t len;
	char *data;
};

struct item {
	enum {
		ITEM_DECL,
		ITEM_CALL
	} kind;
	union {
		struct decl decl;
		struct fcall call;
	} d;
};

struct items {
	size_t cap;
	size_t len;
	struct item *data;
};

struct decls {
	size_t cap;
	size_t len;
	uint64_t *data;
};

enum primitive {
	P_INT,
	P_STR,
};

enum binop {
	OP_PLUS,
	OP_MINUS,
};

struct value {
	enum primitive type;
	union {
		uint64_t val;
		struct slice s;
	} v;
};

enum exprkind {
	EXPR_LIT,
	EXPR_IDENT,
	EXPR_BINARY
};

struct expr {
	enum exprkind kind;
	union {
		struct value v;
		enum binop op;
		struct slice s;
	} d;
	size_t left;
	size_t right;
};

struct exprs {
	size_t cap;
	size_t len;
	struct expr *data;
};
