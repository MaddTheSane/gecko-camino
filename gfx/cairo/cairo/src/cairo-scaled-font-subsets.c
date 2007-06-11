/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2003 University of Southern California
 * Copyright © 2005 Red Hat, Inc
 * Copyright © 2006 Keith Packard
 * Copyright © 2006 Red Hat, Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is University of Southern
 * California.
 *
 * Contributor(s):
 *	Carl D. Worth <cworth@cworth.org>
 *	Kristian Høgsberg <krh@redhat.com>
 *	Keith Packard <keithp@keithp.com>
 */

#include "cairoint.h"
#include "cairo-scaled-font-subsets-private.h"

#define MAX_GLYPHS_PER_SIMPLE_FONT 256
#define MAX_GLYPHS_PER_COMPOSITE_FONT 65536

typedef enum {
    CAIRO_SUBSETS_SCALED,
    CAIRO_SUBSETS_SIMPLE,
    CAIRO_SUBSETS_COMPOSITE
} cairo_subsets_type_t;

struct _cairo_scaled_font_subsets {
    cairo_subsets_type_t type;

    int max_glyphs_per_unscaled_subset_used;
    cairo_hash_table_t *unscaled_sub_fonts;

    int max_glyphs_per_scaled_subset_used;
    cairo_hash_table_t *scaled_sub_fonts;

    int num_sub_fonts;
};

typedef struct _cairo_sub_font {
    cairo_hash_entry_t base;

    cairo_bool_t is_scaled;
    cairo_bool_t is_composite;
    cairo_scaled_font_subsets_t *parent;
    cairo_scaled_font_t *scaled_font;
    unsigned int font_id;

    int current_subset;
    int num_glyphs_in_current_subset;
    int max_glyphs_per_subset;

    cairo_hash_table_t *sub_font_glyphs;
} cairo_sub_font_t;

typedef struct _cairo_sub_font_glyph {
    cairo_hash_entry_t base;

    unsigned int subset_id;
    unsigned int subset_glyph_index;
    double       x_advance;
} cairo_sub_font_glyph_t;

typedef struct _cairo_sub_font_collection {
    unsigned long *glyphs; /* scaled_font_glyph_index */
    unsigned int glyphs_size;
    unsigned int max_glyph;
    unsigned int num_glyphs;

    unsigned int subset_id;

    cairo_scaled_font_subset_callback_func_t font_subset_callback;
    void *font_subset_callback_closure;
} cairo_sub_font_collection_t;

static void
_cairo_sub_font_glyph_init_key (cairo_sub_font_glyph_t  *sub_font_glyph,
				unsigned long		 scaled_font_glyph_index)
{
    sub_font_glyph->base.hash = scaled_font_glyph_index;
}

static cairo_bool_t
_cairo_sub_font_glyphs_equal (const void *key_a, const void *key_b)
{
    const cairo_sub_font_glyph_t *sub_font_glyph_a = key_a;
    const cairo_sub_font_glyph_t *sub_font_glyph_b = key_b;

    return sub_font_glyph_a->base.hash == sub_font_glyph_b->base.hash;
}

static cairo_sub_font_glyph_t *
_cairo_sub_font_glyph_create (unsigned long	scaled_font_glyph_index,
			      unsigned int	subset_id,
			      unsigned int	subset_glyph_index,
                              double            x_advance)
{
    cairo_sub_font_glyph_t *sub_font_glyph;

    sub_font_glyph = malloc (sizeof (cairo_sub_font_glyph_t));
    if (sub_font_glyph == NULL)
	return NULL;

    _cairo_sub_font_glyph_init_key (sub_font_glyph, scaled_font_glyph_index);
    sub_font_glyph->subset_id = subset_id;
    sub_font_glyph->subset_glyph_index = subset_glyph_index;
    sub_font_glyph->x_advance = x_advance;

    return sub_font_glyph;
}

static void
_cairo_sub_font_glyph_destroy (cairo_sub_font_glyph_t *sub_font_glyph)
{
    free (sub_font_glyph);
}

