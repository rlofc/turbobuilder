%prefix "prebase"
%value "const char *"
%header {
#include "model.h"
struct t_parser {
    FILE* in;
    char* ns;
    struct entity* e;
    struct relation* r;
    struct field* f;
    struct translation *t;
    struct label *l;
    args_stack_element * head;
    int error;
    int col,line;
};
int get_character(struct t_parser* parser);


}
%source {

#include <stdio.h>
#include <stdbool.h>

#include "model.h"
#include "core/iterators.h"
#include "sds/sds.h"
#include "coastguard/coastguard.h"
#include <wchar.h>

#define PCC_GETCHAR(auxil) get_character(auxil)
#define FOREACH_HASHED(T, V, HM) \
    for ( T V = HM; V != NULL; V = V->hh.next )

int
get_character(struct t_parser* parser)
{
    static int col = 0;
    static int line = 0;
    unsigned int c = fgetwc(parser->in);
    if (c == '\n') {
        line++;
        col = 0;
    } else
        col++;
    parser->col = col;
    parser->line = line;
    if (c == WEOF)
        return -1;
    return c;
}

}

model <- _ e:compound {}

compound <- 
    e:application { $$ = e; } /
    e:entity { $$ = e; } /
    e:translation {$$ = e; }
    / '#' ( !EOL .)* EOL {}
    / !. { }
    / .* { printf("Syntax error: expecting entity or translation "
            "definition.\n");  exit(1);}

application <- l:primary _ '=' _ 'application' _ '{' application_defs  { 
        struct t_parser * parser = auxil;
        if (parser->error == 1) {
            printf("in application [%s]\n",l);
            exit(1);
        }
        $$ = l;
    }
application_defs <- 
    _ '}' {}
    / _ 'title' _ '=' _ '"' l:tidentifier '"' _ ';' _ translation_defs { 
        sdsfree(g_title);
        g_title = sdsnew(l);
        struct t_parser * parser = auxil;
    }

translation <- l:primary _ '=' _ 'translation' _ '{' translation_defs  { 
        struct t_parser * parser = auxil;
        if (parser->error == 1) {
            printf("in translation [%s]\n",l);
            exit(1);
        }
        parser->t->language = sdsnew(l);
        reg_translation(&g_translations, parser->t);
        parser->t = NULL;
        $$ = l;
    }

translation_defs <- 
    _ '}' {}
    / _ e:translation_term translation_defs { 
        struct t_parser * parser = auxil;
    }
    / !. { printf("unclosed trans block\n"); exit(1); }
    / .* { 
        struct t_parser * parser = auxil;
        parser->error = 1;
        printf("Syntax error: expecting a valid label term "); 
    }

translation_term <- _ nn:identifier _ '=' _ '"' asd:tidentifier '"' _ ';' {
        struct t_parser * parser = auxil;
        if (parser->t == NULL) parser->t = create_translation();
        if (parser->l == NULL) parser->l = create_label();
        parser->l->term = sdsnew(nn);
        parser->l->label = sdsnew(asd);
        reg_label(parser->t, parser->l);
        parser->l = NULL;
}

entity <- l:primary _ '=' _ 'entity' _ '{' entity_defs  { 
        struct t_parser * parser = auxil;
        if (parser->error == 1) {
            printf("in entity [%s]\n",l);
            exit(1);
        }
        if (parser->e == NULL) parser->e = create_entity();
        parser->e->name = sdsnew(l);
        reg_entity(&g_entities, parser->e);
        parser->e = NULL;
        $$ = l; 
    }


relation <- l:primary _ '=' _ 'relation' _ '{' relation_defs  { 
        struct t_parser * parser = auxil;
        parser->r->name = sdsnew(l);
        if (parser->e == NULL) parser->e = create_entity();
        reg_relation(parser->e, parser->r);
        parser->r = NULL;
        $$ = l; 
    }


