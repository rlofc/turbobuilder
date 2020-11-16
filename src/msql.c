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
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>

#include "coastguard/coastguard.h"
#include "core/iterators.h"
#include "sds/sds.h"

#include "model.h"
#include "rdsl.h"

#include "msql.h"

struct query_extensions
{
    sds select;
    sds from;
    sds join;
    sds where;
};
$typedef(struct query_extensions) wrapped_qe;

wrapped_qe
augment_entity_query_inner(struct entity*          p,
                           struct relation*        pr,
                           struct entity*          e,
                           struct field*           ff,
                           struct func*            f,
                           struct query_extensions pqe);

/* -- DATABASE INITIALIZATION -- */

wrapped_sql
new_entity_columns(struct entity* e)
{
    sds sql;
    $check(sql = sdsempty(), error);
    $check(e, error);
    $foreach_hashed(struct field*, f, e->fields)
    {
        if (f->type == AUTO) break;
        sql = sdscatprintf(sql, ", [%s] %s", f->name, FTYPES[f->type]);
        $check(sql, error);
    }
    return (wrapped_sql){ sql };
error:
    if (sql != NULL) sdsfree(sql);
    return $invalid(wrapped_sql);
}

wrapped_sql
new_entity_table(struct entity* e)
{
    sds sql;
    $check(sql = sdsempty());
    $check(e);
    const char* template =
      "CREATE TABLE [%ss](Id INTEGER PRIMARY KEY, _archived INTEGER %s);";
    wrapped_sql columns = new_entity_columns(e);
    $inspect(columns, error);
    $check(sql = sdscatprintf(sql, template, e->name, columns.v));
    sdsfree(columns.v);
    return (wrapped_sql){ sql };
error:
    $log_error("error trying to create a new entity table");
    if (sql != NULL) sdsfree(sql);
    return $invalid(wrapped_sql);
}

$status
create_tables_from_model(sqlite3* db)
{
    char* err_msg = 0;
    $foreach_hashed(struct entity*, e, g_entities)
    {
        wrapped_sql sql = new_entity_table(e);
        $inspect(sql, error);
        if (sqlite3_exec(db, sql.v, 0, 0, &err_msg) != SQLITE_OK) {
            $log_debug("%s", sql.v);
            $log_error(
              "Failed to create table for entity [%s]: %s", e->name, err_msg);
            sqlite3_free(err_msg);
        } else {
            $log_info("Tables created successfully");
        }
        sdsfree(sql.v);
    }
    return $okay;
error:
    return $error("failure creating model tables");
}

wrapped_sql
build_entity_query_joins(struct entity* e)
{
    sds join;
    $check(join = sdsempty());
    $foreach_hashed(struct field*, f, e->fields)
    {
        struct entity* next_entity = e;
        struct field*  next_field  = f;
        while (next_field->type == REF) {
            $check(join =
                     sdscatprintf(join,
                                  " INNER JOIN [%ss] ON [%ss].Id = [%ss].[%s]",
                                  next_field->ref.eid,
                                  next_field->ref.eid,
                                  next_entity->name,
                                  next_field->name));
            $check(find_entity(g_entities, next_field->ref.eid, &next_entity) ==
                   0);
            $check(find_field(next_entity->fields,
                              next_field->ref.fid,
                              &next_field) == 0);
        }
    }
    return (wrapped_sql){ join };
error:
    if (join != NULL) sdsfree(join);
    return $invalid(wrapped_sql);
}
sds
replace_string_in_sds(sds source, sds old, sds new)
{
    sds  newjoin = sdsempty();
    sds* tokens;
    int  count, j;
    tokens  = sdssplitlen(source, sdslen(source), old, sdslen(old), &count);
    newjoin = sdscatprintf(newjoin, "%s", tokens[0]);
    for (j = 1; j < count; j++) {
        newjoin = sdscatprintf(newjoin, "[%s]%s", new, tokens[j]);
    }
    sdsfreesplitres(tokens, count);
    sdsfree(old);
    sdsfree(new);
    sdsfree(source);
    return newjoin;
}

