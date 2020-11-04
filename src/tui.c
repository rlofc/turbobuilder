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
#include <wchar.h>

#include "core/iterators.h"
#include "sds/sds.h"

#include "model.h"
#include "msql.h"
#include "tui.h"

#define COLOR_ERROR 1

#define OUTPUT_IN_MESSAGE_BOX(LEVEL)                                           \
    void $output_##LEVEL(const char* fmt, ...)                                 \
    {                                                                          \
        va_list args;                                                          \
        va_start(args, fmt);                                                   \
        sds msg = sdsempty();                                                  \
        msg     = sdscatvprintf(msg, fmt, args);                               \
        newtWinMessage(#LEVEL, "close", "%s", msg);                            \
        sdsfree(msg);                                                          \
        va_end(args);                                                          \
    }

#define OUTPUT_IN_BUFFER(LEVEL)                                                \
    void $output_##LEVEL(const char* fmt, ...)                                 \
    {                                                                          \
        va_list args;                                                          \
        va_start(args, fmt);                                                   \
        output_buffer = sdscatvprintf(output_buffer, fmt, args);               \
        va_end(args);                                                          \
    }

OUTPUT_IN_MESSAGE_BOX(error);

static sds output_buffer;

OUTPUT_IN_BUFFER(info);
OUTPUT_IN_BUFFER(debug);

struct field_value_tui
{
    struct field_value* ef;
    newtComponent       field_label;
    newtComponent       field_entry;
    UT_hash_handle      hh;
};

struct entity_value_tui
{
    struct entity_value*    ee;
    struct field_value_tui* fields_tui;
};

typedef struct
{
    struct entity*      e;
    struct field_value* f;
    sqlite3*            db;
} lookup_data;

struct window_size
{
    unsigned int w;
    unsigned int h;
};

static const unsigned int MAX_SEARCH_TERM_SIZE = 1024 + 1;

typedef struct
{
    newtComponent form;
    newtComponent entities_listbox;
    newtComponent search_label;
    newtComponent search_entry;
    newtComponent close_button;
    const char*   value;
    char          search_term_buffer[MAX_SEARCH_TERM_SIZE];
} newt_lookup_form;

typedef struct
{
    newtComponent form;
    int           exit;
} generic_form;

$typedef(struct entity_value_tui*) wrapped_entity_value;

wrapped_entity_value
create_entity_value(struct entity* e)
{
    struct entity_value*     ee    = calloc(1, sizeof(struct entity_value));
    struct entity_value_tui* eetui = calloc(1, sizeof(struct entity_value_tui));
    ee->base                       = e;
    eetui->ee                      = ee;

    $foreach_hashed(struct field*, f, e->fields)
    {
        struct field_value*     ef = calloc(1, sizeof(struct field_value));
        struct field_value_tui* eftui =
          calloc(1, sizeof(struct field_value_tui));
        ef->base     = f;
        ef->is_valid = true;
        eftui->ef    = ef;
        HASH_ADD_STR(ee->fields, base->name, ef);
        HASH_ADD_STR(eetui->fields_tui, ef->base->name, eftui);
    }

    return (wrapped_entity_value){ eetui };
}

void
destroy_entity_value(struct entity_value_tui* eetui)
{
    struct field_value *fv, *tmp_fv;
    HASH_ITER(hh, eetui->ee->fields, fv, tmp_fv)
    {
        HASH_DEL(eetui->ee->fields, fv);
        if (fv->_init_value != NULL) free(fv->_init_value);
        free(fv);
    }
    struct field_value_tui *fvtui, *tmp_fvtui;
    HASH_ITER(hh, eetui->fields_tui, fvtui, tmp_fvtui)
    {
        HASH_DEL(eetui->fields_tui, fvtui);
        free(fvtui);
    }
    free(eetui->ee);
    free(eetui);
}

int
show_lookup_form(struct entity*  e,
                 sqlite3*        db,
                 struct context* ctx,
                 struct fpath*   order,
                 bool            lookup_only);

lookup_data ld;

int
ref_field_filter(newtComponent entry, void* data, int ch, int cursor)
{
    struct field_value* f = data;
    if (ch == 13 && f->_kvalue != 0) {
        return 13;
    }
    struct entity* e     = f->_data;
    sds            title = sdscatprintf(sdsempty(), "%s Lookup", _TR(e->name));
    newtCenteredWindow(40, 20, title);
    int key = show_lookup_form(f->_data, ld.db, NULL, &f->base->order, true);
    newtPopWindow();
    if (key == -2) return 0;
    f->_kvalue = key;
    sds v      = get_ref_value(ld.db, key, f->base->ref.eid, f->base->ref.fid);
    newtEntrySet(entry, v, 1);
    sdsfree(v);
    return 0;
}

/* -- SPECIFIC DATE FIELD BEHAVIOR -- */

bool
is_valid_date_str(const char* dstr)
{
    int d, m, y;
    int daysinmonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (sscanf(dstr, "%i-%i-%i", &y, &m, &d) != 3) return false;
    if (y % 400 == 0 || (y % 100 != 0 && y % 4 == 0)) daysinmonth[1] = 29;
    return (m < 13 && d > 0 && d <= daysinmonth[m - 1]);
}

bool
date_field_ch_at_cursor_is_valid(const char* val, int ch, int cursor)
{
    return !((cursor == 5 && (ch > 48 + 1)) ||
             (cursor == 6 && val[cursor - 1] == '1' && ch > 48 + 2) ||
             (cursor == 6 && val[cursor - 1] == '0' && ch == 48 + 0) ||
             (cursor == 8 && val[5] == '0' && val[6] == '2' && ch > 48 + 2) ||
             (cursor == 8 && ch > 48 + 3) ||
             (cursor == 9 && val[cursor - 1] == '3' && ch > 48 + 1) ||
             (cursor == 9 && val[cursor - 1] == '0' && ch == 48 + 0));
}

int
date_field_filter(newtComponent entry, void* data, int ch, int cursor)
{
    int                 ret = 0;
    struct field_value* f   = data;
    sds                 val = sdsnew(newtEntryGetValue(entry));
    f->is_valid             = true;
    if (cursor >= 10) {
        switch (ch) {
            case NEWT_KEY_LEFT:
                ret = NEWT_KEY_LEFT;
                goto cleanup;
            case NEWT_KEY_BKSPC:
                ret = NEWT_KEY_LEFT;
                goto cleanup;
            case NEWT_KEY_HOME:
                ret = NEWT_KEY_HOME;
                goto cleanup;
            case NEWT_KEY_TAB:
                ret = NEWT_KEY_TAB;
                goto cleanup;
            case NEWT_KEY_ENTER:
                ret = NEWT_KEY_ENTER;
                goto cleanup;
            default:
                ret = 0;
                goto cleanup;
        }
    }
    if (ch == NEWT_KEY_BKSPC) ch = NEWT_KEY_LEFT;
    if (ch == NEWT_KEY_LEFT) {
        if (cursor == 5 || cursor == 8) {
            newtEntrySetCursorPosition(entry, cursor - 1);
        }
        ret = NEWT_KEY_LEFT;
        goto cleanup;
    }
    if (ch == NEWT_KEY_RIGHT) {
        if (cursor == 3 || cursor == 6) {
            newtEntrySetCursorPosition(entry, cursor + 1);
        }
        ret = NEWT_KEY_RIGHT;
        goto cleanup;
    }
    if (ch >= 48 + 0 && ch <= 48 + 9) {
        if (date_field_ch_at_cursor_is_valid(val, ch, cursor)) {
            val[cursor] = ch;
            newtEntrySet(entry, val, 0);
            newtEntrySetCursorPosition(
              entry, cursor == 3 || cursor == 6 ? cursor + 1 : cursor);
            if (is_valid_date_str(val) == 0)
                newtEntrySetColors(entry,
                                   NEWT_COLORSET_CUSTOM(COLOR_ERROR),
                                   NEWT_COLORSET_CUSTOM(COLOR_ERROR));
            else
                newtEntrySetColors(
                  entry, NEWT_COLORSET_ENTRY, NEWT_COLORSET_ENTRY);
            ret = NEWT_KEY_RIGHT;
        }
    }
cleanup:
    sdsfree(val);
    return ret;
}

/* -- WINDOWS DIMENSION HELPERS -- */

int
get_ideal_list_window_size(struct entity* e)
{
    int          wcols, wrows;
    unsigned int width = 1;
    unsigned int maxlw = 0;

    newtGetScreenSize(&wcols, &wrows);
    unsigned int max_width = wcols * 0.8;

    $foreach_hashed(struct field*, f, e->fields)
    {
        if (f->listed) {
            width = width + f->length + 1;
        }
    }
    width = width + 2;
    if (width < 60) width = 60;
    if (width > max_width) width = max_width;

    return width;
}

struct window_size
get_ideal_form_window_size(struct entity* e)
{
    int wcols, wrows;
    newtGetScreenSize(&wcols, &wrows);
    unsigned int max_width = wcols * 0.8;

    int          row   = 1;
    unsigned int col   = 1;
    unsigned int maxlw = 0;
    $foreach_hashed(struct field*, f, e->fields)
    {
        if (col + strlen(f->name) + 2 + f->length + 1 > max_width) {
            col = 1;
            row = row + 2;
        }
        col = col + strlen(f->name) + 2 + f->length + 1;
        if (col > maxlw) maxlw = col;
    }
    if (maxlw < 26) maxlw = 26;
    return (struct window_size){ .w = maxlw + 1, .h = row };
}

void
add_form_fields(struct entity_value_tui* e, sqlite3* db, newtComponent form)
{
    unsigned int row = 1;
    unsigned int col = 1;

    struct window_size s = get_ideal_form_window_size(e->ee->base);
    $foreach_hashed(struct field_value_tui*, f, e->fields_tui)
    {
        if (col + strlen(f->ef->base->name) + 2 + f->ef->base->length + 1 >
            s.w) {
            col = 1;
            row = row + 2;
        }
        char label[1024];
        snprintf(label, 1024, "%s: ", _TR(f->ef->base->name));
        f->field_label = newtLabel(col, row, label);
        col            = col + strlen(label);
        const char* defv =
          (char*)f->ef->_init_value == NULL ? "" : (char*)f->ef->_init_value;
        f->field_entry = newtEntry(col,
                                   row,
                                   defv,
                                   f->ef->base->length,
                                   (const char**)&(f->ef->_ret_value),
                                   NEWT_FLAG_SCROLL);
        if (f->ef->base->type == DATE) {
            if (f->ef->_init_value == NULL ||
                strlen((const char*)f->ef->_init_value) < 10)
                newtEntrySet(f->field_entry, "____-__-__", 0);
            newtEntrySetFilter(f->field_entry, date_field_filter, f->ef);
            newtEntrySetCursorPosition(f->field_entry, 0);
        }
        if (f->ef->base->type == REF) {
            ld.db = db;
            ld.f  = f->ef;
            find_entity(g_entities, f->ef->base->ref.eid, &(ld.e));
            f->ef->_data = ld.e;
            newtEntrySetFilter(f->field_entry, ref_field_filter, f->ef);
        }
        col = col + f->ef->base->length + 1;
        newtFormAddComponents(form, f->field_label, f->field_entry, NULL);
    }
}

$typedef(struct field_value*) wrapped_field_value;

wrapped_field_value
find_field_value(struct field_value* fs, const char* name)
{
    wrapped_field_value result;
    HASH_FIND_STR(fs, name, result.v);
    $certify(result.v != NULL, result.status);
    return result;
}

void
init_context(struct entity_value* e, sqlite3* db, struct context* ctx)
{
    if (ctx == NULL) return;
    wrapped_field_value wef = find_field_value(e->fields, ctx->fname);
    struct field_value* f   = $unwrap(wef);
    f->_kvalue              = ctx->k;
    sds v = get_ref_value(db, ctx->k, f->base->ref.eid, f->base->ref.fid);
    if (v != NULL) {
        if (f->_init_value != NULL) free(f->_init_value);
        f->_init_value = malloc(strlen((char*)v) + 1);
        strcpy((char*)f->_init_value, (char*)v);
    }
    sdsfree(v);
    return;
error:
    $log_error("Unable to find editable field %s in entity %s",
               ctx->fname,
               e->base->name);
}

struct window_size
create_form_window(struct entity* e)
{
    struct window_size s = get_ideal_form_window_size(e);
    newtCenteredWindow(s.w, s.h + 4, _TR(e->name));
    return s;
}

const int RELATION_KEYS[] = { NEWT_KEY_F1,
                              NEWT_KEY_F2,
                              NEWT_KEY_F3,
                              NEWT_KEY_F4,
                              NEWT_KEY_F5 };

char*
add_relations_hotkeys(struct entity* e, newtComponent f)
{
    int i        = 0;
    sds helpline = sdsempty();
    $foreach_hashed(struct relation*, r, e->relations)
    {
        newtFormAddHotKey(f, RELATION_KEYS[i]);
        helpline = sdscatprintf(helpline, "F%d-%ss ", i + 1, _TR(r->fk.eid));
        if (++i == sizeof(RELATION_KEYS) / sizeof(int)) break;
    }
    return helpline;
}

$typedef(struct relation*) wrapped_relation;

wrapped_relation
get_relation_by_hotkey(struct entity* e, int k)
{
    int i = 0;
    $foreach_hashed(struct relation*, r, e->relations)
    {
        if (k == RELATION_KEYS[i]) return (wrapped_relation){ r };
        i++;
    }
    return $invalid(wrapped_relation);
}

void
show_relation_list_view(newtComponent    parent,
                        struct entity*   e,
                        struct relation* r,
                        int              key,
                        sqlite3*         db)
{
    int            px, py;
    sds            title = sdsempty();
    struct entity* re;
    struct context ctx;

    find_entity(g_entities, r->fk.eid, &re);
    newtComponentGetPosition(parent, &px, &py);
    title = sdscatprintf(title, "%s %ss", _TR(e->name), _TR(r->fk.eid));
    newtCenteredWindow(get_ideal_list_window_size(re), 10, title);
    ctx.fname = r->fk.fid;
    ctx.k     = key;
    show_lookup_form(re, db, &ctx, &r->order, false);
    newtPopWindow();
    sdsfree(title);
}

int
show_entity_form_view(struct entity_value_tui* e,
                      sqlite3*                 db,
                      void (*add_fields)(struct entity_value_tui*,
                                         sqlite3* db,
                                         newtComponent),
                      int key)
{
    int                ret = -1;
    struct window_size s   = create_form_window(e->ee->base);

    newtComponent form = newtForm(NULL, NULL, 0);
    add_fields(e, db, form);
    sds helpline = sdsempty();
    if (key > 0) {
        sdsfree(helpline);
        helpline = add_relations_hotkeys(e->ee->base, form);
    }
    newtPushHelpLine(key > 0 ? helpline : "");
    newtFormAddHotKey(form, NEWT_KEY_F11);
    newtRefresh();

    newtComponent save_button, close_button;
    save_button  = newtCompactButton(s.w - 20, s.h + 3, "Save");
    close_button = newtCompactButton(s.w - 12, s.h + 3, "Close");
    newtFormAddComponents(form, save_button, close_button, NULL);

    int exit = 1;
    while (exit > 0) {
        exit = -1;
        struct newtExitStruct ee;
        newtFormRun(form, &ee);
        if (ee.reason == NEWT_EXIT_COMPONENT) {
            newtComponent last = ee.u.co;
            if (last != close_button) {
                exit = 0;
            }
            if (last == save_button) {
                // TODO check all fields is_valid
                wrapped_key wk = apply_form(e->ee, db, key);
                $ifvalid(wk)
                {
                    key = ret = wk.v;
                    newtPopHelpLine();
                    sdsfree(helpline);
                    helpline = add_relations_hotkeys(e->ee->base, form);
                    newtPushHelpLine(helpline);
                    newtFormSetCurrent(form, close_button);
                    exit = 1;
                }
                else { $log_error("Could not save. %s", wk.status.message); }
            }
        }
        if (ee.reason == NEWT_EXIT_HOTKEY) {
            if (ee.u.key == NEWT_KEY_F11) {
                // was refresh
            } else {
                wrapped_relation r =
                  get_relation_by_hotkey(e->ee->base, ee.u.key);
                if ($isvalid(r) && key > 0) {
                    exit = 1;
                    show_relation_list_view(form, e->ee->base, r.v, key, db);
                    // Refresh
                    init_fields(e->ee, db, key);
                    exit = 1;
                    $foreach_hashed(struct field_value_tui*, f, e->fields_tui)
                    {
                        const char* defv = (char*)f->ef->_init_value == NULL
                                             ? ""
                                             : (char*)f->ef->_init_value;
                        newtEntrySet(f->field_entry, defv, 0);
                    }
                }
            }
        }
    }
    newtFormDestroy(form);
    newtPopHelpLine();
    newtPopWindow();
    sdsfree(helpline);
    return ret;
}

int
show_entity_edit_form(struct entity*  e,
                      sqlite3*        db,
                      int             key,
                      struct context* ctx)
{
    int                      ret   = -1;
    wrapped_entity_value     wee   = create_entity_value(e);
    struct entity_value_tui* eetui = $unwrap(wee);

    init_fields(eetui->ee, db, key);
    init_context(eetui->ee, db, ctx);
    ret = show_entity_form_view(eetui, db, add_form_fields, key);
    destroy_entity_value(eetui);

error:
    return ret;
}

/* -- LOOKUP FORM -- */

$status
append_row_to_listbox(struct entity* e,
                      sqlite3_stmt*  res,
                      newtComponent  entities_listbox)
{
    $status status      = $okay;
    sds     val         = sdsempty();
    int     field_index = 0;

    $foreach_hashed(struct field*, f, e->fields)
    {
        if (f->listed) {
            sds value = field_value_to_string(f, res, field_index + 1);
            val       = sdscatprintf(val, " %-*s", f->length, value);
            sdsfree(value);
        }
        field_index++;
    }
    intptr_t key = sqlite3_column_int(res, 0);
    newtListboxAppendEntry(entities_listbox, val, (void*)key);
    sdsfree(val);

    return status;
}

$status
query_in_listbox(struct entity*  e,
                 sqlite3*        db,
                 newtComponent   entities_listbox,
                 const char*     search_term,
                 struct context* ctx,
                 struct fpath*   order)
{
    $status       status       = $okay;
    sds           search_query = sdsempty();
    sqlite3_stmt* res;

    wrapped_sql maybe_list_query = build_list_query(e, ctx, order);
    sds query_sql = $unwrap(maybe_list_query, status, query_build_error);
    $check(sqlite3_prepare_v2(db, query_sql, -1, &res, 0) == SQLITE_OK,
           "",
           sqlite_prepare_error);
    int idx      = sqlite3_bind_parameter_index(res, "@name");
    search_query = sdscatprintf(search_query, "%%%s%%", search_term);
    $check(SQLITE_OK ==
             sqlite3_bind_text(
               res, idx, search_query, sdslen(search_query), SQLITE_TRANSIENT),
           status,
           bind_error);
    while (sqlite3_step(res) == SQLITE_ROW) {
        append_row_to_listbox(e, res, entities_listbox);
    }

bind_error:
    sqlite3_finalize(res);
sqlite_prepare_error:
    sdsfree(query_sql);
query_build_error:
    sdsfree(search_query);
    return status;
}

void
lookup_form_setup(newt_lookup_form* f, int rows, int cols)
{
    f->entities_listbox =
      newtListbox(0, 3, rows - 4, NEWT_FLAG_RETURNEXIT | NEWT_FLAG_SCROLL);
    newtListboxSetWidth(f->entities_listbox, cols);
    newtPushHelpLine("INSERT to add, F12 to exit");
    newtRefresh();
    f->form         = newtForm(NULL, NULL, 0);
    f->close_button = newtCompactButton(30, rows - 1, "Ok");
    f->search_label = newtLabel(1, 1, "Search:");
    f->search_entry = newtEntry(9,
                                1,
                                (const char*)&f->search_term_buffer,
                                14,
                                &f->value,
                                NEWT_FLAG_SCROLL | NEWT_FLAG_RETURNEXIT);
    newtFormAddComponents(f->form,
                          f->search_label,
                          f->search_entry,
                          f->entities_listbox,
                          f->close_button,
                          NULL);
    newtFormAddHotKey(f->form, NEWT_KEY_INSERT);

    if (strcmp(f->search_term_buffer, "") != 0)
        newtFormSetCurrent(f->form, f->entities_listbox);
}

int
show_lookup_form(struct entity*  e,
                 sqlite3*        db,
                 struct context* ctx,
                 struct fpath*   order,
                 bool            lookup_only)
{
    newt_lookup_form f = { .search_term_buffer = "" };
    lookup_form_setup(&f,
                      ctx == NULL ? 20 : 10,
                      ctx == NULL ? 40 : get_ideal_list_window_size(e));
    int      exit  = 0;
    intptr_t ret   = -2;
    intptr_t resel = -1;
    while (exit != 1) {
        newtListboxClear(f.entities_listbox);
        if ($iserror(query_in_listbox(
          e, db, f.entities_listbox, f.search_term_buffer, ctx, order))) {
            break;
        }
        if (resel != -1)
            newtListboxSetCurrentByKey(f.entities_listbox, (void*)resel);
        struct newtExitStruct ee;
        newtFormRun(f.form, &ee);
        if (ee.reason == NEWT_EXIT_COMPONENT) {
            newtComponent last = ee.u.co;
            if (last == f.search_entry) {
                strcpy(f.search_term_buffer, f.value);
                newtFormSetCurrent(f.form, f.entities_listbox);
            }
            if (last == f.entities_listbox) {
                intptr_t key =
                  (intptr_t)newtListboxGetCurrent(f.entities_listbox);
                if (lookup_only) {
                    ret  = key;
                    exit = 1;
                } else {
                    show_entity_edit_form(e, db, key, NULL);
                    resel = key;
                }
            }
            if (last == f.close_button) {
                if (lookup_only)
                    ret = (intptr_t)newtListboxGetCurrent(f.entities_listbox);
                exit = 1;
            }
        } else {
            if (ee.u.key == NEWT_KEY_INSERT)
                resel = show_entity_edit_form(e, db, -1, ctx);
            if (ee.u.key == NEWT_KEY_F12) exit = 1;
            if (ee.u.key == NEWT_KEY_ESCAPE) exit = 1;
        }
    }
    newtFormDestroy(f.form);
    newtPopHelpLine();
    return ret;
}

void
show_output_buffer_view()
{
    int wcols, wrows;

    newtGetScreenSize(&wcols, &wrows);
    wcols = wcols * 0.8;
    wrows = wrows * 0.8;
    newtCenteredWindow(wcols, wrows, "INFO messages");
    newtComponent tb = newtTextbox(
      0, 0, wcols - 2, wrows, NEWT_TEXTBOX_WRAP | NEWT_TEXTBOX_SCROLL);
    newtComponent form = newtForm(NULL, NULL, 0);
    newtFormAddComponents(form, tb, NULL);
    newtTextboxSetText(tb, output_buffer);
    struct newtExitStruct iee;
    newtFormRun(form, &iee);
    newtFormDestroy(form);
    newtPopWindow();
}

void
show_entities_form(sqlite3* db)
{
    int exit = 0;
    while (exit == 0) {
        newtComponent entities_listbox;
        entities_listbox = newtListbox(0, 0, 9, NEWT_FLAG_RETURNEXIT);
        $foreach_hashed(struct entity*, e, g_entities)
        {
            newtListboxAppendEntry(entities_listbox, _TR(e->name), e);
        }
        newtListboxSetWidth(entities_listbox, 20);
        newtOpenWindow(1, 1, 20, 10, "Entities");
        newtRefresh();
        newtComponent form = newtForm(NULL, NULL, 0);
        newtFormAddHotKey(form, NEWT_KEY_F1);
        newtFormAddComponents(form, entities_listbox, NULL);
        struct newtExitStruct ee;
        newtFormRun(form, &ee);
        if (ee.reason == NEWT_EXIT_COMPONENT) {
            struct entity* sel = newtListboxGetCurrent(entities_listbox);
            newtCenteredWindow(40, 20, _TR(sel->name));
            int r = show_lookup_form(sel, db, NULL, NULL, false);
            newtPopWindow();
        }
        if (ee.reason == NEWT_EXIT_HOTKEY) {
            if (ee.u.key == NEWT_KEY_F1) {
                show_output_buffer_view();
            } else
                exit = 1;
        }
        newtFormDestroy(form);
        newtPopWindow();
    }
}

void
draw_background()
{
    int wcols, wrows;

    newtGetScreenSize(&wcols, &wrows);
    sds emblem = sdsnew(g_title);
    for (int j = 0; j < wcols / sdslen(g_title) + wrows; j++) {
        emblem = sdscatprintf(emblem, "%s ", g_title);
    }
    for (int j = 0; j < wrows; j++)
        newtDrawRootText(0, j, &emblem[(j * 2)]);
    sdsfree(emblem);
}

void
init_tui()
{
    output_buffer = sdsempty();
    newtInit();
    newtSetColor(NEWT_COLORSET_ROOTTEXT, "color025", "blue");
    newtSetColor(NEWT_COLORSET_CUSTOM(COLOR_ERROR), "white", "color124");
    newtCls();
}

void
run_tui(sqlite3* db)
{
    draw_background();
    show_entities_form(db);
}

void
shutdown_tui()
{
    newtFinished();
    sdsfree(output_buffer);
}
