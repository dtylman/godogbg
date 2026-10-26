/*
 * Host hooks the GNU Backgammon engine expects from the (excluded)
 * application layer: data directory lookup and error output.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 3 or later of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <glib.h>

#include "util.h"
#include "output.h"

char *pkg_datadir = NULL;

void
gnubg_port_set_datadir(const char *dir)
{
    g_free(pkg_datadir);
    pkg_datadir = g_strdup(dir);
}

extern char *
getPkgDataDir(void)
{
    if (!pkg_datadir)
        pkg_datadir = g_strdup(".");
    return pkg_datadir;
}

extern void
PrintError(const char *message)
{
    g_printerr("gnubg: %s\n", message);
}

extern void
outputerrv(const char *sz, va_list val)
{
    vfprintf(stderr, sz, val);
}

extern void
outputerrf(const char *sz, ...)
{
    va_list val;

    va_start(val, sz);
    outputerrv(sz, val);
    va_end(val);
}
