/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2006 Carl Worth
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef SKIPLIST_H
#define SKIPLIST_H

#include "cairoint.h"

#define MAX_LEVEL   31

/*
 * Skip list element. In order to use the skip list, the caller must
 * generate a structure for list elements that has as its final member
 * a skip_elt_t, (which will be allocated with variable size).
 *
 * The caller must also pass the size of the structure to
 * skip_list_init.
 */
typedef struct _skip_elt {
    int prev_index;
    struct _skip_elt *prev;
    struct _skip_elt *next[1];
} skip_elt_t;

#define SKIP_LIST_ELT_TO_DATA(type, elt) ((type *) ((char *) (elt) - (sizeof (type) - sizeof (skip_elt_t))))

typedef int
(*skip_list_compare_t) (void *list, void *a, void *b);

typedef struct _skip_list {
    skip_list_compare_t compare;
    size_t elt_size;
    size_t data_size;
    skip_elt_t *chains[MAX_LEVEL];
    skip_elt_t *freelists[MAX_LEVEL];
    int		max_level;
} skip_list_t;

/* Initialize a new skip list. The compare function accepts a pointer
 * to the list as well as pointers to two elements. The function must
 * return a value greater than zero, zero, or less then 0 if the first
 * element is considered respectively greater than, equal to, or less
 * than the second element. The size of each object, (as computed by
 * sizeof) is passed for elt_size. Note that the structure used for
 * list elements must have as its final member a skip_elt_t
 */
cairo_private void
skip_list_init (skip_list_t		*list,
		skip_list_compare_t	 compare,
		size_t			 elt_size);


/* Deallocate resources associated with a skip list and all elements
 * in it. (XXX: currently this simply deletes all elements.)
 */
cairo_private void
skip_list_fini (skip_list_t		*list);

/* Insert a new element into the list at the correct sort order as
 * determined by compare. If unique is true, then duplicate elements
 * are ignored and the already inserted element is returned.
 * Otherwise data will be copied (elt_size bytes from <data> via
 * memcpy) and the new element is returned. */
cairo_private void *
skip_list_insert (skip_list_t *list, void *data, int unique);

/* Find an element which compare considers equal to <data> */
cairo_private void *
skip_list_find (skip_list_t *list, void *data);

/* Delete an element which compare considers equal to <data> */
cairo_private void
skip_list_delete (skip_list_t *list, void *data);

/* Delete the given element from the list. */
cairo_private void
skip_list_delete_given (skip_list_t *list, skip_elt_t *given);

#endif
