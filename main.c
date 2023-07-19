#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

int token;  // current token
char* src, *old_src;  // pointer to source code string;
int poolsize;  // default size of text/data/stack
int line;  // line number

int64_t* text,  // text segment
    * old_text, // for dump text segment
    * stack;  // stack
char* data;  // data segment

int64_t* pc, *bp, *sp, ax, cycle;  // virtual machine registers

// instructions
enum {
    LEA,   // load effective address
    IMM,  // load immediate number into `ax`
    JMP,   // jump to
    CALL,   // call function
    JZ,   // jump if zero
    JNZ,  // jump if not zero
    ENT,  // enter (make new call frame)
    ADJ,  // remove arguments from frame
    LEV,  // restore old call frame
    LI,  // load integer addressed by `ax` into `ax`
    LC,  // load char addressed by `ax` into `ax`
    SI,  // store `ax` as integer to memory addressed by TOS
    SC,  // store `ax` as char to memory addressed by TOS
    PUSH,  // push
    OR,  // or
    XOR,  // xor
    AND,  // and
    EQ,  // equal
    NE,  // not equal
    LT,  // less than
    GT,  // greater than
    LE,  // less than or equal
    GE,  // greater than or equal
    SHL,  // shift left
    SHR,  // shift right
    ADD,  // add
    SUB,  // subtract
    MUL,  // multiply
    DIV,  // div
    MOD,  // mod
    OPEN,  // open
    READ,  // read
    CLOS,  // close
    PRTF,  // printf
    MALC,  // malloc
    MSET,  // memset
    MCMP,  // memcmp
    EXIT  // exit
};

// token and classes (operators last and in precedence order)
enum {
    Num = 128,
    Fun,  // function
    Sys,  // system call ?
    Glo,  // global ?
    Loc,  // local ?
    Id,  // Identifier
    Char,  // 'char'
    Else,  // 'else'
    Enum,  // 'enum'
    If,  // 'if'
    Int,  // 'int'
    Return,  // 'return'
    Sizeof,  // 'sizeof'
    While,  // 'while'
    // operators, in precedence order
    Assign,  // '='
    Cond,  // '?'
    Lor,  // logical or: ||
    Lan,  // logical and: &&
    Or,  // bit-wise or: |
    Xor,  // bit-wise xor: ^
    And,  // bit-wise and: &
    Eq,  // '=='
    Ne,  // '!='
    Lt,  // '<'
    Gt,   // '>'
    Le,  // '<='
    Ge,  // '>='
    Shl,  // '<<'
    Shr,  // '>>'
    Add,  // '+'
    Sub,  // '-'
    Mul,  // '*'
    Div,  // '/'
    Mod,  // '%'
    Inc,  // '++'
    Dec,  // '--'
    Brak,  // '['
};

struct identifier {
    int token;  // identifier or keyword
    int hash;
    char* name;
    int class;  // number, global, local?
    int type;
    int value;
    // global same variable information, meeting same name
    int Bclass;
    int Btype;
    int Bvalue;
};

int64_t token_val;  // value of current token (mainly for number)
int64_t* current_id,  // current parsed ID
    * symbols;  // symbol table

// 我们不支持struct，故用下面的编码方式来表示struct
// fields of identifier
enum {
    Token,
    Hash,
    Name,
    Type,
    Class,
    Value,
    BType,
    BClass,
    BValue,
    IdSize
};

// types of variables/function
enum { CHAR, INT, PTR };
int64_t* idmain;  // the `main` function
/*
program ::= {global_declaration}+

global_declaration ::= enum_decl | variable_decl | function_decl

enum_decl ::= 'enum' [id] '{' id ['=' 'num'] {',' id ['=' 'num'] '}'

variable_decl ::= type {'*'} id { ',' {'*'} id } ';'

function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'

parameter_decl ::= type {'*'} id {',' type {'*'} id}

body_decl ::= {variable_decl}, {statement}

statement ::= non_empty_statement | empty_statement

non_empty_statement ::= if_statement | while_statement | '{' statement '}'
                     | 'return' expression | expression ';'

if_statement ::= 'if' '(' expression ')' statement ['else' non_empty_statement]

while_statement ::= 'while' '(' expression ')' non_empty_statement

*/

int basetype;  // the type of a declaration, make it global for convenience
int expr_type;  // the type of an expression

// function frame
//
// 0: arg1
// 1: arg2
// 2: arg3
// 3: return address
// 4: old bp ponter <- index_of_bp
// 5: local var 1
// 6: local var 2
int index_of_bp;  // index of bp pointer on stack