wrapped_qe
augment_entity_query_agg(struct entity*          p,
                         struct relation*        pr,
                         struct entity*          e,
                         struct field*           ff,
                         struct func*            f,
                         const char*             agg,
                         struct query_extensions pqe)
{
    sds select = sdsempty();
    sds from   = sdsempty();
    sds gb     = sdscatprintf(sdsempty(), "GROUP BY [%ss].[Id]", e->name);
    const char* template = "(SELECT %s(%s) %s %s FROM %s %s %s %s %s) %s ";
    if (f->args[0]->type == ATREF) {
        struct relation* r;
        struct entity*   r_entity;
        struct field*    r_field;
        $check(find_relation(e->relations, f->args[0]->atentity, &r) == 0);
        $check(find_entity(g_entities, r->fk.eid, &r_entity) == 0);
        $check(find_field(r_entity->fields, f->args[0]->atfield, &r_field) ==
               0);
        sds uuid = sdscatprintf(
          sdsempty(), "uuid%s%s%s", r_field->name, e->name, ff->name);
        $check(select = sdscatprintf(select, "%s.%s", uuid, uuid));

        wrapped_sql join         = build_entity_query_joins(r_entity);
        sds         more_selects = sdsempty();
        $foreach_hashed(struct field*, f, e->fields)
        {
            if (f->type == REF) {
                $check(
                  more_selects = sdscatprintf(
                    more_selects, ", [%ss].%s %s", e->name, f->name, f->name));
            }
        }
        sds tmp = sdscatprintf(sdsempty(),
                               "INNER JOIN [%ss] ON [%ss].[%s]=[%ss].[Id] ",
                               e->name,
                               r->fk.eid,
                               r->fk.fid,
                               e->name);
        if (p == NULL) {
            tmp = sdscatprintf(tmp, "WHERE [%ss].Id=@id", e->name);
        }

        sds tmpjoins = sdscatprintf(sdsempty(), "%s %s", tmp, pqe.join);
        if (r_field->type == AUTO) {
            wrapped_qe a;
            a = augment_entity_query_inner(
              e,
              r,
              r_entity,
              r_field,
              r_field->autofunc,
              (struct query_extensions){ .where = pqe.where,
                                         .join  = tmpjoins });
            if (strcmp(a.v.from, "") == 0)
                a.v.from = sdscatprintf(a.v.from, "[%ss]", r->fk.eid);
            sds where = sdsempty();
            if (strstr(a.v.from, "SELECT") != NULL) {
                sds searchstr =
                  sdscatprintf(sdsempty(), "[%ss]", r_entity->name);
                sds otheruuid =
                  sdscatprintf(sdsempty(),
                               "uuid%s%s%s",
                               r_field->autofunc->args[0]->atfield,
                               r_entity->name,
                               r_field->name);
                join.v = replace_string_in_sds(join.v, searchstr, otheruuid);
            } else {
                if (p == NULL) {
                    pqe.join =
                      sdscatprintf(pqe.join, " WHERE [%ss].Id=@id", e->name);
                }
                pqe.join = sdscatprintf(
                  pqe.join, " AND ([%ss]._archived IS NULL)", r_entity->name);
            }
            $check(from = sdscatprintf(from,
                                       template,
                                       agg,
                                       a.v.select,
                                       uuid,
                                       more_selects,
                                       a.v.from,
                                       join.v,
                                       pqe.join,
                                       r_field->type != AUTO ? pqe.where : "",
                                       p != NULL ? gb : "",
                                       uuid));
        } else {
            sds sum = sdsempty();
            $check(sum = sdscatprintf(
                     sum, "[%ss].%s", r->fk.eid, f->args[0]->atfield));
            sds fr = sdscatprintf(sdsempty(), "%ss", r->fk.eid);
            $check(from = sdscatprintf(from,
                                       template,
                                       agg,
                                       sum,
                                       uuid,
                                       more_selects,
                                       fr,
                                       tmpjoins,
                                       "",
                                       pqe.where,
                                       "",
                                       uuid));
            sdsfree(sum);
            sdsfree(fr);
        }
        sdsfree(tmp);
        sdsfree(more_selects);
        sdsfree(tmpjoins);
    }
    sdsfree(gb);
    return (wrapped_qe){ (struct query_extensions){ select, from } };
error:
    sdsfree(gb);
    sdsfree(from);
    sdsfree(select);
    return $invalid(wrapped_qe);
}

/* -- FORMULAS (AUTOMATIC FIELDS) -- */

wrapped_qe
augment_entity_query_nocond_agg(struct entity*          p,
                                struct relation*        pr,
                                struct entity*          e,
                                struct field*           ff,
                                struct func*            f,
                                const char*             agg,
                                struct query_extensions pqe)
{
    wrapped_qe qe = augment_entity_query_agg(p, pr, e, ff, f, agg, pqe);
    $inspect(qe);
error:
    return qe;
}

