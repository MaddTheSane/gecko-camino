/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2000 Keith Packard
 * Copyright © 2005 Red Hat, Inc
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
 * The Initial Developer of the Original Code is Red Hat, Inc.
 *
 * Contributor(s):
 *      Graydon Hoare <graydon@redhat.com>
 *	Owen Taylor <otaylor@redhat.com>
 *      Keith Packard <keithp@keithp.com>
 *      Carl Worth <cworth@cworth.org>
 */

#include "cairoint.h"

#include "cairo-ft-private.h"

#include <float.h>

#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_IMAGE_H
#include FT_TRUETYPE_TABLES_H
#if HAVE_FT_GLYPHSLOT_EMBOLDEN
#include FT_SYNTHESIS_H
#endif

#define DOUBLE_TO_26_6(d) ((FT_F26Dot6)((d) * 64.0))
#define DOUBLE_FROM_26_6(t) ((double)(t) / 64.0)
#define DOUBLE_TO_16_16(d) ((FT_Fixed)((d) * 65536.0))
#define DOUBLE_FROM_16_16(t) ((double)(t) / 65536.0)

/* This is the max number of FT_face objects we keep open at once
 */
#define MAX_OPEN_FACES 10

/* This is the maximum font size we allow to be passed to FT_Set_Char_Size
 */
#define MAX_FONT_SIZE 1000

/*
 * The simple 2x2 matrix is converted into separate scale and shape
 * factors so that hinting works right
 */

typedef struct _cairo_ft_font_transform {
    double  x_scale, y_scale;
    double  shape[2][2];
} cairo_ft_font_transform_t;

/*
 * We create an object that corresponds to a single font on the disk;
 * (identified by a filename/id pair) these are shared between all
 * fonts using that file.  For cairo_ft_font_face_create_for_ft_face(), we
 * just create a one-off version with a permanent face value.
 */

typedef struct _cairo_ft_font_face cairo_ft_font_face_t;

struct _cairo_ft_unscaled_font {
    cairo_unscaled_font_t base;

    cairo_bool_t from_face; /* from cairo_ft_font_face_create_for_ft_face()? */
    FT_Face face;	    /* provided or cached face */

    /* only set if from_face is false */
    char *filename;
    int id;

    /* We temporarily scale the unscaled font as needed */
    cairo_bool_t have_scale;
    cairo_matrix_t current_scale;
    double x_scale;		/* Extracted X scale factor */
    double y_scale;             /* Extracted Y scale factor */
    cairo_bool_t have_shape;	/* true if the current scale has a non-scale component*/
    cairo_matrix_t current_shape;
    FT_Matrix Current_Shape;

    cairo_mutex_t mutex;
    int lock_count;

    cairo_ft_font_face_t *faces;	/* Linked list of faces for this font */
};

static int
_cairo_ft_unscaled_font_keys_equal (const void *key_a,
				    const void *key_b);

static void
_cairo_ft_unscaled_font_fini (cairo_ft_unscaled_font_t *unscaled);

typedef enum _cairo_ft_extra_flags {
    CAIRO_FT_OPTIONS_HINT_METRICS = (1 << 0),
    CAIRO_FT_OPTIONS_EMBOLDEN = (1 << 1)
} cairo_ft_extra_flags_t;

typedef struct _cairo_ft_options {
    cairo_font_options_t    base;
    int			    load_flags;	 /* flags for FT_Load_Glyph */
    cairo_ft_extra_flags_t  extra_flags; /* other flags that affect results */
} cairo_ft_options_t;

struct _cairo_ft_font_face {
    cairo_font_face_t base;
    cairo_ft_unscaled_font_t *unscaled;
    cairo_ft_options_t ft_options;
    cairo_ft_font_face_t *next;
};

static const cairo_unscaled_font_backend_t cairo_ft_unscaled_font_backend;

/*
 * We maintain a hash table to map file/id => cairo_ft_unscaled_font_t.
 * The hash table itself isn't limited in size. However, we limit the
 * number of FT_Face objects we keep around; when we've exceeded that
 * limit and need to create a new FT_Face, we dump the FT_Face from a
 * random cairo_ft_unscaled_font_t which has an unlocked FT_Face, (if
 * there are any).
 */

typedef struct _cairo_ft_unscaled_font_map {
    cairo_hash_table_t *hash_table;
    FT_Library ft_library;
    int num_open_faces;
} cairo_ft_unscaled_font_map_t;

static cairo_ft_unscaled_font_map_t *cairo_ft_unscaled_font_map = NULL;

static void
_font_map_release_face_lock_held (cairo_ft_unscaled_font_map_t *font_map,
				  cairo_ft_unscaled_font_t *unscaled)
{
    if (unscaled->face) {
	FT_Done_Face (unscaled->face);
	unscaled->face = NULL;
	unscaled->have_scale = FALSE;

	font_map->num_open_faces--;
    }
}

static void
_cairo_ft_unscaled_font_map_create (void)
{
    cairo_ft_unscaled_font_map_t *font_map;

    /* This function is only intended to be called from
     * _cairo_ft_unscaled_font_map_lock. So we'll crash if we can
     * detect some other call path. */
    assert (cairo_ft_unscaled_font_map == NULL);

    font_map = malloc (sizeof (cairo_ft_unscaled_font_map_t));
    if (font_map == NULL)
	goto FAIL;

    font_map->hash_table =
	_cairo_hash_table_create (_cairo_ft_unscaled_font_keys_equal);

    if (font_map->hash_table == NULL)
	goto FAIL;

    if (FT_Init_FreeType (&font_map->ft_library))
	goto FAIL;

    font_map->num_open_faces = 0;

    cairo_ft_unscaled_font_map = font_map;
    return;

FAIL:
    if (font_map) {
	if (font_map->hash_table)
	    _cairo_hash_table_destroy (font_map->hash_table);
	free (font_map);
    }
    cairo_ft_unscaled_font_map = NULL;
}

static void
_cairo_ft_unscaled_font_map_destroy (void)
{
    cairo_ft_unscaled_font_t *unscaled;
    cairo_ft_unscaled_font_map_t *font_map;

    CAIRO_MUTEX_LOCK (_cairo_ft_unscaled_font_map_mutex);

    if (cairo_ft_unscaled_font_map) {
	font_map = cairo_ft_unscaled_font_map;

	/* This is rather inefficient, but destroying the hash table
	 * is something we only do during debugging, (during
	 * cairo_debug_reset_static_data), when efficiency is not
	 * relevant. */
        while (1) {
	    unscaled = _cairo_hash_table_random_entry (font_map->hash_table,
						       NULL);
	    if (unscaled == NULL)
		break;
	    _cairo_hash_table_remove (font_map->hash_table,
				      &unscaled->base.hash_entry);

	    _font_map_release_face_lock_held (font_map, unscaled);
	    _cairo_ft_unscaled_font_fini (unscaled);
	    free (unscaled);
	}

	assert (font_map->num_open_faces == 0);

	FT_Done_FreeType (font_map->ft_library);

	_cairo_hash_table_destroy (font_map->hash_table);

	free (font_map);

	cairo_ft_unscaled_font_map = NULL;
    }

    CAIRO_MUTEX_UNLOCK (_cairo_ft_unscaled_font_map_mutex);
}

static cairo_ft_unscaled_font_map_t *
_cairo_ft_unscaled_font_map_lock (void)
{
    CAIRO_MUTEX_LOCK (_cairo_ft_unscaled_font_map_mutex);

    if (cairo_ft_unscaled_font_map == NULL)
    {
	_cairo_ft_unscaled_font_map_create ();

	if (cairo_ft_unscaled_font_map == NULL) {
	    CAIRO_MUTEX_UNLOCK (_cairo_ft_unscaled_font_map_mutex);
	    _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    return NULL;
	}
    }

    return cairo_ft_unscaled_font_map;
}

static void
_cairo_ft_unscaled_font_map_unlock (void)
{
    CAIRO_MUTEX_UNLOCK (_cairo_ft_unscaled_font_map_mutex);
}

static void
_cairo_ft_unscaled_font_init_key (cairo_ft_unscaled_font_t *key,
				  char			   *filename,
				  int			    id)
{
    unsigned long hash;

    key->filename = filename;
    key->id = id;

    /* 1607 is just an arbitrary prime. */
    hash = _cairo_hash_string (filename);
    hash += ((unsigned long) id) * 1607;

    key->base.hash_entry.hash = hash;
}

/**
 * _cairo_ft_unscaled_font_init:
 *
 * Initialize a cairo_ft_unscaled_font_t.
 *
 * There are two basic flavors of cairo_ft_unscaled_font_t, one
 * created from an FT_Face and the other created from a filename/id
 * pair. These two flavors are identified as from_face and !from_face.
 *
 * To initialize a from_face font, pass filename==NULL, id=0 and the
 * desired face.
 *
 * To initialize a !from_face font, pass the filename/id as desired
 * and face==NULL.
 *
 * Note that the code handles these two flavors in very distinct
 * ways. For example there is a hash_table mapping
 * filename/id->cairo_unscaled_font_t in the !from_face case, but no
 * parallel in the from_face case, (where the calling code would have
 * to do its own mapping to ensure similar sharing).
 **/
static cairo_status_t
_cairo_ft_unscaled_font_init (cairo_ft_unscaled_font_t *unscaled,
			      const char	       *filename,
			      int			id,
			      FT_Face			face)
{
    _cairo_unscaled_font_init (&unscaled->base,
			       &cairo_ft_unscaled_font_backend);

    if (face) {
	unscaled->from_face = TRUE;
	unscaled->face = face;
	unscaled->filename = NULL;
	unscaled->id = 0;
    } else {
	char *filename_copy;

	unscaled->from_face = FALSE;
	unscaled->face = NULL;

	filename_copy = strdup (filename);
	if (filename_copy == NULL) {
	    _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    return CAIRO_STATUS_NO_MEMORY;
	}

	_cairo_ft_unscaled_font_init_key (unscaled, filename_copy, id);
    }

    unscaled->have_scale = FALSE;
    CAIRO_MUTEX_INIT (unscaled->mutex);
    unscaled->lock_count = 0;

    unscaled->faces = NULL;

    return CAIRO_STATUS_SUCCESS;
}

cairo_bool_t
_cairo_unscaled_font_is_ft (cairo_unscaled_font_t *unscaled_font)
{
    return unscaled_font->backend == &cairo_ft_unscaled_font_backend;
}

/**
 * _cairo_ft_unscaled_font_fini:
 *
 * Free all data associated with a cairo_ft_unscaled_font_t.
 *
 * CAUTION: The unscaled->face field must be NULL before calling this
 * function. This is because the cairo_ft_unscaled_font_map keeps a
 * count of these faces (font_map->num_open_faces) so it maintains the
 * unscaled->face field while it has its lock held. See
 * _font_map_release_face_lock_held().
 **/
static void
_cairo_ft_unscaled_font_fini (cairo_ft_unscaled_font_t *unscaled)
{
    assert (unscaled->face == NULL);

    if (unscaled->filename) {
	free (unscaled->filename);
	unscaled->filename = NULL;
    }

    CAIRO_MUTEX_FINI (unscaled->mutex);
}

static int
_cairo_ft_unscaled_font_keys_equal (const void *key_a,
				    const void *key_b)
{
    const cairo_ft_unscaled_font_t *unscaled_a = key_a;
    const cairo_ft_unscaled_font_t *unscaled_b = key_b;

    return (strcmp (unscaled_a->filename, unscaled_b->filename) == 0 &&
	    unscaled_a->id == unscaled_b->id);
}