void next()
{
    char* last_pos;
    int hash;

    while ((token = *src) != 0) {
        ++src;
        // parse token here
        if (token == '\n') {
            ++line;
        }
        else if (token == '=') {
            // parse '==' and '='
            if (*src == '=') {
                src ++;
                token = Eq;
            } else {
                token = Assign;
            }
            return;
        }
        else if (token == '+') {
            // parse '+' and '++'
            if (*src == '+') {
                src ++;
                token = Inc;
            } else {
                token = Add;
            }
            return;
        }
        else if (token == '-') {
            // parse '-' and '--'
            if (*src == '-') {
                src ++;
                token = Dec;
            } else {
                token = Sub;
            }
            return;
        }
        else if (token == '!') {
            // parse '!='
            if (*src == '=') {
                src++;
                token = Ne;
            }
            return;
        }
        else if (token == '<') {
            // parse '<=', '<<' or '<'
            if (*src == '=') {
                src ++;
                token = Le;
            } else if (*src == '<') {
                src ++;
                token = Shl;
            } else {
                token = Lt;
            }
            return;
        }
        else if (token == '>') {
            // parse '>=', '>>' or '>'
            if (*src == '=') {
                src ++;
                token = Ge;
            } else if (*src == '>') {
                src ++;
                token = Shr;
            } else {
                token = Gt;
            }
            return;
        }
        else if (token == '|') {
            // parse '|' or '||'
            if (*src == '|') {
                src ++;
                token = Lor;
            } else {
                token = Or;
            }
            return;
        }
        else if (token == '&') {
            // parse '&' and '&&'
            if (*src == '&') {
                src ++;
                token = Lan;
            } else {
                token = And;
            }
            return;
        }
        else if (token == '^') {
            token = Xor;
            return;
        }
        else if (token == '%') {
            token = Mod;
            return;
        }
        else if (token == '*') {
            token = Mul;
            return;
        }
        else if (token == '[') {
            token = Brak;
            return;
        }
        else if (token == '?') {
            token = Cond;
            return;
        }
        else if (token == '~' || token == ';' || token == '{' || token == '}' || token == '(' || token == ')' || token == ']' || token == ',' || token == ':') {
            // directly return the character as token;
            return;
        }
        // parse comments
        else if (token == '/') {
            if (*src == '/') {
                // skip comments
                while (*src != 0 && *src != '\n') {
                    ++src;
                }
            } else {
                // divide operator
                token = Div;
                return;
            }
        }
        // parse string
        else if (token == '"' || token == '\'') {
            // parse string literal, currently, the only supported escape
            // character is '\n', store the string literal into data.
            last_pos = data;
            while (*src != 0 && *src != token) {
                token_val = *src++;
                if (token_val == '\\') {
                    // escape character
                    token_val = *src++;
                    if (token_val == 'n') {
                        token_val = '\n';
                    }
                }

                if (token == '"') {
                    *data++ = token_val;
                }
            }

            src++;
            // if it is a single character, return Num token
            if (token == '"') {
                token_val = (int64_t)last_pos;
            } else {
                token = Num;
            }

            return;
        }
        // parse number
        else if (token >= '0' && token <= '9') {
            // parse number, three kinds: dec(123) hex(0x123) oct(017)
            token_val = token - '0';
            if (token_val > 0) {
                // dec, starts with [1-9]
                while (*src >= '0' && *src <= '9') {
                    token_val = token_val*10 + *src++ - '0';
                }
            } else {
                // starts with number 0
                if (*src == 'x' || *src == 'X') {
                    //hex
                    token = *++src;
                    while ((token >= '0' && token <= '9') || (token >= 'a' && token <= 'f') || (token >= 'A' && token <= 'F')) {
                        token_val = token_val * 16 + (token & 15) + (token >= 'A' ? 9 : 0);
                        token = *++src;
                    }
                } else {
                    // oct
                    while (*src >= '0' && *src <= '7') {
                        token_val = token_val*8 + *src++ - '0';
                    }
                }
            }

            token = Num;
            return;
        }
        else if ((token >= 'a' && token <= 'z') || (token >= 'A' && token <= 'Z') || token == '_')
        {
            // parse identifier
            last_pos = src - 1;
            hash = token;
            while ((*src >= 'a' && *src <= 'z') || (*src >= 'A' && *src <= 'Z') ||
                (*src >= '0' && *src <= '9') || (*src == '_'))
            {
                hash = hash * 147 + *src;
                src++;
            }
            // look for existing identifier, linear search
            current_id = symbols;
            while (current_id[Token]) {
                if (current_id[Hash] == hash &&
                    !memcmp((char*)current_id[Name], last_pos, src - last_pos))
                {
                    // found one, return
                    token = current_id[Token];
                    return;
                }
                current_id = current_id + IdSize;
            }
            // store new ID
            current_id[Name] = (int64_t)last_pos;
            current_id[Hash] = hash;
            token = current_id[Token] = Id;
            return;
        }
        else if (token == '#') {
            // skip macro, because we will not support it
            while (*src != 0 && *src != '\n') {
                src++;
            }
        }
    }
    return;
}

