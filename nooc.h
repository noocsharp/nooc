#define TEXT_OFFSET 0x101000
#define DATA_OFFSET 0x102000

#define BLOCKSTACKSIZE 32

enum tokentype {
	TOK_NONE = 0,
	TOK_NAME,

	TOK_LPAREN,
	TOK_RPAREN,
	TOK_LCURLY,
	TOK_RCURLY,
	TOK_LSQUARE,
	TOK_RSQUARE,

	TOK_PLUS,
	TOK_MINUS,
	TOK_GREATER,

	TOK_DOLLAR,

	TOK_COMMA,
	TOK_EQUAL,

	TOK_NUM,
	TOK_STRING,

	TOK_LET,
	TOK_IF,
	TOK_ELSE,
	TOK_LOOP,
	TOK_RETURN
};

struct slice {
	size_t cap;
	size_t len;
	char *data;
};

struct token {
	enum tokentype type;
	size_t line, col;
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

struct typelist {
	size_t cap;
	size_t len;
	size_t *data; // struct types
};

enum typeclass {
	TYPE_NONE,
	TYPE_INT,
	TYPE_ARRAY,
	TYPE_REF,
	TYPE_PROC,
};

struct type {
	enum typeclass class;
	size_t size;
	union {
		struct {
			struct typelist in;
			struct typelist out;
		} params;
		struct {
			size_t len;
			size_t subtype; // struct types
		} arr;
		size_t subtype; // struct types
	} d;
};

struct nametype {
	struct slice name;
	size_t type; // struct types
};

struct types {
	size_t cap;
	size_t len;
	struct type *data;
};

struct nametypes {
	size_t cap;
	size_t len;
	struct nametype *data;
};

struct assgn {
	struct slice s;
	size_t val; // struct exprs
	struct token *start;
};

struct assgns {
	size_t cap;
	size_t len;
	struct assgn *data;
};

struct decl {
	struct slice s;
	size_t type;
	size_t val; // struct exprs
	bool in;
	bool out;
	bool toplevel;
	uint64_t index;

	union {
		int64_t offset;
		uint64_t addr;
	} w;
	struct token *start;
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
		ITEM_ASSGN,
		ITEM_EXPR,
		ITEM_RETURN,
	} kind;
	size_t idx;
	struct token *start;
};

struct block {
	struct {
		size_t cap;
		size_t len;
		struct decl *data;
	} decls;
	size_t datasize;
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

struct proc {
	struct nametypes in;
	struct nametypes out;
	struct block block;
};

struct binop {
	enum {
		BOP_PLUS,
		BOP_MINUS,
		BOP_GREATER,
		BOP_EQUAL,
	} kind;
	size_t left;
	size_t right;
};

struct unop {
	enum {
		UOP_REF,
	} kind;
	size_t expr;
};

struct value {
	union {
		int64_t i64;
		int32_t i32;
		struct slice s;
	} v;
};

enum exprkind {
	EXPR_LIT,
	EXPR_IDENT,
	EXPR_BINARY,
	EXPR_UNARY,
	EXPR_FCALL,
	EXPR_COND,
	EXPR_LOOP,
	EXPR_PROC,
};

enum class {
	C_INT = 1,
	C_REF,
	C_STR,
	C_PROC,
};

struct expr {
	enum exprkind kind;
	enum class class;
	union {
		struct value v;
		struct binop bop;
		struct unop uop;
		struct slice s;
		struct fcall call;
		struct cond cond;
		struct loop loop;
		struct proc proc;
	} d;
	struct token *start;
};

struct exprs {
	size_t cap;
	size_t len;
	struct expr *data;
};
