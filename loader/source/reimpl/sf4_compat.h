/*
 * sf4-vita: extra bionic imports, see sf4_compat.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SF4_COMPAT_H
#define SF4_COMPAT_H

#include <stdio.h>
#include <so_util/so_util.h>

// Table entries for dynlib.c, placed before default_dynlib so they take priority.
extern so_default_dynlib sf4_dynlib[];
extern const int sf4_dynlib_size;   // in bytes, like so_resolve expects
extern FILE *sf4_stderr;

#endif