void match(int tk)
{
    if (token != tk) {
        printf("expected token: %d(%c), got: %d(%c)\n", tk, tk, token, token);
        exit(-1);
    }
    next();
}

void expression(int level)
{
    int64_t *id;
    int tmp;
    int64_t *addr;
    {
        if (!token) {
            printf("%d: unexpected token EOF of expression\n", line);
            exit(-1);
        }

        if (token == Num) {
            match(Num);

            // emit code
            *++text = IMM;
            *++text = token_val;
            printf("code: IMM %ld\n", *text);
            expr_type = INT;
        } else if (token == '"') {
            // continuous string "abc" "abc"
            // emit code
            *++text = IMM;
            *++text = token_val;
            printf("code: IMM %ld\n", *text);

            match('"');
            // store the rest strings
            while (token == '"') {
                match('"');
            }
            // append the end of string character '\0', all the data are the default
            // to 0, so just move data one position forward.
            data = (char*)(((intptr_t)data + sizeof(intptr_t)) & (-sizeof(intptr_t)));
            expr_type = PTR;
        } else if (token == Sizeof) {
            // sizeof is actually an unary operator
            // now only `sizeof(int)`, `sizeof(char)`, and `sizeof(*...)` are supported
            match(Sizeof);
            match('(');
            expr_type = INT;

            if (token == Int) {
                match(Int);
            } else if (token == Char) {
                match(Char);
                expr_type = CHAR;
            }

            while (token == Mul) {
                match(Mul);
                expr_type = expr_type + PTR;
            }
            match(')');

            // emit code
            *++text = IMM;
            *++text = (expr_type == CHAR) ? sizeof(char) : sizeof(int);
            printf("code: IMM %ld\n", *text);

            expr_type = INT;
        } else if (token == Id) {
            // there are several type when occurs to Id
            // but this is unit, so it can only be
            // 1. function call
            // 2. Enum variable
            // 3. global/local variable
            match(Id);

            id = current_id;
            if (token == '(') {
                // function call
                match('(');

                tmp = 0; // number of arguments
                while (token != ')') {
                    expression(Assign);
                    *++text = PUSH;
                    tmp++;

                    if (token == ',') {
                        match(',');
                    }
                }
                match(')');
                // emit code
                if (id[Class] == Sys) {
                    // system functions
                    *++text = id[Value];
                } else if (id[Class] == Fun) {
                    // function call
                    *++text = CALL;
                    *++text = id[Value];
                } else {
                    printf("%d: bad function call\n", line);
                    exit(-1);
                }
                // clean the stack for arguments
                if (tmp > 0) {
                    *++text = ADJ;
                    *++text = tmp;
                    printf("code: ADJ %ld\n", *text);
                }
                expr_type = id[Type];
            } else if (id[Class] == Num) {
                // enum variable
                *++text = IMM;
                *++text = id[Value];
                printf("code: IMM %ld\n", *text);
                expr_type = INT;
            } else {
                // variable
                if (id[Class] == Loc) {
                    *++text = LEA;
                    *++text = index_of_bp - id[Value];
                    printf("code: LEA %ld\n", *text);
                } else if (id[Class] == Glo) {
                    *++text = IMM;
                    *++text = id[Value];
                    printf("code: IMM %ld\n", *text);
                } else {
                    printf("%d: undefined variable\n", line);
                    exit(-1);
                }
                // emit code, default behavior is to load the value of
                // address which is stored in `ax`
                expr_type = id[Type];
                *++text = (expr_type == Char) ? LC : LI;
                printf("code: LC\n");
            }
        } else if (token == '(') {
            // cast or paranthesis
            match('(');
            if (token == Int || token == Char) {
                tmp = (token == Char) ? CHAR : INT;  // cast type
                match(token);
                while (token == Mul) {
                    match(Mul);
                    tmp = tmp + PTR;
                }

                match(')');
                expression(Inc);  // cast has precedence as Inc(++)
                expr_type = tmp;
            } else {
                // normal parenthesis
                expression(Assign);
                match(')');
            }
        } else if (token == Mul) {
            // dereference *<addr>
            match(Mul);
            expression(Inc);  // dereference has the same precedence as Inc(++)

            if (expr_type >= PTR) {
                expr_type = expr_type - PTR;
            } else {
                printf("%d: bad dereference\n", line);
                exit(-1);
            }
            *++text = (expr_type == CHAR) ? LC : LI;
            printf("code: %s\n", *text == LC ? "LC" : "LI");
        } else if (token == And) {
            // get the address of
            match(And);
            expression(Inc);
            if (*text == LC || *text == LI) {
                text--;
            } else {
                printf("%d: bad address of\n", line);
                exit(-1);
            }
            expr_type = expr_type + PTR;
        } else if (token == '!') {
            // not
            match('!');
            expression(Inc);

            // emit code, use <expr> == 0
            *++text = PUSH;
            *++text = IMM;
            *++text = 0;
            *++text = EQ;
            expr_type = INT;
        } else if (token == '~') {
            // bitwise not
            match('~');
            expression(Inc);

            // emit code, use <expr> XOR -1
            *++text = PUSH;
            *++text = IMM;
            *++text = -1;
            *++text = XOR;

            expr_type = INT;
        } else if (token == Add) {
            // +var, do nothing
            match(Add);
            expression(Inc);
            expr_type = INT;
        } else if (token == Sub) {
            // -var
            match(Sub);
            if (token == Num) {
                *++text = IMM;
                *++text = -token_val;
                match(Num);
            } else {
                *++text = IMM;
                *++text = -1;
                *++text = PUSH;
                expression(Inc);
                *++text = MUL;
            }
            expr_type = INT;
        } else if (token == Inc || token == Dec) {
            tmp = token;
            match(token);
            expression(Inc);
            if (*text == LC) {
                *text = PUSH; // to duplicate the address
                *++text = LC;
            } else if (*text == LI) {
                *text = PUSH;
                *++text = LI;
            } else {
                printf("%d: bad lvalue of pre-increment\n", line);
                exit(-1);
            }
            *++text = PUSH;
            *++text = IMM;
            *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
            *++text = (tmp == Inc) ? ADD : SUB;
            *++text = (expr_type == CHAR) ? SC : SI;
        } else {
            printf("%d: bad expression\n", line);
            exit(-1);
        }
    }

    // binary operator and postfix operators
    {
        while (token >= level) {
            // handle according to current operator's precedence
            tmp = expr_type;
            if (token == Assign) {
                // var = expr
                match(Assign);
                if (*text == LC || *text == LI) {
                    *text = PUSH;  // save the lvalue's pointer
                } else {
                    printf("%d: bad lvalue in assignment\n", line);
                    exit(-1);
                }
                expression(Assign);

                expr_type = tmp;
                *++text = (expr_type == CHAR) ? SC : SI;
            } else if (token == Cond) {
                // expr ? a : b
                match(Cond);
                *++text = JZ;
                addr = ++text;
                expression(Assign);
                if (token == ':') {
                    match(':');
                } else {
                    printf("%d: missing colon in conditional\n", line);
                    exit(-1);
                }
                *addr = (intptr_t)(text + 3);
                *++text = JMP;
                addr = ++text;

                expression(Cond);
                *addr = (intptr_t)(text + 1);
            } else if (token == Lor) {
                // logic or
                match(Lor);

                *++text = JNZ;
                addr = ++text;

                expression(Lan);
                *addr = (intptr_t)(text + 1);
                expr_type = INT;
            } else if (token == Lan) {
                // logic and
                match(Lan);

                *++text = JZ;
                addr = ++text;

                expression(Or);

                *addr = (intptr_t)(text + 1);
                expr_type = INT;
            } else if (token == Or) {
                // bitwise or
                match(Or);

                *++text = PUSH;
                expression(Xor);
                *++text = OR;
                expr_type = INT;
            } else if (token == Xor) {
                // bitwise xor
                match(Xor);

                *++text = PUSH;
                expression(And);
                *++text = XOR;
                expr_type = INT;
            } else if (token == And) {
                // bitwise and
                match(And);

                *++text = PUSH;
                expression(Eq);
                *++text = AND;
                expr_type = INT;
            } else if (token == Eq) {
                // equal ==
                match(Eq);

                *++text = PUSH;
                expression(Ne);
                *++text = EQ;
                expr_type = INT;
            } else if (token == Ne) {
                // not equal !=
                match(Ne);

                *++text = PUSH;
                expression(Lt);
                *++text = NE;
                expr_type = INT;
            } else if (token == Lt) {
                // less than
                match(Lt);

                *++text = PUSH;
                expression(Shl);
                *++text = LT;
                expr_type = INT;
            } else if (token == Gt) {
                // greater than
                match(Gt);

                *++text = PUSH;
                expression(Shl);
                *++text = GT;
                expr_type = INT;
            } else if (token == Le) {
                // less than or equal to
                match(Le);

                *++text = PUSH;
                expression(Shl);
                *++text = LE;
                expr_type = INT;
            } else if (token == Ge) {
                // greater than or equal to
                match(Ge);

                *++text = PUSH;
                expression(Shl);
                *++text = GE;
                expr_type = INT;
            } else if (token == Shl) {
                // shift left
                match(Shl);

                *++text = PUSH;
                expression(Add);
                *++text = SHL;
                expr_type = INT;
            } else if (token == Shr) {
                // shift right
                match(Shr);

                *++text = PUSH;
                expression(Add);
                *++text = SHR;
                expr_type = INT;
            } else if (token == Add) {
                // add
                match(Add);

                *++text = PUSH;
                expression(Mul);

                expr_type = tmp;
                if (expr_type > PTR) {
                    // pointer type, and not `char*`
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int64_t);
                    *++text = MUL;
                }
                *++text = ADD;
            } else if (token == Sub){
                // Sub
                match (Sub);

                *++text = PUSH;
                expression(Mul);
                if (tmp > PTR && tmp == expr_type) {
                    // pointer subtraction
                    *++text = SUB;
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int64_t);
                    *++text = DIV;
                    expr_type = INT;
                } else if (tmp > PTR) {
                    // pointer movement
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int64_t);
                    *++text = MUL;
                    *++text = SUB;
                    expr_type = tmp;
                } else {
                    // numeral subtraction
                    *++text = SUB;
                    expr_type = tmp;
                }
            } else if (token == Mul) {
                // multiply
                match(Mul);
                *++text = PUSH;
                expression(Inc);
                *++text = MUL;
                expr_type = tmp;
            } else if (token == Div) {
                // divide
                match(Div);
                *++text = PUSH;
                expression(Inc);
                *++text = DIV;
                expr_type = tmp;
            } else if (token == Mod) {
                // Modulo
                match(Mod);
                *++text = PUSH;
                expression(Inc);
                *++text = MOD;
                expr_type = tmp;
            } else if (token == Inc || token == Dec) {
                // postfix inc(++) and dec(--)
                // we will increase the value to the variable and decrease it
                // on `ax` to get its original value.
                if (*text == LI) {
                    *text = PUSH;
                    *++text = LI;
                }
                else if (*text == LC) {
                    *text = PUSH;
                    *++text = LC;
                }
                else {
                    printf("%d: bad value in increment\n", line);
                    exit(-1);
                }

                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? ADD : SUB;
                *++text = (expr_type == CHAR) ? SC : SI;
                *++text = PUSH;
                *++text = IMM;
                *++text = (expr_type > PTR) ? sizeof(int) : sizeof(char);
                *++text = (token == Inc) ? SUB : ADD;
                match(token);
            } else if (token == Brak) {
                // array access var[xx]
                match(Brak);
                *++text = PUSH;
                expression(Assign);
                match(']');

                if (tmp > PTR) {
                    // pointer, `not char *`
                    *++text = PUSH;
                    *++text = IMM;
                    *++text = sizeof(int64_t);
                    *++text = MUL;
                }
                else if (tmp < PTR) {
                    printf("%d: pointer type expected\n", line);
                    exit(-1);
                }
                expr_type = tmp - PTR;
                *++text = ADD;
                *++text = (expr_type == CHAR) ? LC : LI;
            } else {
                printf("%d: compiler error, token = %d\n", line, token);
                exit(-1);
            }
        }
    }
}