relation_defs <- _ '}' {
        struct t_parser * parser = auxil;
        parser->r = create_relation();
    }
    / _ 'ref' _ ':' _ c:identifier '.' a:identifier _ ';' relation_defs { 
        struct t_parser * parser = auxil;
        parser->r->fk.eid = sdsnew(c);
        parser->r->fk.fid = sdsnew(a);
    }
    / _ 'orderdesc' _ ':' _ c:identifier '.' a:identifier _ ';' relation_defs { 
        struct t_parser * parser = auxil;
        parser->r->order.fpath.eid = sdsnew(c);
        parser->r->order.fpath.fid = sdsnew(a);
        parser->r->order.asc = false;
    }
    / _ 'orderasc' _ ':' _ c:identifier '.' a:identifier _ ';' relation_defs { 
        struct t_parser * parser = auxil;
        parser->r->order.fpath.eid = sdsnew(c);
        parser->r->order.fpath.fid = sdsnew(a);
        parser->r->order.asc = true;
    }


field <- l:primary _ '=' _ 'field' _ '{' field_defs { 
        struct t_parser * parser = auxil;
        if (parser->error == 1) {
            printf("in field [%s]\n",l);
        } else {
        parser->f->name = sdsnew(l);
        if (parser->e == NULL) parser->e = create_entity();
        reg_field(parser->e, parser->f);
        parser->f = NULL;
        //$$ = l; 
        }
    }


entity_defs <- _ '}' { 
        struct t_parser * parser = auxil;
    }
    / _ e:field _ entity_defs { 
        struct t_parser * parser = auxil;
    }
    / _ e:relation _ entity_defs { 
        struct t_parser * parser = auxil;
    }
    / .* {
        struct t_parser * parser = auxil;
        parser->error = 1;
        printf("Expecting a valid field or relation definition line "); 
    }


field_defs <- _ '}' {
        struct t_parser * parser = auxil;
        parser->f = create_field();
    }
    / _ 'listed' _ ':' _ b:identifier _ ';' field_defs {
        struct t_parser * parser = auxil;
        if (strcmp(b, "true") == 0) parser->f->listed = true;
    }
    / _ 'hidden' _ ':' _ b:identifier _ ';' field_defs {
        struct t_parser * parser = auxil;
        if (strcmp(b, "true") == 0) parser->f->hidden = true;
    }
    / _ 'bar' _ ':' _ b:identifier _ ';' field_defs {
        struct t_parser * parser = auxil;
        if (strcmp(b, "true") == 0) parser->f->bar = true;
    }
    / _ 'ref' _ ':' _ c:identifier '.' a:identifier _ ';' field_defs { 
        struct t_parser * parser = auxil;
        parser->f->type = REF;
        parser->f->ref.eid = sdsnew(c);
        parser->f->ref.fid = sdsnew(a);
    }
    / _ 'sortby' _ ':' _ c:identifier '.' a:identifier _ ';' field_defs { 
        struct t_parser * parser = auxil;
        parser->f->order.fpath.eid = sdsnew(c);
        parser->f->order.fpath.fid = sdsnew(a);
    }
    / _ 'filter' _ ':' _ v:filter _ ';' field_defs {
        struct t_parser * parser = auxil;
        args_stack_element * item;
        STACK_POP(parser->head, item);
        parser->f->filter = item->arg->atfunc;
        free(item->arg);
        free(item);
    }
    / _ 'value' _ ':' _ v:formula _ ';' field_defs { 
        struct t_parser * parser = auxil;
        args_stack_element * item;
        STACK_POP(parser->head, item);
        parser->f->autofunc = item->arg->atfunc;
        free(item->arg);
        free(item);
    }
    / _ 'format' _ ':' _ '"' v:tidentifier '"' _ ';' field_defs { 
        struct t_parser * parser = auxil;
        parser->f->format = sdsnew(v);
    }
    / _ e:identifier _ ':' _ v:identifier _ ';' field_defs { 
        struct t_parser * parser = auxil;
        if (strcmp(e, "type") == 0) {
            parser->f->type = UNKNOWN;
            if (strcmp(v, "text") == 0) parser->f->type = TEXT;
            if (strcmp(v, "integer") == 0) parser->f->type = INTEGER;
            if (strcmp(v, "real") == 0) parser->f->type = REAL;
            if (strcmp(v, "ref") == 0) parser->f->type = REF;
            if (strcmp(v, "date") == 0) parser->f->type = DATE;
            if (strcmp(v, "boolean") == 0) parser->f->type = BOOLEAN;
            if (strcmp(v, "auto") == 0) parser->f->type = AUTO;
            if (parser->f->type == UNKNOWN) {
                parser->error = 1;
                printf("Invalid field type '%s'\n", v); 
                exit(1);
            }
        }
        if (strcmp(e, "size") == 0) {
            parser->f->length = atoi(v);
        }
    }
    / .* {
        struct t_parser * parser = auxil;
        parser->error = 1;
        printf("Invalid field definition "); 
    }

