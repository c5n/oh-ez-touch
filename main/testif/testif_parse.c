/**
 * @file testif_parse.c
 *
 * See testif_parse.h.
 *
 * No sdkconfig guard and no target branch: this is plain string handling, it is
 * linked into test/host/ as well as into the simulator, and there is nothing in
 * it that a device could not run if the interface ever reaches one.
 */
#include "testif_parse.h"

#include <string.h>

static bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

bool testif_parse(char *line, testif_cmd_t *out)
{
    memset(out, 0, sizeof(*out));

    if (line == NULL)
        return false;

    char *p = line;

    /* A trailing newline is not a token. Dropping it here rather than in each
     * comparison is what lets `nc -u` and the CLI send the same bytes. */
    size_t len = strlen(p);

    while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r'))
        p[--len] = '\0';

    while (*p != '\0')
    {
        while (is_space(*p) == true)
            p++;

        if (*p == '\0')
            break;

        char *token;

        if (*p == '"')
        {
            token = ++p;

            while (*p != '\0' && *p != '"')
                p++;
        }
        else
        {
            token = p;

            while (*p != '\0' && is_space(*p) == false)
                p++;
        }

        /* Remember whether there is more line before the terminator goes in. */
        bool more = (*p != '\0');

        *p = '\0';

        if (more == true)
            p++;

        /* The id, if there is one, is the first token and only the first: a
         * later `@foo` is an argument, which matters because a setting value
         * could begin with one. `@` alone carries no id. */
        if (out->argc == 0 && out->id == NULL && token[0] == '@' && token[1] != '\0')
        {
            out->id = token + 1;
            continue;
        }

        if (out->argc >= TESTIF_ARGV_MAX)
        {
            out->truncated = true;
            break;
        }

        out->argv[out->argc++] = token;
    }

    return out->argc > 0;
}