void statement()
{
    // there are 6 kinds of statements here:
    // 1. if (...) <statement> [else <statement>]
    // 2. while (...) <statement>
    // 3. { <statement> }
    // 4. return xxx;
    // 5. <empty statement>;
    // 6. expression; (expression end with semicolon)

    int64_t *a, *b;  // bless for branch control

    if (token == If) {
        // if (...) <statement> [else <statement]
        //
        // if (...)       <cond>
        //                JZ a
        //  <statement>   <statement>
        // else:          JMP b
        // a:             a:
        //  <statement>   <statement>
        // b:             b:
        //

        match(If);
        match('(');
        expression(Assign); // parse condition
        match(')');

        // emit code for if
        *++text = JZ;
        b = ++text;  // 跳转到对应else分支的指令地址的text位置，待会要填充
        // pointing to label a

        statement();
        if (token == Else) {
            match(Else);

            // +1 => JMP
            // +1 => label b address
            // +1 => first op instr in 'else' statement
            *b = (int64_t)(text + 3);
            // emit code for JMP b
            *++text = JMP;
            b = ++text;  // pointing to label b

            statement();
        }
        *b = (int64_t)(text + 1);  // now, we know label b
    }
    else if (token == While) {
        // a:                   a:
        //  while (<cond>)      <cond>
        //                      JZ b
        //    <statement>       <statement>
        //                      JMP a
        // b:                   b:
        match(While);

        a = text + 1;

        match('(');
        expression(Assign);
        match(')');

        *++text = JZ;
        b = ++text;

        statement();

        *++text = JMP;
        *++text = (int64_t)a;
        *b = (int64_t)(text + 1);
    } else if (token == '{') {
        // { <statement> ... }
        match('{');

        while (token != '}') {
            statement();
        }

        match('}');
    } else if (token == Return) {
        // return [<expression>] ;
        match(token);
        if (token != ';') {
            expression(Assign);
        }
        match(';');

        // emit code for return
        *++text = LEV;
    } else if (token == ';') {
        // empty statement
        match(';');
    } else {
        // a = b; or function_call();
        expression(Assign);
        match(';');
    }
}