filter <-  fname:identifier _ '(' _ formula_args _ ')' _ {
        struct t_parser * parser = auxil;
        int nargs = 0;
        if (strcmp(fname, "RefEq") == 0) nargs = 2;
        struct func * f = calloc(1, sizeof(struct func));
        f->name = sdsnew(fname);
        f->n_args = nargs;
        for (int i = nargs-1; i >=0 ; --i) {
            args_stack_element * item;
            STACK_POP(parser->head, item);
            f->args[i] = item->arg;
            free(item);
        }
        args_stack_element * item = calloc(1, sizeof(args_stack_element));
        item->arg = calloc(1, sizeof(struct arg));
        item->arg->type = ATFUNC;
        item->arg->atfunc = f;
        STACK_PUSH(parser->head, item);
    }

formula <-  fname:identifier _ '(' _ formula_args _ ')' _ { 
        struct t_parser * parser = auxil;
        int nargs = 0;
        if (strcmp(fname, "Get") == 0) nargs = 1;
        if (strcmp(fname, "Sum") == 0) nargs = 1;
        if (strcmp(fname, "Min") == 0) nargs = 1;
        if (strcmp(fname, "Max") == 0) nargs = 1;
        if (strcmp(fname, "Avg") == 0) nargs = 1;
        if (strcmp(fname, "RollingDaysAvg") == 0) nargs = 3;
        if (strcmp(fname, "RollingDaysSum") == 0) nargs = 3;
        if (strcmp(fname, "AvgIfEq") == 0) nargs = 3;
        if (strcmp(fname, "CountIfEq") == 0) nargs = 3;
        if (strcmp(fname, "Count") == 0) nargs = 1;
        if (strcmp(fname, "Sub") == 0) nargs = 2;
        if (strcmp(fname, "Mul") == 0) nargs = 2;
        if (strcmp(fname, "Div") == 0) nargs = 2;
        struct func * f = calloc(1, sizeof(struct func));
        f->name = sdsnew(fname);
        f->n_args = nargs;
        for (int i = nargs-1; i >=0 ; --i) {
            args_stack_element * item;
            STACK_POP(parser->head, item);
            f->args[i] = item->arg;
            free(item);
        }
        args_stack_element * item = calloc(1, sizeof(args_stack_element));
        item->arg = calloc(1, sizeof(struct arg));
        item->arg->type = ATFUNC;
        item->arg->atfunc = f;
        STACK_PUSH(parser->head, item);
    }

formula_args <-  _ formula_args _ (',' _ formula_args)*  _ {
    }
    / formula {
        struct t_parser * parser = auxil;
    }
    / _ a:identifier '.' b:identifier _ {
        struct t_parser * parser = auxil;
        args_stack_element * item = calloc(1, sizeof(args_stack_element));
        item->arg = calloc(1, sizeof(struct arg));
        item->arg->type=ATREF;
        item->arg->atfield = sdsnew(b);
        item->arg->atentity = sdsnew(a);
        STACK_PUSH(parser->head, item);
    }
    / _ a:identifier _ {
        struct t_parser * parser = auxil;
        args_stack_element * item = calloc(1, sizeof(args_stack_element));
        item->arg = calloc(1, sizeof(struct arg));
        item->arg->type=ATFIELD;
        item->arg->atfield = sdsnew(a);
        STACK_PUSH(parser->head, item);
    }


identifier <- < [a-zA-Z0-9]* >?               { $$ = $1; }
tidentifier <- < [\u00E4a-zA-Z0-9%\. ]* >?               { $$ = $1; }
primary <- < [a-zA-Z0-9]+ >               { $$ = $1; }
_      <- [ \t\n]*
EOL    <- '\n' / '\r\n' / '\r' / ';'

%%