/* Finds or creates a cairo_ft_unscaled_font for the filename/id from
 * pattern.  Returns a new reference to the unscaled font.
 */
static cairo_ft_unscaled_font_t *
_cairo_ft_unscaled_font_create_for_pattern (FcPattern *pattern)
{
    cairo_ft_unscaled_font_t key, *unscaled;
    cairo_ft_unscaled_font_map_t *font_map;
    cairo_status_t status;
    FcChar8 *fc_filename;
    char *filename;
    int id;

    if (FcPatternGetString (pattern, FC_FILE, 0, &fc_filename) != FcResultMatch)
	goto UNWIND;
    filename = (char *) fc_filename;

    if (FcPatternGetInteger (pattern, FC_INDEX, 0, &id) != FcResultMatch)
	goto UNWIND;

    font_map = _cairo_ft_unscaled_font_map_lock ();
    if (font_map == NULL)
	goto UNWIND;

    _cairo_ft_unscaled_font_init_key (&key, filename, id);

    /* Return existing unscaled font if it exists in the hash table. */
    if (_cairo_hash_table_lookup (font_map->hash_table, &key.base.hash_entry,
				  (cairo_hash_entry_t **) &unscaled))
    {
	_cairo_unscaled_font_reference (&unscaled->base);
	_cairo_ft_unscaled_font_map_unlock ();
	return unscaled;
    }

    /* Otherwise create it and insert into hash table. */
    unscaled = malloc (sizeof (cairo_ft_unscaled_font_t));
    if (unscaled == NULL)
	goto UNWIND_FONT_MAP_LOCK;

    status = _cairo_ft_unscaled_font_init (unscaled, filename, id, NULL);
    if (status)
	goto UNWIND_UNSCALED_MALLOC;

    status = _cairo_hash_table_insert (font_map->hash_table,
				       &unscaled->base.hash_entry);
    if (status)
	goto UNWIND_UNSCALED_FONT_INIT;

    _cairo_ft_unscaled_font_map_unlock ();

    return unscaled;

UNWIND_UNSCALED_FONT_INIT:
    _cairo_ft_unscaled_font_fini (unscaled);
UNWIND_UNSCALED_MALLOC:
    free (unscaled);
UNWIND_FONT_MAP_LOCK:
    _cairo_ft_unscaled_font_map_unlock ();
UNWIND:
    return NULL;
}

static cairo_ft_unscaled_font_t *
_cairo_ft_unscaled_font_create_from_face (FT_Face face)
{
    cairo_status_t status;
    cairo_ft_unscaled_font_t *unscaled;

    unscaled = malloc (sizeof (cairo_ft_unscaled_font_t));
    if (unscaled == NULL)
	return NULL;

    status = _cairo_ft_unscaled_font_init (unscaled, NULL, 0, face);
    if (status) {
	free (unscaled);
	return NULL;
    }

    return unscaled;
}

static void
_cairo_ft_unscaled_font_destroy (void *abstract_font)
{
    cairo_ft_unscaled_font_t *unscaled  = abstract_font;

    if (unscaled == NULL)
	return;

    if (unscaled->from_face) {
	/* See comments in _ft_font_face_destroy about the "zombie" state
	 * for a _ft_font_face.
	 */
	if (unscaled->faces && !unscaled->faces->unscaled)
	    cairo_font_face_destroy (&unscaled->faces->base);
    } else {
	cairo_ft_unscaled_font_map_t *font_map;

	font_map = _cairo_ft_unscaled_font_map_lock ();
	/* All created objects must have been mapped in the font map. */
	assert (font_map != NULL);

	_cairo_hash_table_remove (font_map->hash_table,
				  &unscaled->base.hash_entry);

	_font_map_release_face_lock_held (font_map, unscaled);
	_cairo_ft_unscaled_font_fini (unscaled);

	_cairo_ft_unscaled_font_map_unlock ();
    }
}

static cairo_bool_t
_has_unlocked_face (void *entry)
{
    cairo_ft_unscaled_font_t *unscaled = entry;

    return (unscaled->lock_count == 0 && unscaled->face);
}

/* Ensures that an unscaled font has a face object. If we exceed
 * MAX_OPEN_FACES, try to close some.
 *
 * This differs from _cairo_ft_scaled_font_lock_face in that it doesn't
 * set the scale on the face, but just returns it at the last scale.
 */
FT_Face
_cairo_ft_unscaled_font_lock_face (cairo_ft_unscaled_font_t *unscaled)
{
    cairo_ft_unscaled_font_map_t *font_map;
    FT_Face face = NULL;

    CAIRO_MUTEX_LOCK (unscaled->mutex);
    unscaled->lock_count++;

    if (unscaled->face)
	return unscaled->face;

    /* If this unscaled font was created from an FT_Face then we just
     * returned it above. */
    assert (!unscaled->from_face);

    font_map = _cairo_ft_unscaled_font_map_lock ();
    {
	assert (font_map != NULL);

	while (font_map->num_open_faces >= MAX_OPEN_FACES)
	{
	    cairo_ft_unscaled_font_t *entry;

	    entry = _cairo_hash_table_random_entry (font_map->hash_table,
						    _has_unlocked_face);
	    if (entry == NULL)
		break;

	    _font_map_release_face_lock_held (font_map, entry);
	}
    }
    _cairo_ft_unscaled_font_map_unlock ();

    if (FT_New_Face (font_map->ft_library,
		     unscaled->filename,
		     unscaled->id,
		     &face) != FT_Err_Ok)
    {
	unscaled->lock_count--;
	CAIRO_MUTEX_UNLOCK (unscaled->mutex);
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return NULL;
    }

    unscaled->face = face;

    font_map->num_open_faces++;

    return face;
}
slim_hidden_def (cairo_ft_scaled_font_lock_face);

/* Unlock unscaled font locked with _cairo_ft_unscaled_font_lock_face
 */
void
_cairo_ft_unscaled_font_unlock_face (cairo_ft_unscaled_font_t *unscaled)
{
    assert (unscaled->lock_count > 0);

    unscaled->lock_count--;

    CAIRO_MUTEX_UNLOCK (unscaled->mutex);
}
slim_hidden_def (cairo_ft_scaled_font_unlock_face);

static void
_compute_transform (cairo_ft_font_transform_t *sf,
		    cairo_matrix_t      *scale)
{
    cairo_matrix_t normalized = *scale;

    /* The font matrix has x and y "scale" components which we extract and
     * use as character scale values. These influence the way freetype
     * chooses hints, as well as selecting different bitmaps in
     * hand-rendered fonts. We also copy the normalized matrix to
     * freetype's transformation.
     */

    _cairo_matrix_compute_scale_factors (&normalized,
					 &sf->x_scale, &sf->y_scale,
					 /* XXX */ 1);

    if (sf->x_scale != 0 && sf->y_scale != 0) {
	cairo_matrix_scale (&normalized, 1.0 / sf->x_scale, 1.0 / sf->y_scale);

	_cairo_matrix_get_affine (&normalized,
				  &sf->shape[0][0], &sf->shape[0][1],
				  &sf->shape[1][0], &sf->shape[1][1],
				  NULL, NULL);
    } else {
	sf->shape[0][0] = sf->shape[1][1] = 1.0;
	sf->shape[0][1] = sf->shape[1][0] = 0.0;
    }
}

/* Temporarily scales an unscaled font to the give scale. We catch
 * scaling to the same size, since changing a FT_Face is expensive.
 */
static cairo_status_t
_cairo_ft_unscaled_font_set_scale (cairo_ft_unscaled_font_t *unscaled,
				   cairo_matrix_t	      *scale)
{
    cairo_ft_font_transform_t sf;
    FT_Matrix mat;
    FT_Error error;

    assert (unscaled->face != NULL);

    if (unscaled->have_scale &&
	scale->xx == unscaled->current_scale.xx &&
	scale->yx == unscaled->current_scale.yx &&
	scale->xy == unscaled->current_scale.xy &&
	scale->yy == unscaled->current_scale.yy)
	return CAIRO_STATUS_SUCCESS;

    unscaled->have_scale = TRUE;
    unscaled->current_scale = *scale;

    _compute_transform (&sf, scale);

    unscaled->x_scale = sf.x_scale;
    unscaled->y_scale = sf.y_scale;

    mat.xx = DOUBLE_TO_16_16(sf.shape[0][0]);
    mat.yx = - DOUBLE_TO_16_16(sf.shape[0][1]);
    mat.xy = - DOUBLE_TO_16_16(sf.shape[1][0]);
    mat.yy = DOUBLE_TO_16_16(sf.shape[1][1]);

    unscaled->have_shape = (mat.xx != 0x10000 ||
			    mat.yx != 0x00000 ||
			    mat.xy != 0x00000 ||
			    mat.yy != 0x10000);

    unscaled->Current_Shape = mat;
    cairo_matrix_init (&unscaled->current_shape,
		       sf.shape[0][0], sf.shape[0][1],
		       sf.shape[1][0], sf.shape[1][1],
		       0.0, 0.0);

    FT_Set_Transform(unscaled->face, &mat, NULL);

    if ((unscaled->face->face_flags & FT_FACE_FLAG_SCALABLE) != 0) {
        double x_scale = sf.x_scale;
        double y_scale = sf.y_scale;
        if (x_scale > MAX_FONT_SIZE) {
            x_scale = MAX_FONT_SIZE;
        }
        if (y_scale > MAX_FONT_SIZE) {
            y_scale = MAX_FONT_SIZE;
        }

	error = FT_Set_Char_Size (unscaled->face,
				  x_scale * 64.0,
				  y_scale * 64.0,
				  0, 0);
	if (error) {
	    _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    return CAIRO_STATUS_NO_MEMORY;
	}
    } else {
	double min_distance = DBL_MAX;
	int i;
	int best_i = 0;

	for (i = 0; i < unscaled->face->num_fixed_sizes; i++) {
#if HAVE_FT_BITMAP_SIZE_Y_PPEM
	    double size = unscaled->face->available_sizes[i].y_ppem / 64.;
#else
	    double size = unscaled->face->available_sizes[i].height;
#endif
	    double distance = fabs (size - sf.y_scale);

	    if (distance <= min_distance) {
		min_distance = distance;
		best_i = i;
	    }
	}
#if HAVE_FT_BITMAP_SIZE_Y_PPEM
	error = FT_Set_Char_Size (unscaled->face,
				  unscaled->face->available_sizes[best_i].x_ppem,
				  unscaled->face->available_sizes[best_i].y_ppem,
				  0, 0);
	if (error)
#endif
	    error = FT_Set_Pixel_Sizes (unscaled->face,
					unscaled->face->available_sizes[best_i].width,
					unscaled->face->available_sizes[best_i].height);
	if (error) {
	    _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    return CAIRO_STATUS_NO_MEMORY;
	}
    }

    return CAIRO_STATUS_SUCCESS;
}

/* Empirically-derived subpixel filtering values thanks to Keith
 * Packard and libXft. */
static const int    filters[3][3] = {
    /* red */
#if 0
    {    65538*4/7,65538*2/7,65538*1/7 },
    /* green */
    {    65536*1/4, 65536*2/4, 65537*1/4 },
    /* blue */
    {    65538*1/7,65538*2/7,65538*4/7 },
#endif
    {    65538*9/13,65538*3/13,65538*1/13 },
    /* green */
    {    65538*1/6, 65538*4/6, 65538*1/6 },
    /* blue */
    {    65538*1/13,65538*3/13,65538*9/13 },
};

/* Fills in val->image with an image surface created from @bitmap
 */