void function_parameter()
{
    int type;
    int params;
    params = 0;
    while (token != ')') {
        // int name, ...
        type = INT;
        if (token == Int) {
            match(Int);
        } else if (token == Char) {
            match(Char);
            type = CHAR;
        }
        // point type?
        while (token == Mul) {
            match(Mul);
            type = type + PTR;
        }

        // parameter name
        if (token != Id) {
            printf("%d: bad parameter declaration\n", line);
            exit(-1);
        }
        if (current_id[Class] == Loc) {
            printf("%d: duplicate parameter declaration\n", line);
            exit(-1);
        }

        match(Id);
        // store the local variable
        current_id[BClass] = current_id[Class];
        current_id[Class] = Loc;
        current_id[BType] = current_id[Type];
        current_id[Type] = type;
        current_id[BValue] = current_id[Value];
        current_id[Value] = params++;

        if (token == ',') {
            match(',');
        }
    }
    index_of_bp = params + 1;
}

void function_body()
{
    // type func_name(...) { ... }
    // {
    //  1. local declarations
    //  2. statements
    // }
    int pos_local;  // position of local variables on the stack
    int type;
    pos_local = index_of_bp;

    while (token == Int || token == Char) {
        // local variable declaration, just like global ones
        basetype = (token == Int) ? INT : CHAR;
        match(token);

        while (token != ';') {
            type = basetype;
            while (token == Mul) {
                match(Mul);
                type = type + PTR;
            }

            if (token != Id) {
                printf("%d: bad local declaration\n", line);
                exit(-1);
            }
            if (current_id[Class] == Loc) {
                printf("%d: duplicate local declaration\n", line);
                exit(-1);
            }
            match(Id);

            // store the local variable
            current_id[BClass] = current_id[Class];
            current_id[Class] = Loc;
            current_id[BType] = current_id[Type];
            current_id[Type] = type;
            current_id[BValue] = current_id[Value];
            current_id[Value] = ++pos_local;

            if (token == ',') {
                match(',');
            }
        }
        match(';');
    }

    // save the stack size for local variables
    *++text = ENT;
    *++text = pos_local - index_of_bp;

    // statements
    while (token != '}') {
        statement();
    }

    // emit code for leaving the sub function
    *++text = LEV;
}

