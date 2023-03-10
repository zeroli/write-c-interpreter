#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>

int token;  // current token
char* src, *old_src;  // pointer to source code string;
int poolsize;  // default size of text/data/stack
int line;  // line number

int* text,  // text segment
    * old_text, // for dump text segment
    * stack;  // stack
char* data;  // data segment

int* pc, *bp, *sp, ax, cycle;  // virtual machine registers

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

int token_val;  // value of current token (mainly for number)
int* current_id,  // current parsed ID
    * symbols;  // symbol table

// ???????????????struct???????????????????????????????????????struct
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
int* idmain;  // the `main` function
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

void next()
{
    char* last_pos;
    int hash;

    while (token = *src) {
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
                token_val = (int)last_pos;
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
            current_id[Name] = (int)last_pos;
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

void enum_declaration()
{
    // parse:
    // enum [id] { a [= 1] [, b [= 3] ]* }
    int i;
    i = 0;
    // ??????????????? {, ???'a'??????
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
    // ????????????????????????????????????????????????????????????????????????
    // ???????????????????????????????????????
    while (token != ';' && token != '}') {
        // ???????????????
        // ??????????????????????????? int a;
        // ????????????????????????????????????int foo(); int foo(...) { }
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
        // identifier???????????????????????????????????????
        match(Id);
        current_id[Type] = type;

        // ??????'('?????????????????????????????????
        if (token == '(') {
            current_id[Class] = Fun;
            current_id[Value] = (int)(text + 1);  // the memory address
            function_declaration();
        } else {  // ????????????????????????????????????
            // variable declaration
            current_id[Class] = Glo;  // global variable
            current_id[Value] = (int)data;  // assign memory address
            data = data + sizeof(int);
        }

        if (token == ',') {
            match(',');
        }
    }
    next();
}

void expression(int level)
{
    // do nothing
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
    int op, *tmp;
    while (1) {
        // IMM (<-- pc)
        if (op == IMM) { ax = *pc++; }
        // LC (loc <-- ax)
        else if (op == LC) { ax = *(char*)ax; }
        // LI (loc <-- ax)
        else if (op == LI) { ax = *(int*)ax; }
        // SC (loc <-- sp)
        else if (op == SC) { *(char*)*sp++ = ax; }
        // SI (loc <-- sp)
        else if (op == SI) { *(int*)*sp++ = ax; }
        // PUSH
        else if (op == PUSH) { *--sp = ax; }
        // JMP <addr> (<-- pc)
        else if (op == JMP) { pc = (int*)*pc; }
        // JZ <addr> (<-- pc)
        else if (op == JZ) { pc = ax ? (pc + 1) : (int*)*pc; }
        // JNZ <addr> (<-- pc)
        else if (op == JNZ) { pc = ax ? (int*)*pc : (pc + 1); }
        // CALL <addr> (<-- pc)
        else if (op == CALL) { *--sp = (int)(pc + 1); pc = (int*)*pc; }
        // ENT <num of int> (<-- pc)
        else if (op == ENT) { *--sp = (int)bp; bp = sp; sp = sp - *pc++; }
        // ADJ <num of int> (<-- pc)
        else if (op == ADJ) { sp = sp + *pc++; }
        // LEV
        else if (op == LEV) { sp = bp; bp = (int*)*sp++; pc = (int*)*sp++; }
        // LEA <n-th argument> in callee
        // arg: 1  ===> new_bp + 4
        // arg: 2  ===> new_bp + 3
        // arg: 3  ===> new_bp + 2
        // return address => new_bp + 1
        // old_bp ===> new_bp
        // local var 1 ===> new_bp - 1
        // local var 2 ===> new_bp - 2
        else if (op == LEA) { ax = (int)(bp + *pc++); }
        // arithmetic operations
        // ax := (sp OP ax), sp++
        else if (op == OR) { ax = *sp++ | ax; }
        else if (op == XOR) { ax = *sp++ ^ ax; }
        else if (op == AND) { ax = *sp++ & ax; }
        else if (op == EQ) { ax = *sp++ == ax; }
        else if (op == NE) { ax = *sp++ != ax; }
        else if (op == LT) { ax = *sp++ < ax; }
        else if (op == LE) { ax = *sp++ <= ax; }
        else if (op == GT) { ax = *sp++ > ax; }
        else if (op == GE) { ax = *sp++ >= ax; }
        else if (op == SHL) { ax = *sp++ << ax; }
        else if (op == SHR) { ax = *sp++ >> ax; }
        else if (op == ADD) { ax = *sp++ + ax; }
        else if (op == SUB) { ax = *sp++ - ax; }
        else if (op == MUL) { ax = *sp++ * ax; }
        else if (op == DIV) { ax = *sp++ / ax; }
        else if (op == MOD) { ax = *sp++ % ax; }

        // helper operations
        else if (op == EXIT) { printf("exit(%d)", *sp); return *sp; }
        else if (op == OPEN) { ax = open((char*)sp[1], sp[0]); }
        else if (op == CLOS) { ax = close(*sp); }
        else if (op == READ) { ax = read(sp[2], (char*)sp[1], *sp); }
        else if (op == PRTF) {
            tmp = sp + pc[1];
            ax = printf((char*)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]);
        }
        else if (op == MALC) { ax = (int)malloc(*sp); }
        else if (op == MSET) { ax = (int)memset((char*)sp[2], sp[1], *sp); }
        else if (op == MCMP) { ax = memcmp((char*)sp[2], (char*)sp[1], *sp); }
        else {
            printf("unknown instruction: %d\n", op);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    int i, fd;
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
    bp = sp = (int*)((char*)stack + poolsize);
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

    return eval();
}
