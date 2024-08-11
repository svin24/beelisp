#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "ext/mpc.h"
#include <editline/readline.h>
#include <string.h>

typedef struct lval {
    int type;
    long num;

    char* err;
    char* sym;

    int count;
    struct lval** cell;
} lval;

// possible error types
//enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };
// possible lval values
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR};

lval* lval_num(long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char* m){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;
    v->err = malloc(strlen(m) + 1);
    strcpy(v->err,m);
    return v;
}

lval* lval_sym(char* s){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(s) + 1);
    strcpy(v->sym,s);
    return v;
}

lval* lval_sexpr(void){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

void lval_print(lval* v);

void lval_expr_print(lval* v, char open, char close){
    putchar(open);
    for (int i = 0; i < v->count; i++){
        lval_print(v->cell[i]);

        if(i != (v->count -1)){
            putchar(' ');
        }
    }
    putchar(close);
}

// Print stuff
void lval_print(lval* v){
    switch (v->type){
        case LVAL_NUM:printf("%li",v->num); break;
        case LVAL_ERR:printf("Error: %s", v->err); break;
        case LVAL_SYM:printf("%s", v->sym); break;
        case LVAL_SEXPR: lval_expr_print(v,'(',')'); break;
    }
}

void lval_println(lval* v){ lval_print(v); putchar('\n');};

//cleanup function
void lval_del(lval* v){
    switch(v->type){
        case LVAL_NUM: break;

        case LVAL_ERR: free(v->err); break;
        case LVAL_SYM: free(v->sym); break;

        case LVAL_SEXPR:
            for(int i=0; i < v->count; i++){
                lval_del(v->cell[i]);
            }
            free(v->cell);
            break;
    }
    free(v);
}

lval* lval_read_num(mpc_ast_t* t){
    errno = 0;
    long x =strtol(t->contents, NULL, 10);
    if(errno != ERANGE)
        return lval_num(x);
    else
        return lval_err("invalid number");
}

lval* lval_add(lval* v, lval* x){
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count-1] = x;
    return v;
}

lval* lval_read(mpc_ast_t* t){
    if (strstr(t->tag,"number")) return lval_read_num(t);
    if (strstr(t->tag,"symbol")) return lval_sym(t->contents);

    lval* x = NULL;
    if (strcmp(t->tag, ">") == 0) x = lval_sexpr();
    if (strstr(t->tag, "sexpr"))  x = lval_sexpr(); 

    for(int i; i < t->children_num; i++){
        if (strcmp(t->children[i]->contents, "(") == 0) continue;
        if (strcmp(t->children[i]->contents, ")") == 0) continue;
        if (strcmp(t->children[i]->tag, "regex") == 0) continue;
        x = lval_add(x, lval_read(t->children[i]));
    }

    return x;
}

// EVAL STUFF
lval* lval_eval_sexpr(lval* v);

lval* lval_eval(lval* v){
    if (v->type == LVAL_SEXPR) return lval_eval_sexpr(v);
    return v;
}

lval* lval_pop(lval* v,int i){
    lval* x = v->cell[i];
    
    memmove(&v->cell[i],
            &v->cell[i+1],
            sizeof(lval*) * (v->count-i-1));

    v->count--;

    v->cell = realloc(v->cell,sizeof(lval*) * v->count);
    return x;
}

lval* lval_take(lval* v, int i){
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

lval* builtin_op(lval* v, char* op){
    for(int i = 0; i < v->count; i++){
        if (v->cell[i]->type != LVAL_NUM){
            lval_del(v);
            return lval_err("Can't operate on non number");
        }
    }

    lval* x = lval_pop(v, 0);

    // if no arguments -> unary negation
    if ((strcmp(op,"-") == 0) && v->count == 0) x->num = -x->num;

    while (v->count > 0) {
        lval* y = lval_pop(v, 0);
        
        if (strcmp(op, "+") == 0) x->num += y->num;
        if (strcmp(op, "-") == 0) x->num -= y->num;
        if (strcmp(op, "*") == 0) x->num *= y->num; 
        if (strcmp(op, "/") == 0) {
            if(y->num == 0){
                lval_del(x);
                lval_del(y);
                x = lval_err("Division by zero");
                break;
            }
            x->num /= y->num;
        }
        lval_del(y);
    }

    lval_del(v);
    return x;
}

lval* lval_eval_sexpr(lval* v) {

    // children evalutation
    for (int i = 0; i < v->count; i++){
        v->cell[i] = lval_eval(v->cell[i]);
    }

    for (int i = 0; i < v->count; i++){
        if (v->cell[i]->type == LVAL_ERR) return lval_take(v,i);
    }

    // empty and single expression
    if (v->count == 0) return v;
    if (v->count == 1) return lval_take(v,0);

    // First element is symbol
    lval* f = lval_pop(v,0);
    if(f->type != LVAL_SYM){
        lval_del(f);
        lval_del(v);
        return lval_err("S-Expression does not start with Symbol!");
    }

    lval* result = builtin_op(v, f->sym);
    lval_del(f);
    return result;
}

int main(int argc, char **argv){
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Lisp = mpc_new("lisp");

    mpca_lang(MPCA_LANG_DEFAULT,
        "                                        \
        number : /-?[0-9]+/ ;                    \
        symbol : '+' | '-' | '*' | '/' ;         \
        sexpr  : '(' <expr>* ')' ;               \
        expr   : <number> | <symbol> | <sexpr> ; \
        lisp   : /^/ <expr>* /$/ ;               \
        ",        
        Number,Symbol,Sexpr,Expr,Lisp);

    puts("Lisp by svin");

    while (1){
        char* input = readline("bee> ");
        add_history(input);

        mpc_result_t r;
        if (mpc_parse("<stdin>",input, Lisp, &r)){
            lval* result = lval_eval(lval_read(r.output));
            lval_println(result);
            lval_del(result);
            //mpc_ast_print(r.output);
            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        free(input);
    }
    mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Lisp);
    return 0;
}