void function_declaration()
{
    // type func_name (...) { ... }
    match('(');
    function_parameter();
    match(')');
    match('{');
    function_body();
    // match('}');  // later someone will consume it

    // unwind local variable declarations for all local variables
    current_id = symbols;
    while (current_id[Token]) {
        if (current_id[Class] == Loc) {
            current_id[Class] = current_id[BClass];
            current_id[Type] = current_id[BType];
            current_id[Value] = current_id[BValue];
        }
        current_id = current_id + IdSize;
    }
}

void enum_declaration()
{
    // parse:
    // enum [id] { a [= 1] [, b [= 3] ]* }
    int i;
    i = 0;
    // 已经吞掉了 {, 从'a'开始
    while (token != '}') {
        if (token != Id) {
            printf("%d: bad enum identifier %d\n", line, token);
            exit(-1);
        }
        next();
        if (token == Assign) {
            // like {a=10}
            next();
            if (token != Num) {
                printf("%d: bad enum initializer\n", line);
                exit(-1);
            }
            i = token_val;
            next();
        }

        current_id[Class] = Num;
        current_id[Type] = INT;
        current_id[Value] = i++;

        if (token == ',') {
            next();
        }
    }

}

void global_declaration()
{
    // global_declaration ::= enum_decl | variable_decl | function_decl
    //
    // enum_decl ::= 'enum' [id] '{' id ['=' 'num'] {',' id ['=' 'num'} '}'
    //
    // variable_decl ::= type {'*'} id { ',' {'*'} id } ';'
    //
    // function_decl ::= type {'*'} id '(' parameter_decl ')' '{' body_decl '}'
    int type;  // tmp, actual type for variable
    int i;  // tmp
    basetype = INT;

    // parse enum, this should be treated alone
    if (token == Enum) {
        // enum [id] { a = 10, b = 20, ... }
        match(Enum);
        if (token != '{') {
            match(Id);  // skip the [id] part
        }
        if (token == '{') {
            // parse the assign part
            match('{');
            enum_declaration();
            match('}');
        }
        match(';');
        return;
    }
    // parse type information
    if (token == Int) {
        match(Int);
    } else if (token == Char) {
        match(Char);
        basetype = CHAR;
    }

    // parse the comma separated variable declaration
    // 碰到分号代表变量申明或定义结束，或者函数声明结束
    // 碰到右括号代表函数定义结束
    while (token != ';' && token != '}') {
        // 类型解析：
        // 要么是变量的类型： int a;
        // 要么是函数返回值的类型：int foo(); int foo(...) { }
        type = basetype;
        // parse pointer type, note that there might exit 'int******'
        while (token == Mul) {
            match(Mul);
            type = type + PTR;
        }

        if (token != Id) {
            // invalid declaration
            printf("%d: bad global declaration\n", line);
            exit(-1);
        }
        if (current_id[Class]) {
            // identifier exists
            printf("%d: duplicate global declaration\n", line);
            exit(-1);
        }
        // identifier要么是变量名，要么是函数名
        match(Id);
        current_id[Type] = type;

        // 碰到'('，就是函数声明或者定义
        if (token == '(') {
            current_id[Class] = Fun;
            current_id[Value] = (int64_t)(text + 1);  // the memory address
            function_declaration();
        } else {  // 否则就是变量声明或者定义
            // variable declaration
            current_id[Class] = Glo;  // global variable
            current_id[Value] = (int64_t)data;  // assign memory address
            data = data + sizeof(int64_t);
        }

        if (token == ',') {
            match(',');
        }
    }
    next();
}