static cairo_status_t
_get_bitmap_surface (FT_Bitmap		     *bitmap,
		     cairo_bool_t	      own_buffer,
		     cairo_font_options_t    *font_options,
		     cairo_image_surface_t  **surface)
{
    int width, height, stride;
    unsigned char *data;
    int format = CAIRO_FORMAT_A8;
    cairo_bool_t subpixel = FALSE;

    width = bitmap->width;
    height = bitmap->rows;

    if (width == 0 || height == 0) {
	*surface = (cairo_image_surface_t *)
	    cairo_image_surface_create_for_data (NULL, format, 0, 0, 0);
	return (*surface)->base.status;
    }

    switch (bitmap->pixel_mode) {
    case FT_PIXEL_MODE_MONO:
	stride = (((width + 31) & ~31) >> 3);
	if (own_buffer) {
	    data = bitmap->buffer;
	    assert (stride == bitmap->pitch);
	} else {
	    data = _cairo_malloc_ab (height, stride);
	    if (!data) {
		_cairo_error (CAIRO_STATUS_NO_MEMORY);
		return CAIRO_STATUS_NO_MEMORY;
	    }

	    if (stride == bitmap->pitch) {
		memcpy (data, bitmap->buffer, stride * height);
	    } else {
		int i;
		unsigned char *source, *dest;

		source = bitmap->buffer;
		dest = data;
		for (i = height; i; i--) {
		    memcpy (dest, source, bitmap->pitch);
		    memset (dest + bitmap->pitch, '\0', stride - bitmap->pitch);

		    source += bitmap->pitch;
		    dest += stride;
		}
	    }
	}

#ifndef WORDS_BIGENDIAN
	{
	    unsigned char   *d = data;
	    int		count = stride * height;

	    while (count--) {
		*d = CAIRO_BITSWAP8 (*d);
		d++;
	    }
	}
#endif
	format = CAIRO_FORMAT_A1;
	break;

    case FT_PIXEL_MODE_LCD:
    case FT_PIXEL_MODE_LCD_V:
    case FT_PIXEL_MODE_GRAY:
	switch (font_options->antialias) {
	case CAIRO_ANTIALIAS_DEFAULT:
	case CAIRO_ANTIALIAS_GRAY:
	case CAIRO_ANTIALIAS_NONE:
	default:
	    stride = bitmap->pitch;
	    if (own_buffer) {
		data = bitmap->buffer;
	    } else {
		data = _cairo_malloc_ab (height, stride);
		if (!data) {
		    _cairo_error (CAIRO_STATUS_NO_MEMORY);
		    return CAIRO_STATUS_NO_MEMORY;
		}
		memcpy (data, bitmap->buffer, stride * height);
	    }
	    format = CAIRO_FORMAT_A8;
	    break;
	case CAIRO_ANTIALIAS_SUBPIXEL: {
	    int		    x, y;
	    unsigned char   *in_line, *out_line, *in;
	    unsigned int    *out;
	    unsigned int    red, green, blue;
	    int		    rf, gf, bf;
	    int		    s;
	    int		    o, os;
	    unsigned char   *data_rgba;
	    unsigned int    width_rgba, stride_rgba;
	    int		    vmul = 1;
	    int		    hmul = 1;

	    switch (font_options->subpixel_order) {
	    case CAIRO_SUBPIXEL_ORDER_DEFAULT:
	    case CAIRO_SUBPIXEL_ORDER_RGB:
	    case CAIRO_SUBPIXEL_ORDER_BGR:
	    default:
		width /= 3;
		hmul = 3;
		break;
	    case CAIRO_SUBPIXEL_ORDER_VRGB:
	    case CAIRO_SUBPIXEL_ORDER_VBGR:
		vmul = 3;
		height /= 3;
		break;
	    }
	    /*
	     * Filter the glyph to soften the color fringes
	     */
	    width_rgba = width;
	    stride = bitmap->pitch;
	    stride_rgba = (width_rgba * 4 + 3) & ~3;
	    data_rgba = calloc (1, stride_rgba * height);
	    if (data_rgba == NULL) {
		if (own_buffer)
		    free (bitmap->buffer);
		_cairo_error (CAIRO_STATUS_NO_MEMORY);
		return CAIRO_STATUS_NO_MEMORY;
	    }

	    os = 1;
	    switch (font_options->subpixel_order) {
	    case CAIRO_SUBPIXEL_ORDER_VRGB:
		os = stride;
	    case CAIRO_SUBPIXEL_ORDER_DEFAULT:
	    case CAIRO_SUBPIXEL_ORDER_RGB:
	    default:
		rf = 0;
		gf = 1;
		bf = 2;
		break;
	    case CAIRO_SUBPIXEL_ORDER_VBGR:
		os = stride;
	    case CAIRO_SUBPIXEL_ORDER_BGR:
		bf = 0;
		gf = 1;
		rf = 2;
		break;
	    }
	    in_line = bitmap->buffer;
	    out_line = data_rgba;
	    for (y = 0; y < height; y++)
	    {
		in = in_line;
		out = (unsigned int *) out_line;
		in_line += stride * vmul;
		out_line += stride_rgba;
		for (x = 0; x < width * hmul; x += hmul)
		{
		    red = green = blue = 0;
		    o = 0;
		    for (s = 0; s < 3; s++)
		    {
			red += filters[rf][s]*in[x+o];
			green += filters[gf][s]*in[x+o];
			blue += filters[bf][s]*in[x+o];
			o += os;
		    }
		    red = red / 65536;
		    green = green / 65536;
		    blue = blue / 65536;
		    *out++ = (green << 24) | (red << 16) | (green << 8) | blue;
		}
	    }

	    /* Images here are stored in native format. The
	     * backend must convert to its own format as needed
	     */

	    if (own_buffer)
		free (bitmap->buffer);
	    data = data_rgba;
	    stride = stride_rgba;
	    format = CAIRO_FORMAT_ARGB32;
	    subpixel = TRUE;
	    break;
	}
	}
	break;
    case FT_PIXEL_MODE_GRAY2:
    case FT_PIXEL_MODE_GRAY4:
	/* These could be triggered by very rare types of TrueType fonts */
    default:
	if (own_buffer)
	    free (bitmap->buffer);
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return CAIRO_STATUS_NO_MEMORY;
    }

    *surface = (cairo_image_surface_t *)
	cairo_image_surface_create_for_data (data,
					     format,
					     width, height, stride);
    if ((*surface)->base.status) {
	free (data);
	return CAIRO_STATUS_NO_MEMORY;
    }

    if (subpixel)
	pixman_image_set_component_alpha ((*surface)->pixman_image, TRUE);

    _cairo_image_surface_assume_ownership_of_data ((*surface));

    return CAIRO_STATUS_SUCCESS;
}

/* Converts an outline FT_GlyphSlot into an image
 *
 * This could go through _render_glyph_bitmap as well, letting
 * FreeType convert the outline to a bitmap, but doing it ourselves
 * has two minor advantages: first, we save a copy of the bitmap
 * buffer: we can directly use the buffer that FreeType renders
 * into.
 *
 * Second, it may help when we add support for subpixel
 * rendering: the Xft code does it this way. (Keith thinks that
 * it may also be possible to get the subpixel rendering with
 * FT_Render_Glyph: something worth looking into in more detail
 * when we add subpixel support. If so, we may want to eliminate
 * this version of the code path entirely.
 */
static cairo_status_t
_render_glyph_outline (FT_Face                    face,
		       cairo_font_options_t	 *font_options,
		       cairo_image_surface_t	**surface)
{
    FT_GlyphSlot glyphslot = face->glyph;
    FT_Outline *outline = &glyphslot->outline;
    FT_Bitmap bitmap;
    FT_BBox cbox;
    FT_Matrix matrix;
    int hmul = 1;
    int vmul = 1;
    unsigned int width, height, stride;
    cairo_bool_t subpixel = FALSE;
    cairo_status_t status;

    FT_Outline_Get_CBox (outline, &cbox);

    cbox.xMin &= -64;
    cbox.yMin &= -64;
    cbox.xMax = (cbox.xMax + 63) & -64;
    cbox.yMax = (cbox.yMax + 63) & -64;

    width = (unsigned int) ((cbox.xMax - cbox.xMin) >> 6);
    height = (unsigned int) ((cbox.yMax - cbox.yMin) >> 6);
    stride = (width * hmul + 3) & ~3;

    if (width * height == 0) {
	cairo_format_t format;
	/* Looks like fb handles zero-sized images just fine */
	switch (font_options->antialias) {
	case CAIRO_ANTIALIAS_NONE:
	    format = CAIRO_FORMAT_A1;
	    break;
	case CAIRO_ANTIALIAS_SUBPIXEL:
	    format= CAIRO_FORMAT_ARGB32;
	    break;
	case CAIRO_ANTIALIAS_DEFAULT:
	case CAIRO_ANTIALIAS_GRAY:
	default:
	    format = CAIRO_FORMAT_A8;
	    break;
	}

	(*surface) = (cairo_image_surface_t *)
	    cairo_image_surface_create_for_data (NULL, format, 0, 0, 0);
	if ((*surface)->base.status)
	    return CAIRO_STATUS_NO_MEMORY;
    } else  {

	matrix.xx = matrix.yy = 0x10000L;
	matrix.xy = matrix.yx = 0;

	switch (font_options->antialias) {
	case CAIRO_ANTIALIAS_NONE:
	    bitmap.pixel_mode = FT_PIXEL_MODE_MONO;
	    bitmap.num_grays  = 1;
	    stride = ((width + 31) & -32) >> 3;
	    break;
	case CAIRO_ANTIALIAS_DEFAULT:
	case CAIRO_ANTIALIAS_GRAY:
	    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
	    bitmap.num_grays  = 256;
	    stride = (width + 3) & -4;
	    break;
	case CAIRO_ANTIALIAS_SUBPIXEL:
	    switch (font_options->subpixel_order) {
	    case CAIRO_SUBPIXEL_ORDER_RGB:
	    case CAIRO_SUBPIXEL_ORDER_BGR:
	    case CAIRO_SUBPIXEL_ORDER_DEFAULT:
	    default:
		matrix.xx *= 3;
		hmul = 3;
		subpixel = TRUE;
		break;
	    case CAIRO_SUBPIXEL_ORDER_VRGB:
	    case CAIRO_SUBPIXEL_ORDER_VBGR:
		matrix.yy *= 3;
		vmul = 3;
		subpixel = TRUE;
		break;
	    }
	    FT_Outline_Transform (outline, &matrix);

	    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
	    bitmap.num_grays  = 256;
	    stride = (width * hmul + 3) & -4;
	}

	bitmap.pitch = stride;
	bitmap.width = width * hmul;
	bitmap.rows = height * vmul;
	bitmap.buffer = calloc (1, stride * bitmap.rows);

	if (bitmap.buffer == NULL) {
	    _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    return CAIRO_STATUS_NO_MEMORY;
	}

	FT_Outline_Translate (outline, -cbox.xMin*hmul, -cbox.yMin*vmul);

	if (FT_Outline_Get_Bitmap (glyphslot->library, outline, &bitmap) != 0) {
	    free (bitmap.buffer);
	    _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    return CAIRO_STATUS_NO_MEMORY;
	}

	status = _get_bitmap_surface (&bitmap, TRUE, font_options, surface);
	if (status)
	    return status;
    }

    /*
     * Note: the font's coordinate system is upside down from ours, so the
     * Y coordinate of the control box needs to be negated.  Moreover, device
     * offsets are position of glyph origin relative to top left while xMin
     * and yMax are offsets of top left relative to origin.  Another negation.
     */
    cairo_surface_set_device_offset (&(*surface)->base,
				     floor (-(double) cbox.xMin / 64.0),
				     floor (+(double) cbox.yMax / 64.0));

    return CAIRO_STATUS_SUCCESS;
}