wrapped_qe
augment_entity_query_if_cond_agg(struct entity*          p,
                                 struct relation*        pr,
                                 struct entity*          e,
                                 struct field*           ff,
                                 struct func*            f,
                                 const char*             agg,
                                 const char*             op,
                                 struct query_extensions pqe)
{
    const char* template = " AND [%ss].%s %s '%s'";
    sds where_addition   = sdscatprintf(sdsempty(),
                                      template,
                                      f->args[1]->atentity,
                                      f->args[1]->atfield,
                                      op,
                                      f->args[2]->atfield);

    pqe.where     = where_addition;
    wrapped_qe qe = augment_entity_query_agg(p, pr, e, ff, f, agg, pqe);
    $inspect(qe);
error:
    return qe;
}

wrapped_qe
augment_entity_query_date_cond_agg(struct entity*          p,
                                   struct relation*        pr,
                                   struct entity*          e,
                                   struct field*           ff,
                                   struct func*            f,
                                   const char*             agg,
                                   const char*             op,
                                   const char*             tdelta_template,
                                   struct query_extensions pqe)
{
    const char* template = " AND DATE([%s].%s,'unixepoch')%sDATE('now','%s')";
    sds tdelta = sdscatprintf(sdsempty(), tdelta_template, f->args[2]->atfield);
    sds where_addition = sdscatprintf(sdsempty(),
                                      template,
                                      f->args[1]->atentity,
                                      f->args[1]->atfield,
                                      op,
                                      tdelta);
    pqe.where          = where_addition;
    wrapped_qe qe      = augment_entity_query_agg(p, pr, e, ff, f, agg, pqe);
    $inspect(qe);
error:
    return qe;
}

wrapped_qe
augment_entity_query_op_arg(struct arg*             arg,
                            struct entity*          p,
                            struct relation*        pr,
                            struct entity*          e,
                            struct field*           ff,
                            struct query_extensions pqe)
{
    sds select = sdsempty();
    sds from   = sdsempty();
    if (arg->type == ATFUNC) {
        wrapped_qe t =
          augment_entity_query_inner(p, pr, e, ff, arg->atfunc, pqe);
        $check(select = sdscatprintf(select, "%s", t.v.select));
        $check(from = sdscatprintf(from, "%s", t.v.from));
    }
    if (arg->type == ATFIELD) {
        struct field* of;
        $check(find_field(e->fields, arg->atfield, &of) == 0);
        if (of->type == AUTO) {
            wrapped_qe t =
              augment_entity_query_inner(p, pr, e, of, of->autofunc, pqe);
            $check(select = sdscatprintf(select, "%s", t.v.select));
        } else {
            $check(select =
                     sdscatprintf(select, "[%ss].%s", e->name, arg->atfield));
        }
    }
    if (arg->type == ATREF) {
        $check(select =
                 sdscatprintf(select, "[%ss].%s", arg->atentity, arg->atfield));
    }
    return (wrapped_qe){ (struct query_extensions){ select, from } };
error:
    sdsfree(select);
    sdsfree(from);
    return $invalid(wrapped_qe);
}

wrapped_qe
augment_entity_query_op(struct entity*          p,
                        struct relation*        pr,
                        struct entity*          e,
                        struct field*           ff,
                        struct func*            f,
                        const char*             op,
                        struct query_extensions pqe)
{
    sds select           = sdsempty();
    sds from             = sdsempty();
    const char* template = "(%s %s %s)";

    wrapped_qe arg1, arg0;
    arg1 = augment_entity_query_op_arg(f->args[0], p, pr, e, ff, pqe);
    arg0 = augment_entity_query_op_arg(f->args[1], p, pr, e, ff, pqe);
    $inspect(arg0, error);
    $inspect(arg1, error);

    $check(select =
             sdscatprintf(select, template, arg1.v.select, op, arg0.v.select));
    if (sdslen(arg1.v.from) > 0 && sdslen(arg0.v.from) > 0)
        $check(from = sdscatprintf(from, "%s,%s", arg1.v.from, arg0.v.from));
    if (sdslen(arg1.v.from) > 0 && sdslen(arg0.v.from) == 0)
        $check(from = sdscatprintf(from, "%s", arg1.v.from));
    if (sdslen(arg1.v.from) == 0 && sdslen(arg0.v.from) > 0)
        $check(from = sdscatprintf(from, "%s", arg0.v.from));
    sdsfree(arg1.v.select);
    sdsfree(arg0.v.select);
    sdsfree(arg1.v.from);
    sdsfree(arg0.v.from);
    return (wrapped_qe){ (struct query_extensions){ select, from } };

error:
    sdsfree(select);
    sdsfree(from);
    return $invalid(wrapped_qe);
}

