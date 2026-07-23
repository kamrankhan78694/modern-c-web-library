/*
 * worker_d1.c - Cloudflare D1 Database Bindings
 *
 * Provides a pure-C abstraction over Cloudflare D1 (SQLite-based)
 * database.  Mirrors the D1 Workers API: prepare, bind, run, first,
 * all, batch.  For native/test builds an in-memory table simulator
 * is used.
 *
 * D1 API surface (mirrors Cloudflare D1 Workers binding):
 *   - prepare(sql)     → prepared statement
 *   - bind(stmt, ...)  → bind parameters to placeholders
 *   - run(stmt)        → execute with metadata
 *   - first(stmt)      → return first row
 *   - all(stmt)        → return all rows as JSON
 *   - batch(stmts[])   → atomic batch execution
 *   - exec(sql)        → raw SQL execution
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Portable case-insensitive substring search (replaces POSIX strcasestr) */
static const char *_ci_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        bool match = true;
        for (size_t i = 0; i < nlen; i++) {
            if (!haystack[i] ||
                tolower((unsigned char)haystack[i]) !=
                tolower((unsigned char)needle[i])) {
                match = false;
                break;
            }
        }
        if (match) return haystack;
    }
    return NULL;
}

/* ===== Internal types ===== */

#define D1_MAX_PARAMS       100
#define D1_MAX_COLUMNS      32
#define D1_MAX_ROWS         1024
#define D1_MAX_SQL_LEN      8192
#define D1_MAX_TABLES       32
#define D1_MAX_COL_NAME     64
#define D1_MAX_CELL_LEN     1024

typedef struct {
    char name[D1_MAX_COL_NAME];
} d1_column_def_t;

typedef struct {
    char cells[D1_MAX_COLUMNS][D1_MAX_CELL_LEN];
} d1_row_data_t;

typedef struct {
    char            name[D1_MAX_COL_NAME];
    d1_column_def_t columns[D1_MAX_COLUMNS];
    int             column_count;
    d1_row_data_t   rows[D1_MAX_ROWS];
    int             row_count;
} d1_table_t;

struct worker_d1 {
    char        *database_name;
    d1_table_t   tables[D1_MAX_TABLES];
    int           table_count;
    int           changes;     /* rows affected by last write */
};

struct worker_d1_stmt {
    worker_d1_t *db;
    char         sql[D1_MAX_SQL_LEN];
    char        *params[D1_MAX_PARAMS];
    int          param_count;
};

/* ===== Lifecycle ===== */

worker_d1_t *worker_d1_create(const char *database_name) {
    if (!database_name) return NULL;
    worker_d1_t *db = calloc(1, sizeof(worker_d1_t));
    if (!db) return NULL;
    db->database_name = strdup(database_name);
    if (!db->database_name) { free(db); return NULL; }
    return db;
}

void worker_d1_destroy(worker_d1_t *db) {
    if (!db) return;
    free(db->database_name);
    free(db);
}

const char *worker_d1_get_name(const worker_d1_t *db) {
    return db ? db->database_name : NULL;
}

/* ===== Internal helpers ===== */

static d1_table_t *_d1_find_table(worker_d1_t *db, const char *name) {
    for (int i = 0; i < db->table_count; i++) {
        if (strcasecmp(db->tables[i].name, name) == 0) {
            return &db->tables[i];
        }
    }
    return NULL;
}

/* Skip whitespace */
static const char *_skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Case-insensitive prefix match */
static bool _prefix_ci(const char *str, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix))
            return false;
        str++;
        prefix++;
    }
    return true;
}

/* Parse a word (identifier) from input into buf */
static const char *_parse_word(const char *s, char *buf, size_t bufsize) {
    s = _skip_ws(s);
    size_t i = 0;
    while (*s && !isspace((unsigned char)*s) && *s != '(' && *s != ',' &&
           *s != ')' && *s != ';' && i < bufsize - 1) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
    return s;
}

/* ===== Statement API ===== */

worker_d1_stmt_t *worker_d1_prepare(worker_d1_t *db, const char *sql) {
    if (!db || !sql) return NULL;
    if (strlen(sql) >= D1_MAX_SQL_LEN) return NULL;

    worker_d1_stmt_t *stmt = calloc(1, sizeof(worker_d1_stmt_t));
    if (!stmt) return NULL;
    stmt->db = db;
    strncpy(stmt->sql, sql, D1_MAX_SQL_LEN - 1);
    return stmt;
}

void worker_d1_stmt_destroy(worker_d1_stmt_t *stmt) {
    if (!stmt) return;
    for (int i = 0; i < stmt->param_count; i++) {
        free(stmt->params[i]);
    }
    free(stmt);
}

