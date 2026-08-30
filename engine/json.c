/* Just enough JSON for a config file. No streaming, no comments, no numbers
   larger than a double - the config is written by our own UI. */
#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    const char *p;
    char       *err;
    size_t      errCap;
    int         failed;
} P;

static Json *parse_value(P *s);

static void fail(P *s, const char *msg) {
    if (s->failed) return;
    s->failed = 1;
    if (s->err && s->errCap) {
        _snprintf(s->err, s->errCap - 1, "%s", msg);
        s->err[s->errCap - 1] = 0;
    }
}

static void skip_ws(P *s) {
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r') s->p++;
}

static Json *node(JsonType t) {
    Json *j = (Json *)calloc(1, sizeof(Json));
    if (j) j->type = t;
    return j;
}

static void utf8_put(char **out, unsigned cp) {
    char *o = *out;
    if (cp < 0x80) {
        *o++ = (char)cp;
    } else if (cp < 0x800) {
        *o++ = (char)(0xC0 | (cp >> 6));
        *o++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *o++ = (char)(0xE0 | (cp >> 12));
        *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *o++ = (char)(0xF0 | (cp >> 18));
        *o++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *o++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char)(0x80 | (cp & 0x3F));
    }
    *out = o;
}

static int hex4(const char *p, unsigned *out) {
    int i;
    unsigned v = 0;
    for (i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if      (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

/* Worst case one input byte becomes one output byte, so the remaining source
   length is always a safe allocation size. */
static char *parse_string(P *s) {
    const char *p = s->p;
    char *buf, *o;

    if (*p != '"') { fail(s, "expected a string"); return NULL; }
    p++;
    buf = (char *)malloc(strlen(p) + 1);
    if (!buf) { fail(s, "out of memory"); return NULL; }
    o = buf;

    while (*p && *p != '"') {
        if (*p != '\\') { *o++ = *p++; continue; }
        p++;
        switch (*p) {
        case '"':  *o++ = '"';  p++; break;
        case '\\': *o++ = '\\'; p++; break;
        case '/':  *o++ = '/';  p++; break;
        case 'b':  *o++ = '\b'; p++; break;
        case 'f':  *o++ = '\f'; p++; break;
        case 'n':  *o++ = '\n'; p++; break;
        case 'r':  *o++ = '\r'; p++; break;
        case 't':  *o++ = '\t'; p++; break;
        case 'u': {
            unsigned cp, lo;
            if (!hex4(p + 1, &cp)) { free(buf); fail(s, "bad \\u escape"); return NULL; }
            p += 5;
            if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u' &&
                hex4(p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                p += 6;
            }
            utf8_put(&o, cp);
            break;
        }
        default:
            free(buf);
            fail(s, "bad escape sequence");
            return NULL;
        }
    }
    if (*p != '"') { free(buf); fail(s, "unterminated string"); return NULL; }
    *o = 0;
    s->p = p + 1;
    return buf;
}

static Json *parse_container(P *s, int isObj) {
    Json *j = node(isObj ? JS_OBJ : JS_ARR);
    Json *tail = NULL;
    char close = isObj ? '}' : ']';

    if (!j) { fail(s, "out of memory"); return NULL; }
    s->p++;                                  /* consume the opening bracket */
    skip_ws(s);
    if (*s->p == close) { s->p++; return j; }

    for (;;) {
        Json *item;
        char *key = NULL;

        skip_ws(s);
        if (isObj) {
            key = parse_string(s);
            if (s->failed) { json_free(j); return NULL; }
            skip_ws(s);
            if (*s->p != ':') { free(key); json_free(j); fail(s, "expected a colon"); return NULL; }
            s->p++;
        }
        item = parse_value(s);
        if (!item) { free(key); json_free(j); return NULL; }
        item->key = key;

        if (tail) tail->next = item; else j->child = item;
        tail = item;

        skip_ws(s);
        if (*s->p == ',')   { s->p++; continue; }
        if (*s->p == close) { s->p++; return j; }
        json_free(j);
        fail(s, isObj ? "expected a comma or a closing brace"
                      : "expected a comma or a closing bracket");
        return NULL;
    }
}

static Json *parse_value(P *s) {
    skip_ws(s);
    switch (*s->p) {
    case '{': return parse_container(s, 1);
    case '[': return parse_container(s, 0);
    case '"': {
        Json *j = node(JS_STR);
        if (!j) { fail(s, "out of memory"); return NULL; }
        j->str = parse_string(s);
        if (s->failed) { json_free(j); return NULL; }
        return j;
    }
    case 't':
        if (strncmp(s->p, "true", 4) == 0) {
            Json *j = node(JS_BOOL);
            s->p += 4;
            if (j) j->num = 1;
            return j;
        }
        break;
    case 'f':
        if (strncmp(s->p, "false", 5) == 0) {
            Json *j = node(JS_BOOL);
            s->p += 5;
            return j;
        }
        break;
    case 'n':
        if (strncmp(s->p, "null", 4) == 0) { s->p += 4; return node(JS_NULL); }
        break;
    default: {
        char *end;
        double v = strtod(s->p, &end);
        if (end != s->p) {
            Json *j = node(JS_NUM);
            s->p = end;
            if (j) j->num = v;
            return j;
        }
    }
    }
    fail(s, "unexpected token");
    return NULL;
}

Json *json_parse(const char *utf8, char *err, size_t errCap) {
    P s;
    Json *root;

    if (err && errCap) err[0] = 0;
    s.p = utf8; s.err = err; s.errCap = errCap; s.failed = 0;

    /* tolerate a UTF-8 BOM - Notepad still writes them */
    if ((unsigned char)s.p[0] == 0xEF && (unsigned char)s.p[1] == 0xBB &&
        (unsigned char)s.p[2] == 0xBF) s.p += 3;

    root = parse_value(&s);
    if (!root) return NULL;
    skip_ws(&s);
    if (*s.p) {
        json_free(root);
        fail(&s, "trailing content after the top-level value");
        return NULL;
    }
    return root;
}

void json_free(Json *j) {
    while (j) {
        Json *next = j->next;
        json_free(j->child);
        free(j->key);
        free(j->str);
        free(j);
        j = next;
    }
}

Json *json_get(const Json *obj, const char *key) {
    Json *c;
    if (!obj || obj->type != JS_OBJ) return NULL;
    for (c = obj->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0) return c;
    return NULL;
}

Json *json_at(const Json *arr, int index) {
    Json *c;
    if (!arr) return NULL;
    for (c = arr->child; c; c = c->next)
        if (index-- == 0) return c;
    return NULL;
}

int json_count(const Json *j) {
    int n = 0;
    Json *c;
    if (!j) return 0;
    for (c = j->child; c; c = c->next) n++;
    return n;
}

const char *json_str(const Json *j, const char *def) {
    return (j && j->type == JS_STR && j->str) ? j->str : def;
}

int json_int(const Json *j, int def) {
    return (j && j->type == JS_NUM) ? (int)j->num : def;
}

int json_bool(const Json *j, int def) {
    if (!j) return def;
    if (j->type == JS_BOOL) return j->num != 0;
    if (j->type == JS_NUM)  return j->num != 0;
    return def;
}
