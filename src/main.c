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
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "argtable3/argtable3.h"
#include "core/args.h"
#include "core/iterators.h"

#include "rdsl.h"
#include "tui.h"

char*               g_title;
struct entity*      g_entities;
struct translation* g_translations;

$status
parse_model_file(const char* filename)
{
    $status ret = $okay;
    FILE*   in  = fopen(filename, "r");
    if (!in) {
        ret = $error("unable to open file");
    } else {
        struct t_parser parser = { 0 };
        parser.in              = in;
        prebase_context_t* ctx = prebase_create(&parser);
        while (prebase_parse(ctx, NULL))
            ;
        prebase_destroy(ctx);
        fclose(in);
    }
    return ret;
}

int
main(int argc, const char** argv)
{
    struct arg_lit* parse = arg_lit0(NULL, "parse", "Parse the model");
    struct arg_lit* init =
      arg_lit0(NULL, "init", "Initialize a new database file");
    struct arg_lit* inmemdb =
      arg_lit0(NULL, "memdb", "Use an in-memory database");
    struct arg_file* model =
      arg_file0(NULL, "model", "<output>", "Model filename.");
    struct arg_file* database = arg_file0(
      NULL, "db", "<output>", "Database file. Default is \"records.db\")");
    database->filename[0] = "records.db";
    add_base_args();
    arg_append(model);
    arg_append(parse);
    arg_append(init);
    arg_append(inmemdb);
    arg_append(database);
    parse_all_args(argc, argv, "test");

    g_title = sdsnew("TURBOBUILDER");

    if $iserror (parse_model_file(model->filename[0])) {
        goto cleanup_args;
    }

    if (parse->count > 0) goto cleanup_args;

    init_tui();

    sqlite3* db;
    int      rc;

    if (inmemdb->count > 0) {
        $log_info("using memory stored database");
        rc = sqlite3_open(":memory:", &db);
    } else {
        $log_info("using [%s] as database", database->filename[0]);
        rc = sqlite3_open(database->filename[0], &db);
    }
    if (rc != SQLITE_OK) {
        $log_error("Cannot open database: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        goto cleanup;
    }
    if (inmemdb->count > 0 || init->count > 0) {
        create_tables_from_model(db);
    }

    run_tui(db);

cleanup:
    shutdown_tui();
    sqlite3_close(db);
    cleanup_translations(g_translations);
    cleanup_entities(g_entities);
    sdsfree(g_title);
cleanup_args:
    arg_freeall();
    return 0;
}