int worker_d1_stmt_bind(worker_d1_stmt_t *stmt, int index,
                        const char *value) {
    char *new_value = NULL;

    if (!stmt) return -1;
    if (index < 1 || index > D1_MAX_PARAMS) return -1;

    int idx = index - 1;  /* 1-based to 0-based */

    if (value) {
        new_value = strdup(value);
        if (!new_value) return -1;
    }

    free(stmt->params[idx]);
    stmt->params[idx] = new_value;
    if (idx >= stmt->param_count) stmt->param_count = idx + 1;
    return 0;
}

/* ===== SQL Execution Engine (simplified in-memory) ===== */

/*
 * This is a simplified SQL engine that supports:
 *   CREATE TABLE name (col1, col2, ...)
 *   INSERT INTO name (col1, col2) VALUES (?, ?)
 *   SELECT * FROM name [WHERE col = ?]
 *   DELETE FROM name [WHERE col = ?]
 *   DROP TABLE name
 */

static worker_d1_result_t *_d1_exec_create_table(worker_d1_t *db,
                                                  const char *sql) {
    /* CREATE TABLE name (col1, col2, ...) */
    const char *p = sql;
    p = _skip_ws(p);
    /* Skip "CREATE TABLE" */
    p += 12;
    if (_prefix_ci(p, " IF NOT EXISTS")) p += 14;

    char tname[D1_MAX_COL_NAME];
    p = _parse_word(p, tname, sizeof(tname));

    if (_d1_find_table(db, tname)) {
        /* Table already exists — not an error for IF NOT EXISTS */
        worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
        if (r) { r->success = true; r->meta_changes = 0; }
        return r;
    }

    if (db->table_count >= D1_MAX_TABLES) return NULL;

    d1_table_t *t = &db->tables[db->table_count];
    memset(t, 0, sizeof(d1_table_t));
    strncpy(t->name, tname, D1_MAX_COL_NAME - 1);

    /* Parse columns from parentheses */
    p = strchr(p, '(');
    if (p) {
        p++;
        while (*p && *p != ')' && t->column_count < D1_MAX_COLUMNS) {
            char col[D1_MAX_COL_NAME];
            p = _parse_word(p, col, sizeof(col));
            if (col[0]) {
                strncpy(t->columns[t->column_count].name, col,
                        D1_MAX_COL_NAME - 1);
                t->column_count++;
            }
            /* Skip type annotations and constraints */
            while (*p && *p != ',' && *p != ')') p++;
            if (*p == ',') p++;
        }
    }

    db->table_count++;
    worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
    if (r) { r->success = true; r->meta_changes = 0; }
    return r;
}

static worker_d1_result_t *_d1_exec_insert(worker_d1_t *db, const char *sql,
                                           char **params, int param_count) {
    /* INSERT INTO name (cols) VALUES (?,?,...) */
    const char *p = sql;
    p = _skip_ws(p);
    p += 11;  /* Skip "INSERT INTO" */

    char tname[D1_MAX_COL_NAME];
    p = _parse_word(p, tname, sizeof(tname));

    d1_table_t *t = _d1_find_table(db, tname);
    if (!t) return NULL;
    if (t->row_count >= D1_MAX_ROWS) return NULL;

    /* Determine the column mapping. If the statement names columns --
     * INSERT INTO t (b, a) VALUES (?, ?) -- bind each parameter to the NAMED
     * column, not to physical order. Without a column list, bind positionally
     * (SQL default: all columns in declared order). */
    int col_map[D1_MAX_COLUMNS];
    int named_count = -1;   /* -1 => no explicit column list */

    const char *vals = _ci_strstr(p, "VALUES");
    const char *open = strchr(p, '(');
    if (open && (!vals || open < vals)) {
        named_count = 0;
        const char *q = open + 1;
        while (*q && *q != ')' && named_count < D1_MAX_COLUMNS) {
            char col[D1_MAX_COL_NAME];
            q = _parse_word(q, col, sizeof(col));
            if (col[0]) {
                int idx = -1;
                for (int c = 0; c < t->column_count; c++) {
                    if (strcasecmp(t->columns[c].name, col) == 0) { idx = c; break; }
                }
                if (idx < 0) {
                    /* Unknown column named in the INSERT: fail rather than
                     * writing the value to the wrong cell. */
                    worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
                    if (r) { r->success = false; }
                    return r;
                }
                col_map[named_count++] = idx;
            }
            while (*q && *q != ',' && *q != ')') q++;
            if (*q == ',') q++;
        }
    }

    /* With an explicit column list, the number of bound parameters must match
     * the number of named columns, and each must be bound: extra binds would be
     * silently dropped and missing binds would leave empty cells -- both hide
     * bugs. (Positional inserts keep the lenient behaviour.) */
    if (named_count >= 0) {
        if (param_count != named_count) {
            worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
            if (r) { r->success = false; }
            return r;
        }
        for (int i = 0; i < named_count; i++) {
            if (!params[i]) {
                worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
                if (r) { r->success = false; }
                return r;
            }
        }
    }

    d1_row_data_t *row = &t->rows[t->row_count];
    memset(row, 0, sizeof(d1_row_data_t));

    if (named_count >= 0) {
        /* Bind each parameter to its named column (validated 1:1 above). */
        for (int i = 0; i < named_count; i++) {
            strncpy(row->cells[col_map[i]], params[i], D1_MAX_CELL_LEN - 1);
        }
    } else {
        /* No column list: positional binding to physical columns. */
        for (int i = 0; i < param_count && i < t->column_count; i++) {
            if (params[i]) {
                strncpy(row->cells[i], params[i], D1_MAX_CELL_LEN - 1);
            }
        }
    }
    t->row_count++;
    db->changes = 1;

    worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
    if (r) { r->success = true; r->meta_changes = 1; }
    return r;
}