wrapped_qe
augment_entity_query_inner(struct entity*          p,
                           struct relation*        pr,
                           struct entity*          e,
                           struct field*           ff,
                           struct func*            f,
                           struct query_extensions pqe)
{
    if (strcmp(f->name, "Sub") == 0)
        return augment_entity_query_op(p, pr, e, ff, f, "-", pqe);
    if (strcmp(f->name, "Mul") == 0)
        return augment_entity_query_op(p, pr, e, ff, f, "*", pqe);
    if (strcmp(f->name, "Div") == 0)
        return augment_entity_query_op(p, pr, e, ff, f, "/", pqe);
    if (strcmp(f->name, "Sum") == 0)
        return augment_entity_query_nocond_agg(p, pr, e, ff, f, "SUM", pqe);
    if (strcmp(f->name, "Min") == 0)
        return augment_entity_query_nocond_agg(p, pr, e, ff, f, "MIN", pqe);
    if (strcmp(f->name, "Max") == 0)
        return augment_entity_query_nocond_agg(p, pr, e, ff, f, "MAX", pqe);
    if (strcmp(f->name, "Avg") == 0)
        return augment_entity_query_nocond_agg(p, pr, e, ff, f, "AVG", pqe);
    if (strcmp(f->name, "RollingDaysAvg") == 0)
        return augment_entity_query_date_cond_agg(
          p, pr, e, ff, f, "AVG", ">=", "-%s days", pqe);
    if (strcmp(f->name, "RollingDaysSum") == 0)
        return augment_entity_query_date_cond_agg(
          p, pr, e, ff, f, "Sum", ">=", "-%s days", pqe);
    if (strcmp(f->name, "AvgIfEq") == 0)
        return augment_entity_query_if_cond_agg(
          p, pr, e, ff, f, "AVG", "=", pqe);
    if (strcmp(f->name, "Count") == 0)
        return augment_entity_query_nocond_agg(p, pr, e, ff, f, "COUNT", pqe);

    $log_error("unknown auto field function %s", f->name);
    return $invalid(wrapped_qe);
}

wrapped_sql
build_entity_query_columns(struct entity* e, bool include_refid)
{
    sds columns = sdsempty();
    $check(columns = sdscatprintf(columns, "[%ss].Id", e->name));
    $foreach_hashed(struct field*, f, e->fields)
    {
        struct entity* next_entity = e;
        struct field*  next_field  = f;
        struct entity* cur_entity  = next_entity;
        struct field*  cur_field   = next_field;
        if (next_field->type == REF) {
            while (next_field->type == REF) {
                cur_entity = next_entity;
                cur_field  = next_field;
                $check(find_entity(
                         g_entities, next_field->ref.eid, &next_entity) == 0);
                $check(find_field(next_entity->fields,
                                  next_field->ref.fid,
                                  &next_field) == 0);
            }
            if (include_refid) {
                $check(columns = sdscatprintf(columns,
                                              ",[%ss].[%s],[%ss]._archived,[%ss].[%s]",
                                              e->name,
                                              f->name,
                                              cur_field->ref.eid,
                                              cur_field->ref.eid,
                                              cur_field->ref.fid));
            } else {
                $check(columns = sdscatprintf(columns,
                                              ",[%ss].[%s]",
                                              cur_field->ref.eid,
                                              cur_field->ref.fid));
            }
        } else if (f->type != AUTO) {
            $check(columns =
                     sdscatprintf(columns, ",[%ss].[%s]", e->name, f->name));
        } else if (include_refid == true) {
            wrapped_qe a;
            a = augment_entity_query_inner(
              NULL,
              NULL,
              e,
              f,
              f->autofunc,
              (struct query_extensions){ .where = sdsempty(),
                                         .join  = sdsempty() });
            $inspect(a, error);
            columns = sdscatprintf(columns, ",%s", a.v.select);
            sdsfree(a.v.select);
            sdsfree(a.v.from);
            $check(columns);
        }
    }
    return (wrapped_sql){ columns };

error:
    sdsfree(columns);
    return $invalid(wrapped_sql);
}

