/*
 * TURBOBUILDER
 * Copyright (C) 2020 Ithai Levi
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of  MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <newt.h>
#include <stdbool.h>
#include <stdio.h>

#include "core/iterators.h"
#include "sds/sds.h"

#include "model.h"

struct entity*
create_entity()
{
    struct entity* wrapper = calloc(1, sizeof(struct entity));
    return wrapper;
}

struct field*
create_field()
{
    struct field* wrapper = calloc(1, sizeof(struct field));
    return wrapper;
}

struct relation*
create_relation()
{
    struct relation* wrapper = calloc(1, sizeof(struct relation));
    return wrapper;
}

struct translation*
create_translation()
{
    struct translation* wrapper = calloc(1, sizeof(struct translation));
    return wrapper;
}

struct label*
create_label()
{
    struct label* wrapper = calloc(1, sizeof(struct label));
    return wrapper;
}

void
reg_entity(struct entity** es, struct entity* e)
{
    HASH_ADD_STR(*es, name, e);
}

void
reg_field(struct entity* e, struct field* f)
{
    HASH_ADD_STR(e->fields, name, f);
}

void
reg_relation(struct entity* e, struct relation* r)
{
    HASH_ADD_STR(e->relations, name, r);
}
void
reg_translation(struct translation** et, struct translation* t)
{
    HASH_ADD_STR(*et, language, t);
}

void
reg_label(struct translation* e, struct label* l)
{
    HASH_ADD_STR(e->labels, term, l);
}

const char*
_TR(const char* term)
{
    struct translation* t;
    HASH_FIND_STR(g_translations, "English", t);
    if (t == NULL) return term;
    struct label* wrapper;
    HASH_FIND_STR(t->labels, term, wrapper);
    if (wrapper == NULL) return term;
    return wrapper->label;
}

int
find_relation(const struct relation* rs, const char* name, struct relation** r)
{
    struct relation* wrapper;
    HASH_FIND_STR(rs, name, wrapper);
    *r = wrapper != NULL ? wrapper : NULL;
    if (wrapper == NULL) printf("%s\n", "wrapper");
    return 0;
}

int
find_field(const struct field* rs, const char* name, struct field** r)
{
    struct field* wrapper;
    HASH_FIND_STR(rs, name, wrapper);
    *r = wrapper != NULL ? wrapper : NULL;
    if (wrapper == NULL) printf("%s\n", "wrapper");
    return 0;
}

int
find_entity(const struct entity* es, const char* name, struct entity** e)
{
    struct entity* wrapper;
    HASH_FIND_STR(es, name, wrapper);
    *e = wrapper != NULL ? wrapper : NULL;
    if (wrapper == NULL) printf("%s\n", "wrapper");
    return 0;
}

/* -- MEMORY CLEANUP -- */

void
cleanup_relation(struct relation* r)
{
    sdsfree(r->name);
    sdsfree(r->fk.eid);
    sdsfree(r->fk.fid);
    sdsfree(r->order.eid);
    sdsfree(r->order.fid);
}

void
cleanup_func(struct func* f)
{
    if (f == NULL) return;
    for (int i = 0; i < f->n_args; i++) {
        struct arg* a = f->args[i];
        if (a->type == ATREF) {
            sdsfree(a->atfield);
            sdsfree(a->atentity);
        }
        if (a->type == ATFIELD) {
            sdsfree(a->atfield);
        }
        if (a->atfunc != NULL) {
            cleanup_func(a->atfunc);
        }
        free(a);
    }
    sdsfree(f->name);
    free(f);
}

void
cleanup_field(struct field* f)
{
    sdsfree(f->name);
    if (f->type == REF) {
        sdsfree(f->ref.eid);
        sdsfree(f->ref.fid);
        sdsfree(f->order.eid);
        sdsfree(f->order.fid);
    }
    sdsfree(f->format);
    if (f->type == REAL) {
    }
    if (f->type == AUTO) {
        cleanup_func(f->autofunc);
        cleanup_func(f->autocond);
    }
}

void
cleanup_entities(struct entity* es)
{
    struct entity *e, *tmp_e;
    HASH_ITER(hh, es, e, tmp_e)
    {
        struct relation *r, *tmp_r;
        HASH_ITER(hh, e->relations, r, tmp_r)
        {
            cleanup_relation(r);
            HASH_DEL(e->relations, r);
            free(r);
        }
        struct field *f, *tmp_f;
        HASH_ITER(hh, e->fields, f, tmp_f)
        {
            cleanup_field(f);
            HASH_DEL(e->fields, f);
            free(f);
        }
        sdsfree(e->name);
        HASH_DEL(es, e);
        free(e);
    }
}

void
cleanup_translations(struct translation* ts)
{
    struct translation *t, *tmp_t;
    HASH_ITER(hh, ts, t, tmp_t)
    {
        struct label *l, *tmp_l;
        HASH_ITER(hh, t->labels, l, tmp_l)
        {
            sdsfree(l->term);
            sdsfree(l->label);
            HASH_DEL(t->labels, l);
            free(l);
        }
        sdsfree(t->language);
        HASH_DEL(ts, t);
        free(t);
    }
}