static worker_d1_result_t *_d1_exec_select(worker_d1_t *db, const char *sql,
                                           char **params, int param_count) {
    /* SELECT * FROM name [WHERE col = ?] */
    const char *p = sql;
    p = _skip_ws(p);

    /* Find "FROM" */
    const char *from = _ci_strstr(p, "FROM");
    if (!from) return NULL;
    from += 4;

    char tname[D1_MAX_COL_NAME];
    from = _parse_word(from, tname, sizeof(tname));

    d1_table_t *t = _d1_find_table(db, tname);
    if (!t) return NULL;

    /* Check for WHERE clause. Search only PAST the table name, so a table whose
     * name contains "where" (e.g. "elsewhere", "nowhere") is not misdetected as
     * having a WHERE clause -- which the fail-closed guard would then refuse. */
    const char *where = _ci_strstr(from, "WHERE");
    int where_col = -1;
    if (where) {
        where += 5;
        char wcol[D1_MAX_COL_NAME];
        _parse_word(where, wcol, sizeof(wcol));
        for (int c = 0; c < t->column_count; c++) {
            if (strcasecmp(t->columns[c].name, wcol) == 0) {
                where_col = c;
                break;
            }
        }
        /* A WHERE clause that cannot be evaluated (unknown column or no bound
         * parameter) must fail, not silently return every row. */
        if (where_col < 0 || param_count <= 0 || !params[0]) {
            worker_d1_result_t *err = calloc(1, sizeof(worker_d1_result_t));
            if (err) { err->success = false; }
            return err;
        }
    }

    /* Build result as JSON array */
    worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
    if (!r) return NULL;
    r->success = true;

    json_value_t *arr = json_array_create();
    if (!arr) {
        free(r);
        return NULL;
    }

    for (int i = 0; i < t->row_count; i++) {
        /* Apply WHERE filter (where_col >= 0 implies a valid bound param). */
        if (where_col >= 0) {
            if (strcmp(t->rows[i].cells[where_col], params[0]) != 0)
                continue;
        }

        json_value_t *row_obj = json_object_create();
        for (int c = 0; c < t->column_count; c++) {
            json_object_set(row_obj, t->columns[c].name,
                           json_string_create(t->rows[i].cells[c]));
        }
        json_array_append(arr, row_obj);
    }

    r->results = arr;
    return r;
}

static worker_d1_result_t *_d1_exec_delete(worker_d1_t *db, const char *sql,
                                           char **params, int param_count) {
    /* DELETE FROM name [WHERE col = ?] */
    const char *p = sql;
    p = _skip_ws(p);
    p += 11;  /* Skip "DELETE FROM" */

    char tname[D1_MAX_COL_NAME];
    p = _parse_word(p, tname, sizeof(tname));

    d1_table_t *t = _d1_find_table(db, tname);
    if (!t) return NULL;

    /* Search for WHERE only past the table name (see the SELECT note): a table
     * named "elsewhere"/"nowhere" must not be misread as having a WHERE clause. */
    const char *where = _ci_strstr(p, "WHERE");
    int where_col = -1;
    if (where) {
        where += 5;
        char wcol[D1_MAX_COL_NAME];
        _parse_word(where, wcol, sizeof(wcol));
        for (int c = 0; c < t->column_count; c++) {
            if (strcasecmp(t->columns[c].name, wcol) == 0) {
                where_col = c;
                break;
            }
        }
        /* A WHERE clause was given but cannot be evaluated (unknown column or no
         * bound parameter). Refuse the statement rather than silently deleting
         * EVERY row -- a "WHERE bogus = ?" must never become DELETE-all. */
        if (where_col < 0 || param_count <= 0 || !params[0]) {
            worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
            if (r) { r->success = false; r->meta_changes = 0; }
            return r;
        }
    }

    int deleted = 0;
    for (int i = t->row_count - 1; i >= 0; i--) {
        /* No WHERE => delete all (intentional); otherwise filter by the
         * resolved predicate. */
        bool match = (where_col < 0)
                     ? true
                     : (strcmp(t->rows[i].cells[where_col], params[0]) == 0);
        if (match) {
            /* Shift rows */
            for (int j = i; j < t->row_count - 1; j++) {
                t->rows[j] = t->rows[j + 1];
            }
            t->row_count--;
            deleted++;
        }
    }

    db->changes = deleted;
    worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
    if (r) { r->success = true; r->meta_changes = deleted; }
    return r;
}