/* -- LIST QUERIES -- */

wrapped_sql
build_list_filters(struct entity* e)
{
    const char* inner_template = " OR [%ss].[%s] LIKE @name";
    sds         w              = sdsempty();
    w                          = sdscatprintf(w, "0");
    $foreach_hashed(struct field*, f, e->fields)
    {
        if (f->listed) {
            if (f->type == REF) {
                $check(w = sdscatprintf(
                         w, inner_template, f->ref.eid, f->ref.fid, "%s"));
            } else {
                $check(
                  w = sdscatprintf(w, inner_template, e->name, f->name, "%s"));
            }
        }
    }
    return (wrapped_sql){ w };

error:
    sdsfree(w);
    return $invalid(wrapped_sql);
}

wrapped_sql
build_list_query(struct entity* e, struct context* ctx, struct order* order)
{
    wrapped_sql ret;
    const char* template =
      "SELECT %s from [%ss] %s WHERE (([%ss]._archived IS NULL) AND (%s))";
    sds         sql     = sdsempty();
    wrapped_sql columns = build_entity_query_columns(e, false);
    wrapped_sql join    = build_entity_query_joins(e);
    wrapped_sql filters = build_list_filters(e);
    $inspect(columns, error);
    $inspect(join, error);
    $inspect(filters, error);
    $check(sql = sdscatprintf(
             sql, template, columns.v, e->name, join.v, e->name, filters.v));
    if (ctx != NULL) {
        $check(sql = sdscatprintf(
                 sql, " AND [%ss].[%s] = %d", e->name, ctx->fname, ctx->k));
    }
    if (order != NULL) {
        if (order->fpath.fid)
            $check(sql = sdscatprintf(sql,
                                      " ORDER BY [%ss].[%s] %s",
                                      order->fpath.eid,
                                      order->fpath.fid,
                                      order->asc ? "ASC" : "DESC"));
    }
    ret = (wrapped_sql){ sql };
    goto cleanup;
error:
    sdsfree(sql);
    ret = $invalid(wrapped_sql);
cleanup:
    sdsfree(columns.v);
    sdsfree(join.v);
    sdsfree(filters.v);
    return ret;
}

sds
build_obj_query(struct entity* e)
{
    const char* template = "SELECT %s from [%ss] %s %s WHERE [%ss].Id = @id;";
    sds         ret      = sdsempty();
    wrapped_sql columns  = build_entity_query_columns(e, true);
    $inspect(columns, error);
    wrapped_sql join = build_entity_query_joins(e);
    $inspect(join, error);
    sds froms = sdsempty();
    $foreach_hashed(struct field*, f, e->fields)
    {
        if (f->type == AUTO) {
            wrapped_qe a;
            a = augment_entity_query_inner(
              NULL,
              NULL,
              e,
              f,
              f->autofunc,
              (struct query_extensions){ .where = sdsempty(),
                                         .join  = sdsempty() });
            $inspect(a, error);
            if (strcmp(a.v.from, "") != 0) $check(froms = sdscat(froms, ","));
            $check(froms = sdscat(froms, a.v.from));
            sdsfree(a.v.select);
            sdsfree(a.v.from);
        }
    }

    $check(ret = sdscatprintf(
             ret, template, columns.v, e->name, froms, join.v, e->name));
    sdsfree(froms);
    $log_info("----------------- build obj query");
    $log_info("SQL is %s", ret);
error:
    sdsfree(columns.v);
    sdsfree(join.v);
    return ret;
}

sds
field_value_to_string(struct field* f, sqlite3_stmt* res, int index)
{
    sds    ret = sdsempty();
    time_t s;
    switch (f->type) {
        case REF: {
            struct entity* ref_entity;
            $check(find_entity(g_entities, f->ref.eid, &ref_entity) == 0);
            struct field* ref_field;
            $check(find_field(ref_entity->fields, f->ref.fid, &ref_field) == 0);
            sdsfree(ret);
            ret = field_value_to_string(ref_field, res, index);
        } break;
        case TEXT:
            ret = sdscatprintf(ret, "%s", sqlite3_column_text(res, index));
            break;
        case INTEGER:
            ret = sdscatprintf(ret, "%d", sqlite3_column_int(res, index));
            break;
        case DATE:
            s = sqlite3_column_int(res, index);
            struct tm ts;
            char      buf[80];
            ts = *localtime(&s);
            strftime(buf, sizeof(buf), "%Y-%m-%d", &ts);
            ret = sdscatprintf(ret, "%s", buf);
            break;
        case REAL:
            ret = sdscatprintf(ret, "%.2f", sqlite3_column_double(res, index));
            break;
        case AUTO:
            ret =
              f->format
                ? sdscatprintf(
                    ret, f->format, sqlite3_column_double(res, index))
                : sdscatprintf(ret, "%.2f", sqlite3_column_double(res, index));

            break;
        default:
            break;
    }
error:
    return ret;
}