/* Converts a bitmap (or other) FT_GlyphSlot into an image */
static cairo_status_t
_render_glyph_bitmap (FT_Face		      face,
		      cairo_font_options_t   *font_options,
		      cairo_image_surface_t **surface)
{
    FT_GlyphSlot glyphslot = face->glyph;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    FT_Error error;

    /* According to the FreeType docs, glyphslot->format could be
     * something other than FT_GLYPH_FORMAT_OUTLINE or
     * FT_GLYPH_FORMAT_BITMAP. Calling FT_Render_Glyph gives FreeType
     * the opportunity to convert such to
     * bitmap. FT_GLYPH_FORMAT_COMPOSITE will not be encountered since
     * we avoid the FT_LOAD_NO_RECURSE flag.
     */
    error = FT_Render_Glyph (glyphslot, FT_RENDER_MODE_NORMAL);
    /* XXX ignoring all other errors for now.  They are not fatal, typically
     * just a glyph-not-found. */
    if (error == FT_Err_Out_Of_Memory) {
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return CAIRO_STATUS_NO_MEMORY;
    }

    status = _get_bitmap_surface (&glyphslot->bitmap, FALSE, font_options, surface);
    if (status)
	return status;

    /*
     * Note: the font's coordinate system is upside down from ours, so the
     * Y coordinate of the control box needs to be negated.  Moreover, device
     * offsets are position of glyph origin relative to top left while
     * bitmap_left and bitmap_top are offsets of top left relative to origin.
     * Another negation.
     */
    cairo_surface_set_device_offset (&(*surface)->base,
				     -glyphslot->bitmap_left,
				     +glyphslot->bitmap_top);

    return status;
}

static cairo_status_t
_transform_glyph_bitmap (cairo_matrix_t         * shape,
			 cairo_image_surface_t ** surface)
{
    cairo_matrix_t original_to_transformed;
    cairo_matrix_t transformed_to_original;
    cairo_image_surface_t *old_image;
    cairo_surface_t *image;
    double x[4], y[4];
    double origin_x, origin_y;
    int orig_width, orig_height;
    int i;
    int x_min, y_min, x_max, y_max;
    int width, height;
    cairo_status_t status;
    cairo_surface_pattern_t pattern;

    /* We want to compute a transform that takes the origin
     * (device_x_offset, device_y_offset) to 0,0, then applies
     * the "shape" portion of the font transform
     */
    original_to_transformed = *shape;
    
    cairo_surface_get_device_offset (&(*surface)->base, &origin_x, &origin_y);
    orig_width = cairo_image_surface_get_width (&(*surface)->base);
    orig_height = cairo_image_surface_get_height (&(*surface)->base);

    cairo_matrix_translate (&original_to_transformed,
			    -origin_x, -origin_y);

    /* Find the bounding box of the original bitmap under that
     * transform
     */
    x[0] = 0;          y[0] = 0;
    x[1] = orig_width; y[1] = 0;
    x[2] = orig_width; y[2] = orig_height;
    x[3] = 0;          y[3] = orig_height;

    for (i = 0; i < 4; i++)
      cairo_matrix_transform_point (&original_to_transformed,
				    &x[i], &y[i]);

    x_min = floor (x[0]);   y_min = floor (y[0]);
    x_max =  ceil (x[0]);   y_max =  ceil (y[0]);

    for (i = 1; i < 4; i++) {
	if (x[i] < x_min)
	    x_min = floor (x[i]);
	else if (x[i] > x_max)
	    x_max = ceil (x[i]);
	if (y[i] < y_min)
	    y_min = floor (y[i]);
	else if (y[i] > y_max)
	    y_max = ceil (y[i]);
    }

    /* Adjust the transform so that the bounding box starts at 0,0 ...
     * this gives our final transform from original bitmap to transformed
     * bitmap.
     */
    original_to_transformed.x0 -= x_min;
    original_to_transformed.y0 -= y_min;

    /* Create the transformed bitmap
     */
    width = x_max - x_min;
    height = y_max - y_min;

    transformed_to_original = original_to_transformed;
    status = cairo_matrix_invert (&transformed_to_original);
    if (status)
	return status;

    /* We need to pad out the width to 32-bit intervals for cairo-xlib-surface.c */
    width = (width + 3) & ~3;
    image = cairo_image_surface_create (CAIRO_FORMAT_A8, width, height);
    if (image->status)
	return CAIRO_STATUS_NO_MEMORY;

    /* Initialize it to empty
     */
    status = _cairo_surface_fill_rectangle (image, CAIRO_OPERATOR_CLEAR,
				            CAIRO_COLOR_TRANSPARENT,
					    0, 0,
					    width, height);
    if (status) {
	cairo_surface_destroy (image);
	return status;
    }

    /* Draw the original bitmap transformed into the new bitmap
     */
    _cairo_pattern_init_for_surface (&pattern, &(*surface)->base);
    cairo_pattern_set_matrix (&pattern.base, &transformed_to_original);

    status = _cairo_surface_composite (CAIRO_OPERATOR_OVER,
			               &pattern.base, NULL, image,
				       0, 0, 0, 0, 0, 0,
				       width,
				       height);

    _cairo_pattern_fini (&pattern.base);

    if (status) {
	cairo_surface_destroy (image);
	return status;
    }

    /* Now update the cache entry for the new bitmap, recomputing
     * the origin based on the final transform.
     */
    cairo_matrix_transform_point (&original_to_transformed,
				  &origin_x, &origin_y);

    old_image = (*surface);
    (*surface) = (cairo_image_surface_t *)image;
    cairo_surface_destroy (&old_image->base);

    cairo_surface_set_device_offset (&(*surface)->base,
				     _cairo_lround (origin_x),
				     _cairo_lround (origin_y));
    return status;
}

static const cairo_unscaled_font_backend_t cairo_ft_unscaled_font_backend = {
    _cairo_ft_unscaled_font_destroy,
#if 0
    _cairo_ft_unscaled_font_create_glyph
#endif
};

/* cairo_ft_scaled_font_t */

typedef struct _cairo_ft_scaled_font {
    cairo_scaled_font_t base;
    cairo_ft_unscaled_font_t *unscaled;
    cairo_ft_options_t ft_options;
} cairo_ft_scaled_font_t;

const cairo_scaled_font_backend_t cairo_ft_scaled_font_backend;

/* The load flags passed to FT_Load_Glyph control aspects like hinting and
 * antialiasing. Here we compute them from the fields of a FcPattern.
 */
static void
_get_pattern_ft_options (FcPattern *pattern, cairo_ft_options_t *ret)
{
    FcBool antialias, vertical_layout, hinting, autohint, bitmap, embolden;
    cairo_ft_options_t ft_options;
    int rgba;
#ifdef FC_HINT_STYLE
    int hintstyle;
#endif

    _cairo_font_options_init_default (&ft_options.base);
    ft_options.load_flags = FT_LOAD_DEFAULT;
    ft_options.extra_flags = 0;

#ifndef FC_EMBEDDED_BITMAP
#define FC_EMBEDDED_BITMAP "embeddedbitmap"
#endif

    /* Check whether to force use of embedded bitmaps */
    if (FcPatternGetBool (pattern,
			  FC_EMBEDDED_BITMAP, 0, &bitmap) != FcResultMatch)
	bitmap = FcFalse;

    /* disable antialiasing if requested */
    if (FcPatternGetBool (pattern,
			  FC_ANTIALIAS, 0, &antialias) != FcResultMatch)
	antialias = FcTrue;
    
    if (antialias) {
	cairo_subpixel_order_t subpixel_order;

	if (!bitmap)
	    ft_options.load_flags |= FT_LOAD_NO_BITMAP;
	
	/* disable hinting if requested */
	if (FcPatternGetBool (pattern,
			      FC_HINTING, 0, &hinting) != FcResultMatch)
	    hinting = FcTrue;

	if (FcPatternGetInteger (pattern,
				 FC_RGBA, 0, &rgba) != FcResultMatch)
	    rgba = FC_RGBA_UNKNOWN;

	switch (rgba) {
	case FC_RGBA_RGB:
	    subpixel_order = CAIRO_SUBPIXEL_ORDER_RGB;
	    break;
	case FC_RGBA_BGR:
	    subpixel_order = CAIRO_SUBPIXEL_ORDER_BGR;
	    break;
	case FC_RGBA_VRGB:
	    subpixel_order = CAIRO_SUBPIXEL_ORDER_VRGB;
	    break;
	case FC_RGBA_VBGR:
	    subpixel_order = CAIRO_SUBPIXEL_ORDER_VBGR;
	    break;
	case FC_RGBA_UNKNOWN:
	case FC_RGBA_NONE:
	default:
	    subpixel_order = CAIRO_SUBPIXEL_ORDER_DEFAULT;
	    break;
	}

	if (subpixel_order != CAIRO_SUBPIXEL_ORDER_DEFAULT) {
	    ft_options.base.subpixel_order = subpixel_order;
	    ft_options.base.antialias = CAIRO_ANTIALIAS_SUBPIXEL;
	}

#ifdef FC_HINT_STYLE    
	if (FcPatternGetInteger (pattern, 
				 FC_HINT_STYLE, 0, &hintstyle) != FcResultMatch)
	    hintstyle = FC_HINT_FULL;

	if (!hinting)
	    hintstyle = FC_HINT_NONE;

	switch (hintstyle) {
	case FC_HINT_NONE:
	    ft_options.base.hint_style = CAIRO_HINT_STYLE_NONE;	
	    break;
	case FC_HINT_SLIGHT:
	    ft_options.base.hint_style = CAIRO_HINT_STYLE_SLIGHT;
	    break;
	case FC_HINT_MEDIUM:
	default:
	    ft_options.base.hint_style = CAIRO_HINT_STYLE_MEDIUM;
	    break;
	case FC_HINT_FULL:
	    ft_options.base.hint_style = CAIRO_HINT_STYLE_FULL;
	    break;
	}
#else /* !FC_HINT_STYLE */
	if (!hinting) {
	    ft_options.base.hint_style = CAIRO_HINT_STYLE_NONE;
	}
#endif /* FC_FHINT_STYLE */
    } else {
	ft_options.base.antialias = CAIRO_ANTIALIAS_NONE;
    }

    /* force autohinting if requested */
    if (FcPatternGetBool (pattern,
			  FC_AUTOHINT, 0, &autohint) != FcResultMatch)
	autohint = FcFalse;

    if (autohint)
	ft_options.load_flags |= FT_LOAD_FORCE_AUTOHINT;

    if (FcPatternGetBool (pattern,
			  FC_VERTICAL_LAYOUT, 0, &vertical_layout) != FcResultMatch)
	vertical_layout = FcFalse;

    if (vertical_layout)
	ft_options.load_flags |= FT_LOAD_VERTICAL_LAYOUT;
    
#ifndef FC_EMBOLDEN
#define FC_EMBOLDEN "embolden"
#endif
    if (FcPatternGetBool (pattern,
			  FC_EMBOLDEN, 0, &embolden) != FcResultMatch)
	embolden = FcFalse;
    
    if (embolden)
	ft_options.extra_flags |= CAIRO_FT_OPTIONS_EMBOLDEN;

    *ret = ft_options;
}

