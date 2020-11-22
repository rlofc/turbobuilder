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
#ifndef _TURBOBUILDER_MODEL_H_
#define _TURBOBUILDER_MODEL_H_

#include <stdbool.h>
#include <stdio.h>

#include "ut/uthash.h"
#include "ut/utstack.h"

static const int MAX_ID = 32 + 1;

/* -- FIELD TYPES -- */

typedef enum
{
    UNKNOWN,
    TEXT,
    DATE,
    INTEGER,
    BOOLEAN,
    REAL,
    REF,
    AUTO
} ftype;

static const char* FTYPES[] = { "UNKNOWN", "TEXT",    "INTEGER", "INTEGER",
                                "INTEGER", "REAL",    "INTEGER", "*******" };

struct fpath
{
    char* eid;
    char* fid;
};

struct order
{
    struct fpath fpath;
    bool asc;
};

struct arg;

#define MAX_ARGS 10
struct func
{
    char*       name;
    int         n_args;
    struct arg* args[MAX_ARGS];
};

struct arg
{
    enum t
    {
        ATFUNC,
        ATFIELD,
        ATREF
    } type;
    struct func* atfunc;
    char*        atfield;
    char*        atentity;
};

typedef struct args_stack_element
{
    struct arg*                arg;
    struct args_stack_element* next;
} args_stack_element;

struct field
{
    UT_hash_handle hh;

    char*        name;
    ftype        type;
    int          length;
    struct fpath ref;
    struct order order;
    char*        format;
    bool         listed;
    bool         hidden;
    struct func* filter;
    struct func* autofunc;
    struct func* autocond;
};

struct relation
{
    char*          name;
    struct fpath   fk;
    struct order   order;
    UT_hash_handle hh;
};

struct entity
{
    UT_hash_handle hh;

    char*            name;
    struct field*    fields;
    struct relation* relations;
} entity;

struct label
{
    UT_hash_handle hh;

    char* term;
    char* label;
};

struct translation
{
    UT_hash_handle hh;

    char*         language;
    struct label* labels;
};

/* -- ENTITY HELPERS -- */

struct entity*
create_entity();
void
reg_entity(struct entity** es, struct entity* e);
int
find_entity(const struct entity* es, const char* name, struct entity** e);
void
cleanup_entities(struct entity* es);

/* -- FIELD HELPERS -- */

struct field*
create_field();
void
reg_field(struct entity* e, struct field* f);
int
find_field(const struct field* rs, const char* name, struct field** r);

/* -- RELATION HELPERS -- */

struct relation*
create_relation();
void
reg_relation(struct entity* e, struct relation* f);
int
find_relation(const struct relation* rs, const char* name, struct relation** r);

/* -- TRANSLATION HELPERS -- */

struct translation*
create_translation();
void
reg_translation(struct translation** et, struct translation* t);

/* -- LABEL HELPERS -- */

struct label*
create_label();
void
reg_label(struct translation* e, struct label* l);
const char*
translate(const char*);
void
cleanup_translations(struct translation* ts);
#define _TR translate

/* -- EXTERN STORAGE -- */

extern struct entity*      g_entities;
extern struct translation* g_translations;
extern char*               g_title;

#endif