sds
get_ref_value(sqlite3* db, int key, const char* ename, const char* efield)
{
    sds ret  = sdsempty();
    sds sql  = sdsempty();
    sds join = sdsempty();

    struct entity* ref_entity;
    struct field*  ref_field;
    $check(find_entity(g_entities, ename, &ref_entity) == 0);
    $check(find_field(ref_entity->fields, efield, &ref_field) == 0);
    while (ref_field->type == REF) {
        $check(join = sdscatprintf(join,
                                   " INNER JOIN [%ss] ON [%ss].Id = [%ss].[%s]",
                                   ref_entity->name,
                                   ref_field->ref.eid,
                                   ref_entity->name,
                                   ref_field->name));
        $check(find_entity(g_entities, ref_field->ref.eid, &ref_entity) == 0);
        $check(find_field(ref_entity->fields, ref_field->ref.fid, &ref_field) ==
               0);
    }

    sql = sdscatprintf(sql,
                       "SELECT [%ss].[%s] FROM [%ss] %s WHERE [%ss].[Id] = @id",
                       ref_entity->name,
                       ref_field->name,
                       ref_entity->name,
                       join,
                       ename);

    if (sql == NULL) return NULL;
    sqlite3_stmt* res;
    $check(sqlite3_prepare_v2(db, sql, -1, &res, 0) == SQLITE_OK,
           sqlite3_errmsg(db),
           error);
    int idx = sqlite3_bind_parameter_index(res, "@id");
    sqlite3_bind_int(res, idx, key);
    if (sqlite3_step(res) == SQLITE_ROW) {
        ret = field_value_to_string(ref_field, res, 0);
    }
error:
    sdsfree(sql);
    return ret;
}

sds
create_insert_statement(struct entity* e)
{
    sds buf  = sdsempty();
    sds buf2 = sdsempty();
    $foreach_hashed(struct field*, f, e->fields)
    {
        if (f->type == AUTO) break;
        buf  = sdscatprintf(buf, "[%s],", f->name);
        buf2 = sdscatprintf(buf2, "@%s,", f->name);
    }
    buf               = sdstrim(buf, ",");
    buf2              = sdstrim(buf2, ",");
    const char* sql   = "INSERT INTO [%ss](%s) VALUES (%s);";
    sds         final = sdscatprintf(sdsempty(), sql, e->name, buf, buf2);
    sdsfree(buf);
    sdsfree(buf2);
    return final;
}

sds
create_update_statement(struct entity* e)
{
    sds tmp = sdsempty();
    $foreach_hashed(struct field*, f, e->fields)
    {
        if (f->type == AUTO) break;
        tmp = sdscatprintf(tmp, "[%s]=@%s,", f->name, f->name);
    }
    tmp               = sdstrim(tmp, ",");
    const char* sql   = "UPDATE [%ss] SET %s WHERE Id = @id;";
    sds         final = sdscatprintf(sdsempty(), sql, e->name, tmp);
    sdsfree(tmp);
    return final;
}

sds
create_archive_statement(struct entity* e)
{
    sds         tmp   = sdsempty();
    const char* sql   = "UPDATE [%ss] SET _archived=1 WHERE Id = @id;";
    sds         final = sdscatprintf(sdsempty(), sql, e->name);
    sdsfree(tmp);
    return final;
}

$typedef(time_t) wrapped_time_t;
wrapped_time_t
parse_date_field(const char* str)
{
    struct tm ti = { 0 };
    if (sscanf(str, "%d-%d-%d", &ti.tm_year, &ti.tm_mon, &ti.tm_mday) != 3) {
        return $invalid(wrapped_time_t);
    }
    ti.tm_year -= 1900;
    ti.tm_mon -= 1;
    ti.tm_isdst = -1;
    return (wrapped_time_t){ mktime(&ti) };
}