void program()
{
    next();  // get next token
    while (token > 0) {
        global_declaration();
    }
}

int eval()
{
    int64_t op;
    int64_t* tmp;
    while (1) {
        op = *pc++; // get next operation code
        switch (op) {
        // IMM (<-- pc)
        case IMM: {
            ax = *pc++;
            break;
        }
        // LC (loc <-- ax)
        case LC: {
            ax = *(char*)ax;
            break;
        }
        // LI (loc <-- ax)
        case LI:  {
            ax = *(int64_t*)ax;
            break;
        }
        // SC (loc <-- sp)
        case SC: {
            *(char*)*sp++ = ax;
            break;
        }
        // SI (loc <-- sp)
        case SI: {
            *(int64_t*)*sp++ = ax;
            break;
        }
        // PUSH
        case PUSH: {
            *--sp = ax;
            break;
        }
        // JMP <addr> (<-- pc)
        case JMP: {
            pc = (int64_t*)*pc;
            break;
        }
        // JZ <addr> (<-- pc)
        case JZ: {
            pc = ax ? (pc + 1) : (int64_t*)*pc;
            break;
        }
            // JNZ <addr> (<-- pc)
        case JNZ: {
            pc = ax ? (int64_t*)*pc : (pc + 1);
            break;
        }
            // CALL <addr> (<-- pc)
        case CALL: {
            *--sp = (int64_t)(pc + 1);
            pc = (int64_t*)*pc;
            break;
        }
            // ENT <num of int> (<-- pc)
        case ENT: {
            *--sp = (int64_t)bp;
            bp = sp;
            sp = sp - *pc++;
            break;
        }
            // ADJ <num of int> (<-- pc)
        case ADJ: {
            sp = sp + *pc++;
            break;
        }
            // LEV
        case LEV: {
            sp = bp;
            bp = (int64_t*)*sp++;
            pc = (int64_t*)*sp++;
            break;
        }
            // LEA <n-th argument> in callee
            // arg: 1  ===> new_bp + 4
            // arg: 2  ===> new_bp + 3
            // arg: 3  ===> new_bp + 2
            // return address => new_bp + 1
            // old_bp ===> new_bp
            // local var 1 ===> new_bp - 1
            // local var 2 ===> new_bp - 2
        case LEA: {
            ax = (int64_t)(bp + *pc++);
            break;
        }
            // arithmetic operations
            // ax := (sp OP ax), sp++
        case OR: {
            ax = *sp++ | ax;
            break;
        }
        case XOR: {
            ax = *sp++ ^ ax;
            break;
        }
        case AND: {
            ax = *sp++ & ax;
            break;
        }
        case EQ: {
            ax = *sp++ == ax;
            break;
        }
        case NE: {
            ax = *sp++ != ax;
            break;
        }
        case LT: {
            ax = *sp++ < ax;
            break;
        }
        case LE: {
            ax = *sp++ <= ax;
            break;
        }
        case GT: {
            ax = *sp++ > ax;
            break;
        }
        case GE: {
            ax = *sp++ >= ax;
            break;
        }
        case SHL: {
            ax = *sp++ << ax;
            break;
        }
        case SHR: {
            ax = *sp++ >> ax;
            break;
        }
        case ADD: {
            ax = *sp++ + ax;
            break;
        }
        case SUB: {
            ax = *sp++ - ax;
            break;
        }
        case MUL: {
            ax = *sp++ * ax;
            break;
        }
        case DIV: {
            ax = *sp++ / ax;
            break;
        }
        case MOD: {
            ax = *sp++ % ax;
            break;
        }

            // helper operations
        case EXIT: {
            printf("exit(%ld)", *sp);
            return *sp;
        }
        case OPEN: {
            ax = open((char*)sp[1], sp[0]);
            break;
        }
        case CLOS: {
            ax = close(*sp);
            break;
        }
        case READ: {
            ax = read(sp[2], (char*)sp[1], *sp);
            break;
        }
        case PRTF: {
            tmp = sp + pc[1];
            ax = printf((char*)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]);
            break;
        }
        case MALC: {
            ax = (int64_t)malloc(*sp);
            break;
        }
        case MSET: {
            ax = (int64_t)memset((char*)sp[2], sp[1], *sp);
            break;
        }
        case MCMP: {
            ax = memcmp((char*)sp[2], (char*)sp[1], *sp);
            break;
        }
        default: {
            printf("unknown instruction: %d\n", op);
            return -1;
        }
        } // end of switch/case
    }
    return 0;
}

