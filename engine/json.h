#ifndef MH_JSON_H
#define MH_JSON_H
#include <stddef.h>

typedef enum { JS_NULL, JS_BOOL, JS_NUM, JS_STR, JS_ARR, JS_OBJ } JsonType;

typedef struct Json Json;
struct Json {
    JsonType type;
    char    *key;          /* member name when this node sits in an object */
    char    *str;          /* JS_STR, UTF-8 */
    double   num;          /* JS_NUM, and 0/1 for JS_BOOL */
    Json    *child, *next;
};

Json       *json_parse(const char *utf8, char *err, size_t errCap);
void        json_free(Json *j);
Json       *json_get(const Json *obj, const char *key);
Json       *json_at(const Json *arr, int index);
int         json_count(const Json *j);
const char *json_str(const Json *j, const char *def);
int         json_int(const Json *j, int def);
int         json_bool(const Json *j, int def);

#endif
