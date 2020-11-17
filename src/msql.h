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
#ifndef _TURBOBUILDER_MSQL_H_
#define _TURBOBUILDER_MSQL_H_

#include "coastguard/coastguard.h"
#include "sds/sds.h"
#include "sqlite/sqlite3.h"

#include "model.h"

$typedef(sds) wrapped_sql;

struct field_value
{
    struct field* base;
    union
    {
        const unsigned char* _c_init_value;
        unsigned char*       _init_value;
    };
    char*          _ret_value;
    void*          _data;
    int            _kvalue;
    bool           is_archived;
    bool           is_valid;
    UT_hash_handle hh;
};

struct entity_value
{
    struct entity*      base;
    struct field_value* fields;
};

struct lookup_filter_data
{
    int                  k;
    struct field_value*  fv;
    struct entity_value* ev;
    sqlite3*             db;
};

struct context
{
    char* fname;
    int   k;
};

$status
create_tables_from_model(sqlite3* db);

sds
get_ref_value(sqlite3* db, int key, const char* ename, const char* efield);

wrapped_sql
build_list_query(struct entity* e, sqlite3* db, struct context* ctx, struct lookup_filter_data * ldf,struct order* order);

sds
field_value_to_string(struct field* f, sqlite3_stmt* res, int index);

sds
build_obj_query(struct entity* e);

void
init_fields(struct entity_value* e, sqlite3* db, int key);

$typedef(int) wrapped_key;

wrapped_key
apply_form(struct entity_value* e, sqlite3* db, int key);

wrapped_key
archive_obj(struct entity* e, sqlite3* db, int key);

#endif