int main(int argc, char** argv)
{
    int i, fd;
    int64_t* tmp;

    argc--;
    argv++;

    poolsize = 256 * 1024;
    line = 1;

    // allocate memory for virtual
    if (!(text = old_text = malloc(poolsize))) {
        printf("could not malloc(%d) for text area\n", poolsize);
        return -1;
    }
    if (!(data = malloc(poolsize))) {
        printf("could not malloc(%d) for data area\n", poolsize);
        return -1;
    }
    if (!(stack = malloc(poolsize))) {
        printf("could not malloc(%d) for stack area\n", poolsize);
        return -1;
    }
    if (!(symbols = malloc(poolsize))) {
        printf("could not malloc(%d) for symbol table\n", poolsize);
        return -1;
    }

    memset(text, 0, poolsize);
    memset(data, 0, poolsize);
    memset(stack, 0, poolsize);
    memset(symbols, 0, poolsize);

    // point to stack base/bottom
    bp = sp = (int64_t*)((char*)stack + poolsize);
    ax = 0;

    src = "char else enum if int return sizeof while "
        "open read close printf malloc memset memcmp exit void main";
    // add keywords to symbol table
    i = Char;
    while (i <= While) {
        next();
        current_id[Token] = i++;
    }
    // add library to symbol table
    i = OPEN;
    while (i <= EXIT) {
        next();
        current_id[Class] = Sys;
        current_id[Type] = INT;
        current_id[Value] = i++;
    }
    next(); current_id[Token] = Char;  // handle void type
    next(); idmain = current_id;  // keep track of main

    if ((fd = open(*argv, 0)) < 0) {
        printf("could not open(%s)\n", *argv);
        return -1;
    }
    if (!(src = old_src = malloc(poolsize))) {
        printf("could not malloc(%d) for source area\n", poolsize);
        return -1;
    }
    // read the source file
    if ((i = read(fd, src, poolsize - 1)) <= 0) {
        printf("read() returned %d\n", i);
        return -1;
    }

    src[i] = 0;  // add EOF character
    close(fd);

    program();

    // 设置程序启动运行的函数是main函数
    // 之后手动调用main，放入2个参数到栈中，设置返回IP跳转地址
    if (!(pc = (int64_t*)idmain[Value])) {
        printf("main() not defined\n");
        return -1;
    }
    // setup stack
    sp = (int64_t*)((int64_t)stack + poolsize);
    *--sp = EXIT;  // call exit if main returns
    *--sp = PUSH;
    tmp = sp;
    // push main函数的第一个参数
    *--sp = argc;
    // push main函数的第二个参数
    *--sp = (int64_t)argv;
    // push main函数返回后的跳转地址IP
    // 这个跳转地址就是之前的stack的位置，那里面存有PUSH op
    //  EXIT
    //  PUSH  <----- 跳转到这里
    // 注意这里是一个栈
    // PUSH指令将ax寄存器内容放入到--sp中
    //   EXIT
    //   PUSH  <------ IP执行这里
    //   (ax)  <----- return from 用户代码中的main，sp指向这里
    // 然后IP执行EXIT指令，将*sp返回，也就是(ax)
    *--sp = (int64_t)tmp;
    return eval();
}