$status
bind_sql_params(struct entity_value* e, sqlite3_stmt* res, int key)
{
    $status ret   = $okay;
    sds     fname = sdsempty();
    $foreach_hashed(struct field_value*, f, e->fields)
    {
        fname   = sdscatprintf(fname, "@%s", f->base->name);
        int idx = sqlite3_bind_parameter_index(res, fname);
        if (f->base->type == REF) {
            sqlite3_bind_int(res, idx, f->_kvalue);
        } else if (f->base->type == DATE) {
            wrapped_time_t wt = parse_date_field((const char*)f->_ret_value);
            time_t         t  = $unwrap(wt, ret, cleanup);
            sqlite3_bind_int(res, idx, t);
        } else {
            sqlite3_bind_text(res,
                              idx,
                              (const char*)f->_ret_value,
                              strlen((char*)f->_ret_value),
                              SQLITE_TRANSIENT);
        }
        sdsfree(fname);
        fname = sdsempty();
    }
    if (key >= 0) {
        int idx = sqlite3_bind_parameter_index(res, "@id");
        sqlite3_bind_int(res, idx, key);
    }
cleanup:
    sdsfree(fname);
    return ret;
}

/* -- UI FUNCTIONS -- */

void
init_fields(struct entity_value* e, sqlite3* db, int key)
{
    if (key <= 0) return;
    sds sql;
    sql = build_obj_query(e->base);
    if (sql == NULL) return;
    sqlite3_stmt* res;
    $check(sqlite3_prepare_v2(db, sql, -1, &res, 0) == SQLITE_OK,
           sqlite3_errmsg(db),
           error);
    int idx = sqlite3_bind_parameter_index(res, "@id");
    sqlite3_bind_int(res, idx, key);
    while (sqlite3_step(res) == SQLITE_ROW) {
        int i = 1;
        $foreach_hashed(struct field_value*, f, e->fields)
        {
            if (f->base->type == REF) {
                f->_kvalue = sqlite3_column_int(res, i);
                i++;
                f->is_archived = (sqlite3_column_type(res, i)!=SQLITE_NULL);
                i++;
            }
            sds v = field_value_to_string(f->base, res, i);
            if (v != NULL) {
                if (f->_init_value != NULL) free(f->_init_value);
                f->_init_value = malloc(strlen((char*)v) + 1);
                strcpy((char*)f->_init_value, (char*)v);
            }
            sdsfree(v);
            i++;
        }
    }
    sqlite3_reset(res);
    sqlite3_finalize(res);
error:
    sdsfree(sql);
}

wrapped_key
apply_form(struct entity_value* e, sqlite3* db, int key)
{
    wrapped_key ret;
    sds         sql2;
    if (key >= 0) {
        sql2 = create_update_statement(e->base);
    } else {
        sql2 = create_insert_statement(e->base);
    }
    sqlite3_stmt* res;
    $check(sqlite3_prepare_v2(db, sql2, -1, &res, 0) == SQLITE_OK,
           ret.status,
           sqlite3_errmsg(db),
           cleanup_sql);
    $onerror2(bind_sql_params(e, res, key))
    {
        ret = $invalid(wrapped_key);
        goto cleanup_sqlite;
    }
    if (sqlite3_step(res) == SQLITE_DONE) {
        if (key <= 0) key = sqlite3_last_insert_rowid(db);
    }
    ret = (wrapped_key){ key };
cleanup_sqlite:
    sqlite3_finalize(res);
cleanup_sql:
    sdsfree(sql2);
    return ret;
}

wrapped_key
archive_obj(struct entity* e, sqlite3* db, int key)
{
    wrapped_key ret;
    sds         sql2;
    sql2 = create_archive_statement(e);
    sqlite3_stmt* res;
    $check(sqlite3_prepare_v2(db, sql2, -1, &res, 0) == SQLITE_OK,
           ret.status,
           sqlite3_errmsg(db),
           cleanup_sql);
    int idx = sqlite3_bind_parameter_index(res, "@id");
    sqlite3_bind_int(res, idx, key);
    $log_info("----------SQL DELETE\n%s key: %d", sql2, key);
    if (sqlite3_step(res) == SQLITE_DONE) {
        if (key <= 0) key = sqlite3_last_insert_rowid(db);
    }
    ret = (wrapped_key){ key };
    sqlite3_finalize(res);
cleanup_sql:
    sdsfree(sql2);
    return ret;
}