static void
_cairo_sub_font_glyph_pluck (void *entry, void *closure)
{
    cairo_sub_font_glyph_t *sub_font_glyph = entry;
    cairo_hash_table_t *sub_font_glyphs = closure;

    _cairo_hash_table_remove (sub_font_glyphs, &sub_font_glyph->base);
    _cairo_sub_font_glyph_destroy (sub_font_glyph);
}

static void
_cairo_sub_font_glyph_collect (void *entry, void *closure)
{
    cairo_sub_font_glyph_t *sub_font_glyph = entry;
    cairo_sub_font_collection_t *collection = closure;
    unsigned long scaled_font_glyph_index;
    unsigned int subset_glyph_index;

    if (sub_font_glyph->subset_id != collection->subset_id)
	return;

    scaled_font_glyph_index = sub_font_glyph->base.hash;
    subset_glyph_index = sub_font_glyph->subset_glyph_index;

    /* Ensure we don't exceed the allocated bounds. */
    assert (subset_glyph_index < collection->glyphs_size);

    collection->glyphs[subset_glyph_index] = scaled_font_glyph_index;
    if (subset_glyph_index > collection->max_glyph)
	collection->max_glyph = subset_glyph_index;

    collection->num_glyphs++;
}

static cairo_bool_t
_cairo_sub_fonts_equal (const void *key_a, const void *key_b)
{
    const cairo_sub_font_t *sub_font_a = key_a;
    const cairo_sub_font_t *sub_font_b = key_b;

    if (sub_font_a->is_scaled)
        return sub_font_a->scaled_font == sub_font_b->scaled_font;
    else
        return sub_font_a->scaled_font->font_face == sub_font_b->scaled_font->font_face;
}

static void
_cairo_sub_font_init_key (cairo_sub_font_t	*sub_font,
			  cairo_scaled_font_t	*scaled_font)
{
    if (sub_font->is_scaled)
    {
        sub_font->base.hash = (unsigned long) scaled_font;
        sub_font->scaled_font = scaled_font;
    }
    else
    {
        sub_font->base.hash = (unsigned long) scaled_font->font_face;
        sub_font->scaled_font = scaled_font;
    }
}

static cairo_sub_font_t *
_cairo_sub_font_create (cairo_scaled_font_subsets_t	*parent,
			cairo_scaled_font_t		*scaled_font,
			unsigned int			 font_id,
			int				 max_glyphs_per_subset,
                        cairo_bool_t                     is_scaled,
                        cairo_bool_t                     is_composite)
{
    cairo_sub_font_t *sub_font;

    sub_font = malloc (sizeof (cairo_sub_font_t));
    if (sub_font == NULL)
	return NULL;

    sub_font->is_scaled = is_scaled;
    sub_font->is_composite = is_composite;
    _cairo_sub_font_init_key (sub_font, scaled_font);

    sub_font->parent = parent;
    sub_font->scaled_font = scaled_font;
    sub_font->font_id = font_id;

    sub_font->current_subset = 0;
    sub_font->num_glyphs_in_current_subset = 0;
    sub_font->max_glyphs_per_subset = max_glyphs_per_subset;

    sub_font->sub_font_glyphs = _cairo_hash_table_create (_cairo_sub_font_glyphs_equal);
    if (! sub_font->sub_font_glyphs) {
	free (sub_font);
	return NULL;
    }

    if (parent->type != CAIRO_SUBSETS_SCALED) {
        /* Reserve first glyph in subset for the .notdef glyph */
        sub_font->num_glyphs_in_current_subset++;
    }

    return sub_font;
}

static void
_cairo_sub_font_destroy (cairo_sub_font_t *sub_font)
{
    _cairo_hash_table_foreach (sub_font->sub_font_glyphs,
			       _cairo_sub_font_glyph_pluck,
			       sub_font->sub_font_glyphs);
    _cairo_hash_table_destroy (sub_font->sub_font_glyphs);
    cairo_scaled_font_destroy (sub_font->scaled_font);
    free (sub_font);
}

static void
_cairo_sub_font_pluck (void *entry, void *closure)
{
    cairo_sub_font_t *sub_font = entry;
    cairo_hash_table_t *sub_fonts = closure;

    _cairo_hash_table_remove (sub_fonts, &sub_font->base);
    _cairo_sub_font_destroy (sub_font);
}