static void
_cairo_ft_options_merge (cairo_ft_options_t *options,
			 cairo_ft_options_t *other)
{
    int load_flags = other->load_flags;
    int load_target = FT_LOAD_TARGET_NORMAL;

    /* clear load target mode */
    load_flags &= ~(FT_LOAD_TARGET_(FT_LOAD_TARGET_MODE(other->load_flags)));
    
    if (load_flags & FT_LOAD_NO_HINTING)
	other->base.hint_style = CAIRO_HINT_STYLE_NONE;

    if (other->base.antialias == CAIRO_ANTIALIAS_NONE ||
	options->base.antialias == CAIRO_ANTIALIAS_NONE) {
	options->base.antialias = CAIRO_ANTIALIAS_NONE;
	options->base.subpixel_order = CAIRO_SUBPIXEL_ORDER_DEFAULT;
    }

    if (other->base.antialias == CAIRO_ANTIALIAS_SUBPIXEL &&
	(options->base.antialias == CAIRO_ANTIALIAS_DEFAULT || 
	 options->base.antialias == CAIRO_ANTIALIAS_GRAY)) {
	options->base.antialias = CAIRO_ANTIALIAS_SUBPIXEL;
	options->base.subpixel_order = other->base.subpixel_order;
    }

    if (options->base.hint_style == CAIRO_HINT_STYLE_DEFAULT)
	options->base.hint_style = other->base.hint_style;

    if (other->base.hint_style == CAIRO_HINT_STYLE_NONE)
	options->base.hint_style = CAIRO_HINT_STYLE_NONE;

    if (options->base.antialias == CAIRO_ANTIALIAS_NONE) {
	if (options->base.hint_style == CAIRO_HINT_STYLE_NONE)
	    load_flags |= FT_LOAD_NO_HINTING;
	else
	    load_target = FT_LOAD_TARGET_MONO;
	load_flags |= FT_LOAD_MONOCHROME;
    } else {
	switch (options->base.hint_style) {
	case CAIRO_HINT_STYLE_NONE:
	    load_flags |= FT_LOAD_NO_HINTING;
	    break;
	case CAIRO_HINT_STYLE_SLIGHT:
	    load_target = FT_LOAD_TARGET_LIGHT;
	    break;
	case CAIRO_HINT_STYLE_MEDIUM:
	    break;
	case CAIRO_HINT_STYLE_FULL:
	case CAIRO_HINT_STYLE_DEFAULT:
	    if (options->base.antialias == CAIRO_ANTIALIAS_SUBPIXEL) {
		switch (options->base.subpixel_order) {
		case CAIRO_SUBPIXEL_ORDER_DEFAULT:
		case CAIRO_SUBPIXEL_ORDER_RGB:
		case CAIRO_SUBPIXEL_ORDER_BGR:
		    load_target |= FT_LOAD_TARGET_LCD;
		    break;
		case CAIRO_SUBPIXEL_ORDER_VRGB:
		case CAIRO_SUBPIXEL_ORDER_VBGR:
		    load_target |= FT_LOAD_TARGET_LCD_V;
		break;
		}
	    }
	    break;
	}
    }

    options->load_flags = load_flags | load_target;
    options->extra_flags = other->extra_flags;
}

static cairo_scaled_font_t *
_cairo_ft_scaled_font_create (cairo_ft_unscaled_font_t	 *unscaled,
			      cairo_font_face_t		 *font_face,
			      const cairo_matrix_t	 *font_matrix,
			      const cairo_matrix_t	 *ctm,
			      const cairo_font_options_t *options,
			      cairo_ft_options_t	  ft_options)
{
    cairo_ft_scaled_font_t *scaled_font = NULL;
    FT_Face face;
    FT_Size_Metrics *metrics;
    cairo_font_extents_t fs_metrics;
    cairo_status_t status;

    face = _cairo_ft_unscaled_font_lock_face (unscaled);
    if (!face)
	return NULL;

    scaled_font = malloc (sizeof(cairo_ft_scaled_font_t));
    if (scaled_font == NULL) {
	_cairo_ft_unscaled_font_unlock_face (unscaled);
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return NULL;
    }

    _cairo_unscaled_font_reference (&unscaled->base);
    scaled_font->unscaled = unscaled;

    if (options->hint_metrics != CAIRO_HINT_METRICS_OFF)
	ft_options.extra_flags |= CAIRO_FT_OPTIONS_HINT_METRICS;

    _cairo_font_options_init_copy (&scaled_font->ft_options.base, options);
    _cairo_ft_options_merge (&scaled_font->ft_options, &ft_options);

    status = _cairo_scaled_font_init (&scaled_font->base,
			              font_face,
				      font_matrix, ctm, options,
				      &cairo_ft_scaled_font_backend);
    if (status) {
	free (scaled_font);
	_cairo_unscaled_font_destroy (&unscaled->base);
	_cairo_ft_unscaled_font_unlock_face (unscaled);
	return NULL;
    }

    status = _cairo_ft_unscaled_font_set_scale (unscaled,
				                &scaled_font->base.scale);
    if (status) {
	free (scaled_font);
	_cairo_unscaled_font_destroy (&unscaled->base);
	_cairo_ft_unscaled_font_unlock_face (unscaled);
	return NULL;
    }


    metrics = &face->size->metrics;

    /*
     * Get to unscaled metrics so that the upper level can get back to
     * user space
     */
    if (scaled_font->base.options.hint_metrics != CAIRO_HINT_METRICS_OFF) {
	double x_factor, y_factor;

	if (unscaled->x_scale == 0)
	    x_factor = 0;
	else
	    x_factor = 1 / unscaled->x_scale;

	if (unscaled->y_scale == 0)
	    y_factor = 0;
	else
	    y_factor = 1 / unscaled->y_scale;

	fs_metrics.ascent =        DOUBLE_FROM_26_6(metrics->ascender) * y_factor;
	fs_metrics.descent =       DOUBLE_FROM_26_6(- metrics->descender) * y_factor;
	fs_metrics.height =        DOUBLE_FROM_26_6(metrics->height) * y_factor;
	if (!_cairo_ft_scaled_font_is_vertical (&scaled_font->base)) {
	    fs_metrics.max_x_advance = DOUBLE_FROM_26_6(metrics->max_advance) * x_factor;
	    fs_metrics.max_y_advance = 0;
	} else {
	    fs_metrics.max_x_advance = 0;
	    fs_metrics.max_y_advance = DOUBLE_FROM_26_6(metrics->max_advance) * y_factor;
	}
    } else {
	double scale = face->units_per_EM;

	fs_metrics.ascent =        face->ascender / scale;
	fs_metrics.descent =       - face->descender / scale;
	fs_metrics.height =        face->height / scale;
	if (!_cairo_ft_scaled_font_is_vertical (&scaled_font->base)) {
	    fs_metrics.max_x_advance = face->max_advance_width / scale;
	    fs_metrics.max_y_advance = 0;
	} else {
	    fs_metrics.max_x_advance = 0;
	    fs_metrics.max_y_advance = face->max_advance_height / scale;
	}
    }

    _cairo_scaled_font_set_metrics (&scaled_font->base, &fs_metrics);

    _cairo_ft_unscaled_font_unlock_face (unscaled);

    return &scaled_font->base;
}

cairo_bool_t
_cairo_scaled_font_is_ft (cairo_scaled_font_t *scaled_font)
{
    return scaled_font->backend == &cairo_ft_scaled_font_backend;
}

static cairo_status_t
_cairo_ft_scaled_font_create_toy (cairo_toy_font_face_t	      *toy_face,
				  const cairo_matrix_t	      *font_matrix,
				  const cairo_matrix_t	      *ctm,
				  const cairo_font_options_t  *font_options,
				  cairo_scaled_font_t	     **font)
{
    FcPattern *pattern, *resolved;
    cairo_ft_unscaled_font_t *unscaled;
    cairo_scaled_font_t *new_font = NULL;
    FcResult result;
    int fcslant;
    int fcweight;
    cairo_matrix_t scale;
    cairo_ft_font_transform_t sf;
    cairo_ft_options_t ft_options;
    unsigned char *family = (unsigned char*) toy_face->family;

    pattern = FcPatternCreate ();
    if (!pattern) {
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return CAIRO_STATUS_NO_MEMORY;
    }

    switch (toy_face->weight)
    {
    case CAIRO_FONT_WEIGHT_BOLD:
        fcweight = FC_WEIGHT_BOLD;
        break;
    case CAIRO_FONT_WEIGHT_NORMAL:
    default:
        fcweight = FC_WEIGHT_MEDIUM;
        break;
    }

    switch (toy_face->slant)
    {
    case CAIRO_FONT_SLANT_ITALIC:
        fcslant = FC_SLANT_ITALIC;
        break;
    case CAIRO_FONT_SLANT_OBLIQUE:
	fcslant = FC_SLANT_OBLIQUE;
        break;
    case CAIRO_FONT_SLANT_NORMAL:
    default:
        fcslant = FC_SLANT_ROMAN;
        break;
    }

    if (!FcPatternAddString (pattern, FC_FAMILY, family))
	goto FREE_PATTERN;
    if (!FcPatternAddInteger (pattern, FC_SLANT, fcslant))
	goto FREE_PATTERN;
    if (!FcPatternAddInteger (pattern, FC_WEIGHT, fcweight))
	goto FREE_PATTERN;

    cairo_matrix_multiply (&scale, font_matrix, ctm);
    _compute_transform (&sf, &scale);

    FcPatternAddInteger (pattern, FC_PIXEL_SIZE, sf.y_scale);

    FcConfigSubstitute (NULL, pattern, FcMatchPattern);
    cairo_ft_font_options_substitute (font_options, pattern);
    FcDefaultSubstitute (pattern);

    resolved = FcFontMatch (NULL, pattern, &result);
    if (!resolved)
	goto FREE_PATTERN;

    unscaled = _cairo_ft_unscaled_font_create_for_pattern (resolved);
    if (!unscaled)
	goto FREE_RESOLVED;

    _get_pattern_ft_options (resolved, &ft_options);

    new_font = _cairo_ft_scaled_font_create (unscaled,
					     &toy_face->base,
					     font_matrix, ctm,
					     font_options, ft_options);

    _cairo_unscaled_font_destroy (&unscaled->base);

 FREE_RESOLVED:
    FcPatternDestroy (resolved);

 FREE_PATTERN:
    FcPatternDestroy (pattern);

    if (new_font) {
	*font = new_font;
	return CAIRO_STATUS_SUCCESS;
    } else {
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return CAIRO_STATUS_NO_MEMORY;
    }
}

static void
_cairo_ft_scaled_font_fini (void *abstract_font)
{
    cairo_ft_scaled_font_t *scaled_font = abstract_font;

    if (scaled_font == NULL)
        return;

    _cairo_unscaled_font_destroy (&scaled_font->unscaled->base);
}

static int
_move_to (FT_Vector *to, void *closure)
{
    cairo_path_fixed_t *path = closure;
    cairo_fixed_t x, y;

    x = _cairo_fixed_from_26_6 (to->x);
    y = _cairo_fixed_from_26_6 (to->y);

    if (_cairo_path_fixed_close_path (path) != CAIRO_STATUS_SUCCESS)
	return 1;
    if (_cairo_path_fixed_move_to (path, x, y) != CAIRO_STATUS_SUCCESS)
	return 1;

    return 0;
}

static int
_line_to (FT_Vector *to, void *closure)
{
    cairo_path_fixed_t *path = closure;
    cairo_fixed_t x, y;

    x = _cairo_fixed_from_26_6 (to->x);
    y = _cairo_fixed_from_26_6 (to->y);

    if (_cairo_path_fixed_line_to (path, x, y) != CAIRO_STATUS_SUCCESS)
	return 1;

    return 0;
}

