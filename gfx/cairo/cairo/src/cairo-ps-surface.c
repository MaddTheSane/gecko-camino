/* -*- Mode: c; c-basic-offset: 4; indent-tabs-mode: t; tab-width: 8; -*- */
/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2003 University of Southern California
 * Copyright © 2005 Red Hat, Inc
 * Copyright © 2007 Adrian Johnson
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
 *	Adrian Johnson <ajohnson@redneon.com>
 */

#include "cairoint.h"
#include "cairo-ps.h"
#include "cairo-ps-surface-private.h"
#include "cairo-scaled-font-subsets-private.h"
#include "cairo-paginated-private.h"
#include "cairo-meta-surface-private.h"
#include "cairo-output-stream-private.h"

#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include <errno.h>

#define DEBUG_PS 0

#ifndef HAVE_CTIME_R
#define ctime_r(T, BUF) ctime (T)
#endif

typedef enum _cairo_image_transparency {
    CAIRO_IMAGE_IS_OPAQUE,
    CAIRO_IMAGE_HAS_BILEVEL_ALPHA,
    CAIRO_IMAGE_HAS_ALPHA
} cairo_image_transparency_t;

static const cairo_surface_backend_t cairo_ps_surface_backend;
static const cairo_paginated_surface_backend_t cairo_ps_surface_paginated_backend;

static const cairo_ps_level_t _cairo_ps_levels[] =
{
    CAIRO_PS_LEVEL_2,
    CAIRO_PS_LEVEL_3
};

#define CAIRO_PS_LEVEL_LAST ARRAY_LENGTH (_cairo_ps_levels)

static const char * _cairo_ps_level_strings[CAIRO_PS_LEVEL_LAST] =
{
    "PS Level 2",
    "PS Level 3"
};

/* A word wrap stream can be used as a filter to do word wrapping on
 * top of an existing output stream. The word wrapping is quite
 * simple, using isspace to determine characters that separate
 * words. Any word that will cause the column count exceed the given
 * max_column will have a '\n' character emitted before it.
 *
 * The stream is careful to maintain integrity for words that cross
 * the boundary from one call to write to the next.
 *
 * Note: This stream does not guarantee that the output will never
 * exceed max_column. In particular, if a single word is larger than
 * max_column it will not be broken up.
 */
typedef struct _word_wrap_stream {
    cairo_output_stream_t base;
    cairo_output_stream_t *output;
    int max_column;
    int column;
    cairo_bool_t last_write_was_space;
} word_wrap_stream_t;

static int
_count_word_up_to (const unsigned char *s, int length)
{
    int word = 0;

    while (length--) {
	if (! isspace (*s++))
	    word++;
	else
	    return word;
    }

    return word;
}

static cairo_status_t
_word_wrap_stream_write (cairo_output_stream_t  *base,
			 const unsigned char	*data,
			 unsigned int		 length)
{
    word_wrap_stream_t *stream = (word_wrap_stream_t *) base;
    cairo_bool_t newline;
    int word;

    while (length) {
	if (isspace (*data)) {
	    newline =  (*data == '\n' || *data == '\r');
	    if (! newline && stream->column >= stream->max_column) {
		_cairo_output_stream_printf (stream->output, "\n");
		stream->column = 0;
	    }
	    _cairo_output_stream_write (stream->output, data, 1);
	    data++;
	    length--;
	    if (newline)
		stream->column = 0;
	    else
		stream->column++;
	    stream->last_write_was_space = TRUE;
	} else {
	    word = _count_word_up_to (data, length);
	    /* Don't wrap if this word is a continuation of a word
	     * from a previous call to write. */
	    if (stream->column + word >= stream->max_column &&
		stream->last_write_was_space)
	    {
		_cairo_output_stream_printf (stream->output, "\n");
		stream->column = 0;
	    }
	    _cairo_output_stream_write (stream->output, data, word);
	    data += word;
	    length -= word;
	    stream->column += word;
	    stream->last_write_was_space = FALSE;
	}
    }

    return _cairo_output_stream_get_status (stream->output);
}

static cairo_status_t
_word_wrap_stream_close (cairo_output_stream_t *base)
{
    word_wrap_stream_t *stream = (word_wrap_stream_t *) base;

    return _cairo_output_stream_get_status (stream->output);
}

static cairo_output_stream_t *
_word_wrap_stream_create (cairo_output_stream_t *output, int max_column)
{
    word_wrap_stream_t *stream;

    if (output->status)
	return _cairo_output_stream_create_in_error (output->status);

    stream = malloc (sizeof (word_wrap_stream_t));
    if (stream == NULL) {
	_cairo_error_throw (CAIRO_STATUS_NO_MEMORY);
	return (cairo_output_stream_t *) &_cairo_output_stream_nil;
    }

    _cairo_output_stream_init (&stream->base,
			       _word_wrap_stream_write,
			       _word_wrap_stream_close);
    stream->output = output;
    stream->max_column = max_column;
    stream->column = 0;
    stream->last_write_was_space = FALSE;

    return &stream->base;
}

typedef struct _ps_path_info {
    cairo_ps_surface_t *surface;
    cairo_output_stream_t *stream;
    cairo_line_cap_t line_cap;
    cairo_point_t last_move_to_point;
    cairo_bool_t has_sub_path;
} ps_path_info_t;