static cairo_status_t
_cairo_sub_font_lookup_glyph (cairo_sub_font_t	                *sub_font,
                              unsigned long	                 scaled_font_glyph_index,
                              cairo_scaled_font_subsets_glyph_t *subset_glyph)
{
    cairo_sub_font_glyph_t key, *sub_font_glyph;

    _cairo_sub_font_glyph_init_key (&key, scaled_font_glyph_index);
    if (_cairo_hash_table_lookup (sub_font->sub_font_glyphs, &key.base,
				    (cairo_hash_entry_t **) &sub_font_glyph))
    {
        subset_glyph->font_id = sub_font->font_id;
        subset_glyph->subset_id = sub_font_glyph->subset_id;
        subset_glyph->subset_glyph_index = sub_font_glyph->subset_glyph_index;
        subset_glyph->is_scaled = sub_font->is_scaled;
        subset_glyph->is_composite = sub_font->is_composite;
        subset_glyph->x_advance = sub_font_glyph->x_advance;

        return CAIRO_STATUS_SUCCESS;
    }

    return CAIRO_STATUS_NULL_POINTER;
}

static cairo_status_t
_cairo_sub_font_map_glyph (cairo_sub_font_t	*sub_font,
			   unsigned long	 scaled_font_glyph_index,
                           cairo_scaled_font_subsets_glyph_t *subset_glyph)
{
    cairo_sub_font_glyph_t key, *sub_font_glyph;
    cairo_status_t status;
    cairo_scaled_glyph_t *scaled_glyph;

    _cairo_sub_font_glyph_init_key (&key, scaled_font_glyph_index);
    if (! _cairo_hash_table_lookup (sub_font->sub_font_glyphs, &key.base,
				    (cairo_hash_entry_t **) &sub_font_glyph))
    {
	if (sub_font->num_glyphs_in_current_subset == sub_font->max_glyphs_per_subset)
	{
	    sub_font->current_subset++;
	    sub_font->num_glyphs_in_current_subset = 0;

            if (sub_font->parent->type != CAIRO_SUBSETS_SCALED) {
                /* Reserve first glyph in subset for the .notdef glyph */
                sub_font->num_glyphs_in_current_subset++;
            }
	}

        status = _cairo_scaled_glyph_lookup (sub_font->scaled_font,
                                             scaled_font_glyph_index,
                                             CAIRO_SCALED_GLYPH_INFO_METRICS,
                                             &scaled_glyph);
	if (status)
	    return status;

        sub_font_glyph = _cairo_sub_font_glyph_create (scaled_font_glyph_index,
						       sub_font->current_subset,
						       sub_font->num_glyphs_in_current_subset++,
                                                       scaled_glyph->metrics.x_advance);
	if (sub_font_glyph == NULL)
	    return CAIRO_STATUS_NO_MEMORY;

        if (sub_font->is_scaled)
        {
            if (sub_font->num_glyphs_in_current_subset > sub_font->parent->max_glyphs_per_scaled_subset_used)
                sub_font->parent->max_glyphs_per_scaled_subset_used = sub_font->num_glyphs_in_current_subset;
        }
        else
        {
            if (sub_font->num_glyphs_in_current_subset > sub_font->parent->max_glyphs_per_unscaled_subset_used)
                sub_font->parent->max_glyphs_per_unscaled_subset_used = sub_font->num_glyphs_in_current_subset;
        }

	status = _cairo_hash_table_insert (sub_font->sub_font_glyphs, &sub_font_glyph->base);
	if (status) {
	    _cairo_sub_font_glyph_destroy (sub_font_glyph);
	    return status;
	}
    }

    subset_glyph->font_id = sub_font->font_id;
    subset_glyph->subset_id = sub_font_glyph->subset_id;
    subset_glyph->subset_glyph_index = sub_font_glyph->subset_glyph_index;
    subset_glyph->is_scaled = sub_font->is_scaled;
    subset_glyph->is_composite = sub_font->is_composite;
    subset_glyph->x_advance = sub_font_glyph->x_advance;

    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_sub_font_collect (void *entry, void *closure)
{
    cairo_sub_font_t *sub_font = entry;
    cairo_sub_font_collection_t *collection = closure;
    cairo_scaled_font_subset_t subset;
    int i;
    unsigned int j;

    for (i = 0; i <= sub_font->current_subset; i++) {
	collection->subset_id = i;

        if (sub_font->parent->type == CAIRO_SUBSETS_SCALED) {
            collection->num_glyphs = 0;
            collection->max_glyph = 0;
        } else {
            /* Assign .notdef glyph to the first glyph in the subset */
            collection->glyphs[0] = 0;
            collection->num_glyphs = 1;
            collection->max_glyph = 0;
        }

	_cairo_hash_table_foreach (sub_font->sub_font_glyphs,
				   _cairo_sub_font_glyph_collect, collection);

        /* Ensure the resulting array has no uninitialized holes */
	assert (collection->num_glyphs == collection->max_glyph + 1);

	subset.scaled_font = sub_font->scaled_font;
	subset.is_composite = sub_font->is_composite;
	subset.font_id = sub_font->font_id;
	subset.subset_id = i;
	subset.glyphs = collection->glyphs;
	subset.num_glyphs = collection->num_glyphs;
        /* No need to check for out of memory here. If to_unicode is NULL, the PDF
         * surface does not emit an ToUnicode stream */
        subset.to_unicode = malloc (collection->num_glyphs*sizeof(unsigned long));
        if (subset.to_unicode) {
            for (j = 0; j < collection->num_glyphs; j++) {
                /* default unicode character required when mapping fails */
                subset.to_unicode[j] = 0xfffd;
            }
        }
        (collection->font_subset_callback) (&subset,
					    collection->font_subset_callback_closure);

        if (subset.to_unicode != NULL)
            free (subset.to_unicode);
    }
}

static cairo_scaled_font_subsets_t *
_cairo_scaled_font_subsets_create_internal (cairo_subsets_type_t type)
{
    cairo_scaled_font_subsets_t *subsets;

    subsets = malloc (sizeof (cairo_scaled_font_subsets_t));
    if (subsets == NULL)
	return NULL;

    subsets->type = type;
    subsets->max_glyphs_per_unscaled_subset_used = 0;
    subsets->max_glyphs_per_scaled_subset_used = 0;
    subsets->num_sub_fonts = 0;

    subsets->unscaled_sub_fonts = _cairo_hash_table_create (_cairo_sub_fonts_equal);
    if (! subsets->unscaled_sub_fonts) {
	free (subsets);
	return NULL;
    }

    subsets->scaled_sub_fonts = _cairo_hash_table_create (_cairo_sub_fonts_equal);
    if (! subsets->scaled_sub_fonts) {
	_cairo_hash_table_destroy (subsets->unscaled_sub_fonts);
	free (subsets);
	return NULL;
    }

    return subsets;
}

cairo_scaled_font_subsets_t *
_cairo_scaled_font_subsets_create_scaled (void)
{
    return _cairo_scaled_font_subsets_create_internal (CAIRO_SUBSETS_SCALED);
}

cairo_scaled_font_subsets_t *
_cairo_scaled_font_subsets_create_simple (void)
{
    return _cairo_scaled_font_subsets_create_internal (CAIRO_SUBSETS_SIMPLE);
}

cairo_scaled_font_subsets_t *
_cairo_scaled_font_subsets_create_composite (void)
{
    return _cairo_scaled_font_subsets_create_internal (CAIRO_SUBSETS_COMPOSITE);
}

void
_cairo_scaled_font_subsets_destroy (cairo_scaled_font_subsets_t *subsets)
{
    _cairo_hash_table_foreach (subsets->scaled_sub_fonts, _cairo_sub_font_pluck, subsets->scaled_sub_fonts);
    _cairo_hash_table_destroy (subsets->scaled_sub_fonts);

    _cairo_hash_table_foreach (subsets->unscaled_sub_fonts, _cairo_sub_font_pluck, subsets->unscaled_sub_fonts);
    _cairo_hash_table_destroy (subsets->unscaled_sub_fonts);
    free (subsets);
}

cairo_private cairo_status_t
_cairo_scaled_font_subsets_map_glyph (cairo_scaled_font_subsets_t	*subsets,
				      cairo_scaled_font_t		*scaled_font,
				      unsigned long			 scaled_font_glyph_index,
                                      cairo_scaled_font_subsets_glyph_t *subset_glyph)
{
    cairo_sub_font_t key, *sub_font;
    cairo_scaled_glyph_t *scaled_glyph;
    cairo_font_face_t *font_face;
    cairo_matrix_t identity;
    cairo_font_options_t font_options;
    cairo_scaled_font_t	*unscaled_font;
    cairo_status_t status;
    int max_glyphs;
    cairo_bool_t type1_font;

    /* Lookup glyph in unscaled subsets */
    if (subsets->type != CAIRO_SUBSETS_SCALED) {
        key.is_scaled = FALSE;
        _cairo_sub_font_init_key (&key, scaled_font);
        if (_cairo_hash_table_lookup (subsets->unscaled_sub_fonts, &key.base,
                                        (cairo_hash_entry_t **) &sub_font))
        {
            status = _cairo_sub_font_lookup_glyph (sub_font,
                                                   scaled_font_glyph_index,
                                                   subset_glyph);
            if (status == CAIRO_STATUS_SUCCESS)
                return CAIRO_STATUS_SUCCESS;
        }
    }

    /* Lookup glyph in scaled subsets */
    key.is_scaled = TRUE;
    _cairo_sub_font_init_key (&key, scaled_font);
    if (_cairo_hash_table_lookup (subsets->scaled_sub_fonts, &key.base,
                                  (cairo_hash_entry_t **) &sub_font))
    {
        status = _cairo_sub_font_lookup_glyph (sub_font,
                                               scaled_font_glyph_index,
                                               subset_glyph);
        if (status == CAIRO_STATUS_SUCCESS)
            return CAIRO_STATUS_SUCCESS;
    }

    /* Glyph not found. Determine whether the glyph is outline or
     * bitmap and add to the appropriate subset */
    status = _cairo_scaled_glyph_lookup (scaled_font,
                                         scaled_font_glyph_index,
					 CAIRO_SCALED_GLYPH_INFO_PATH,
                                         &scaled_glyph);
    if (status && status != CAIRO_INT_STATUS_UNSUPPORTED)
        return status;

    if (status == 0 && subsets->type != CAIRO_SUBSETS_SCALED) {
        /* Path available. Add to unscaled subset. */
        key.is_scaled = FALSE;
        _cairo_sub_font_init_key (&key, scaled_font);
        if (! _cairo_hash_table_lookup (subsets->unscaled_sub_fonts, &key.base,
                                        (cairo_hash_entry_t **) &sub_font))
        {
            font_face = cairo_scaled_font_get_font_face (scaled_font);
            cairo_matrix_init_identity (&identity);
            _cairo_font_options_init_default (&font_options);
            cairo_font_options_set_hint_style (&font_options, CAIRO_HINT_STYLE_NONE);
            cairo_font_options_set_hint_metrics (&font_options, CAIRO_HINT_METRICS_OFF);
            unscaled_font = cairo_scaled_font_create (font_face,
                                                      &identity,
                                                      &identity,
                                                      &font_options);
	    if (unscaled_font->status)
		return unscaled_font->status;

            subset_glyph->is_scaled = FALSE;
            type1_font = FALSE;
#if CAIRO_HAS_FT_FONT
            type1_font = _cairo_type1_scaled_font_is_type1 (unscaled_font);
#endif
            if (subsets->type == CAIRO_SUBSETS_COMPOSITE && !type1_font) {
                max_glyphs = MAX_GLYPHS_PER_COMPOSITE_FONT;
                subset_glyph->is_composite = TRUE;
            } else {
                max_glyphs = MAX_GLYPHS_PER_SIMPLE_FONT;
                subset_glyph->is_composite = FALSE;
            }

            sub_font = _cairo_sub_font_create (subsets,
                                               unscaled_font,
                                               subsets->num_sub_fonts++,
                                               max_glyphs,
                                               subset_glyph->is_scaled,
                                               subset_glyph->is_composite);
            if (sub_font == NULL) {
		cairo_scaled_font_destroy (unscaled_font);
                return CAIRO_STATUS_NO_MEMORY;
	    }

            status = _cairo_hash_table_insert (subsets->unscaled_sub_fonts,
                                               &sub_font->base);
            if (status) {
		_cairo_sub_font_destroy (sub_font);
                return status;
	    }
        }
    } else {
        /* No path available. Add to scaled subset. */
        key.is_scaled = TRUE;
        _cairo_sub_font_init_key (&key, scaled_font);
        if (! _cairo_hash_table_lookup (subsets->scaled_sub_fonts, &key.base,
                                        (cairo_hash_entry_t **) &sub_font))
        {
            subset_glyph->is_scaled = TRUE;
            subset_glyph->is_composite = FALSE;
            if (subsets->type == CAIRO_SUBSETS_SCALED)
                max_glyphs = INT_MAX;
            else
                max_glyphs = MAX_GLYPHS_PER_SIMPLE_FONT;

            sub_font = _cairo_sub_font_create (subsets,
                                               cairo_scaled_font_reference (scaled_font),
                                               subsets->num_sub_fonts++,
                                               max_glyphs,
                                               subset_glyph->is_scaled,
                                               subset_glyph->is_composite);
            if (sub_font == NULL) {
		cairo_scaled_font_destroy (scaled_font);
                return CAIRO_STATUS_NO_MEMORY;
	    }

            status = _cairo_hash_table_insert (subsets->scaled_sub_fonts,
                                               &sub_font->base);
            if (status) {
		_cairo_sub_font_destroy (sub_font);
                return status;
	    }
        }
    }

    return _cairo_sub_font_map_glyph (sub_font,
                                      scaled_font_glyph_index,
                                      subset_glyph);
}

static cairo_status_t
_cairo_scaled_font_subsets_foreach_internal (cairo_scaled_font_subsets_t              *font_subsets,
                                             cairo_scaled_font_subset_callback_func_t  font_subset_callback,
                                             void				      *closure,
                                             cairo_bool_t                              is_scaled)
{
    cairo_sub_font_collection_t collection;

    if (is_scaled)
        collection.glyphs_size = font_subsets->max_glyphs_per_scaled_subset_used;
    else
        collection.glyphs_size = font_subsets->max_glyphs_per_unscaled_subset_used;

    if (! collection.glyphs_size)
	return CAIRO_STATUS_SUCCESS;

    collection.glyphs = malloc (collection.glyphs_size * sizeof(unsigned long));
    if (collection.glyphs == NULL)
	return CAIRO_STATUS_NO_MEMORY;

    collection.font_subset_callback = font_subset_callback;
    collection.font_subset_callback_closure = closure;

    if (is_scaled)
        _cairo_hash_table_foreach (font_subsets->scaled_sub_fonts,
                                   _cairo_sub_font_collect, &collection);
    else
        _cairo_hash_table_foreach (font_subsets->unscaled_sub_fonts,
                                   _cairo_sub_font_collect, &collection);

    free (collection.glyphs);

    return CAIRO_STATUS_SUCCESS;
}

cairo_private cairo_status_t
_cairo_scaled_font_subsets_foreach_scaled (cairo_scaled_font_subsets_t		    *font_subsets,
                                           cairo_scaled_font_subset_callback_func_t  font_subset_callback,
                                           void					    *closure)
{
    return _cairo_scaled_font_subsets_foreach_internal (font_subsets,
                                                        font_subset_callback,
                                                        closure,
                                                        TRUE);
}

cairo_private cairo_status_t
_cairo_scaled_font_subsets_foreach_unscaled (cairo_scaled_font_subsets_t	    *font_subsets,
                                           cairo_scaled_font_subset_callback_func_t  font_subset_callback,
                                           void					    *closure)
{
    return _cairo_scaled_font_subsets_foreach_internal (font_subsets,
                                                        font_subset_callback,
                                                        closure,
                                                        FALSE);
}