static worker_d1_result_t *_d1_exec_drop(worker_d1_t *db, const char *sql) {
    const char *p = sql;
    p = _skip_ws(p);
    p += 10;  /* Skip "DROP TABLE" */
    if (_prefix_ci(p, " IF EXISTS")) p += 10;

    char tname[D1_MAX_COL_NAME];
    _parse_word(p, tname, sizeof(tname));

    for (int i = 0; i < db->table_count; i++) {
        if (strcasecmp(db->tables[i].name, tname) == 0) {
            for (int j = i; j < db->table_count - 1; j++) {
                db->tables[j] = db->tables[j + 1];
            }
            db->table_count--;
            worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
            if (r) { r->success = true; r->meta_changes = 0; }
            return r;
        }
    }

    /* Table not found — success for IF EXISTS */
    worker_d1_result_t *r = calloc(1, sizeof(worker_d1_result_t));
    if (r) { r->success = true; r->meta_changes = 0; }
    return r;
}

/* Dispatch SQL to the appropriate handler */
static worker_d1_result_t *_d1_execute(worker_d1_t *db, const char *sql,
                                       char **params, int param_count) {
    const char *s = _skip_ws(sql);

    if (_prefix_ci(s, "CREATE TABLE"))
        return _d1_exec_create_table(db, s);
    if (_prefix_ci(s, "INSERT INTO"))
        return _d1_exec_insert(db, s, params, param_count);
    if (_prefix_ci(s, "SELECT"))
        return _d1_exec_select(db, s, params, param_count);
    if (_prefix_ci(s, "DELETE FROM"))
        return _d1_exec_delete(db, s, params, param_count);
    if (_prefix_ci(s, "DROP TABLE"))
        return _d1_exec_drop(db, s);

    /* Unsupported statement */
    return NULL;
}

/* ===== Public execution API ===== */

worker_d1_result_t *worker_d1_stmt_run(worker_d1_stmt_t *stmt) {
    if (!stmt || !stmt->db) return NULL;
    return _d1_execute(stmt->db, stmt->sql, stmt->params, stmt->param_count);
}

json_value_t *worker_d1_stmt_first(worker_d1_stmt_t *stmt) {
    worker_d1_result_t *r = worker_d1_stmt_run(stmt);
    if (!r) return NULL;

    json_value_t *first = NULL;
    if (r->results) {
        /* Get first element from array */
        first = json_array_get(r->results, 0);
        if (first) {
            /* Detach from array — caller owns it */
            char *str = json_stringify(first);
            first = json_parse(str);
            free(str);
        }
    }

    worker_d1_result_destroy(r);
    return first;
}

json_value_t *worker_d1_stmt_all(worker_d1_stmt_t *stmt) {
    worker_d1_result_t *r = worker_d1_stmt_run(stmt);
    if (!r) return NULL;

    json_value_t *results = r->results;
    r->results = NULL;  /* Transfer ownership */
    worker_d1_result_destroy(r);
    return results;
}

worker_d1_result_t *worker_d1_exec(worker_d1_t *db, const char *sql) {
    if (!db || !sql) return NULL;
    return _d1_execute(db, sql, NULL, 0);
}

worker_d1_result_t **worker_d1_batch(worker_d1_t *db,
                                     worker_d1_stmt_t **stmts, int count,
                                     int *out_count) {
    if (!db || !stmts || count <= 0) return NULL;

    worker_d1_result_t **results = calloc((size_t)count,
                                          sizeof(worker_d1_result_t *));
    if (!results) return NULL;

    for (int i = 0; i < count; i++) {
        if (stmts[i] && stmts[i]->db == db) {
            results[i] = worker_d1_stmt_run(stmts[i]);
        }
        if (!results[i]) {
            /* Batch failure — clean up */
            for (int j = 0; j < i; j++) {
                worker_d1_result_destroy(results[j]);
            }
            free(results);
            if (out_count) *out_count = 0;
            return NULL;
        }
    }

    if (out_count) *out_count = count;
    return results;
}

void worker_d1_result_destroy(worker_d1_result_t *result) {
    if (!result) return;
    if (result->results) json_value_free(result->results);
    free(result);
}