static int
_conic_to (FT_Vector *control, FT_Vector *to, void *closure)
{
    cairo_path_fixed_t *path = closure;

    cairo_fixed_t x0, y0;
    cairo_fixed_t x1, y1;
    cairo_fixed_t x2, y2;
    cairo_fixed_t x3, y3;
    cairo_point_t conic;

    if (_cairo_path_fixed_get_current_point (path, &x0, &y0) !=
	    CAIRO_STATUS_SUCCESS)
	return 1;

    conic.x = _cairo_fixed_from_26_6 (control->x);
    conic.y = _cairo_fixed_from_26_6 (control->y);

    x3 = _cairo_fixed_from_26_6 (to->x);
    y3 = _cairo_fixed_from_26_6 (to->y);

    x1 = x0 + 2.0/3.0 * (conic.x - x0);
    y1 = y0 + 2.0/3.0 * (conic.y - y0);

    x2 = x3 + 2.0/3.0 * (conic.x - x3);
    y2 = y3 + 2.0/3.0 * (conic.y - y3);

    if (_cairo_path_fixed_curve_to (path,
				    x1, y1,
				    x2, y2,
				    x3, y3) != CAIRO_STATUS_SUCCESS)
	return 1;

    return 0;
}

static int
_cubic_to (FT_Vector *control1, FT_Vector *control2,
	   FT_Vector *to, void *closure)
{
    cairo_path_fixed_t *path = closure;
    cairo_fixed_t x0, y0;
    cairo_fixed_t x1, y1;
    cairo_fixed_t x2, y2;

    x0 = _cairo_fixed_from_26_6 (control1->x);
    y0 = _cairo_fixed_from_26_6 (control1->y);

    x1 = _cairo_fixed_from_26_6 (control2->x);
    y1 = _cairo_fixed_from_26_6 (control2->y);

    x2 = _cairo_fixed_from_26_6 (to->x);
    y2 = _cairo_fixed_from_26_6 (to->y);

    if (_cairo_path_fixed_curve_to (path,
				    x0, y0,
				    x1, y1,
				    x2, y2) != CAIRO_STATUS_SUCCESS)
	return 1;

    return 0;
}

static cairo_status_t
_decompose_glyph_outline (FT_Face		  face,
			  cairo_font_options_t	 *options,
			  cairo_path_fixed_t	**pathp)
{
    static const FT_Outline_Funcs outline_funcs = {
	(FT_Outline_MoveToFunc)_move_to,
	(FT_Outline_LineToFunc)_line_to,
	(FT_Outline_ConicToFunc)_conic_to,
	(FT_Outline_CubicToFunc)_cubic_to,
	0, /* shift */
	0, /* delta */
    };
    static const FT_Matrix invert_y = {
	DOUBLE_TO_16_16 (1.0), 0,
	0, DOUBLE_TO_16_16 (-1.0),
    };

    FT_GlyphSlot glyph;
    cairo_path_fixed_t *path;
    cairo_status_t status;

    path = _cairo_path_fixed_create ();
    if (!path)
	return CAIRO_STATUS_NO_MEMORY;

    glyph = face->glyph;

    /* Font glyphs have an inverted Y axis compared to cairo. */
    FT_Outline_Transform (&glyph->outline, &invert_y);
    if (FT_Outline_Decompose (&glyph->outline, &outline_funcs, path)) {
	_cairo_path_fixed_destroy (path);
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return CAIRO_STATUS_NO_MEMORY;
    }

    status = _cairo_path_fixed_close_path (path);
    if (status) {
	_cairo_path_fixed_destroy (path);
	return status;
    }

    *pathp = path;

    return CAIRO_STATUS_SUCCESS;
}

/*
 * Translate glyph to match its metrics.
 */
static void
_cairo_ft_scaled_glyph_vertical_layout_bearing_fix (void        *abstract_font,
						    FT_GlyphSlot glyph)
{
    cairo_ft_scaled_font_t *scaled_font = abstract_font;
    FT_Vector vector;

    vector.x = glyph->metrics.vertBearingX - glyph->metrics.horiBearingX;
    vector.y = -glyph->metrics.vertBearingY - glyph->metrics.horiBearingY;

    if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
	FT_Vector_Transform (&vector, &scaled_font->unscaled->Current_Shape);
	FT_Outline_Translate(&glyph->outline, vector.x, vector.y);
    } else if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
	glyph->bitmap_left += vector.x / 64;
	glyph->bitmap_top  += vector.y / 64;
    }
}

static cairo_int_status_t
_cairo_ft_scaled_glyph_init (void			*abstract_font,
			     cairo_scaled_glyph_t	*scaled_glyph,
			     cairo_scaled_glyph_info_t	 info)
{
    cairo_text_extents_t    fs_metrics;
    cairo_ft_scaled_font_t *scaled_font = abstract_font;
    cairo_ft_unscaled_font_t *unscaled = scaled_font->unscaled;
    FT_GlyphSlot glyph;
    FT_Face face;
    FT_Error error;
    int load_flags = scaled_font->ft_options.load_flags;
    FT_Glyph_Metrics *metrics;
    double x_factor, y_factor;
    cairo_bool_t vertical_layout = FALSE;
    cairo_status_t status;

    face = _cairo_ft_unscaled_font_lock_face (unscaled);
    if (!face)
	return CAIRO_STATUS_NO_MEMORY;

    status = _cairo_ft_unscaled_font_set_scale (scaled_font->unscaled,
				                &scaled_font->base.scale);
    if (status)
	goto FAIL;

    /* Ignore global advance unconditionally */
    load_flags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;

    if ((info & CAIRO_SCALED_GLYPH_INFO_PATH) != 0 &&
	(info & CAIRO_SCALED_GLYPH_INFO_SURFACE) == 0)
	load_flags |= FT_LOAD_NO_BITMAP;

    /*
     * Don't pass FT_LOAD_VERTICAL_LAYOUT to FT_Load_Glyph here as
     * suggested by freetype people.
     */
    if (load_flags & FT_LOAD_VERTICAL_LAYOUT) {
	load_flags &= ~FT_LOAD_VERTICAL_LAYOUT;
	vertical_layout = TRUE;
    }

    error = FT_Load_Glyph (scaled_font->unscaled->face,
			   _cairo_scaled_glyph_index(scaled_glyph),
			   load_flags);
    /* XXX ignoring all other errors for now.  They are not fatal, typically
     * just a glyph-not-found. */
    if (error == FT_Err_Out_Of_Memory) {
	status = CAIRO_STATUS_NO_MEMORY;
	goto FAIL;
    }

    glyph = face->glyph;

#if HAVE_FT_GLYPHSLOT_EMBOLDEN
    /*
     * embolden glyphs if requested
     */
    if (scaled_font->ft_options.extra_flags & CAIRO_FT_OPTIONS_EMBOLDEN)
	FT_GlyphSlot_Embolden (glyph);
#endif

    if (vertical_layout)
	_cairo_ft_scaled_glyph_vertical_layout_bearing_fix (scaled_font, glyph);

    if (info & CAIRO_SCALED_GLYPH_INFO_METRICS) {

	cairo_bool_t hint_metrics = scaled_font->base.options.hint_metrics != CAIRO_HINT_METRICS_OFF;
	/*
	 * Compute font-space metrics
	 */
	metrics = &glyph->metrics;

	if (unscaled->x_scale == 0)
	    x_factor = 0;
	else
	    x_factor = 1 / unscaled->x_scale;

	if (unscaled->y_scale == 0)
	    y_factor = 0;
	else
	    y_factor = 1 / unscaled->y_scale;

	/*
	 * Note: Y coordinates of the horizontal bearing need to be negated.
	 *
	 * Scale metrics back to glyph space from the scaled glyph space returned
	 * by FreeType
	 *
	 * If we want hinted metrics but aren't asking for hinted glyphs from
	 * FreeType, then we need to do the metric hinting ourselves.
	 */

	if (hint_metrics && (load_flags & FT_LOAD_NO_HINTING))
	{
	    FT_Pos x1, x2;
	    FT_Pos y1, y2;
	    FT_Pos advance;
	    
	    if (!vertical_layout) {
		x1 = (metrics->horiBearingX) & -64;
		x2 = (metrics->horiBearingX + metrics->width + 63) & -64;
		y1 = (-metrics->horiBearingY) & -64;
		y2 = (-metrics->horiBearingY + metrics->height + 63) & -64;
		
		advance = ((metrics->horiAdvance + 32) & -64);
		
		fs_metrics.x_bearing = DOUBLE_FROM_26_6 (x1) * x_factor;
		fs_metrics.y_bearing = DOUBLE_FROM_26_6 (y1) * y_factor;
		
		fs_metrics.width  = DOUBLE_FROM_26_6 (x2 - x1) * x_factor;
		fs_metrics.height  = DOUBLE_FROM_26_6 (y2 - y1) * y_factor;

		fs_metrics.x_advance = DOUBLE_FROM_26_6 (advance) * x_factor;
		fs_metrics.y_advance = 0;
	    } else {
		x1 = (metrics->vertBearingX) & -64;
		x2 = (metrics->vertBearingX + metrics->width + 63) & -64;
		y1 = (metrics->vertBearingY) & -64;
		y2 = (metrics->vertBearingY + metrics->height + 63) & -64;
		
		advance = ((metrics->vertAdvance + 32) & -64);
		
		fs_metrics.x_bearing = DOUBLE_FROM_26_6 (x1) * x_factor;
		fs_metrics.y_bearing = DOUBLE_FROM_26_6 (y1) * y_factor;
		
		fs_metrics.width  = DOUBLE_FROM_26_6 (x2 - x1) * x_factor;
		fs_metrics.height  = DOUBLE_FROM_26_6 (y2 - y1) * y_factor;

		fs_metrics.x_advance = 0;
		fs_metrics.y_advance = DOUBLE_FROM_26_6 (advance) * y_factor;
	    }
	 } else {
	    fs_metrics.width  = DOUBLE_FROM_26_6 (metrics->width) * x_factor;
	    fs_metrics.height = DOUBLE_FROM_26_6 (metrics->height) * y_factor;

	    if (!vertical_layout) {
		fs_metrics.x_bearing = DOUBLE_FROM_26_6 (metrics->horiBearingX) * x_factor;
		fs_metrics.y_bearing = DOUBLE_FROM_26_6 (-metrics->horiBearingY) * y_factor;
		
		if (hint_metrics || glyph->format != FT_GLYPH_FORMAT_OUTLINE)
		    fs_metrics.x_advance = DOUBLE_FROM_26_6 (metrics->horiAdvance) * x_factor;
		else
		    fs_metrics.x_advance = DOUBLE_FROM_16_16 (glyph->linearHoriAdvance) * x_factor;
		fs_metrics.y_advance = 0 * y_factor;
	    } else {
		fs_metrics.x_bearing = DOUBLE_FROM_26_6 (metrics->vertBearingX) * x_factor;
		fs_metrics.y_bearing = DOUBLE_FROM_26_6 (metrics->vertBearingY) * y_factor;
		
		fs_metrics.x_advance = 0 * x_factor;
		if (hint_metrics || glyph->format != FT_GLYPH_FORMAT_OUTLINE)
		    fs_metrics.y_advance = DOUBLE_FROM_26_6 (metrics->vertAdvance) * y_factor;
		else
		    fs_metrics.y_advance = DOUBLE_FROM_26_6 (glyph->linearVertAdvance) * y_factor;
	    }
	 }

	_cairo_scaled_glyph_set_metrics (scaled_glyph,
					 &scaled_font->base,
					 &fs_metrics);
    }

    if ((info & CAIRO_SCALED_GLYPH_INFO_SURFACE) != 0) {
	cairo_image_surface_t	*surface;

	if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
	    status = _render_glyph_outline (face, &scaled_font->ft_options.base,
					    &surface);
	} else {
	    status = _render_glyph_bitmap (face, &scaled_font->ft_options.base,
					   &surface);
	    if (status == CAIRO_STATUS_SUCCESS && unscaled->have_shape) {
		status = _transform_glyph_bitmap (&unscaled->current_shape,
						  &surface);
		if (status)
		    cairo_surface_destroy (&surface->base);
	    }
	}
	if (status)
	    goto FAIL;

	_cairo_scaled_glyph_set_surface (scaled_glyph,
					 &scaled_font->base,
					 surface);
    }

    if (info & CAIRO_SCALED_GLYPH_INFO_PATH) {
	cairo_path_fixed_t *path;

	/*
	 * A kludge -- the above code will trash the outline,
	 * so reload it. This will probably never occur though
	 */
	if ((info & CAIRO_SCALED_GLYPH_INFO_SURFACE) != 0) {
	    error = FT_Load_Glyph (face,
				   _cairo_scaled_glyph_index(scaled_glyph),
				   load_flags | FT_LOAD_NO_BITMAP);
	    /* XXX ignoring all other errors for now.  They are not fatal, typically
	     * just a glyph-not-found. */
	    if (error == FT_Err_Out_Of_Memory) {
		_cairo_ft_unscaled_font_unlock_face (unscaled);
		_cairo_error (CAIRO_STATUS_NO_MEMORY);
		return CAIRO_STATUS_NO_MEMORY;
	    }
#if HAVE_FT_GLYPHSLOT_EMBOLDEN
	    /*
	     * embolden glyphs if requested
	     */
	    if (scaled_font->ft_options.extra_flags & CAIRO_FT_OPTIONS_EMBOLDEN)
		FT_GlyphSlot_Embolden (glyph);
#endif
	    if (vertical_layout)
		_cairo_ft_scaled_glyph_vertical_layout_bearing_fix (scaled_font, glyph);

	}
	if (glyph->format == FT_GLYPH_FORMAT_OUTLINE)
	    status = _decompose_glyph_outline (face, &scaled_font->ft_options.base,
					       &path);
	else
	    status = CAIRO_INT_STATUS_UNSUPPORTED;

	if (status)
	    goto FAIL;

	_cairo_scaled_glyph_set_path (scaled_glyph,
				      &scaled_font->base,
				      path);
    }
 FAIL:
    _cairo_ft_unscaled_font_unlock_face (unscaled);

    return status;
}

