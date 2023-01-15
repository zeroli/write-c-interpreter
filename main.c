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

void next()
{
    token = *src++;
    return;
}

void expression(int level)
{
    // do nothing
}

void program()
{
    next();  // get next token
    while (token > 0) {
        printf("token is: %s\n", token);
        next();
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
    memset(text, 0, poolsize);
    memset(data, 0, poolsize);
    memset(stack, 0, poolsize);

    // point to stack base/bottom
    bp = sp = (int*)((char*)stack + poolsize);
    ax = 0;

    // test code: 10 + 20
    i = 0;
    text[i++] = IMM;
    text[i++] = 10;
    text[i++] = PUSH;
    text[i++] = IMM;
    text[i++] = 20;
    text[i++] = ADD;
    text[i++] = PUSH;
    text[i++] = EXIT;
    pc = text;

    program();

    return eval();
}