static cairo_status_t
_cairo_ps_surface_path_move_to (void *closure, cairo_point_t *point)
{
    ps_path_info_t *path_info = closure;

    path_info->last_move_to_point = *point;
    path_info->has_sub_path = FALSE;

    _cairo_output_stream_printf (path_info->stream,
				 "%f %f M ",
				 _cairo_fixed_to_double (point->x),
				 _cairo_fixed_to_double (point->y));

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_path_line_to (void *closure, cairo_point_t *point)
{
    ps_path_info_t *path_info = closure;

    if (path_info->line_cap != CAIRO_LINE_CAP_ROUND &&
	! path_info->has_sub_path &&
	point->x == path_info->last_move_to_point.x &&
	point->y == path_info->last_move_to_point.y)
    {
	return CAIRO_STATUS_SUCCESS;
    }

    path_info->has_sub_path = TRUE;

    _cairo_output_stream_printf (path_info->stream,
				 "%f %f L ",
				 _cairo_fixed_to_double (point->x),
				 _cairo_fixed_to_double (point->y));

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_path_curve_to (void          *closure,
				 cairo_point_t *b,
				 cairo_point_t *c,
				 cairo_point_t *d)
{
    ps_path_info_t *path_info = closure;

    path_info->has_sub_path = TRUE;

    _cairo_output_stream_printf (path_info->stream,
				 "%f %f %f %f %f %f C ",
				 _cairo_fixed_to_double (b->x),
				 _cairo_fixed_to_double (b->y),
				 _cairo_fixed_to_double (c->x),
				 _cairo_fixed_to_double (c->y),
				 _cairo_fixed_to_double (d->x),
				 _cairo_fixed_to_double (d->y));

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_path_close_path (void *closure)
{
    ps_path_info_t *path_info = closure;

    if (path_info->line_cap != CAIRO_LINE_CAP_ROUND &&
	! path_info->has_sub_path)
    {
	return CAIRO_STATUS_SUCCESS;
    }

    _cairo_output_stream_printf (path_info->stream,
				 "P\n");

    return CAIRO_STATUS_SUCCESS;
}

/* The line cap value is needed to workaround the fact that PostScript
 * semantics for stroking degenerate sub-paths do not match cairo
 * semantics. (PostScript draws something for any line cap value,
 * while cairo draws something only for round caps).
 *
 * When using this function to emit a path to be filled, rather than
 * stroked, simply pass CAIRO_LINE_CAP_ROUND which will guarantee that
 * the stroke workaround will not modify the path being emitted.
 */
static cairo_status_t
_cairo_ps_surface_emit_path (cairo_ps_surface_t	   *surface,
			     cairo_output_stream_t *stream,
			     cairo_path_fixed_t    *path,
			     cairo_line_cap_t	    line_cap)
{
    cairo_output_stream_t *word_wrap;
    cairo_status_t status, status2;
    ps_path_info_t path_info;

    word_wrap = _word_wrap_stream_create (stream, 79);
    status = _cairo_output_stream_get_status (word_wrap);
    if (status)
	return _cairo_output_stream_destroy (word_wrap);

    path_info.surface = surface;
    path_info.stream = word_wrap;
    path_info.line_cap = line_cap;
    status = _cairo_path_fixed_interpret (path,
					  CAIRO_DIRECTION_FORWARD,
					  _cairo_ps_surface_path_move_to,
					  _cairo_ps_surface_path_line_to,
					  _cairo_ps_surface_path_curve_to,
					  _cairo_ps_surface_path_close_path,
					  &path_info);

    status2 = _cairo_output_stream_destroy (word_wrap);
    if (status == CAIRO_STATUS_SUCCESS)
	status = status2;

    return status;
}

static void
_cairo_ps_surface_emit_header (cairo_ps_surface_t *surface)
{
    char ctime_buf[26];
    time_t now;
    char **comments;
    int i, num_comments;
    int level;
    const char *eps_header = "";

    now = time (NULL);

    if (surface->ps_level_used == CAIRO_PS_LEVEL_2)
	level = 2;
    else
	level = 3;

    if (surface->eps)
	eps_header = " EPSF-3.0";

    _cairo_output_stream_printf (surface->final_stream,
				 "%%!PS-Adobe-3.0%s\n"
				 "%%%%Creator: cairo %s (http://cairographics.org)\n"
				 "%%%%CreationDate: %s"
				 "%%%%Pages: %d\n"
				 "%%%%BoundingBox: %d %d %d %d\n",
				 eps_header,
				 cairo_version_string (),
				 ctime_r (&now, ctime_buf),
				 surface->num_pages,
				 surface->bbox_x1,
				 surface->bbox_y1,
				 surface->bbox_x2,
				 surface->bbox_y2);

    _cairo_output_stream_printf (surface->final_stream,
				 "%%%%DocumentData: Clean7Bit\n"
				 "%%%%LanguageLevel: %d\n",
				 level);

    num_comments = _cairo_array_num_elements (&surface->dsc_header_comments);
    comments = _cairo_array_index (&surface->dsc_header_comments, 0);
    for (i = 0; i < num_comments; i++) {
	_cairo_output_stream_printf (surface->final_stream,
				     "%s\n", comments[i]);
	free (comments[i]);
	comments[i] = NULL;
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "%%%%EndComments\n");

    _cairo_output_stream_printf (surface->final_stream,
				 "%%%%BeginProlog\n");

    if (surface->eps) {
	_cairo_output_stream_printf (surface->final_stream,
				     "/cairo_eps_state save def\n"
				     "/dict_count countdictstack def\n"
				     "/op_count count 1 sub def\n"
				     "userdict begin\n");
    } else {
	_cairo_output_stream_printf (surface->final_stream,
				     "/languagelevel where{pop languagelevel}{1}ifelse %d lt{/Helvetica\n"
				     "findfont 12 scalefont setfont 50 500 moveto\n"
				     "(This print job requires a PostScript Language Level %d printer.)show\n"
				     "showpage quit}if\n",
				     level,
				     level);
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "/C{curveto}bind def\n"
				 "/F{fill}bind def\n"
				 "/G{setgray}bind def\n"
				 "/L{lineto}bind def\n"
				 "/M{moveto}bind def\n"
				 "/P{closepath}bind def\n"
				 "/R{setrgbcolor}bind def\n"
				 "/S{show}bind def\n"
				 "/xS{xshow}bind def\n"
				 "/yS{yshow}bind def\n"
				 "/xyS{xyshow}bind def\n"
				 "%%%%EndProlog\n");

    num_comments = _cairo_array_num_elements (&surface->dsc_setup_comments);
    if (num_comments) {
	_cairo_output_stream_printf (surface->final_stream,
				     "%%%%BeginSetup\n");

	comments = _cairo_array_index (&surface->dsc_setup_comments, 0);
	for (i = 0; i < num_comments; i++) {
	    _cairo_output_stream_printf (surface->final_stream,
					 "%s\n", comments[i]);
	    free (comments[i]);
	    comments[i] = NULL;
	}

	_cairo_output_stream_printf (surface->final_stream,
				     "%%%%EndSetup\n");
    }
}

#if CAIRO_HAS_FT_FONT
static cairo_status_t
_cairo_ps_surface_emit_type1_font_subset (cairo_ps_surface_t		*surface,
					  cairo_scaled_font_subset_t	*font_subset)


{
    cairo_type1_subset_t subset;
    cairo_status_t status;
    int length;
    char name[64];

    snprintf (name, sizeof name, "CairoFont-%d-%d",
	      font_subset->font_id, font_subset->subset_id);
    status = _cairo_type1_subset_init (&subset, name, font_subset, TRUE);
    if (status)
	return status;

    /* FIXME: Figure out document structure convention for fonts */

#if DEBUG_PS
    _cairo_output_stream_printf (surface->final_stream,
				 "%% _cairo_ps_surface_emit_type1_font_subset\n");
#endif

    length = subset.header_length + subset.data_length + subset.trailer_length;
    _cairo_output_stream_write (surface->final_stream, subset.data, length);

    _cairo_type1_subset_fini (&subset);

    return CAIRO_STATUS_SUCCESS;
}
#endif

static cairo_status_t
_cairo_ps_surface_emit_type1_font_fallback (cairo_ps_surface_t		*surface,
                                            cairo_scaled_font_subset_t	*font_subset)
{
    cairo_type1_subset_t subset;
    cairo_status_t status;
    int length;
    char name[64];

    snprintf (name, sizeof name, "CairoFont-%d-%d",
	      font_subset->font_id, font_subset->subset_id);
    status = _cairo_type1_fallback_init_hex (&subset, name, font_subset);
    if (status)
	return status;

    /* FIXME: Figure out document structure convention for fonts */

#if DEBUG_PS
    _cairo_output_stream_printf (surface->final_stream,
				 "%% _cairo_ps_surface_emit_type1_font_fallback\n");
#endif

    length = subset.header_length + subset.data_length + subset.trailer_length;
    _cairo_output_stream_write (surface->final_stream, subset.data, length);

    _cairo_type1_fallback_fini (&subset);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_emit_truetype_font_subset (cairo_ps_surface_t		*surface,
					     cairo_scaled_font_subset_t	*font_subset)


{
    cairo_truetype_subset_t subset;
    cairo_status_t status;
    unsigned int i, begin, end;

    status = _cairo_truetype_subset_init (&subset, font_subset);
    if (status)
	return status;

    /* FIXME: Figure out document structure convention for fonts */

#if DEBUG_PS
    _cairo_output_stream_printf (surface->final_stream,
				 "%% _cairo_ps_surface_emit_truetype_font_subset\n");
#endif

    _cairo_output_stream_printf (surface->final_stream,
				 "11 dict begin\n"
				 "/FontType 42 def\n"
				 "/FontName /CairoFont-%d-%d def\n"
				 "/PaintType 0 def\n"
				 "/FontMatrix [ 1 0 0 1 0 0 ] def\n"
				 "/FontBBox [ 0 0 0 0 ] def\n"
				 "/Encoding 256 array def\n"
				 "0 1 255 { Encoding exch /.notdef put } for\n",
				 font_subset->font_id,
				 font_subset->subset_id);

    /* FIXME: Figure out how subset->x_max etc maps to the /FontBBox */

    for (i = 1; i < font_subset->num_glyphs; i++) {
	if (font_subset->glyph_names != NULL) {
	    _cairo_output_stream_printf (surface->final_stream,
					 "Encoding %d /%s put\n",
					 i, font_subset->glyph_names[i]);
	} else {
	    _cairo_output_stream_printf (surface->final_stream,
					 "Encoding %d /g%d put\n", i, i);
	}
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "/CharStrings %d dict dup begin\n"
				 "/.notdef 0 def\n",
				 font_subset->num_glyphs);

    for (i = 1; i < font_subset->num_glyphs; i++) {
	if (font_subset->glyph_names != NULL) {
	    _cairo_output_stream_printf (surface->final_stream,
					 "/%s %d def\n",
					 font_subset->glyph_names[i], i);
	} else {
	    _cairo_output_stream_printf (surface->final_stream,
					 "/g%d %d def\n", i, i);
	}
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "end readonly def\n");

    _cairo_output_stream_printf (surface->final_stream,
				 "/sfnts [\n");
    begin = 0;
    end = 0;
    for (i = 0; i < subset.num_string_offsets; i++) {
        end = subset.string_offsets[i];
        _cairo_output_stream_printf (surface->final_stream,"<");
        _cairo_output_stream_write_hex_string (surface->final_stream,
                                               subset.data + begin, end - begin);
        _cairo_output_stream_printf (surface->final_stream,"00>\n");
        begin = end;
    } 
    if (subset.data_length > end) {
        _cairo_output_stream_printf (surface->final_stream,"<");
        _cairo_output_stream_write_hex_string (surface->final_stream,
                                               subset.data + end, subset.data_length - end);
        _cairo_output_stream_printf (surface->final_stream,"00>\n");
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "] def\n"
				 "FontName currentdict end definefont pop\n");

    _cairo_truetype_subset_fini (&subset);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_ps_surface_emit_outline_glyph_data (cairo_ps_surface_t	*surface,
					   cairo_scaled_font_t	*scaled_font,
					   unsigned long	 glyph_index,
					   cairo_box_t          *bbox)
{
    cairo_scaled_glyph_t *scaled_glyph;
    cairo_status_t status;

    status = _cairo_scaled_glyph_lookup (scaled_font,
					 glyph_index,
					 CAIRO_SCALED_GLYPH_INFO_METRICS|
					 CAIRO_SCALED_GLYPH_INFO_PATH,
					 &scaled_glyph);
    if (status)
	return status;

    *bbox = scaled_glyph->bbox;
    _cairo_output_stream_printf (surface->final_stream,
				 "0 0 %f %f %f %f setcachedevice\n",
				 _cairo_fixed_to_double (scaled_glyph->bbox.p1.x),
				 -_cairo_fixed_to_double (scaled_glyph->bbox.p2.y),
				 _cairo_fixed_to_double (scaled_glyph->bbox.p2.x),
				 -_cairo_fixed_to_double (scaled_glyph->bbox.p1.y));

    /* We're filling not stroking, so we pass CAIRO_LINE_CAP_ROUND. */
    status = _cairo_ps_surface_emit_path (surface,
					  surface->final_stream,
					  scaled_glyph->path,
					  CAIRO_LINE_CAP_ROUND);
    if (status)
	return status;

    _cairo_output_stream_printf (surface->final_stream,
				 "F\n");

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_ps_surface_emit_bitmap_glyph_data (cairo_ps_surface_t	*surface,
					  cairo_scaled_font_t	*scaled_font,
					  unsigned long	 glyph_index,
					  cairo_box_t           *bbox)
{
    cairo_scaled_glyph_t *scaled_glyph;
    cairo_status_t status;
    cairo_image_surface_t *image;
    unsigned char *row, *byte;
    int rows, cols;
    double x_advance, y_advance;

    status = _cairo_scaled_glyph_lookup (scaled_font,
					 glyph_index,
					 CAIRO_SCALED_GLYPH_INFO_METRICS|
					 CAIRO_SCALED_GLYPH_INFO_SURFACE,
					 &scaled_glyph);
    if (status)
	return status;

    *bbox = scaled_glyph->bbox;
    x_advance = scaled_glyph->metrics.x_advance;
    y_advance = scaled_glyph->metrics.y_advance;
    cairo_matrix_transform_distance (&scaled_font->ctm, &x_advance, &y_advance);

    image = scaled_glyph->surface;
    if (image->format != CAIRO_FORMAT_A1) {
	image = _cairo_image_surface_clone (image, CAIRO_FORMAT_A1);
	if (cairo_surface_status (&image->base))
	    return cairo_surface_status (&image->base);
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "%f 0 %f %f %f %f setcachedevice\n",
				 x_advance,
				 _cairo_fixed_to_double (scaled_glyph->bbox.p1.x),
				 _cairo_fixed_to_double (scaled_glyph->bbox.p2.y),
				 _cairo_fixed_to_double (scaled_glyph->bbox.p2.x),
				 _cairo_fixed_to_double (scaled_glyph->bbox.p1.y));

    _cairo_output_stream_printf (surface->final_stream,
				 "<<\n"
				 "   /ImageType 1\n"
				 "   /Width %d\n"
				 "   /Height %d\n"
				 "   /ImageMatrix [%f %f %f %f %f %f]\n"
				 "   /Decode [1 0]\n"
				 "   /BitsPerComponent 1\n",
				 image->width,
				 image->height,
				 image->base.device_transform.xx,
				 image->base.device_transform.yx,
				 image->base.device_transform.xy,
				 image->base.device_transform.yy,
				 image->base.device_transform.x0,
				 image->base.device_transform.y0);

    _cairo_output_stream_printf (surface->final_stream,
				 "   /DataSource   {<");
    for (row = image->data, rows = image->height; rows; row += image->stride, rows--) {
	for (byte = row, cols = (image->width + 7) / 8; cols; byte++, cols--) {
	    unsigned char output_byte = CAIRO_BITSWAP8_IF_LITTLE_ENDIAN (*byte);
	    _cairo_output_stream_printf (surface->final_stream, "%02x ", output_byte);
	}
	_cairo_output_stream_printf (surface->final_stream, "\n   ");
    }
    _cairo_output_stream_printf (surface->final_stream,
				 "   >}\n");
    _cairo_output_stream_printf (surface->final_stream,
				 ">>\n");

    _cairo_output_stream_printf (surface->final_stream,
				 "imagemask\n");

    if (image != scaled_glyph->surface)
	cairo_surface_destroy (&image->base);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_emit_glyph (cairo_ps_surface_t	*surface,
			      cairo_scaled_font_t	*scaled_font,
			      unsigned long		 scaled_font_glyph_index,
			      unsigned int		 subset_glyph_index,
			      cairo_box_t               *bbox)
{
    cairo_status_t	    status = CAIRO_STATUS_SUCCESS;

    _cairo_output_stream_printf (surface->final_stream,
				 "\t\t{ %% %d\n", subset_glyph_index);

    if (subset_glyph_index != 0) {
	status = _cairo_ps_surface_emit_outline_glyph_data (surface,
							    scaled_font,
							    scaled_font_glyph_index,
							    bbox);
	if (status == CAIRO_INT_STATUS_UNSUPPORTED)
	    status = _cairo_ps_surface_emit_bitmap_glyph_data (surface,
							       scaled_font,
							       scaled_font_glyph_index,
							       bbox);
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "\t\t}\n");

    if (status)
	status = _cairo_surface_set_error (&surface->base, status);

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_type3_font_subset (cairo_ps_surface_t		*surface,
					  cairo_scaled_font_subset_t	*font_subset)


{
    cairo_status_t status;
    cairo_matrix_t matrix;
    unsigned int i;
    cairo_box_t font_bbox = {{0,0},{0,0}};
    cairo_box_t bbox = {{0,0},{0,0}};

#if DEBUG_PS
    _cairo_output_stream_printf (surface->final_stream,
				 "%% _cairo_ps_surface_emit_type3_font_subset\n");
#endif

    matrix = font_subset->scaled_font->scale_inverse;
    _cairo_output_stream_printf (surface->final_stream,
				 "8 dict begin\n"
				 "/FontType 3 def\n"
				 "/FontMatrix [%f %f %f %f 0 0] def\n"
				 "/Encoding 256 array def\n"
				 "0 1 255 { Encoding exch /.notdef put } for\n",
				 matrix.xx,
				 matrix.yx,
				 -matrix.xy,
				 -matrix.yy);

    for (i = 1; i < font_subset->num_glyphs; i++) {
	if (font_subset->glyph_names != NULL) {
	    _cairo_output_stream_printf (surface->final_stream,
					 "Encoding %d /%s put\n",
					 i, font_subset->glyph_names[i]);
	} else {
	    _cairo_output_stream_printf (surface->final_stream,
					 "Encoding %d /g%d put\n", i, i);
	}
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "/Glyphs [\n");

    for (i = 0; i < font_subset->num_glyphs; i++) {
	status = _cairo_ps_surface_emit_glyph (surface,
				               font_subset->scaled_font,
					       font_subset->glyphs[i], i,
					       &bbox);
	if (status)
	    return status;

        if (i == 0) {
            font_bbox.p1.x = bbox.p1.x;
            font_bbox.p1.y = bbox.p1.y;
            font_bbox.p2.x = bbox.p2.x;
            font_bbox.p2.y = bbox.p2.y;
        } else {
            if (bbox.p1.x < font_bbox.p1.x)
                font_bbox.p1.x = bbox.p1.x;
            if (bbox.p1.y < font_bbox.p1.y)
                font_bbox.p1.y = bbox.p1.y;
            if (bbox.p2.x > font_bbox.p2.x)
                font_bbox.p2.x = bbox.p2.x;
            if (bbox.p2.y > font_bbox.p2.y)
                font_bbox.p2.y = bbox.p2.y;
        }
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "] def\n"
				 "/FontBBox [%f %f %f %f] def\n"
				 "/BuildChar {\n"
				 "  exch /Glyphs get\n"
				 "  exch get exec\n"
				 "} bind def\n"
				 "currentdict\n"
				 "end\n"
				 "/CairoFont-%d-%d exch definefont pop\n",
				 _cairo_fixed_to_double (font_bbox.p1.x),
				 _cairo_fixed_to_double (font_bbox.p1.y),
				 _cairo_fixed_to_double (font_bbox.p2.x),
				 _cairo_fixed_to_double (font_bbox.p2.y),
				 font_subset->font_id,
				 font_subset->subset_id);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_emit_unscaled_font_subset (cairo_scaled_font_subset_t	*font_subset,
				            void			*closure)
{
    cairo_ps_surface_t *surface = closure;
    cairo_status_t status;


    status = _cairo_scaled_font_subset_create_glyph_names (font_subset);
    if (status && status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

#if CAIRO_HAS_FT_FONT
    status = _cairo_ps_surface_emit_type1_font_subset (surface, font_subset);
    if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;
#endif

    status = _cairo_ps_surface_emit_truetype_font_subset (surface, font_subset);
    if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    status = _cairo_ps_surface_emit_type1_font_fallback (surface, font_subset);
    if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    ASSERT_NOT_REACHED;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_emit_scaled_font_subset (cairo_scaled_font_subset_t *font_subset,
                                           void			      *closure)
{
    cairo_ps_surface_t *surface = closure;
    cairo_status_t status;

    status = _cairo_scaled_font_subset_create_glyph_names (font_subset);
    if (status && status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    status = _cairo_ps_surface_emit_type3_font_subset (surface, font_subset);
    if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    ASSERT_NOT_REACHED;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_emit_font_subsets (cairo_ps_surface_t *surface)
{
    cairo_status_t status;

#if DEBUG_PS
    _cairo_output_stream_printf (surface->final_stream,
				 "%% _cairo_ps_surface_emit_font_subsets\n");
#endif

    status = _cairo_scaled_font_subsets_foreach_unscaled (surface->font_subsets,
                                                          _cairo_ps_surface_emit_unscaled_font_subset,
                                                          surface);
    if (status)
	goto BAIL;

    status = _cairo_scaled_font_subsets_foreach_scaled (surface->font_subsets,
                                                        _cairo_ps_surface_emit_scaled_font_subset,
                                                        surface);

BAIL:
    _cairo_scaled_font_subsets_destroy (surface->font_subsets);
    surface->font_subsets = NULL;

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_body (cairo_ps_surface_t *surface)
{
    char    buf[4096];
    int	    n;

    if (ferror (surface->tmpfile) != 0)
	return _cairo_error (CAIRO_STATUS_TEMP_FILE_ERROR);

    rewind (surface->tmpfile);
    while ((n = fread (buf, 1, sizeof (buf), surface->tmpfile)) > 0)
	_cairo_output_stream_write (surface->final_stream, buf, n);

    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_ps_surface_emit_footer (cairo_ps_surface_t *surface)
{
    _cairo_output_stream_printf (surface->final_stream,
				 "%%%%Trailer\n");

    if (surface->eps) {
	_cairo_output_stream_printf (surface->final_stream,
				     "count op_count sub {pop} repeat\n"
				     "countdictstack dict_count sub {end} repeat\n"
				     "cairo_eps_state restore\n");
    }

    _cairo_output_stream_printf (surface->final_stream,
				 "%%%%EOF\n");
}

static cairo_surface_t *
_cairo_ps_surface_create_for_stream_internal (cairo_output_stream_t *stream,
					      double		     width,
					      double		     height)
{
    cairo_status_t status, status_ignored;
    cairo_ps_surface_t *surface;

    surface = malloc (sizeof (cairo_ps_surface_t));
    if (surface == NULL) {
	status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	goto CLEANUP;
    }

    _cairo_surface_init (&surface->base, &cairo_ps_surface_backend,
			 CAIRO_CONTENT_COLOR_ALPHA);

    surface->final_stream = stream;

    surface->tmpfile = tmpfile ();
    if (surface->tmpfile == NULL) {
	switch (errno) {
	case ENOMEM:
	    status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    break;
	default:
	    status = _cairo_error (CAIRO_STATUS_TEMP_FILE_ERROR);
	    break;
	}
	goto CLEANUP_SURFACE;
    }

    surface->stream = _cairo_output_stream_create_for_file (surface->tmpfile);
    status = _cairo_output_stream_get_status (surface->stream);
    if (status)
	goto CLEANUP_OUTPUT_STREAM;

    surface->font_subsets = _cairo_scaled_font_subsets_create_simple ();
    if (surface->font_subsets == NULL) {
	status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	goto CLEANUP_OUTPUT_STREAM;
    }

    surface->eps = FALSE;
    surface->ps_level = CAIRO_PS_LEVEL_3;
    surface->ps_level_used = CAIRO_PS_LEVEL_2;
    surface->width  = width;
    surface->height = height;
    surface->paginated_mode = CAIRO_PAGINATED_MODE_ANALYZE;
    surface->force_fallbacks = FALSE;
    surface->content = CAIRO_CONTENT_COLOR_ALPHA;

    surface->num_pages = 0;

    _cairo_array_init (&surface->dsc_header_comments, sizeof (char *));
    _cairo_array_init (&surface->dsc_setup_comments, sizeof (char *));
    _cairo_array_init (&surface->dsc_page_setup_comments, sizeof (char *));

    surface->dsc_comment_target = &surface->dsc_header_comments;

    surface->paginated_surface = _cairo_paginated_surface_create (
	                                   &surface->base,
					   CAIRO_CONTENT_COLOR_ALPHA,
					   width, height,
					   &cairo_ps_surface_paginated_backend);
    status = surface->paginated_surface->status;
    if (status == CAIRO_STATUS_SUCCESS)
	return surface->paginated_surface;

    _cairo_scaled_font_subsets_destroy (surface->font_subsets);
 CLEANUP_OUTPUT_STREAM:
    status_ignored = _cairo_output_stream_destroy (surface->stream);
    fclose (surface->tmpfile);
 CLEANUP_SURFACE:
    free (surface);
 CLEANUP:
    /* destroy stream on behalf of caller */
    status_ignored = _cairo_output_stream_destroy (stream);

    return _cairo_surface_create_in_error (status);
}

/**
 * cairo_ps_surface_create:
 * @filename: a filename for the PS output (must be writable)
 * @width_in_points: width of the surface, in points (1 point == 1/72.0 inch)
 * @height_in_points: height of the surface, in points (1 point == 1/72.0 inch)
 *
 * Creates a PostScript surface of the specified size in points to be
 * written to @filename. See cairo_ps_surface_create_for_stream() for
 * a more flexible mechanism for handling the PostScript output than
 * simply writing it to a named file.
 *
 * Note that the size of individual pages of the PostScript output can
 * vary. See cairo_ps_surface_set_size().
 *
 * Return value: a pointer to the newly created surface. The caller
 * owns the surface and should call cairo_surface_destroy when done
 * with it.
 *
 * This function always returns a valid pointer, but it will return a
 * pointer to a "nil" surface if an error such as out of memory
 * occurs. You can use cairo_surface_status() to check for this.
 *
 * Since: 1.2
 **/
cairo_surface_t *
cairo_ps_surface_create (const char		*filename,
			 double			 width_in_points,
			 double			 height_in_points)
{
    cairo_output_stream_t *stream;

    stream = _cairo_output_stream_create_for_filename (filename);
    if (_cairo_output_stream_get_status (stream))
	return _cairo_surface_create_in_error (_cairo_output_stream_destroy (stream));

    return _cairo_ps_surface_create_for_stream_internal (stream,
							 width_in_points,
							 height_in_points);
}

/**
 * cairo_ps_surface_create_for_stream:
 * @write_func: a #cairo_write_func_t to accept the output data
 * @closure: the closure argument for @write_func
 * @width_in_points: width of the surface, in points (1 point == 1/72.0 inch)
 * @height_in_points: height of the surface, in points (1 point == 1/72.0 inch)
 *
 * Creates a PostScript surface of the specified size in points to be
 * written incrementally to the stream represented by @write_func and
 * @closure. See cairo_ps_surface_create() for a more convenient way
 * to simply direct the PostScript output to a named file.
 *
 * Note that the size of individual pages of the PostScript
 * output can vary. See cairo_ps_surface_set_size().
 *
 * Return value: a pointer to the newly created surface. The caller
 * owns the surface and should call cairo_surface_destroy when done
 * with it.
 *
 * This function always returns a valid pointer, but it will return a
 * pointer to a "nil" surface if an error such as out of memory
 * occurs. You can use cairo_surface_status() to check for this.
 *
 * Since: 1.2
 */
cairo_surface_t *
cairo_ps_surface_create_for_stream (cairo_write_func_t	write_func,
				    void	       *closure,
				    double		width_in_points,
				    double		height_in_points)
{
    cairo_output_stream_t *stream;

    stream = _cairo_output_stream_create (write_func, NULL, closure);
    if (_cairo_output_stream_get_status (stream))
	return _cairo_surface_create_in_error (_cairo_output_stream_destroy (stream));

    return _cairo_ps_surface_create_for_stream_internal (stream,
							 width_in_points,
							 height_in_points);
}

static cairo_bool_t
_cairo_surface_is_ps (cairo_surface_t *surface)
{
    return surface->backend == &cairo_ps_surface_backend;
}

/* If the abstract_surface is a paginated surface, and that paginated
 * surface's target is a ps_surface, then set ps_surface to that
 * target. Otherwise return CAIRO_STATUS_SURFACE_TYPE_MISMATCH.
 */
static cairo_status_t
_extract_ps_surface (cairo_surface_t	 *surface,
		     cairo_ps_surface_t **ps_surface)
{
    cairo_surface_t *target;

    if (! _cairo_surface_is_paginated (surface))
	return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    target = _cairo_paginated_surface_get_target (surface);

    if (! _cairo_surface_is_ps (target))
	return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    *ps_surface = (cairo_ps_surface_t *) target;

    return CAIRO_STATUS_SUCCESS;
}

/**
 * cairo_ps_surface_restrict_to_level:
 * @surface: a PostScript #cairo_surface_t
 * @level: PostScript level
 *
 * Restricts the generated PostSript file to @level. See
 * cairo_ps_get_levels() for a list of available level values that
 * can be used here.
 *
 * This function should only be called before any drawing operations
 * have been performed on the given surface. The simplest way to do
 * this is to call this function immediately after creating the
 * surface.
 *
 * Since: 1.6
 **/
void
cairo_ps_surface_restrict_to_level (cairo_surface_t  *surface,
                                    cairo_ps_level_t  level)
{
    cairo_ps_surface_t *ps_surface = NULL;
    cairo_status_t status;

    status = _extract_ps_surface (surface, &ps_surface);
    if (status) {
	status = _cairo_surface_set_error (surface, status);
	return;
    }

    if (level < CAIRO_PS_LEVEL_LAST)
	ps_surface->ps_level = level;
}

/**
 * cairo_ps_get_levels:
 * @levels: supported level list
 * @num_levels: list length
 *
 * Used to retrieve the list of supported levels. See
 * cairo_ps_surface_restrict_to_level().
 *
 * Since: 1.6
 **/
void
cairo_ps_get_levels (cairo_ps_level_t const	**levels,
                     int                     	 *num_levels)
{
    if (levels != NULL)
	*levels = _cairo_ps_levels;

    if (num_levels != NULL)
	*num_levels = CAIRO_PS_LEVEL_LAST;
}

/**
 * cairo_ps_level_to_string:
 * @level: a level id
 *
 * Get the string representation of the given @level id. This function
 * will return NULL if @level id isn't valid. See cairo_ps_get_levels()
 * for a way to get the list of valid level ids.
 *
 * Return value: the string associated to given level.
 *
 * Since: 1.6
 **/
const char *
cairo_ps_level_to_string (cairo_ps_level_t level)
{
    if (level >= CAIRO_PS_LEVEL_LAST)
	return NULL;

    return _cairo_ps_level_strings[level];
}

/**
 * cairo_ps_surface_set_eps:
 * @surface: a PostScript cairo_surface_t
 * @eps: TRUE to output EPS format PostScript
 *
 * If @eps is TRUE, the PostScript surface will output Encapsulated
 * PostScript.
 *
 * This function should only be called before any drawing operations
 * have been performed on the current page. The simplest way to do
 * this is to call this function immediately after creating the
 * surface. An Encapsulated Postscript file should never contain more
 * than one page.
 *
 * Since: 1.6
 **/
void
cairo_ps_surface_set_eps (cairo_surface_t	*surface,
			  cairo_bool_t           eps)
{
    cairo_ps_surface_t *ps_surface = NULL;
    cairo_status_t status;

    status = _extract_ps_surface (surface, &ps_surface);
    if (status) {
	status = _cairo_surface_set_error (surface, status);
	return;
    }

    ps_surface->eps = eps;
}

/**
 * cairo_ps_surface_get_eps:
 * @surface: a PostScript cairo_surface_t
 *
 * Check whether the PostScript surface will output Encapsulated PostScript.
 *
 * Return value: TRUE if the surface will output Encapsulated PostScript.
 *
 * Since: 1.6
 **/
cairo_public cairo_bool_t
cairo_ps_surface_get_eps (cairo_surface_t	*surface)
{
    cairo_ps_surface_t *ps_surface = NULL;
    cairo_status_t status;

    status = _extract_ps_surface (surface, &ps_surface);
    if (status) {
	status = _cairo_surface_set_error (surface, status);
	return FALSE;
    }

    return ps_surface->eps;
}

/**
 * cairo_ps_surface_set_size:
 * @surface: a PostScript cairo_surface_t
 * @width_in_points: new surface width, in points (1 point == 1/72.0 inch)
 * @height_in_points: new surface height, in points (1 point == 1/72.0 inch)
 *
 * Changes the size of a PostScript surface for the current (and
 * subsequent) pages.
 *
 * This function should only be called before any drawing operations
 * have been performed on the current page. The simplest way to do
 * this is to call this function immediately after creating the
 * surface or immediately after completing a page with either
 * cairo_show_page() or cairo_copy_page().
 *
 * Since: 1.2
 **/
void
cairo_ps_surface_set_size (cairo_surface_t	*surface,
			   double		 width_in_points,
			   double		 height_in_points)
{
    cairo_ps_surface_t *ps_surface = NULL;
    cairo_status_t status;

    status = _extract_ps_surface (surface, &ps_surface);
    if (status) {
	status = _cairo_surface_set_error (surface, status);
	return;
    }

    ps_surface->width = width_in_points;
    ps_surface->height = height_in_points;
    status = _cairo_paginated_surface_set_size (ps_surface->paginated_surface,
						width_in_points,
						height_in_points);
    if (status)
	status = _cairo_surface_set_error (surface, status);
}

/**
 * cairo_ps_surface_dsc_comment:
 * @surface: a PostScript cairo_surface_t
 * @comment: a comment string to be emitted into the PostScript output
 *
 * Emit a comment into the PostScript output for the given surface.
 *
 * The comment is expected to conform to the PostScript Language
 * Document Structuring Conventions (DSC). Please see that manual for
 * details on the available comments and their meanings. In
 * particular, the %%IncludeFeature comment allows a
 * device-independent means of controlling printer device features. So
 * the PostScript Printer Description Files Specification will also be
 * a useful reference.
 *
 * The comment string must begin with a percent character (%) and the
 * total length of the string (including any initial percent
 * characters) must not exceed 255 characters. Violating either of
 * these conditions will place @surface into an error state. But
 * beyond these two conditions, this function will not enforce
 * conformance of the comment with any particular specification.
 *
 * The comment string should not have a trailing newline.
 *
 * The DSC specifies different sections in which particular comments
 * can appear. This function provides for comments to be emitted
 * within three sections: the header, the Setup section, and the
 * PageSetup section.  Comments appearing in the first two sections
 * apply to the entire document while comments in the BeginPageSetup
 * section apply only to a single page.
 *
 * For comments to appear in the header section, this function should
 * be called after the surface is created, but before a call to
 * cairo_ps_surface_begin_setup().
 *
 * For comments to appear in the Setup section, this function should
 * be called after a call to cairo_ps_surface_begin_setup() but before
 * a call to cairo_ps_surface_begin_page_setup().
 *
 * For comments to appear in the PageSetup section, this function
 * should be called after a call to cairo_ps_surface_begin_page_setup().
 *
 * Note that it is only necessary to call cairo_ps_surface_begin_page_setup()
 * for the first page of any surface. After a call to
 * cairo_show_page() or cairo_copy_page() comments are unambiguously
 * directed to the PageSetup section of the current page. But it
 * doesn't hurt to call this function at the beginning of every page
 * as that consistency may make the calling code simpler.
 *
 * As a final note, cairo automatically generates several comments on
 * its own. As such, applications must not manually generate any of
 * the following comments:
 *
 * Header section: %!PS-Adobe-3.0, %%Creator, %%CreationDate, %%Pages,
 * %%BoundingBox, %%DocumentData, %%LanguageLevel, %%EndComments.
 *
 * Setup section: %%BeginSetup, %%EndSetup
 *
 * PageSetup section: %%BeginPageSetup, %%PageBoundingBox,
 * %%EndPageSetup.
 *
 * Other sections: %%BeginProlog, %%EndProlog, %%Page, %%Trailer, %%EOF
 *
 * Here is an example sequence showing how this function might be used:
 *
 * <informalexample><programlisting>
 * cairo_surface_t *surface = cairo_ps_surface_create (filename, width, height);
 * ...
 * cairo_ps_surface_dsc_comment (surface, "%%Title: My excellent document");
 * cairo_ps_surface_dsc_comment (surface, "%%Copyright: Copyright (C) 2006 Cairo Lover")
 * ...
 * cairo_ps_surface_dsc_begin_setup (surface);
 * cairo_ps_surface_dsc_comment (surface, "%%IncludeFeature: *MediaColor White");
 * ...
 * cairo_ps_surface_dsc_begin_page_setup (surface);
 * cairo_ps_surface_dsc_comment (surface, "%%IncludeFeature: *PageSize A3");
 * cairo_ps_surface_dsc_comment (surface, "%%IncludeFeature: *InputSlot LargeCapacity");
 * cairo_ps_surface_dsc_comment (surface, "%%IncludeFeature: *MediaType Glossy");
 * cairo_ps_surface_dsc_comment (surface, "%%IncludeFeature: *MediaColor Blue");
 * ... draw to first page here ..
 * cairo_show_page (cr);
 * ...
 * cairo_ps_surface_dsc_comment (surface, "%%IncludeFeature: *PageSize A5");
 * ...
 * </programlisting></informalexample>
 *
 * Since: 1.2
 **/
void
cairo_ps_surface_dsc_comment (cairo_surface_t	*surface,
			      const char	*comment)
{
    cairo_ps_surface_t *ps_surface = NULL;
    cairo_status_t status;
    char *comment_copy;

    status = _extract_ps_surface (surface, &ps_surface);
    if (status) {
	status = _cairo_surface_set_error (surface, status);
	return;
    }

    /* A couple of sanity checks on the comment value. */
    if (comment == NULL) {
	status = _cairo_surface_set_error (surface, CAIRO_STATUS_NULL_POINTER);
	return;
    }

    if (comment[0] != '%' || strlen (comment) > 255) {
	status = _cairo_surface_set_error (surface, CAIRO_STATUS_INVALID_DSC_COMMENT);
	return;
    }

    /* Then, copy the comment and store it in the appropriate array. */
    comment_copy = strdup (comment);
    if (comment_copy == NULL) {
	status = _cairo_surface_set_error (surface, CAIRO_STATUS_NO_MEMORY);
	return;
    }

    status = _cairo_array_append (ps_surface->dsc_comment_target, &comment_copy);
    if (status) {
	free (comment_copy);
	status = _cairo_surface_set_error (surface, status);
	return;
    }
}

/**
 * cairo_ps_surface_dsc_begin_setup:
 * @surface: a PostScript cairo_surface_t
 *
 * This function indicates that subsequent calls to
 * cairo_ps_surface_dsc_comment() should direct comments to the Setup
 * section of the PostScript output.
 *
 * This function should be called at most once per surface, and must
 * be called before any call to cairo_ps_surface_dsc_begin_page_setup()
 * and before any drawing is performed to the surface.
 *
 * See cairo_ps_surface_dsc_comment() for more details.
 *
 * Since: 1.2
 **/
void
cairo_ps_surface_dsc_begin_setup (cairo_surface_t *surface)
{
    cairo_ps_surface_t *ps_surface = NULL;
    cairo_status_t status;

    status = _extract_ps_surface (surface, &ps_surface);
    if (status) {
	status = _cairo_surface_set_error (surface, status);
	return;
    }

    if (ps_surface->dsc_comment_target == &ps_surface->dsc_header_comments)
    {
	ps_surface->dsc_comment_target = &ps_surface->dsc_setup_comments;
    }
}

/**
 * cairo_ps_surface_dsc_begin_page_setup:
 * @surface: a PostScript cairo_surface_t
 *
 * This function indicates that subsequent calls to
 * cairo_ps_surface_dsc_comment() should direct comments to the
 * PageSetup section of the PostScript output.
 *
 * This function call is only needed for the first page of a
 * surface. It should be called after any call to
 * cairo_ps_surface_dsc_begin_setup() and before any drawing is
 * performed to the surface.
 *
 * See cairo_ps_surface_dsc_comment() for more details.
 *
 * Since: 1.2
 **/
void
cairo_ps_surface_dsc_begin_page_setup (cairo_surface_t *surface)
{
    cairo_ps_surface_t *ps_surface = NULL;
    cairo_status_t status;

    status = _extract_ps_surface (surface, &ps_surface);
    if (status) {
	status = _cairo_surface_set_error (surface, status);
	return;
    }

    if (ps_surface->dsc_comment_target == &ps_surface->dsc_header_comments ||
	ps_surface->dsc_comment_target == &ps_surface->dsc_setup_comments)
    {
	ps_surface->dsc_comment_target = &ps_surface->dsc_page_setup_comments;
    }
}

static cairo_surface_t *
_cairo_ps_surface_create_similar (void			*abstract_surface,
				  cairo_content_t	 content,
				  int			 width,
				  int			 height)
{
    return _cairo_meta_surface_create (content, width, height);
}

static cairo_status_t
_cairo_ps_surface_finish (void *abstract_surface)
{
    cairo_status_t status, status2;
    cairo_ps_surface_t *surface = abstract_surface;
    int i, num_comments;
    char **comments;

    _cairo_ps_surface_emit_header (surface);

    status = _cairo_ps_surface_emit_font_subsets (surface);
    if (status)
	goto CLEANUP;

    status = _cairo_ps_surface_emit_body (surface);
    if (status)
	goto CLEANUP;

    _cairo_ps_surface_emit_footer (surface);

CLEANUP:
    status2 = _cairo_output_stream_destroy (surface->stream);
    if (status == CAIRO_STATUS_SUCCESS)
	status = status2;

    fclose (surface->tmpfile);

    status2 = _cairo_output_stream_destroy (surface->final_stream);
    if (status == CAIRO_STATUS_SUCCESS)
	status = status2;

    num_comments = _cairo_array_num_elements (&surface->dsc_header_comments);
    comments = _cairo_array_index (&surface->dsc_header_comments, 0);
    for (i = 0; i < num_comments; i++)
	free (comments[i]);
    _cairo_array_fini (&surface->dsc_header_comments);

    num_comments = _cairo_array_num_elements (&surface->dsc_setup_comments);
    comments = _cairo_array_index (&surface->dsc_setup_comments, 0);
    for (i = 0; i < num_comments; i++)
	free (comments[i]);
    _cairo_array_fini (&surface->dsc_setup_comments);

    num_comments = _cairo_array_num_elements (&surface->dsc_page_setup_comments);
    comments = _cairo_array_index (&surface->dsc_page_setup_comments, 0);
    for (i = 0; i < num_comments; i++)
	free (comments[i]);
    _cairo_array_fini (&surface->dsc_page_setup_comments);

    return status;
}

static cairo_int_status_t
_cairo_ps_surface_start_page (void *abstract_surface)
{
    cairo_ps_surface_t *surface = abstract_surface;

    /* Increment before print so page numbers start at 1. */
    surface->num_pages++;

    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_ps_surface_end_page (cairo_ps_surface_t *surface)
{
    _cairo_output_stream_printf (surface->stream,
				 "grestore grestore\n");
}

static cairo_int_status_t
_cairo_ps_surface_show_page (void *abstract_surface)
{
    cairo_ps_surface_t *surface = abstract_surface;

    _cairo_ps_surface_end_page (surface);

    _cairo_output_stream_printf (surface->stream, "showpage\n");

    return CAIRO_STATUS_SUCCESS;
}

static cairo_bool_t
color_is_gray (double red, double green, double blue)
{
    const double epsilon = 0.00001;

    return (fabs (red - green) < epsilon &&
	    fabs (red - blue) < epsilon);
}

static cairo_status_t
_analyze_image_transparency (cairo_image_surface_t      *image,
			     cairo_image_transparency_t *transparency)
{
    int x, y;

    if (image->format == CAIRO_FORMAT_RGB24) {
	*transparency = CAIRO_IMAGE_IS_OPAQUE;
	return CAIRO_STATUS_SUCCESS;
    }

    if (image->format != CAIRO_FORMAT_ARGB32) {
	/* If the PS surface does not support the image format, assume
	 * that it does have alpha. The image will be converted to
	 * rgb24 when the PS surface blends the image into the page
	 * color to remove the transparency. */
	*transparency = CAIRO_IMAGE_HAS_ALPHA;
	return CAIRO_STATUS_SUCCESS;
    }

    *transparency = CAIRO_IMAGE_IS_OPAQUE;
    for (y = 0; y < image->height; y++) {
	int a;
	uint32_t *pixel = (uint32_t *) (image->data + y * image->stride);

	for (x = 0; x < image->width; x++, pixel++) {
	    a = (*pixel & 0xff000000) >> 24;
	    if (a > 0 && a < 255) {
		*transparency = CAIRO_IMAGE_HAS_ALPHA;
		return CAIRO_STATUS_SUCCESS;
	    } else if (a == 0) {
		*transparency = CAIRO_IMAGE_HAS_BILEVEL_ALPHA;
	    }
	}
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_ps_surface_analyze_surface_pattern_transparency (cairo_ps_surface_t      *surface,
						       cairo_surface_pattern_t *pattern)
{
    cairo_image_surface_t  *image;
    void		   *image_extra;
    cairo_int_status_t      status;
    cairo_image_transparency_t transparency;

    status = _cairo_surface_acquire_source_image (pattern->surface,
						  &image,
						  &image_extra);
    if (status)
	return status;

    if (image->base.status)
	return image->base.status;

    status = _analyze_image_transparency (image, &transparency);
    if (status)
	goto RELEASE_SOURCE;

    switch (transparency) {
    case CAIRO_IMAGE_IS_OPAQUE:
	status = CAIRO_STATUS_SUCCESS;
	break;

    case CAIRO_IMAGE_HAS_BILEVEL_ALPHA:
	if (surface->ps_level == CAIRO_PS_LEVEL_2) {
	    status = CAIRO_INT_STATUS_FLATTEN_TRANSPARENCY;
	} else {
	    surface->ps_level_used = CAIRO_PS_LEVEL_3;
	    status = CAIRO_STATUS_SUCCESS;
	}
	break;

    case CAIRO_IMAGE_HAS_ALPHA:
	status = CAIRO_INT_STATUS_FLATTEN_TRANSPARENCY;
	break;
    }

RELEASE_SOURCE:
    _cairo_surface_release_source_image (pattern->surface, image, image_extra);

    return status;
}

static cairo_bool_t
surface_pattern_supported (cairo_surface_pattern_t *pattern)
{
    cairo_extend_t extend;

    if (_cairo_surface_is_meta (pattern->surface))
	return TRUE;

    if (pattern->surface->backend->acquire_source_image == NULL)
	return FALSE;

    /* Does an ALPHA-only source surface even make sense? Maybe, but I
     * don't think it's worth the extra code to support it. */

/* XXX: Need to write this function here...
    content = cairo_surface_get_content (pattern->surface);
    if (content == CAIRO_CONTENT_ALPHA)
	return FALSE;
*/

    /* Cast away the const, trusting get_extend not to muck with it.
     * And I really wish I had a way to cast away just the const, and
     * not potentially coerce this pointer to an incorrect type at the
     * same time. :-(
     */
    extend = cairo_pattern_get_extend ((cairo_pattern_t*)&pattern->base);
    switch (extend) {
    case CAIRO_EXTEND_NONE:
    case CAIRO_EXTEND_REPEAT:
    case CAIRO_EXTEND_REFLECT:
    /* There's no point returning FALSE for EXTEND_PAD, as the image
     * surface does not currently implement it either */
    case CAIRO_EXTEND_PAD:
	return TRUE;
    }

    ASSERT_NOT_REACHED;
    return FALSE;
}

static cairo_bool_t
_gradient_pattern_supported (cairo_ps_surface_t    *surface,
			     cairo_pattern_t *pattern)
{
    cairo_extend_t extend;

    if (surface->ps_level == CAIRO_PS_LEVEL_2)
	return FALSE;

    surface->ps_level_used = CAIRO_PS_LEVEL_3;
    extend = cairo_pattern_get_extend (pattern);

    if (extend == CAIRO_EXTEND_REPEAT ||
        extend == CAIRO_EXTEND_REFLECT) {
        return FALSE;
    }

    /* Radial gradients are currently only supported when one circle
     * is inside the other. */
    if (pattern->type == CAIRO_PATTERN_TYPE_RADIAL) {
        double x1, y1, x2, y2, r1, r2, d;
        cairo_radial_pattern_t *radial = (cairo_radial_pattern_t *) pattern;

        x1 = _cairo_fixed_to_double (radial->c1.x);
        y1 = _cairo_fixed_to_double (radial->c1.y);
        r1 = _cairo_fixed_to_double (radial->r1);
        x2 = _cairo_fixed_to_double (radial->c2.x);
        y2 = _cairo_fixed_to_double (radial->c2.y);
        r2 = _cairo_fixed_to_double (radial->r2);

        d = sqrt((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));
        if (d > fabs(r2 - r1)) {
            return FALSE;
        }
    }

    return TRUE;
}

static cairo_bool_t
pattern_supported (cairo_ps_surface_t *surface, cairo_pattern_t *pattern)
{
    if (pattern->type == CAIRO_PATTERN_TYPE_SOLID)
	return TRUE;

    if (pattern->type == CAIRO_PATTERN_TYPE_LINEAR ||
	pattern->type == CAIRO_PATTERN_TYPE_RADIAL)
	return _gradient_pattern_supported (surface, pattern);

    if (pattern->type == CAIRO_PATTERN_TYPE_SURFACE)
	return surface_pattern_supported ((cairo_surface_pattern_t *) pattern);

    return FALSE;
}

static cairo_int_status_t
_cairo_ps_surface_analyze_operation (cairo_ps_surface_t    *surface,
				     cairo_operator_t       op,
				     cairo_pattern_t       *pattern)
{
    if (surface->force_fallbacks && surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (! pattern_supported (surface, pattern))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (!(op == CAIRO_OPERATOR_SOURCE ||
	  op == CAIRO_OPERATOR_OVER))
	return CAIRO_INT_STATUS_UNSUPPORTED;

    if (pattern->type == CAIRO_PATTERN_TYPE_SURFACE) {
	cairo_surface_pattern_t *surface_pattern = (cairo_surface_pattern_t *) pattern;

	if ( _cairo_surface_is_meta (surface_pattern->surface))
	    return CAIRO_INT_STATUS_ANALYZE_META_SURFACE_PATTERN;
    }

    if (op == CAIRO_OPERATOR_SOURCE)
	return CAIRO_STATUS_SUCCESS;

    /* CAIRO_OPERATOR_OVER is only supported for opaque patterns. If
     * the pattern contains transparency, we return
     * CAIRO_INT_STATUS_FLATTEN_TRANSPARENCY to the analysis
     * surface. If the analysis surface determines that there is
     * anything drawn under this operation, a fallback image will be
     * used. Otherwise the operation will be replayed during the
     * render stage and we blend the transarency into the white
     * background to convert the pattern to opaque.
     */

    if (pattern->type == CAIRO_PATTERN_TYPE_SURFACE) {
	cairo_surface_pattern_t *surface_pattern = (cairo_surface_pattern_t *) pattern;

	return _cairo_ps_surface_analyze_surface_pattern_transparency (surface,
								       surface_pattern);
    }

    if (_cairo_pattern_is_opaque (pattern))
	return CAIRO_STATUS_SUCCESS;
    else
	return CAIRO_INT_STATUS_FLATTEN_TRANSPARENCY;
}

static cairo_bool_t
_cairo_ps_surface_operation_supported (cairo_ps_surface_t    *surface,
				       cairo_operator_t       op,
				       cairo_pattern_t       *pattern)
{
    if (_cairo_ps_surface_analyze_operation (surface, op, pattern) != CAIRO_INT_STATUS_UNSUPPORTED)
	return TRUE;
    else
	return FALSE;
}

/* The "standard" implementation limit for PostScript string sizes is
 * 65535 characters (see PostScript Language Reference, Appendix
 * B). We go one short of that because we sometimes need two
 * characters in a string to represent a single ASCII85 byte, (for the
 * escape sequences "\\", "\(", and "\)") and we must not split these
 * across two strings. So we'd be in trouble if we went right to the
 * limit and one of these escape sequences just happened to land at
 * the end.
 */
#define STRING_ARRAY_MAX_STRING_SIZE (65535-1)
#define STRING_ARRAY_MAX_COLUMN	     72

typedef struct _string_array_stream {
    cairo_output_stream_t base;
    cairo_output_stream_t *output;
    int column;
    int string_size;
} string_array_stream_t;

static cairo_status_t
_string_array_stream_write (cairo_output_stream_t *base,
			    const unsigned char   *data,
			    unsigned int	   length)
{
    string_array_stream_t *stream = (string_array_stream_t *) base;
    unsigned char c;
    const unsigned char backslash = '\\';

    if (length == 0)
	return CAIRO_STATUS_SUCCESS;

    while (length--) {
	if (stream->string_size == 0) {
	    _cairo_output_stream_printf (stream->output, "(");
	    stream->column++;
	}

	c = *data++;
	switch (c) {
	case '\\':
	case '(':
	case ')':
	    _cairo_output_stream_write (stream->output, &backslash, 1);
	    stream->column++;
	    stream->string_size++;
	    break;
	/* Have to also be careful to never split the final ~> sequence. */
	case '~':
	    _cairo_output_stream_write (stream->output, &c, 1);
	    stream->column++;
	    stream->string_size++;
	    length--;
	    c = *data++;
	    break;
	}
	_cairo_output_stream_write (stream->output, &c, 1);
	stream->column++;
	stream->string_size++;

	if (stream->string_size >= STRING_ARRAY_MAX_STRING_SIZE) {
	    _cairo_output_stream_printf (stream->output, ")\n");
	    stream->string_size = 0;
	    stream->column = 0;
	}
	if (stream->column >= STRING_ARRAY_MAX_COLUMN) {
	    _cairo_output_stream_printf (stream->output, "\n ");
	    stream->string_size += 2;
	    stream->column = 1;
	}
    }

    return _cairo_output_stream_get_status (stream->output);
}

static cairo_status_t
_string_array_stream_close (cairo_output_stream_t *base)
{
    cairo_status_t status;
    string_array_stream_t *stream = (string_array_stream_t *) base;

    _cairo_output_stream_printf (stream->output, ")\n");

    status = _cairo_output_stream_get_status (stream->output);

    return status;
}

/* A string_array_stream wraps an existing output stream. It takes the
 * data provided to it and output one or more consecutive string
 * objects, each within the standard PostScript implementation limit
 * of 65k characters.
 *
 * The strings are each separated by a space character for easy
 * inclusion within an array object, (but the array delimiters are not
 * added by the string_array_stream).
 *
 * The string array stream is also careful to wrap the output within
 * STRING_ARRAY_MAX_COLUMN columns (+/- 1). The stream also adds
 * necessary escaping for special characters within a string,
 * (specifically '\', '(', and ')').
 */
static cairo_output_stream_t *
_string_array_stream_create (cairo_output_stream_t *output)
{
    string_array_stream_t *stream;

    stream = malloc (sizeof (string_array_stream_t));
    if (stream == NULL) {
	_cairo_error_throw (CAIRO_STATUS_NO_MEMORY);
	return (cairo_output_stream_t *) &_cairo_output_stream_nil;
    }

    _cairo_output_stream_init (&stream->base,
			       _string_array_stream_write,
			       _string_array_stream_close);
    stream->output = output;
    stream->column = 0;
    stream->string_size = 0;

    return &stream->base;
}

/* PS Output - this section handles output of the parts of the meta
 * surface we can render natively in PS. */

static cairo_status_t
_cairo_ps_surface_flatten_image_transparency (cairo_ps_surface_t    *surface,
					      cairo_image_surface_t *image,
					      cairo_image_surface_t **opaque_image)
{
    const cairo_color_t *background_color;
    cairo_surface_t *opaque;
    cairo_pattern_union_t pattern;
    cairo_status_t status;

    if (surface->content == CAIRO_CONTENT_COLOR_ALPHA)
	background_color = CAIRO_COLOR_WHITE;
    else
	background_color = CAIRO_COLOR_BLACK;

    opaque = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
					 image->width,
					 image->height);
    if (opaque->status)
	return opaque->status;

    _cairo_pattern_init_for_surface (&pattern.surface, &image->base);

    status = _cairo_surface_fill_rectangle (opaque,
					    CAIRO_OPERATOR_SOURCE,
					    background_color,
					    0, 0,
					    image->width, image->height);
    if (status)
	goto fail;

    status = _cairo_surface_composite (CAIRO_OPERATOR_OVER,
				       &pattern.base,
				       NULL,
				       opaque,
				       0, 0,
				       0, 0,
				       0, 0,
				       image->width,
				       image->height);
    if (status)
	goto fail;

    _cairo_pattern_fini (&pattern.base);
    *opaque_image = (cairo_image_surface_t *) opaque;

    return CAIRO_STATUS_SUCCESS;

fail:
    _cairo_pattern_fini (&pattern.base);
    cairo_surface_destroy (opaque);

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_base85_string (cairo_ps_surface_t    *surface,
				      unsigned char	    *data,
				      unsigned long	     length)
{
    cairo_output_stream_t *base85_stream, *string_array_stream;
    cairo_status_t status, status2;

    string_array_stream = _string_array_stream_create (surface->stream);
    status = _cairo_output_stream_get_status (string_array_stream);
    if (status)
	return _cairo_output_stream_destroy (string_array_stream);

    base85_stream = _cairo_base85_stream_create (string_array_stream);
    status = _cairo_output_stream_get_status (base85_stream);
    if (status) {
	status2 = _cairo_output_stream_destroy (string_array_stream);
	return _cairo_output_stream_destroy (base85_stream);
    }

    _cairo_output_stream_write (base85_stream, data, length);

    status = _cairo_output_stream_destroy (base85_stream);
    status2 = _cairo_output_stream_destroy (string_array_stream);
    if (status == CAIRO_STATUS_SUCCESS)
	status = status2;

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_image (cairo_ps_surface_t    *surface,
			      cairo_image_surface_t *image,
			      const char	    *name,
			      cairo_operator_t	     op)
{
    cairo_status_t status;
    unsigned char *rgb, *rgb_compressed;
    unsigned long rgb_size, rgb_compressed_size;
    unsigned char *mask = NULL, *mask_compressed = NULL;
    unsigned long mask_size = 0, mask_compressed_size = 0;
    cairo_image_surface_t *opaque_image = NULL;
    int x, y, i;
    cairo_image_transparency_t transparency;
    cairo_bool_t use_mask;

    if (image->base.status)
	return image->base.status;

    status = _analyze_image_transparency (image, &transparency);
    if (status)
	return status;

    /* PostScript can not represent the alpha channel, so we blend the
       current image over a white (or black for CONTENT_COLOR
       surfaces) RGB surface to eliminate it. */

    if (op == CAIRO_OPERATOR_SOURCE ||
	transparency == CAIRO_IMAGE_HAS_ALPHA ||
	(transparency == CAIRO_IMAGE_HAS_BILEVEL_ALPHA &&
	 surface->ps_level == CAIRO_PS_LEVEL_2))
    {
	status = _cairo_ps_surface_flatten_image_transparency (surface,
							       image,
							       &opaque_image);
	if (status)
	    return status;

	use_mask = FALSE;
    } else if (transparency == CAIRO_IMAGE_IS_OPAQUE) {
	opaque_image = image;
	use_mask = FALSE;
    } else {
	use_mask = TRUE;
    }

    rgb_size = 3 * image->width * image->height;
    rgb = malloc (rgb_size);
    if (rgb == NULL) {
	status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	goto bail1;
    }

    if (use_mask) {
	mask_size = ((image->width+7) / 8) * image->height;
	mask = malloc (mask_size);
	if (mask == NULL) {
	    status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    goto bail2;
	}
    }

    if (use_mask) {
	int byte = 0;
	int bit = 7;
	i = 0;
	for (y = 0; y < image->height; y++) {
	    uint32_t *pixel = (uint32_t *) (image->data + y * image->stride);
	    for (x = 0; x < image->width; x++, pixel++) {
		if (bit == 7)
		    mask[byte] = 0;
		if (((*pixel & 0xff000000) >> 24) > 0x80)
		    mask[byte] |= (1 << bit);
		bit--;
		if (bit < 0) {
		    bit = 7;
		    byte++;
		}
		rgb[i++] = (*pixel & 0x00ff0000) >> 16;
		rgb[i++] = (*pixel & 0x0000ff00) >>  8;
		rgb[i++] = (*pixel & 0x000000ff) >>  0;
	    }

	    if (bit != 7) {
		bit = 7;
		byte++;
	    }
	}
    } else {
	i = 0;
	for (y = 0; y < opaque_image->height; y++) {
	    uint32_t *pixel = (uint32_t *) (opaque_image->data + y * opaque_image->stride);
	    for (x = 0; x < opaque_image->width; x++, pixel++) {
		rgb[i++] = (*pixel & 0x00ff0000) >> 16;
		rgb[i++] = (*pixel & 0x0000ff00) >>  8;
		rgb[i++] = (*pixel & 0x000000ff) >>  0;
	    }
	}
    }

    /* XXX: Should fix cairo-lzw to provide a stream-based interface
     * instead. */
    rgb_compressed_size = rgb_size;
    rgb_compressed = _cairo_lzw_compress (rgb, &rgb_compressed_size);
    if (rgb_compressed == NULL) {
	status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	goto bail3;
    }

    /* First emit the image data as a base85-encoded string which will
     * be used as the data source for the image operator later. */
    _cairo_output_stream_printf (surface->stream,
				 "/%sData [\n", name);

    status = _cairo_ps_surface_emit_base85_string (surface,
						   rgb_compressed,
						   rgb_compressed_size);
    if (status)
	goto bail4;

    _cairo_output_stream_printf (surface->stream,
				 "] def\n");
    _cairo_output_stream_printf (surface->stream,
				 "/%sDataIndex 0 def\n", name);

    /* Emit the mask data as a base85-encoded string which will
     * be used as the mask source for the image operator later. */
    if (mask) {
	mask_compressed_size = mask_size;
	mask_compressed = _cairo_lzw_compress (mask, &mask_compressed_size);
	if (mask_compressed == NULL) {
	    status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    goto bail4;
	}

	_cairo_output_stream_printf (surface->stream,
				     "/%sMask [\n", name);

	status = _cairo_ps_surface_emit_base85_string (surface,
						       mask_compressed,
						       mask_compressed_size);
	if (status)
	    goto bail5;

	_cairo_output_stream_printf (surface->stream,
				     "] def\n");
	_cairo_output_stream_printf (surface->stream,
				     "/%sMaskIndex 0 def\n", name);
    }

    if (mask) {
	_cairo_output_stream_printf (surface->stream,
				     "/%s {\n"
				     "    /DeviceRGB setcolorspace\n"
				     "    <<\n"
				     "	/ImageType 3\n"
				     "	/InterleaveType 3\n"
				     "	/DataDict <<\n"
				     "		/ImageType 1\n"
				     "		/Width %d\n"
				     "		/Height %d\n"
				     "		/BitsPerComponent 8\n"
				     "		/Decode [ 0 1 0 1 0 1 ]\n"
				     "		/DataSource {\n"
				     "	    		%sData %sDataIndex get\n"
				     "	    		/%sDataIndex %sDataIndex 1 add def\n"
				     "	    		%sDataIndex %sData length 1 sub gt { /%sDataIndex 0 def } if\n"
				     "		} /ASCII85Decode filter /LZWDecode filter\n"
				     "		/ImageMatrix [ 1 0 0 1 0 0 ]\n"
				     "	>>\n"
				     "	/MaskDict <<\n"
				     "		/ImageType 1\n"
				     "		/Width %d\n"
				     "		/Height %d\n"
				     "		/BitsPerComponent 1\n"
				     "		/Decode [ 1 0 ]\n"
				     "		/DataSource {\n"
				     "	    		%sMask %sMaskIndex get\n"
				     "	    		/%sMaskIndex %sMaskIndex 1 add def\n"
				     "	    		%sMaskIndex %sMask length 1 sub gt { /%sMaskIndex 0 def } if\n"
				     "		} /ASCII85Decode filter /LZWDecode filter\n"
				     "		/ImageMatrix [ 1 0 0 1 0 0 ]\n"
				     "	>>\n"
				     "    >>\n"
				     "    image\n"
				     "} def\n",
				     name,
				     image->width,
				     image->height,
				     name, name, name, name, name, name, name,
				     image->width,
				     image->height,
				     name, name, name, name, name, name, name);
    } else {
	_cairo_output_stream_printf (surface->stream,
				     "/%s {\n"
				     "    /DeviceRGB setcolorspace\n"
				     "    <<\n"
				     "	/ImageType 1\n"
				     "	/Width %d\n"
				     "	/Height %d\n"
				     "	/BitsPerComponent 8\n"
				     "	/Decode [ 0 1 0 1 0 1 ]\n"
				     "	/DataSource {\n"
				     "	    %sData %sDataIndex get\n"
				     "	    /%sDataIndex %sDataIndex 1 add def\n"
				     "	    %sDataIndex %sData length 1 sub gt { /%sDataIndex 0 def } if\n"
				     "	} /ASCII85Decode filter /LZWDecode filter\n"
				     "	/ImageMatrix [ 1 0 0 1 0 0 ]\n"
				     "    >>\n"
				     "    image\n"
				     "} def\n",
				     name,
				     opaque_image->width,
				     opaque_image->height,
				     name, name, name, name, name, name, name);
    }

    status = CAIRO_STATUS_SUCCESS;

bail5:
    if (use_mask)
	free (mask_compressed);
bail4:
    free (rgb_compressed);

bail3:
    if (use_mask)
	free (mask);
bail2:
    free (rgb);

bail1:
    if (!use_mask && opaque_image != image)
	cairo_surface_destroy (&opaque_image->base);

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_image_surface (cairo_ps_surface_t      *surface,
				      cairo_surface_pattern_t *pattern,
				      int                     *width,
				      int                     *height,
				      cairo_operator_t	       op)
{
    cairo_image_surface_t  *image;
    void		   *image_extra;
    cairo_status_t          status;

    status = _cairo_surface_acquire_source_image (pattern->surface,
						  &image,
						  &image_extra);
    if (status)
	return status;

    status = _cairo_ps_surface_emit_image (surface, image, "CairoPattern", op);
    if (status)
	goto fail;

    *width = image->width;
    *height = image->height;

fail:
    _cairo_surface_release_source_image (pattern->surface, image, image_extra);

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_meta_surface (cairo_ps_surface_t  *surface,
				     cairo_surface_t      *meta_surface)
{
    double old_width, old_height;
    cairo_content_t old_content;
    cairo_rectangle_int_t meta_extents;
    cairo_status_t status;

    status = _cairo_surface_get_extents (meta_surface, &meta_extents);
    if (status)
	return status;

    old_content = surface->content;
    old_width = surface->width;
    old_height = surface->height;
    surface->width = meta_extents.width;
    surface->height = meta_extents.height;
    _cairo_output_stream_printf (surface->stream,
				 "/CairoPattern {\n"
				 "gsave\n");

    if (cairo_surface_get_content (meta_surface) == CAIRO_CONTENT_COLOR) {
	surface->content = CAIRO_CONTENT_COLOR;
	_cairo_output_stream_printf (surface->stream,
				     "0 G 0 0 %f %f rectfill\n",
				     surface->width,
				     surface->height);
    }

    status = _cairo_meta_surface_replay_region (meta_surface, &surface->base,
						CAIRO_META_REGION_NATIVE);
    assert (status != CAIRO_INT_STATUS_UNSUPPORTED);
    if (status)
	return status;

    _cairo_output_stream_printf (surface->stream,
				 "grestore\n"
				 "} bind def\n");
    surface->content = old_content;
    surface->width = old_width;
    surface->height = old_height;

    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_ps_surface_flatten_transparency (cairo_ps_surface_t	*surface,
					const cairo_color_t	*color,
					double			*red,
					double			*green,
					double			*blue)
{
    *red = color->red;
    *green = color->green;
    *blue = color->blue;

    if (!CAIRO_COLOR_IS_OPAQUE(color)) {
	if (surface->content == CAIRO_CONTENT_COLOR_ALPHA) {
	    uint8_t one_minus_alpha = 255 - (color->alpha_short >> 8);

	    *red   = ((color->red_short   >> 8) + one_minus_alpha) / 255.0;
	    *green = ((color->green_short >> 8) + one_minus_alpha) / 255.0;
	    *blue  = ((color->blue_short  >> 8) + one_minus_alpha) / 255.0;
	} else {
	    *red   = (color->red_short   >> 8) / 255.0;
	    *green = (color->green_short >> 8) / 255.0;
	    *blue  = (color->blue_short  >> 8) / 255.0;
	}
    }
}

static void
_cairo_ps_surface_emit_solid_pattern (cairo_ps_surface_t    *surface,
				      cairo_solid_pattern_t *pattern)
{
    double red, green, blue;

    _cairo_ps_surface_flatten_transparency (surface, &pattern->color, &red, &green, &blue);

    if (color_is_gray (red, green, blue))
	_cairo_output_stream_printf (surface->stream,
				     "%f G\n",
				     red);
    else
	_cairo_output_stream_printf (surface->stream,
				     "%f %f %f R\n",
				     red, green, blue);
}

static cairo_status_t
_cairo_ps_surface_emit_surface_pattern (cairo_ps_surface_t      *surface,
					cairo_surface_pattern_t *pattern,
					cairo_operator_t	 op)
{
    cairo_status_t status;
    int pattern_width = 0; /* squelch bogus compiler warning */
    int pattern_height = 0; /* squelch bogus compiler warning */
    double xstep, ystep;
    cairo_matrix_t inverse = pattern->base.matrix;

    status = cairo_matrix_invert (&inverse);
    /* cairo_pattern_set_matrix ensures the matrix is invertible */
    assert (status == CAIRO_STATUS_SUCCESS);

    if (_cairo_surface_is_meta (pattern->surface)) {
	cairo_surface_t *meta_surface = pattern->surface;
	cairo_rectangle_int_t pattern_extents;

	status = _cairo_ps_surface_emit_meta_surface (surface,
						      meta_surface);
	if (status)
	    return status;

	status = _cairo_surface_get_extents (meta_surface, &pattern_extents);
	if (status)
	    return status;

	pattern_width = pattern_extents.width;
	pattern_height = pattern_extents.height;
    } else {
	status = _cairo_ps_surface_emit_image_surface (surface,
						       pattern,
						       &pattern_width,
						       &pattern_height,
						       op);
	if (status)
	    return status;
    }

    switch (pattern->base.extend) {
	/* We implement EXTEND_PAD like EXTEND_NONE for now */
    case CAIRO_EXTEND_PAD:
    case CAIRO_EXTEND_NONE:
    {
	/* In PS/PDF, (as far as I can tell), all patterns are
	 * repeating. So we support cairo's EXTEND_NONE semantics
	 * by setting the repeat step size to a size large enough
	 * to guarantee that no more than a single occurrence will
	 * be visible.
	 *
	 * First, map the surface extents into pattern space (since
	 * xstep and ystep are in pattern space).  Then use an upper
	 * bound on the length of the diagonal of the pattern image
	 * and the surface as repeat size.  This guarantees to never
	 * repeat visibly.
	 */
	double x1 = 0.0, y1 = 0.0;
	double x2 = surface->width, y2 = surface->height;
	_cairo_matrix_transform_bounding_box (&pattern->base.matrix,
					      &x1, &y1, &x2, &y2,
					      NULL);

	/* Rather than computing precise bounds of the union, just
	 * add the surface extents unconditionally. We only
	 * required an answer that's large enough, we don't really
	 * care if it's not as tight as possible.*/
	xstep = ystep = ceil ((x2 - x1) + (y2 - y1) +
			      pattern_width + pattern_height);
	break;
    }
    case CAIRO_EXTEND_REPEAT:
    case CAIRO_EXTEND_REFLECT:
	xstep = pattern_width;
	ystep = pattern_height;
	break;
	/* All the rest (if any) should have been analyzed away, so these
	 * cases should be unreachable. */
    default:
	ASSERT_NOT_REACHED;
	xstep = 0;
	ystep = 0;
    }

    _cairo_output_stream_printf (surface->stream,
				 "<< /PatternType 1\n"
				 "   /PaintType 1\n"
				 "   /TilingType 1\n");
    _cairo_output_stream_printf (surface->stream,
				 "   /BBox [0 0 %d %d]\n",
				 pattern_width, pattern_height);
    _cairo_output_stream_printf (surface->stream,
				 "   /XStep %f /YStep %f\n",
				 xstep, ystep);
    _cairo_output_stream_printf (surface->stream,
				 "   /PaintProc { CairoPattern } bind\n"
				 ">>\n");
    _cairo_output_stream_printf (surface->stream,
				 "[ %f %f %f %f %f %f ]\n",
				 inverse.xx, inverse.yx,
				 inverse.xy, inverse.yy,
				 inverse.x0, inverse.y0);
    _cairo_output_stream_printf (surface->stream,
				 "makepattern setpattern\n");

    return CAIRO_STATUS_SUCCESS;
}

typedef struct _cairo_ps_color_stop {
    double offset;
    double color[3];
} cairo_ps_color_stop_t;

static void
_cairo_ps_surface_emit_linear_colorgradient (cairo_ps_surface_t     *surface,
					     cairo_ps_color_stop_t  *stop1,
					     cairo_ps_color_stop_t  *stop2)
{
    _cairo_output_stream_printf (surface->stream,
				 "<< /FunctionType 2\n"
				 "   /Domain [ 0 1 ]\n"
				 "   /C0 [ %f %f %f ]\n"
				 "   /C1 [ %f %f %f ]\n"
				 "   /N 1\n"
				 ">>\n",
				 stop1->color[0],
				 stop1->color[1],
				 stop1->color[2],
				 stop2->color[0],
				 stop2->color[1],
				 stop2->color[2]);
}

static void
_cairo_ps_surface_emit_stitched_colorgradient (cairo_ps_surface_t    *surface,
					       unsigned int 	      n_stops,
					       cairo_ps_color_stop_t  stops[])
{
    unsigned int i;

    _cairo_output_stream_printf (surface->stream,
				 "      << /FunctionType 3\n"
				 "         /Domain [ 0 1 ]\n"
				 "         /Functions [\n");
    for (i = 0; i < n_stops - 1; i++)
	_cairo_ps_surface_emit_linear_colorgradient (surface, &stops[i], &stops[i+1]);

    _cairo_output_stream_printf (surface->stream, "         ]\n");

    _cairo_output_stream_printf (surface->stream, "         /Bounds [ ");
    for (i = 1; i < n_stops-1; i++)
	_cairo_output_stream_printf (surface->stream, "%f ", stops[i].offset);
    _cairo_output_stream_printf (surface->stream, "]\n");

    _cairo_output_stream_printf (surface->stream, "         /Encode [ ");
    for (i = 1; i < n_stops; i++)
	_cairo_output_stream_printf (surface->stream, "0 1 ");
    _cairo_output_stream_printf (surface->stream,  "]\n");

    _cairo_output_stream_printf (surface->stream, "      >>\n");
}

#define COLOR_STOP_EPSILON 1e-6

static cairo_status_t
_cairo_ps_surface_emit_pattern_stops (cairo_ps_surface_t       *surface,
				      cairo_gradient_pattern_t *pattern)
{
    cairo_ps_color_stop_t *allstops, *stops;
    unsigned int i, n_stops;

    allstops = _cairo_malloc_ab ((pattern->n_stops + 2), sizeof (cairo_ps_color_stop_t));
    if (allstops == NULL)
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    stops = &allstops[1];
    n_stops = pattern->n_stops;

    for (i = 0; i < n_stops; i++) {
	double red, green, blue;

	_cairo_ps_surface_flatten_transparency (surface,
						&pattern->stops[i].color,
						&red, &green, &blue);
	stops[i].color[0] = red;
	stops[i].color[1] = green;
	stops[i].color[2] = blue;
	stops[i].offset = _cairo_fixed_to_double (pattern->stops[i].x);
    }

    /* make sure first offset is 0.0 and last offset is 1.0 */
    if (stops[0].offset > COLOR_STOP_EPSILON) {
	memcpy (allstops, stops, sizeof (cairo_ps_color_stop_t));
	stops = allstops;
	n_stops++;
    }
    stops[0].offset = 0.0;

    if (stops[n_stops-1].offset < 1.0 - COLOR_STOP_EPSILON) {
	memcpy (&stops[n_stops],
		&stops[n_stops - 1],
		sizeof (cairo_ps_color_stop_t));
	n_stops++;
    }
    stops[n_stops-1].offset = 1.0;

    if (n_stops == 2) {
	/* no need for stitched function */
	_cairo_ps_surface_emit_linear_colorgradient (surface, &stops[0], &stops[1]);
    } else {
	/* multiple stops: stitch. XXX possible optimization: regulary spaced
	 * stops do not require stitching. XXX */
	_cairo_ps_surface_emit_stitched_colorgradient (surface, n_stops,stops);
    }

    free (allstops);

    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairo_ps_surface_emit_linear_pattern (cairo_ps_surface_t     *surface,
				       cairo_linear_pattern_t *pattern)
{
    double x1, y1, x2, y2;
    cairo_extend_t extend;
    cairo_status_t status;
    cairo_matrix_t inverse = pattern->base.base.matrix;

    if (pattern->base.n_stops == 0)
        return CAIRO_INT_STATUS_NOTHING_TO_DO;

    extend = cairo_pattern_get_extend (&pattern->base.base);

    status = cairo_matrix_invert (&inverse);
    if (status)
	return status;

    x1 = _cairo_fixed_to_double (pattern->p1.x);
    y1 = _cairo_fixed_to_double (pattern->p1.y);
    x2 = _cairo_fixed_to_double (pattern->p2.x);
    y2 = _cairo_fixed_to_double (pattern->p2.y);

    _cairo_output_stream_printf (surface->stream,
				 "<< /PatternType 2\n"
				 "   /Shading\n"
				 "   << /ShadingType 2\n"
				 "      /ColorSpace /DeviceRGB\n"
				 "      /Coords [ %f %f %f %f ]\n"
				 "      /Function\n",
				 x1, y1, x2, y2);

    status = _cairo_ps_surface_emit_pattern_stops (surface, &pattern->base);
    if (status)
	return status;

    if (extend == CAIRO_EXTEND_PAD) {
	_cairo_output_stream_printf (surface->stream,
                                     "      /Extend [ true true ]\r\n");
    } else {
	_cairo_output_stream_printf (surface->stream,
                                     "      /Extend [ false false ]\r\n");
    }

    _cairo_output_stream_printf (surface->stream,
				 "   >>\n"
				 ">>\n");
    _cairo_output_stream_printf (surface->stream,
				 "[ %f %f %f %f %f %f ]\n",
				 inverse.xx, inverse.yx,
				 inverse.xy, inverse.yy,
				 inverse.x0, inverse.y0);
    _cairo_output_stream_printf (surface->stream,
				 "makepattern setpattern\n");

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_radial_pattern (cairo_ps_surface_t     *surface,
				       cairo_radial_pattern_t *pattern)
{
    double x1, y1, x2, y2, r1, r2;
    cairo_extend_t extend;
    cairo_status_t status;
    cairo_matrix_t inverse = pattern->base.base.matrix;

    if (pattern->base.n_stops == 0)
        return CAIRO_INT_STATUS_NOTHING_TO_DO;

    extend = cairo_pattern_get_extend (&pattern->base.base);

    status = cairo_matrix_invert (&inverse);
    if (status)
	return status;

    x1 = _cairo_fixed_to_double (pattern->c1.x);
    y1 = _cairo_fixed_to_double (pattern->c1.y);
    r1 = _cairo_fixed_to_double (pattern->r1);
    x2 = _cairo_fixed_to_double (pattern->c2.x);
    y2 = _cairo_fixed_to_double (pattern->c2.y);
    r2 = _cairo_fixed_to_double (pattern->r2);

    _cairo_output_stream_printf (surface->stream,
				 "<< /PatternType 2\n"
				 "   /Shading\n"
				 "   << /ShadingType 3\n"
				 "      /ColorSpace /DeviceRGB\n"
				 "      /Coords [ %f %f %f %f %f %f ]\n"
				 "      /Function\n",
				 x1, y1, r1, x2, y2, r2);

    status = _cairo_ps_surface_emit_pattern_stops (surface, &pattern->base);
    if (status)
	return status;

    if (extend == CAIRO_EXTEND_PAD) {
	_cairo_output_stream_printf (surface->stream,
                                     "      /Extend [ true true ]\r\n");
    } else {
	_cairo_output_stream_printf (surface->stream,
                                     "      /Extend [ false false ]\r\n");
    }

    _cairo_output_stream_printf (surface->stream,
				 "   >>\n"
				 ">>\n");
    _cairo_output_stream_printf (surface->stream,
				 "[ %f %f %f %f %f %f ]\n",
				 inverse.xx, inverse.yx,
				 inverse.xy, inverse.yy,
				 inverse.x0, inverse.y0);
    _cairo_output_stream_printf (surface->stream,
				 "makepattern setpattern\n");

    return status;
}

static cairo_status_t
_cairo_ps_surface_emit_pattern (cairo_ps_surface_t *surface,
				cairo_pattern_t *pattern,
				cairo_operator_t op)
{
    /* FIXME: We should keep track of what pattern is currently set in
     * the postscript file and only emit code if we're setting a
     * different pattern. */
    cairo_status_t status;

    switch (pattern->type) {
    case CAIRO_PATTERN_TYPE_SOLID:
	_cairo_ps_surface_emit_solid_pattern (surface, (cairo_solid_pattern_t *) pattern);
	break;

    case CAIRO_PATTERN_TYPE_SURFACE:
	status = _cairo_ps_surface_emit_surface_pattern (surface,
							 (cairo_surface_pattern_t *) pattern,
							 op);
	if (status)
	    return status;
	break;

    case CAIRO_PATTERN_TYPE_LINEAR:
	status = _cairo_ps_surface_emit_linear_pattern (surface,
					  (cairo_linear_pattern_t *) pattern);
	if (status)
	    return status;
	break;

    case CAIRO_PATTERN_TYPE_RADIAL:
	status = _cairo_ps_surface_emit_radial_pattern (surface,
					  (cairo_radial_pattern_t *) pattern);
	if (status)
	    return status;
	break;
    }

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_ps_surface_intersect_clip_path (void		   *abstract_surface,
				cairo_path_fixed_t *path,
				cairo_fill_rule_t   fill_rule,
				double		    tolerance,
				cairo_antialias_t   antialias)
{
    cairo_ps_surface_t *surface = abstract_surface;
    cairo_output_stream_t *stream = surface->stream;
    cairo_status_t status;
    const char *ps_operator;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
	return CAIRO_STATUS_SUCCESS;

#if DEBUG_PS
    _cairo_output_stream_printf (stream,
				 "%% _cairo_ps_surface_intersect_clip_path\n");
#endif

    if (path == NULL) {
	_cairo_output_stream_printf (stream, "grestore gsave\n");
	return CAIRO_STATUS_SUCCESS;
    }

    /* We're "filling" not stroking, so we pass CAIRO_LINE_CAP_ROUND. */
    status = _cairo_ps_surface_emit_path (surface, stream, path,
					  CAIRO_LINE_CAP_ROUND);

    switch (fill_rule) {
    case CAIRO_FILL_RULE_WINDING:
	ps_operator = "clip";
	break;
    case CAIRO_FILL_RULE_EVEN_ODD:
	ps_operator = "eoclip";
	break;
    default:
	ASSERT_NOT_REACHED;
    }

    _cairo_output_stream_printf (stream,
				 "%s newpath\n",
				 ps_operator);

    return status;
}

static cairo_int_status_t
_cairo_ps_surface_get_extents (void		       *abstract_surface,
			       cairo_rectangle_int_t   *rectangle)
{
    cairo_ps_surface_t *surface = abstract_surface;

    rectangle->x = 0;
    rectangle->y = 0;

    /* XXX: The conversion to integers here is pretty bogus, (not to
     * mention the aribitray limitation of width to a short(!). We
     * may need to come up with a better interface for get_extents.
     */
    rectangle->width  = (int) ceil (surface->width);
    rectangle->height = (int) ceil (surface->height);

    return CAIRO_STATUS_SUCCESS;
}

static void
_cairo_ps_surface_get_font_options (void                  *abstract_surface,
				    cairo_font_options_t  *options)
{
    _cairo_font_options_init_default (options);

    cairo_font_options_set_hint_style (options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics (options, CAIRO_HINT_METRICS_OFF);
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_GRAY);
}

static cairo_int_status_t
_cairo_ps_surface_paint (void			*abstract_surface,
			 cairo_operator_t	 op,
			 cairo_pattern_t	*source)
{
    cairo_ps_surface_t *surface = abstract_surface;
    cairo_output_stream_t *stream = surface->stream;
    cairo_rectangle_int_t extents, pattern_extents;
    cairo_status_t status;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
	return _cairo_ps_surface_analyze_operation (surface, op, source);

    assert (_cairo_ps_surface_operation_supported (surface, op, source));

#if DEBUG_PS
    _cairo_output_stream_printf (stream,
				 "%% _cairo_ps_surface_paint\n");
#endif

    status = _cairo_surface_get_extents (&surface->base, &extents);
    if (status)
	return status;

    status = _cairo_pattern_get_extents (source, &pattern_extents);
    if (status)
	return status;

    _cairo_rectangle_intersect (&extents, &pattern_extents);

    status = _cairo_ps_surface_emit_pattern (surface, source, op);
    if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
        return CAIRO_STATUS_SUCCESS;

    if (status)
	return status;

    _cairo_output_stream_printf (stream, "%d %d M\n",
				 extents.x, extents.y);
    _cairo_output_stream_printf (stream, "%d %d L\n",
				 extents.x + extents.width,
				 extents.y);
    _cairo_output_stream_printf (stream, "%d %d L\n",
				 extents.x + extents.width,
				 extents.y + extents.height);
    _cairo_output_stream_printf (stream, "%d %d L\n",
				 extents.x,
				 extents.y + extents.height);
    _cairo_output_stream_printf (stream, "P F\n");

    return CAIRO_STATUS_SUCCESS;
}

static int
_cairo_ps_line_cap (cairo_line_cap_t cap)
{
    switch (cap) {
    case CAIRO_LINE_CAP_BUTT:
	return 0;
    case CAIRO_LINE_CAP_ROUND:
	return 1;
    case CAIRO_LINE_CAP_SQUARE:
	return 2;
    default:
	ASSERT_NOT_REACHED;
	return 0;
    }
}

static int
_cairo_ps_line_join (cairo_line_join_t join)
{
    switch (join) {
    case CAIRO_LINE_JOIN_MITER:
	return 0;
    case CAIRO_LINE_JOIN_ROUND:
	return 1;
    case CAIRO_LINE_JOIN_BEVEL:
	return 2;
    default:
	ASSERT_NOT_REACHED;
	return 0;
    }
}

static cairo_int_status_t
_cairo_ps_surface_stroke (void			*abstract_surface,
			  cairo_operator_t	 op,
			  cairo_pattern_t	*source,
			  cairo_path_fixed_t	*path,
			  cairo_stroke_style_t	*style,
			  cairo_matrix_t	*ctm,
			  cairo_matrix_t	*ctm_inverse,
			  double		 tolerance,
			  cairo_antialias_t	 antialias)
{
    cairo_ps_surface_t *surface = abstract_surface;
    cairo_output_stream_t *stream = surface->stream;
    cairo_int_status_t status;
    double *dash = style->dash;
    int num_dashes = style->num_dashes;
    double dash_offset = style->dash_offset;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
	return _cairo_ps_surface_analyze_operation (surface, op, source);

    assert (_cairo_ps_surface_operation_supported (surface, op, source));

#if DEBUG_PS
    _cairo_output_stream_printf (stream,
				 "%% _cairo_ps_surface_stroke\n");
#endif

    /* PostScript has "special needs" when it comes to zero-length
     * dash segments with butt caps. It apparently (at least
     * according to ghostscript) draws hairlines for this
     * case. That's not what the cairo semantics want, so we first
     * touch up the array to eliminate any 0.0 values that will
     * result in "on" segments.
     */
    if (num_dashes && style->line_cap == CAIRO_LINE_CAP_BUTT) {
	int i;

	/* If there's an odd number of dash values they will each get
	 * interpreted as both on and off. So we first explicitly
	 * expand the array to remove the duplicate usage so that we
	 * can modify some of the values.
	 */
	if (num_dashes % 2) {
	    dash = _cairo_malloc_abc (num_dashes, 2, sizeof (double));
	    if (dash == NULL)
		return _cairo_error (CAIRO_STATUS_NO_MEMORY);

	    memcpy (dash, style->dash, num_dashes * sizeof (double));
	    memcpy (dash + num_dashes, style->dash, num_dashes * sizeof (double));

	    num_dashes *= 2;
	}

	for (i = 0; i < num_dashes; i += 2) {
	    if (dash[i] == 0.0) {
		/* If we're at the front of the list, we first rotate
		 * two elements from the end of the list to the front
		 * of the list before folding away the 0.0. Or, if
		 * there are only two dash elements, then there is
		 * nothing at all to draw.
		 */
		if (i == 0) {
		    double last_two[2];

		    if (num_dashes == 2) {
			if (dash != style->dash)
			    free (dash);
			return CAIRO_STATUS_SUCCESS;
		    }
		    /* The cases of num_dashes == 0, 1, or 3 elements
		     * cannot exist, so the rotation of 2 elements
		     * will always be safe */
		    memcpy (last_two, dash + num_dashes - 2, sizeof (last_two));
		    memmove (dash + 2, dash, (num_dashes - 2) * sizeof (double));
		    memcpy (dash, last_two, sizeof (last_two));
		    dash_offset += dash[0] + dash[1];
		    i = 2;
		}
		dash[i-1] += dash[i+1];
		num_dashes -= 2;
		memmove (dash + i, dash + i + 2, (num_dashes - i) * sizeof (double));
		/* If we might have just rotated, it's possible that
		 * we rotated a 0.0 value to the front of the list.
		 * Set i to -2 so it will get incremented to 0. */
		if (i == 2)
		    i = -2;
	    }
	}
    }

    status = _cairo_ps_surface_emit_pattern (surface, source, op);
    if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
        return CAIRO_STATUS_SUCCESS;

    if (status) {
	if (dash != style->dash)
	    free (dash);
	return status;
    }

    _cairo_output_stream_printf (stream,
				 "gsave\n");
    status = _cairo_ps_surface_emit_path (surface, stream, path,
					  style->line_cap);
    if (status) {
	if (dash != style->dash)
	    free (dash);
	return status;
    }

    /*
     * Switch to user space to set line parameters
     */
    _cairo_output_stream_printf (stream,
				 "[%f %f %f %f 0 0] concat\n",
				 ctm->xx, ctm->yx, ctm->xy, ctm->yy);
    /* line width */
    _cairo_output_stream_printf (stream, "%f setlinewidth\n",
				 style->line_width);
    /* line cap */
    _cairo_output_stream_printf (stream, "%d setlinecap\n",
				 _cairo_ps_line_cap (style->line_cap));
    /* line join */
    _cairo_output_stream_printf (stream, "%d setlinejoin\n",
				 _cairo_ps_line_join (style->line_join));
    /* dashes */
    if (num_dashes) {
	int d;

	_cairo_output_stream_printf (stream, "[");
	for (d = 0; d < num_dashes; d++)
	    _cairo_output_stream_printf (stream, " %f", dash[d]);
	_cairo_output_stream_printf (stream, "] %f setdash\n",
				     dash_offset);
    }
    if (dash != style->dash)
	free (dash);

    /* miter limit */
    _cairo_output_stream_printf (stream, "%f setmiterlimit\n",
				 style->miter_limit);
    _cairo_output_stream_printf (stream,
				 "stroke\n");
    _cairo_output_stream_printf (stream,
				 "grestore\n");

    return CAIRO_STATUS_SUCCESS;
}

static cairo_int_status_t
_cairo_ps_surface_fill (void		*abstract_surface,
		 cairo_operator_t	 op,
		 cairo_pattern_t	*source,
		 cairo_path_fixed_t	*path,
		 cairo_fill_rule_t	 fill_rule,
		 double			 tolerance,
		 cairo_antialias_t	 antialias)
{
    cairo_ps_surface_t *surface = abstract_surface;
    cairo_output_stream_t *stream = surface->stream;
    cairo_int_status_t status;
    const char *ps_operator;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
	return _cairo_ps_surface_analyze_operation (surface, op, source);

    assert (_cairo_ps_surface_operation_supported (surface, op, source));

#if DEBUG_PS
    _cairo_output_stream_printf (stream,
				 "%% _cairo_ps_surface_fill\n");
#endif

    status = _cairo_ps_surface_emit_pattern (surface, source, op);
    if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
        return CAIRO_STATUS_SUCCESS;

    if (status)
	return status;

    /* We're filling not stroking, so we pass CAIRO_LINE_CAP_ROUND. */
    status = _cairo_ps_surface_emit_path (surface, stream, path,
					  CAIRO_LINE_CAP_ROUND);
    if (status)
	return status;

    switch (fill_rule) {
    case CAIRO_FILL_RULE_WINDING:
	ps_operator = "F";
	break;
    case CAIRO_FILL_RULE_EVEN_ODD:
	ps_operator = "eofill";
	break;
    default:
	ASSERT_NOT_REACHED;
    }

    _cairo_output_stream_printf (stream,
				 "%s\n", ps_operator);

    return CAIRO_STATUS_SUCCESS;
}

/* This size keeps the length of the hex encoded string of glyphs
 * within 80 columns. */
#define MAX_GLYPHS_PER_SHOW  36

typedef struct _cairo_ps_glyph_id {
    unsigned int subset_id;
    unsigned int glyph_id;
} cairo_ps_glyph_id_t;

static cairo_int_status_t
_cairo_ps_surface_show_glyphs (void		     *abstract_surface,
			       cairo_operator_t	      op,
			       cairo_pattern_t	     *source,
			       cairo_glyph_t         *glyphs,
			       int		      num_glyphs,
			       cairo_scaled_font_t   *scaled_font)
{
    cairo_ps_surface_t *surface = abstract_surface;
    cairo_output_stream_t *stream = surface->stream;
    unsigned int current_subset_id = -1;
    cairo_scaled_font_subsets_glyph_t subset_glyph;
    cairo_ps_glyph_id_t *glyph_ids;
    cairo_status_t status;
    unsigned int num_glyphs_unsigned, i, j, last, end;
    cairo_bool_t vertical, horizontal;
    cairo_output_stream_t *word_wrap;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
	return _cairo_ps_surface_analyze_operation (surface, op, source);

    assert (_cairo_ps_surface_operation_supported (surface, op, source));

#if DEBUG_PS
    _cairo_output_stream_printf (stream,
				 "%% _cairo_ps_surface_show_glyphs\n");
#endif

    if (num_glyphs <= 0)
        return CAIRO_STATUS_SUCCESS;

    num_glyphs_unsigned = num_glyphs;

    status = _cairo_ps_surface_emit_pattern (surface, source, op);
    if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
        return CAIRO_STATUS_SUCCESS;

    if (status)
	return status;

    glyph_ids = _cairo_malloc_ab (num_glyphs_unsigned, sizeof (cairo_ps_glyph_id_t));
    if (glyph_ids == NULL)
	return _cairo_error (CAIRO_STATUS_NO_MEMORY);

    for (i = 0; i < num_glyphs_unsigned; i++) {
        status = _cairo_scaled_font_subsets_map_glyph (surface->font_subsets,
                                                       scaled_font, glyphs[i].index,
                                                       &subset_glyph);
        if (status)
            goto fail;

        glyph_ids[i].subset_id = subset_glyph.subset_id;
        glyph_ids[i].glyph_id = subset_glyph.subset_glyph_index;
    }

    i = 0;
    while (i < num_glyphs_unsigned) {
        if (glyph_ids[i].subset_id != current_subset_id) {
            _cairo_output_stream_printf (surface->stream,
                                         "/CairoFont-%d-%d "
                                         "[ %f %f %f %f 0 0 ] selectfont\n",
                                         subset_glyph.font_id,
                                         glyph_ids[i].subset_id,
                                         scaled_font->scale.xx,
                                         scaled_font->scale.yx,
                                         -scaled_font->scale.xy,
                                         -scaled_font->scale.yy);
            current_subset_id = glyph_ids[i].subset_id;
        }

        if (i == 0)
            _cairo_output_stream_printf (stream,
                                         "%f %f M\n",
                                         glyphs[i].x,
                                         glyphs[i].y);

        horizontal = TRUE;
        vertical = TRUE;
        end = num_glyphs_unsigned;
        if (end - i > MAX_GLYPHS_PER_SHOW)
            end = i + MAX_GLYPHS_PER_SHOW;
        last = end - 1;
        for (j = i; j < end - 1; j++) {
            if ((glyphs[j].y != glyphs[j + 1].y))
                horizontal = FALSE;
            if ((glyphs[j].x != glyphs[j + 1].x))
                vertical = FALSE;
            if (glyph_ids[j].subset_id != glyph_ids[j + 1].subset_id) {
                last = j;
                break;
            }
        }

        if (i == last) {
            _cairo_output_stream_printf (surface->stream, "<%02x> S\n", glyph_ids[i].glyph_id);
        } else {
            word_wrap = _word_wrap_stream_create (surface->stream, 79);
	    if (_cairo_output_stream_get_status (word_wrap)) {
		status = _cairo_output_stream_destroy (word_wrap);
		goto fail;
	    }

            _cairo_output_stream_printf (word_wrap, "<");
            for (j = i; j < last+1; j++)
                _cairo_output_stream_printf (word_wrap, "%02x", glyph_ids[j].glyph_id);
            _cairo_output_stream_printf (word_wrap, ">\n[");

            if (horizontal) {
                for (j = i; j < last+1; j++) {
                    if (j == num_glyphs_unsigned - 1)
                        _cairo_output_stream_printf (word_wrap, "0 ");
                    else
                        _cairo_output_stream_printf (word_wrap,
                                                     "%f ", glyphs[j + 1].x - glyphs[j].x);
                }
                _cairo_output_stream_printf (word_wrap, "] xS\n");
            } else if (vertical) {
                for (j = i; j < last+1; j++) {
                    if (j == num_glyphs_unsigned - 1)
                        _cairo_output_stream_printf (word_wrap, "0 ");
                    else
                        _cairo_output_stream_printf (word_wrap,
                                                     "%f ", glyphs[j + 1].y - glyphs[j].y);
                }
                _cairo_output_stream_printf (word_wrap, "] yS\n");
            } else {
                for (j = i; j < last+1; j++) {
                    if (j == num_glyphs_unsigned - 1)
                        _cairo_output_stream_printf (word_wrap, "0 0 ");
                    else
                        _cairo_output_stream_printf (word_wrap,
                                                     "%f %f ",
                                                     glyphs[j + 1].x - glyphs[j].x,
                                                     glyphs[j + 1].y - glyphs[j].y);
                }
                _cairo_output_stream_printf (word_wrap, "] xyS\n");
            }

            status = _cairo_output_stream_destroy (word_wrap);
            if (status)
                goto fail;
        }
        i = last + 1;
    }

    status = _cairo_output_stream_get_status (surface->stream);
fail:
    free (glyph_ids);

    return status;
}

static void
_cairo_ps_surface_set_paginated_mode (void			*abstract_surface,
				      cairo_paginated_mode_t	 paginated_mode)
{
    cairo_ps_surface_t *surface = abstract_surface;

    surface->paginated_mode = paginated_mode;
}

static cairo_int_status_t
_cairo_ps_surface_set_bounding_box (void		*abstract_surface,
				    cairo_box_t		*bbox)
{
    cairo_ps_surface_t *surface = abstract_surface;
    int i, num_comments;
    char **comments;
    int x1, y1, x2, y2;

    if (surface->eps) {
	x1 = (int) floor (_cairo_fixed_to_double (bbox->p1.x));
	y1 = (int) floor (surface->height - _cairo_fixed_to_double (bbox->p2.y));
	x2 = (int) ceil (_cairo_fixed_to_double (bbox->p2.x));
	y2 = (int) ceil (surface->height - _cairo_fixed_to_double (bbox->p1.y));
    } else {
	x1 = 0;
	y1 = 0;
	x2 = (int) ceil (surface->width);
	y2 = (int) ceil (surface->height);
    }

    _cairo_output_stream_printf (surface->stream,
				 "%%%%Page: %d %d\n",
				 surface->num_pages,
				 surface->num_pages);

    _cairo_output_stream_printf (surface->stream,
				 "%%%%BeginPageSetup\n");

    num_comments = _cairo_array_num_elements (&surface->dsc_page_setup_comments);
    comments = _cairo_array_index (&surface->dsc_page_setup_comments, 0);
    for (i = 0; i < num_comments; i++) {
	_cairo_output_stream_printf (surface->stream,
				     "%s\n", comments[i]);
	free (comments[i]);
	comments[i] = NULL;
    }
    _cairo_array_truncate (&surface->dsc_page_setup_comments, 0);

    _cairo_output_stream_printf (surface->stream,
				 "%%%%PageBoundingBox: %d %d %d %d\n"
				 "gsave %f %f translate 1.0 -1.0 scale gsave\n",
				 x1, y1, x2, y2,
				 0.0, surface->height);

    _cairo_output_stream_printf (surface->stream,
                                 "%%%%EndPageSetup\n");

    if (surface->num_pages == 1) {
	surface->bbox_x1 = x1;
	surface->bbox_y1 = y1;
	surface->bbox_x2 = x2;
	surface->bbox_y2 = y2;
    } else {
	if (x1 < surface->bbox_x1)
	    surface->bbox_x1 = x1;
	if (y1 < surface->bbox_y1)
	    surface->bbox_y1 = y1;
	if (x2 > surface->bbox_x2)
	    surface->bbox_x2 = x2;
	if (y2 > surface->bbox_y2)
	    surface->bbox_y2 = y2;
    }

    return _cairo_output_stream_get_status (surface->stream);
}

static const cairo_surface_backend_t cairo_ps_surface_backend = {
    CAIRO_SURFACE_TYPE_PS,
    _cairo_ps_surface_create_similar,
    _cairo_ps_surface_finish,
    NULL, /* acquire_source_image */
    NULL, /* release_source_image */
    NULL, /* acquire_dest_image */
    NULL, /* release_dest_image */
    NULL, /* clone_similar */
    NULL, /* composite */
    NULL, /* fill_rectangles */
    NULL, /* composite_trapezoids */
    NULL, /* cairo_ps_surface_copy_page */
    _cairo_ps_surface_show_page,
    NULL, /* set_clip_region */
    _cairo_ps_surface_intersect_clip_path,
    _cairo_ps_surface_get_extents,
    NULL, /* old_show_glyphs */
    _cairo_ps_surface_get_font_options,
    NULL, /* flush */
    NULL, /* mark_dirty_rectangle */
    NULL, /* scaled_font_fini */
    NULL, /* scaled_glyph_fini */

    /* Here are the drawing functions */

    _cairo_ps_surface_paint, /* paint */
    NULL, /* mask */
    _cairo_ps_surface_stroke,
    _cairo_ps_surface_fill,
    _cairo_ps_surface_show_glyphs,
    NULL, /* snapshot */
};

static const cairo_paginated_surface_backend_t cairo_ps_surface_paginated_backend = {
    _cairo_ps_surface_start_page,
    _cairo_ps_surface_set_paginated_mode,
    _cairo_ps_surface_set_bounding_box,
};