static unsigned long
_cairo_ft_ucs4_to_index (void	    *abstract_font,
			 uint32_t    ucs4)
{
    cairo_ft_scaled_font_t *scaled_font = abstract_font;
    cairo_ft_unscaled_font_t *unscaled = scaled_font->unscaled;
    FT_Face face;
    FT_UInt index;

    face = _cairo_ft_unscaled_font_lock_face (unscaled);
    if (!face)
	return 0;
    index = FT_Get_Char_Index (face, ucs4);
    _cairo_ft_unscaled_font_unlock_face (unscaled);
    return index;
}

static cairo_int_status_t
_cairo_ft_load_truetype_table (void	       *abstract_font,
                              unsigned long     tag,
                              long              offset,
                              unsigned char    *buffer,
                              unsigned long    *length)
{
    cairo_ft_scaled_font_t *scaled_font = abstract_font;
    cairo_ft_unscaled_font_t *unscaled = scaled_font->unscaled;
    FT_Face face;
    cairo_status_t status = CAIRO_INT_STATUS_UNSUPPORTED;

    if (_cairo_ft_scaled_font_is_vertical (&scaled_font->base))
        return CAIRO_INT_STATUS_UNSUPPORTED;

#if HAVE_FT_LOAD_SFNT_TABLE
    face = _cairo_ft_unscaled_font_lock_face (unscaled);
    if (!face)
	return CAIRO_STATUS_NO_MEMORY;

    if (FT_IS_SFNT (face) &&
	FT_Load_Sfnt_Table (face, tag, offset, buffer, length) == 0)
        status = CAIRO_STATUS_SUCCESS;

    _cairo_ft_unscaled_font_unlock_face (unscaled);
#endif

    return status;
}

static void
_cairo_ft_map_glyphs_to_unicode (void	                    *abstract_font,
                                 cairo_scaled_font_subset_t *font_subset)
{
    cairo_ft_scaled_font_t *scaled_font = abstract_font;
    cairo_ft_unscaled_font_t *unscaled = scaled_font->unscaled;
    FT_Face face;
    FT_UInt glyph;
    unsigned long charcode;
    unsigned int i;
    int count;

    face = _cairo_ft_unscaled_font_lock_face (unscaled);
    if (!face)
	return;

    count = font_subset->num_glyphs;
    charcode = FT_Get_First_Char( face, &glyph);
    while (glyph != 0 && count > 0)
    {
        for (i = 0; i < font_subset->num_glyphs; i++) {
            if (font_subset->glyphs[i] == glyph) {
                font_subset->to_unicode[i] = charcode;
                count--;
                break;
            }
        }
        charcode = FT_Get_Next_Char(face, charcode, &glyph);
    }
    _cairo_ft_unscaled_font_unlock_face (unscaled);
}

const cairo_scaled_font_backend_t cairo_ft_scaled_font_backend = {
    CAIRO_FONT_TYPE_FT,
    _cairo_ft_scaled_font_create_toy,
    _cairo_ft_scaled_font_fini,
    _cairo_ft_scaled_glyph_init,
    NULL,			/* text_to_glyphs */
    _cairo_ft_ucs4_to_index,
    NULL, 			/* show_glyphs */
    _cairo_ft_load_truetype_table,
    _cairo_ft_map_glyphs_to_unicode,
};

/* cairo_ft_font_face_t */

static void
_cairo_ft_font_face_destroy (void *abstract_face)
{
    cairo_ft_font_face_t *font_face = abstract_face;

    cairo_ft_font_face_t *tmp_face = NULL;
    cairo_ft_font_face_t *last_face = NULL;

    if (font_face == NULL)
	return;

    /* When destroying the face created by cairo_ft_font_face_create_for_ft_face,
     * we have a special "zombie" state for the face when the unscaled font
     * is still alive but there are no public references to the font face.
     *
     * We go from:
     *
     *   font_face ------> unscaled
     *        <-....weak....../
     *
     * To:
     *
     *    font_face <------- unscaled
     */

    if (font_face->unscaled &&
	font_face->unscaled->from_face &&
	font_face->unscaled->base.ref_count > 1)
    {
	cairo_font_face_reference (&font_face->base);

	_cairo_unscaled_font_destroy (&font_face->unscaled->base);
	font_face->unscaled = NULL;

	return;
    }

    if (font_face->unscaled) {
	/* Remove face from linked list */
	for (tmp_face = font_face->unscaled->faces;
	     tmp_face;
	     tmp_face = tmp_face->next)
	{
	    if (tmp_face == font_face) {
		if (last_face)
		    last_face->next = tmp_face->next;
		else
		    font_face->unscaled->faces = tmp_face->next;
	    }

	    last_face = tmp_face;
	}

	_cairo_unscaled_font_destroy (&font_face->unscaled->base);
	font_face->unscaled = NULL;
    }
}

static cairo_status_t
_cairo_ft_font_face_scaled_font_create (void                     *abstract_face,
					const cairo_matrix_t       *font_matrix,
					const cairo_matrix_t       *ctm,
					const cairo_font_options_t *options,
					cairo_scaled_font_t       **scaled_font)
{
    cairo_ft_font_face_t *font_face = abstract_face;
    cairo_ft_options_t ft_options;

    /* The handling of font options is different depending on how the
     * font face was created. When the user creates a font face with
     * cairo_ft_font_face_create_for_ft_face(), then the load flags
     * passed in augment the load flags for the options.  But for
     * cairo_ft_font_face_create_for_pattern(), the load flags are
     * derived from a pattern where the user has called
     * cairo_ft_font_options_substitute(), so *just* use those load
     * flags and ignore the options.
     */

    ft_options = font_face->ft_options;

    *scaled_font = _cairo_ft_scaled_font_create (font_face->unscaled,
						 &font_face->base,
						 font_matrix, ctm,
						 options, ft_options);
    if (*scaled_font) {
	return CAIRO_STATUS_SUCCESS;
    } else {
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return CAIRO_STATUS_NO_MEMORY;
    }
}

static const cairo_font_face_backend_t _cairo_ft_font_face_backend = {
    CAIRO_FONT_TYPE_FT,
    _cairo_ft_font_face_destroy,
    _cairo_ft_font_face_scaled_font_create
};

static cairo_font_face_t *
_cairo_ft_font_face_create (cairo_ft_unscaled_font_t *unscaled,
			    cairo_ft_options_t	     *ft_options)
{
    cairo_ft_font_face_t *font_face, **prev_font_face;

    /* Looked for an existing matching font face */
    for (font_face = unscaled->faces, prev_font_face = &unscaled->faces;
	 font_face;
	 prev_font_face = &font_face->next, font_face = font_face->next)
    {
	if (font_face->ft_options.load_flags == ft_options->load_flags &&
	    font_face->ft_options.extra_flags == ft_options->extra_flags &&
	    cairo_font_options_equal (&font_face->ft_options.base, &ft_options->base))
	{
	    if (! font_face->base.status)
		return cairo_font_face_reference (&font_face->base);

	    /* The font_face has been left in an error state, abandon it. */
	    *prev_font_face = font_face->next;
	    break;
	}
    }

    /* No match found, create a new one */
    font_face = malloc (sizeof (cairo_ft_font_face_t));
    if (!font_face) {
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return NULL;
    }

    font_face->unscaled = unscaled;
    _cairo_unscaled_font_reference (&unscaled->base);

    font_face->ft_options = *ft_options;

    font_face->next = unscaled->faces;
    unscaled->faces = font_face;

    _cairo_font_face_init (&font_face->base, &_cairo_ft_font_face_backend);

    return &font_face->base;
}

/* implement the platform-specific interface */

/**
 * cairo_ft_font_options_substitute:
 * @options: a #cairo_font_options_t object
 * @pattern: an existing #FcPattern
 *
 * Add options to a #FcPattern based on a #cairo_font_options_t font
 * options object. Options that are already in the pattern, are not overridden,
 * so you should call this function after calling FcConfigSubstitute() (the
 * user's settings should override options based on the surface type), but
 * before calling FcDefaultSubstitute().
 **/
