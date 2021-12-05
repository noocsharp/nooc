#define TEXT_OFFSET 0x101000
#define DATA_OFFSET 0x102000

enum tokentype {
	TOK_NONE = 0,
	TOK_NAME,

	TOK_LPAREN,
	TOK_RPAREN,
	TOK_LCURLY,
	TOK_RCURLY,

	TOK_PLUS,
	TOK_MINUS,
	TOK_GREATER,

	TOK_COMMA,
	TOK_EQUAL,

	TOK_NUM,
	TOK_STRING,

	TOK_LET,
	TOK_IF,
	TOK_ELSE,
	TOK_LOOP
};

struct slice {
	size_t cap;
	size_t len;
	char *data;
};

struct token {
	enum tokentype type;
	struct slice slice;
	struct token *next;
};

struct fparams {
	size_t cap;
	size_t len;
	size_t *data; // struct exprs
};

struct fcall {
	struct slice name;
	struct fparams params;
};

// FIXME: make a struct for more complex types
enum type {
	TYPE_I64,
	TYPE_STR
};

struct decl {
	struct slice s;
	enum type type;
	size_t val; // struct exprs
	size_t addr;
};

struct decls {
	size_t cap;
	size_t len;
	struct decl *data;
};

struct data {
	size_t cap;
	size_t len;
	char *data;
};

struct item {
	enum {
		ITEM_DECL,
		ITEM_EXPR
	} kind;
	size_t idx;
};

struct block {
	size_t cap;
	size_t len;
	struct item *data;
};

struct cond {
	size_t cond; // struct exprs
	struct block bif;
	struct block belse;
};

struct loop {
	struct block block;
};


enum binop {
	OP_PLUS,
	OP_MINUS,
	OP_GREATER,
};

struct value {
	union {
		uint64_t i;
		struct slice s;
	} v;
};

enum exprkind {
	EXPR_LIT,
	EXPR_IDENT,
	EXPR_BINARY,
	EXPR_FCALL,
	EXPR_COND,
	EXPR_LOOP
};

enum class {
	C_INT,
	C_STR,
};

struct expr {
	enum exprkind kind;
	enum class class;
	union {
		struct value v;
		enum binop op;
		struct slice s;
		struct fcall call;
		struct cond cond;
		struct loop loop;
	} d;
	size_t left;
	size_t right;
};

struct exprs {
	size_t cap;
	size_t len;
	struct expr *data;
};
