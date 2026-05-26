/*
 * Written by Alexey Tourbin <at@altlinux.org>.
 *
 * The author has dedicated the code to the public domain.  Anyone is free
 * to copy, modify, publish, use, compile, sell, or distribute the original
 * code, either in source code form or as a compiled binary, for any purpose,
 * commercial or non-commercial, and by any means.
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3ext.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include "pcre2.h"

SQLITE_EXTENSION_INIT1

typedef struct {
    char *s;
    pcre2_code_8 *p;
    pcre2_match_data_8 *m;
} cache_entry;

#ifndef CACHE_SIZE
#define CACHE_SIZE 16
#endif

static
void regexp(sqlite3_context *ctx, int argc, sqlite3_value **argv)
{
    const char unsigned *re, *str;
    pcre2_code_8 *p;
    pcre2_match_data_8 *m;

    assert(argc == 2);

    re = sqlite3_value_text(argv[0]);
    if (!re) {
        sqlite3_result_error(ctx, "no regexp", -1);
        return;
    }

    str = sqlite3_value_text(argv[1]);
    if (!str) {
        sqlite3_result_int(ctx, 0);
        return;
    }

    /* simple LRU cache */
    {
        int i;
        int found = 0;
        cache_entry *cache = sqlite3_user_data(ctx);

        assert(cache);

        for (i = 0; i < CACHE_SIZE && cache[i].s; i++)
            if (strcmp((char *) re, cache[i].s) == 0) {
                found = 1;
                break;
            }
        if (found) {
            if (i > 0) {
                cache_entry c = cache[i];
                memmove(cache + 1, cache, i * sizeof(cache_entry));
                cache[0] = c;
            }
        }
        else {
            cache_entry c;
            int err;
            size_t pos;
            c.p = pcre2_compile_8(re, PCRE2_ZERO_TERMINATED, 0, &err, &pos, NULL);
            if (!c.p) {
                PCRE2_UCHAR errbuf[256];
                pcre2_get_error_message_8(err, errbuf, sizeof(errbuf));
                char *e2 = sqlite3_mprintf("%s: %s (offset %d)", re, (char *) errbuf, (int) pos);
                sqlite3_result_error(ctx, e2, -1);
                sqlite3_free(e2);
                return;
            }
            c.m = pcre2_match_data_create_8(0, NULL);
            c.s = strdup((char *) re);
            if (!c.s) {
                sqlite3_result_error(ctx, "strdup: ENOMEM", -1);
                pcre2_code_free_8(c.p);
                pcre2_match_data_free_8(cache[i].m);
                return;
            }
            i = CACHE_SIZE - 1;
            if (cache[i].s) {
                free(cache[i].s);
                assert(cache[i].p);
                pcre2_code_free_8(cache[i].p);
                pcre2_match_data_free_8(cache[i].m);
            }
            memmove(cache + 1, cache, i * sizeof(cache_entry));
            cache[0] = c;
        }
        p = cache[0].p;
        m = cache[0].m;
    }

    {
        int rc;
        assert(p);
        rc = pcre2_match_8(p, str, PCRE2_ZERO_TERMINATED, 0, 0, m, NULL);
        sqlite3_result_int(ctx, rc > 0);
        return;
    }
}

static
void free_cache(void *ptr) {
    cache_entry *cache = (cache_entry *) ptr;
    for(size_t i = 0; i < CACHE_SIZE; i++) {
        free(cache[i].s);
        pcre2_code_free_8(cache[i].p);
        pcre2_match_data_free_8(cache[i].m);
    }
    free(cache);
}

int sqlite3_pcre2_init(sqlite3 *db, char **err, const sqlite3_api_routines *api)
{
	SQLITE_EXTENSION_INIT2(api)
	cache_entry *cache = calloc(CACHE_SIZE, sizeof(cache_entry));
	if (!cache) {
        if (err) {
            *err = "calloc: ENOMEM";
        }
	    return 1;
	}
	return sqlite3_create_function_v2(db, "REGEXP", 2, SQLITE_UTF8, cache, regexp, NULL, NULL, free_cache);
}