void
cairo_ft_font_options_substitute (const cairo_font_options_t *options,
				  FcPattern                  *pattern)
{
    FcValue v;

    if (options->antialias != CAIRO_ANTIALIAS_DEFAULT)
    {
	if (FcPatternGet (pattern, FC_ANTIALIAS, 0, &v) == FcResultNoMatch)
	{
	    FcPatternAddBool (pattern, FC_ANTIALIAS, options->antialias != CAIRO_ANTIALIAS_NONE);
	    if (options->antialias != CAIRO_ANTIALIAS_SUBPIXEL) {
		FcPatternDel (pattern, FC_RGBA);
		FcPatternAddInteger (pattern, FC_RGBA, FC_RGBA_NONE);
	    }
	}
    }

    if (options->antialias != CAIRO_ANTIALIAS_DEFAULT)
    {
	if (FcPatternGet (pattern, FC_RGBA, 0, &v) == FcResultNoMatch)
	{
	    int rgba;

	    if (options->antialias == CAIRO_ANTIALIAS_SUBPIXEL) {
		switch (options->subpixel_order) {
		case CAIRO_SUBPIXEL_ORDER_DEFAULT:
		case CAIRO_SUBPIXEL_ORDER_RGB:
		default:
		    rgba = FC_RGBA_RGB;
		    break;
		case CAIRO_SUBPIXEL_ORDER_BGR:
		    rgba = FC_RGBA_BGR;
		    break;
		case CAIRO_SUBPIXEL_ORDER_VRGB:
		    rgba = FC_RGBA_VRGB;
		    break;
		case CAIRO_SUBPIXEL_ORDER_VBGR:
		    rgba = FC_RGBA_VBGR;
		    break;
		}
	    } else {
		rgba = FC_RGBA_NONE;
	    }

	    FcPatternAddInteger (pattern, FC_RGBA, rgba);
	}
    }

    if (options->hint_style != CAIRO_HINT_STYLE_DEFAULT)
    {
	if (FcPatternGet (pattern, FC_HINTING, 0, &v) == FcResultNoMatch)
	{
	    FcPatternAddBool (pattern, FC_HINTING, options->hint_style != CAIRO_HINT_STYLE_NONE);
	}

#ifdef FC_HINT_STYLE
	if (FcPatternGet (pattern, FC_HINT_STYLE, 0, &v) == FcResultNoMatch)
	{
	    int hint_style;

	    switch (options->hint_style) {
	    case CAIRO_HINT_STYLE_NONE:
		hint_style = FC_HINT_NONE;
		break;
	    case CAIRO_HINT_STYLE_SLIGHT:
		hint_style = FC_HINT_SLIGHT;
		break;
	    case CAIRO_HINT_STYLE_MEDIUM:
		hint_style = FC_HINT_MEDIUM;
		break;
	    case CAIRO_HINT_STYLE_FULL:
	    case CAIRO_HINT_STYLE_DEFAULT:
	    default:
		hint_style = FC_HINT_FULL;
		break;
	    }

	    FcPatternAddInteger (pattern, FC_HINT_STYLE, hint_style);
	}
#endif
    }
}
slim_hidden_def (cairo_ft_font_options_substitute);

/**
 * cairo_ft_font_face_create_for_pattern:
 * @pattern: A fully resolved fontconfig
 *   pattern. A pattern can be resolved, by, among other things, calling
 *   FcConfigSubstitute(), FcDefaultSubstitute(), then
 *   FcFontMatch(). Cairo will call FcPatternReference() on this
 *   pattern, so you should not further modify the pattern, but you can
 *   release your reference to the pattern with FcPatternDestroy() if
 *   you no longer need to access it.
 *
 * Creates a new font face for the FreeType font backend based on a
 * fontconfig pattern. This font can then be used with
 * cairo_set_font_face() or cairo_scaled_font_create(). The
 * #cairo_scaled_font_t returned from cairo_scaled_font_create() is
 * also for the FreeType backend and can be used with functions such
 * as cairo_ft_font_lock_face().
 *
 * Font rendering options are represented both here and when you
 * call cairo_scaled_font_create(). Font options that have a representation
 * in a #FcPattern must be passed in here; to modify #FcPattern
 * appropriately to reflect the options in a #cairo_font_options_t, call
 * cairo_ft_font_options_substitute().
 *
 * Return value: a newly created #cairo_font_face_t. Free with
 *  cairo_font_face_destroy() when you are done using it.
 **/
cairo_font_face_t *
cairo_ft_font_face_create_for_pattern (FcPattern *pattern)
{
    cairo_ft_unscaled_font_t *unscaled;
    cairo_font_face_t *font_face;
    cairo_ft_options_t ft_options;

    unscaled = _cairo_ft_unscaled_font_create_for_pattern (pattern);
    if (unscaled == NULL) {
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return (cairo_font_face_t *)&_cairo_font_face_nil;
    }

    _get_pattern_ft_options (pattern, &ft_options);
    font_face = _cairo_ft_font_face_create (unscaled, &ft_options);
    _cairo_unscaled_font_destroy (&unscaled->base);

    if (font_face)
	return font_face;
    else
	return (cairo_font_face_t *)&_cairo_font_face_nil;
}

/**
 * cairo_ft_font_face_create_for_ft_face:
 * @face: A FreeType face object, already opened. This must
 *   be kept around until the face's ref_count drops to
 *   zero and it is freed. Since the face may be referenced
 *   internally to Cairo, the best way to determine when it
 *   is safe to free the face is to pass a
 *   #cairo_destroy_func_t to cairo_font_face_set_user_data()
 * @load_flags: flags to pass to FT_Load_Glyph when loading
 *   glyphs from the font. These flags are OR'ed together with
 *   the flags derived from the #cairo_font_options_t passed
 *   to cairo_scaled_font_create(), so only a few values such
 *   as %FT_LOAD_VERTICAL_LAYOUT, and %FT_LOAD_FORCE_AUTOHINT
 *   are useful. You should not pass any of the flags affecting
 *   the load target, such as %FT_LOAD_TARGET_LIGHT.
 *
 * Creates a new font face for the FreeType font backend from a
 * pre-opened FreeType face. This font can then be used with
 * cairo_set_font_face() or cairo_scaled_font_create(). The
 * #cairo_scaled_font_t returned from cairo_scaled_font_create() is
 * also for the FreeType backend and can be used with functions such
 * as cairo_ft_font_lock_face().
 *
 * Return value: a newly created #cairo_font_face_t. Free with
 *  cairo_font_face_destroy() when you are done using it.
 **/
cairo_font_face_t *
cairo_ft_font_face_create_for_ft_face (FT_Face         face,
				       int             load_flags)
{
    cairo_ft_unscaled_font_t *unscaled;
    cairo_font_face_t *font_face;
    cairo_ft_options_t ft_options;

    unscaled = _cairo_ft_unscaled_font_create_from_face (face);
    if (unscaled == NULL) {
	_cairo_error (CAIRO_STATUS_NO_MEMORY);
	return (cairo_font_face_t *)&_cairo_font_face_nil;
    }

    ft_options.load_flags = load_flags;
    ft_options.extra_flags = 0;
    _cairo_font_options_init_default (&ft_options.base);

    font_face = _cairo_ft_font_face_create (unscaled, &ft_options);
    _cairo_unscaled_font_destroy (&unscaled->base);

    if (font_face)
	return font_face;
    else
	return (cairo_font_face_t *)&_cairo_font_face_nil;
}

/**
 * cairo_ft_scaled_font_lock_face:
 * @scaled_font: A #cairo_scaled_font_t from the FreeType font backend. Such an
 *   object can be created by calling cairo_scaled_font_create() on a
 *   FreeType backend font face (see cairo_ft_font_face_create_for_pattern(),
 *   cairo_ft_font_face_create_for_face()).
 *
 * cairo_ft_font_lock_face() gets the #FT_Face object from a FreeType
 * backend font and scales it appropriately for the font. You must
 * release the face with cairo_ft_font_unlock_face()
 * when you are done using it.  Since the #FT_Face object can be
 * shared between multiple #cairo_scaled_font_t objects, you must not
 * lock any other font objects until you unlock this one. A count is
 * kept of the number of times cairo_ft_font_lock_face() is
 * called. cairo_ft_font_unlock_face() must be called the same number
 * of times.
 *
 * You must be careful when using this function in a library or in a
 * threaded application, because freetype's design makes it unsafe to
 * call freetype functions simultaneously from multiple threads, (even
 * if using distinct FT_Face objects). Because of this, application
 * code that acquires an FT_Face object with this call must add it's
 * own locking to protect any use of that object, (and which also must
 * protect any other calls into cairo as almost any cairo function
 * might result in a call into the freetype library).
 *
 * Return value: The #FT_Face object for @font, scaled appropriately,
 * or %NULL if @scaled_font is in an error state (see
 * cairo_scaled_font_status()) or there is insufficient memory.
 **/
FT_Face
cairo_ft_scaled_font_lock_face (cairo_scaled_font_t *abstract_font)
{
    cairo_ft_scaled_font_t *scaled_font = (cairo_ft_scaled_font_t *) abstract_font;
    FT_Face face;
    cairo_status_t status;

    if (scaled_font->base.status)
	return NULL;

    face = _cairo_ft_unscaled_font_lock_face (scaled_font->unscaled);
    if (face == NULL) {
	_cairo_scaled_font_set_error (&scaled_font->base, CAIRO_STATUS_NO_MEMORY);
	return NULL;
    }

    status = _cairo_ft_unscaled_font_set_scale (scaled_font->unscaled,
				                &scaled_font->base.scale);
    if (status) {
	_cairo_ft_unscaled_font_unlock_face (scaled_font->unscaled);
	_cairo_scaled_font_set_error (&scaled_font->base, status);
	return NULL;
    }

    /* NOTE: We deliberately release the unscaled font's mutex here,
     * so that we are not holding a lock across two separate calls to
     * cairo function, (which would give the application some
     * opportunity for creating deadlock. This is obviously unsafe,
     * but as documented, the user must add manual locking when using
     * this function. */
     CAIRO_MUTEX_UNLOCK (scaled_font->unscaled->mutex);

    return face;
}

/**
 * cairo_ft_scaled_font_unlock_face:
 * @scaled_font: A #cairo_scaled_font_t from the FreeType font backend. Such an
 *   object can be created by calling cairo_scaled_font_create() on a
 *   FreeType backend font face (see cairo_ft_font_face_create_for_pattern(),
 *   cairo_ft_font_face_create_for_ft_face()).
 *
 * Releases a face obtained with cairo_ft_scaled_font_lock_face().
 **/
void
cairo_ft_scaled_font_unlock_face (cairo_scaled_font_t *abstract_font)
{
    cairo_ft_scaled_font_t *scaled_font = (cairo_ft_scaled_font_t *) abstract_font;

    if (scaled_font->base.status)
	return;

    /* NOTE: We released the unscaled font's mutex at the end of
     * cairo_ft_scaled_font_lock_face, so we have to acquire it again
     * as _cairo_ft_unscaled_font_unlock_face expects it to be held
     * when we call into it. */
    CAIRO_MUTEX_LOCK (scaled_font->unscaled->mutex);

    _cairo_ft_unscaled_font_unlock_face (scaled_font->unscaled);
}

/* We expose our unscaled font implementation internally for the the
 * PDF backend, which needs to keep track of the the different
 * fonts-on-disk used by a document, so it can embed them.
 */
cairo_unscaled_font_t *
_cairo_ft_scaled_font_get_unscaled_font (cairo_scaled_font_t *abstract_font)
{
    cairo_ft_scaled_font_t *scaled_font = (cairo_ft_scaled_font_t *) abstract_font;

    return &scaled_font->unscaled->base;
}

cairo_bool_t
_cairo_ft_scaled_font_is_vertical (cairo_scaled_font_t *scaled_font)
{
    cairo_ft_scaled_font_t *ft_scaled_font;
    
    if (!_cairo_scaled_font_is_ft (scaled_font))
	return FALSE;
    
    ft_scaled_font = (cairo_ft_scaled_font_t *) scaled_font;
    if (ft_scaled_font->ft_options.load_flags & FT_LOAD_VERTICAL_LAYOUT)
	return TRUE;
    return FALSE;
}

void
_cairo_ft_font_reset_static_data (void)
{
    _cairo_ft_unscaled_font_map_destroy ();
}
