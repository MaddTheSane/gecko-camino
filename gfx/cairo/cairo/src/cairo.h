/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2002 University of Southern California
 * Copyright © 2005 Red Hat, Inc.
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
 */

#ifndef CAIRO_H
#define CAIRO_H

#include "cairo-version.h"
#include "cairo-features.h"
#include "cairo-deprecated.h"

#ifdef  __cplusplus
# define CAIRO_BEGIN_DECLS  extern "C" {
# define CAIRO_END_DECLS    }
#else
# define CAIRO_BEGIN_DECLS
# define CAIRO_END_DECLS
#endif

#ifndef cairo_public
# if defined (_MSC_VER) && ! defined (CAIRO_WIN32_STATIC_BUILD)
#  define cairo_public __declspec(dllimport)
# else
#  define cairo_public
# endif
#endif

CAIRO_BEGIN_DECLS

#define CAIRO_VERSION_ENCODE(major, minor, micro) (	\
	  ((major) * 10000)				\
	+ ((minor) *   100)				\
	+ ((micro) *     1))

#define CAIRO_VERSION CAIRO_VERSION_ENCODE(	\
	CAIRO_VERSION_MAJOR,			\
	CAIRO_VERSION_MINOR,			\
	CAIRO_VERSION_MICRO)


#define CAIRO_VERSION_STRINGIZE_(major, minor, micro)	\
	#major"."#minor"."#micro
#define CAIRO_VERSION_STRINGIZE(major, minor, micro)	\
	CAIRO_VERSION_STRINGIZE_(major, minor, micro)

#define CAIRO_VERSION_STRING CAIRO_VERSION_STRINGIZE(	\
	CAIRO_VERSION_MAJOR,				\
	CAIRO_VERSION_MINOR,				\
	CAIRO_VERSION_MICRO)


cairo_public int
cairo_version (void);

cairo_public const char*
cairo_version_string (void);

/**
 * cairo_bool_t:
 *
 * #cairo_bool_t is used for boolean values. Returns of type
 * #cairo_bool_t will always be either 0 or 1, but testing against
 * these values explicitly is not encouraged; just use the
 * value as a boolean condition.
 *
 * <informalexample><programlisting>
 *  if (cairo_in_stroke (cr, x, y)) {
 *      /<!-- -->* do something *<!-- -->/
 *  }
 * </programlisting></informalexample>
 **/
typedef int cairo_bool_t;

/**
 * cairo_t:
 *
 * A #cairo_t contains the current state of the rendering device,
 * including coordinates of yet to be drawn shapes.
 *
 * Cairo contexts, as #cairo_t objects are named, are central to
 * cairo and all drawing with cairo is always done to a #cairo_t
 * object.
 *
 * Memory management of #cairo_t is done with
 * cairo_reference() and cairo_destroy().
 **/
typedef struct _cairo cairo_t;

/**
 * cairo_surface_t:
 *
 * A #cairo_surface_t represents an image, either as the destination
 * of a drawing operation or as source when drawing onto another
 * surface.  To draw to a #cairo_surface_t, create a cairo context
 * with the surface as the target, using cairo_create().
 *
 * There are different subtypes of #cairo_surface_t for
 * different drawing backends; for example, cairo_image_surface_create()
 * creates a bitmap image in memory.
 * The type of a surface can be queried with cairo_surface_get_type().
 *
 * Memory management of #cairo_surface_t is done with
 * cairo_surface_reference() and cairo_surface_destroy().
 **/
typedef struct _cairo_surface cairo_surface_t;

/**
 * cairo_matrix_t:
 * @xx: xx component of the affine transformation
 * @yx: yx component of the affine transformation
 * @xy: xy component of the affine transformation
 * @yy: yy component of the affine transformation
 * @x0: X translation component of the affine transformation
 * @y0: Y translation component of the affine transformation
 *
 * A #cairo_matrix_t holds an affine transformation, such as a scale,
 * rotation, shear, or a combination of those. The transformation of
 * a point (x, y) is given by:
 * <programlisting>
 *     x_new = xx * x + xy * y + x0;
 *     y_new = yx * x + yy * y + y0;
 * </programlisting>
 **/
typedef struct _cairo_matrix {
    double xx; double yx;
    double xy; double yy;
    double x0; double y0;
} cairo_matrix_t;

/**
 * cairo_pattern_t:
 *
 * A #cairo_pattern_t represents a source when drawing onto a
 * surface. There are different subtypes of #cairo_pattern_t,
 * for different types of sources; for example,
 * cairo_pattern_create_rgb() creates a pattern for a solid
 * opaque color.
 *
 * Other than various cairo_pattern_create_<emphasis>type</emphasis>()
 * functions, some of the pattern types can be implicitly created
 * using various cairo_set_source_<emphasis>type</emphasis>() functions;
 * for example cairo_set_source_rgb().
 *
 * The type of a pattern can be queried with cairo_pattern_get_type().
 *
 * Memory management of #cairo_pattern_t is done with
 * cairo_pattern_reference() and cairo_pattern_destroy().
 **/
typedef struct _cairo_pattern cairo_pattern_t;

/**
 * cairo_destroy_func_t:
 * @data: The data element being destroyed.
 *
 * #cairo_destroy_func_t the type of function which is called when a
 * data element is destroyed. It is passed the pointer to the data
 * element and should free any memory and resources allocated for it.
 **/
typedef void (*cairo_destroy_func_t) (void *data);

/**
 * cairo_user_data_key_t:
 * @unused: not used; ignore.
 *
 * #cairo_user_data_key_t is used for attaching user data to cairo
 * data structures.  The actual contents of the struct is never used,
 * and there is no need to initialize the object; only the unique
 * address of a #cairo_data_key_t object is used.  Typically, you
 * would just use the address of a static #cairo_data_key_t object.
 **/
typedef struct _cairo_user_data_key {
    int unused;
} cairo_user_data_key_t;

/**
 * cairo_status_t:
 * @CAIRO_STATUS_SUCCESS: no error has occurred
 *
 * @CAIRO_STATUS_NO_MEMORY: out of memory
 * @CAIRO_STATUS_INVALID_RESTORE: cairo_restore() called without matching cairo_save()
 * @CAIRO_STATUS_INVALID_POP_GROUP: no saved group to pop
 * @CAIRO_STATUS_NO_CURRENT_POINT: no current point defined
 * @CAIRO_STATUS_INVALID_MATRIX: invalid matrix (not invertible)
 * @CAIRO_STATUS_INVALID_STATUS: invalid value for an input #cairo_status_t
 * @CAIRO_STATUS_NULL_POINTER: %NULL pointer
 * @CAIRO_STATUS_INVALID_STRING: input string not valid UTF-8
 * @CAIRO_STATUS_INVALID_PATH_DATA: input path data not valid
 * @CAIRO_STATUS_READ_ERROR: error while reading from input stream
 * @CAIRO_STATUS_WRITE_ERROR: error while writing to output stream
 * @CAIRO_STATUS_SURFACE_FINISHED: target surface has been finished
 * @CAIRO_STATUS_SURFACE_TYPE_MISMATCH: the surface type is not appropriate for the operation
 * @CAIRO_STATUS_PATTERN_TYPE_MISMATCH: the pattern type is not appropriate for the operation
 * @CAIRO_STATUS_INVALID_CONTENT: invalid value for an input #cairo_content_t
 * @CAIRO_STATUS_INVALID_FORMAT: invalid value for an input #cairo_format_t
 * @CAIRO_STATUS_INVALID_VISUAL: invalid value for an input Visual*
 * @CAIRO_STATUS_FILE_NOT_FOUND: file not found
 * @CAIRO_STATUS_INVALID_DASH: invalid value for a dash setting
 * @CAIRO_STATUS_INVALID_DSC_COMMENT: invalid value for a DSC comment (Since 1.2)
 * @CAIRO_STATUS_INVALID_INDEX: invalid index passed to getter (Since 1.4)
 * @CAIRO_STATUS_CLIP_NOT_REPRESENTABLE: clip region not representable in desired format (Since 1.4)
 * @CAIRO_STATUS_TEMP_FILE_ERROR: error creating or writing to a temporary file (Since 1.6)
 * @CAIRO_STATUS_INVALID_STRIDE: invalid value for stride (Since 1.6)
 * @CAIRO_STATUS_FONT_TYPE_MISMATCH: the font type is not appropriate for the operation (Since 1.8)
 * @CAIRO_STATUS_USER_FONT_IMMUTABLE: the user-font is immutable (Since 1.8)
 * @CAIRO_STATUS_USER_FONT_ERROR: error occurred in a user-font callback function (Since 1.8)
 * @CAIRO_STATUS_NEGATIVE_COUNT: negative number used where it is not allowed (Since 1.8)
 * @CAIRO_STATUS_INVALID_CLUSTERS: input clusters do not represent the accompanying text and glyph array (Since 1.8)
 * @CAIRO_STATUS_INVALID_SLANT: invalid value for an input #cairo_font_slant_t (Since 1.8)
 * @CAIRO_STATUS_INVALID_WEIGHT: invalid value for an input #cairo_font_weight_t (Since 1.8)
 * @CAIRO_STATUS_INVALID_SIZE: invalid value (typically too big) for a size (Since 1.10)
 *
 * @CAIRO_STATUS_LAST_STATUS: this is a special value indicating the number of
 *   status values defined in this enumeration.  When using this value, note
 *   that the version of cairo at run-time may have additional status values
 *   defined than the value of this symbol at compile-time. (Since 1.10)
 *
 * #cairo_status_t is used to indicate errors that can occur when
 * using Cairo. In some cases it is returned directly by functions.
 * but when using #cairo_t, the last error, if any, is stored in
 * the context and can be retrieved with cairo_status().
 *
 * New entries may be added in future versions.  Use cairo_status_to_string()
 * to get a human-readable representation of an error message.
 **/
typedef enum _cairo_status {
    CAIRO_STATUS_SUCCESS = 0,

    CAIRO_STATUS_NO_MEMORY,
    CAIRO_STATUS_INVALID_RESTORE,
    CAIRO_STATUS_INVALID_POP_GROUP,
    CAIRO_STATUS_NO_CURRENT_POINT,
    CAIRO_STATUS_INVALID_MATRIX,
    CAIRO_STATUS_INVALID_STATUS,
    CAIRO_STATUS_NULL_POINTER,
    CAIRO_STATUS_INVALID_STRING,
    CAIRO_STATUS_INVALID_PATH_DATA,
    CAIRO_STATUS_READ_ERROR,
    CAIRO_STATUS_WRITE_ERROR,
    CAIRO_STATUS_SURFACE_FINISHED,
    CAIRO_STATUS_SURFACE_TYPE_MISMATCH,
    CAIRO_STATUS_PATTERN_TYPE_MISMATCH,
    CAIRO_STATUS_INVALID_CONTENT,
    CAIRO_STATUS_INVALID_FORMAT,
    CAIRO_STATUS_INVALID_VISUAL,
    CAIRO_STATUS_FILE_NOT_FOUND,
    CAIRO_STATUS_INVALID_DASH,
    CAIRO_STATUS_INVALID_DSC_COMMENT,
    CAIRO_STATUS_INVALID_INDEX,
    CAIRO_STATUS_CLIP_NOT_REPRESENTABLE,
    CAIRO_STATUS_TEMP_FILE_ERROR,
    CAIRO_STATUS_INVALID_STRIDE,
    CAIRO_STATUS_FONT_TYPE_MISMATCH,
    CAIRO_STATUS_USER_FONT_IMMUTABLE,
    CAIRO_STATUS_USER_FONT_ERROR,
    CAIRO_STATUS_NEGATIVE_COUNT,
    CAIRO_STATUS_INVALID_CLUSTERS,
    CAIRO_STATUS_INVALID_SLANT,
    CAIRO_STATUS_INVALID_WEIGHT,
    CAIRO_STATUS_INVALID_SIZE,

    CAIRO_STATUS_LAST_STATUS
} cairo_status_t;

/**
 * cairo_content_t:
 * @CAIRO_CONTENT_COLOR: The surface will hold color content only.
 * @CAIRO_CONTENT_ALPHA: The surface will hold alpha content only.
 * @CAIRO_CONTENT_COLOR_ALPHA: The surface will hold color and alpha content.
 *
 * #cairo_content_t is used to describe the content that a surface will
 * contain, whether color information, alpha information (translucence
 * vs. opacity), or both.
 *
 * Note: The large values here are designed to keep #cairo_content_t
 * values distinct from #cairo_format_t values so that the
 * implementation can detect the error if users confuse the two types.
 **/
typedef enum _cairo_content {
    CAIRO_CONTENT_COLOR		= 0x1000,
    CAIRO_CONTENT_ALPHA		= 0x2000,
    CAIRO_CONTENT_COLOR_ALPHA	= 0x3000
} cairo_content_t;

/**
 * cairo_write_func_t:
 * @closure: the output closure
 * @data: the buffer containing the data to write
 * @length: the amount of data to write
 *
 * #cairo_write_func_t is the type of function which is called when a
 * backend needs to write data to an output stream.  It is passed the
 * closure which was specified by the user at the time the write
 * function was registered, the data to write and the length of the
 * data in bytes.  The write function should return
 * %CAIRO_STATUS_SUCCESS if all the data was successfully written,
 * %CAIRO_STATUS_WRITE_ERROR otherwise.
 *
 * Returns: the status code of the write operation
 **/
typedef cairo_status_t (*cairo_write_func_t) (void		  *closure,
					      const unsigned char *data,
					      unsigned int	   length);

/**
 * cairo_read_func_t:
 * @closure: the input closure
 * @data: the buffer into which to read the data
 * @length: the amount of data to read
 *
 * #cairo_read_func_t is the type of function which is called when a
 * backend needs to read data from an input stream.  It is passed the
 * closure which was specified by the user at the time the read
 * function was registered, the buffer to read the data into and the
 * length of the data in bytes.  The read function should return
 * %CAIRO_STATUS_SUCCESS if all the data was successfully read,
 * %CAIRO_STATUS_READ_ERROR otherwise.
 *
 * Returns: the status code of the read operation
 **/
typedef cairo_status_t (*cairo_read_func_t) (void		*closure,
					     unsigned char	*data,
					     unsigned int	length);

/* Functions for manipulating state objects */
cairo_public cairo_t *
cairo_create (cairo_surface_t *target);

cairo_public cairo_t *
cairo_reference (cairo_t *cr);

cairo_public void
cairo_destroy (cairo_t *cr);

cairo_public unsigned int
cairo_get_reference_count (cairo_t *cr);

cairo_public void *
cairo_get_user_data (cairo_t			 *cr,
		     const cairo_user_data_key_t *key);

cairo_public cairo_status_t
cairo_set_user_data (cairo_t			 *cr,
		     const cairo_user_data_key_t *key,
		     void			 *user_data,
		     cairo_destroy_func_t	  destroy);

cairo_public void
cairo_save (cairo_t *cr);

cairo_public void
cairo_restore (cairo_t *cr);

cairo_public void
cairo_push_group (cairo_t *cr);

cairo_public void
cairo_push_group_with_content (cairo_t *cr, cairo_content_t content);

cairo_public cairo_pattern_t *
cairo_pop_group (cairo_t *cr);

cairo_public void
cairo_pop_group_to_source (cairo_t *cr);

/* Modify state */

/**
 * cairo_operator_t:
 * @CAIRO_OPERATOR_CLEAR: clear destination layer (bounded)
 * @CAIRO_OPERATOR_SOURCE: replace destination layer (bounded)
 * @CAIRO_OPERATOR_OVER: draw source layer on top of destination layer
 * (bounded)
 * @CAIRO_OPERATOR_IN: draw source where there was destination content
 * (unbounded)
 * @CAIRO_OPERATOR_OUT: draw source where there was no destination
 * content (unbounded)
 * @CAIRO_OPERATOR_ATOP: draw source on top of destination content and
 * only there
 * @CAIRO_OPERATOR_DEST: ignore the source
 * @CAIRO_OPERATOR_DEST_OVER: draw destination on top of source
 * @CAIRO_OPERATOR_DEST_IN: leave destination only where there was
 * source content (unbounded)
 * @CAIRO_OPERATOR_DEST_OUT: leave destination only where there was no
 * source content
 * @CAIRO_OPERATOR_DEST_ATOP: leave destination on top of source content
 * and only there (unbounded)
 * @CAIRO_OPERATOR_XOR: source and destination are shown where there is only
 * one of them
 * @CAIRO_OPERATOR_ADD: source and destination layers are accumulated
 * @CAIRO_OPERATOR_SATURATE: like over, but assuming source and dest are
 * disjoint geometries
 *
 * #cairo_operator_t is used to set the compositing operator for all cairo
 * drawing operations.
 *
 * The default operator is %CAIRO_OPERATOR_OVER.
 *
 * The operators marked as <firstterm>unbounded</firstterm> modify their
 * destination even outside of the mask layer (that is, their effect is not
 * bound by the mask layer).  However, their effect can still be limited by
 * way of clipping.
 *
 * To keep things simple, the operator descriptions here
 * document the behavior for when both source and destination are either fully
 * transparent or fully opaque.  The actual implementation works for
 * translucent layers too.
 * For a more detailed explanation of the effects of each operator, including
 * the mathematical definitions, see
 * <ulink url="http://cairographics.org/operators/">http://cairographics.org/operators/</ulink>.
 **/
typedef enum _cairo_operator {
    CAIRO_OPERATOR_CLEAR,

    CAIRO_OPERATOR_SOURCE,
    CAIRO_OPERATOR_OVER,
    CAIRO_OPERATOR_IN,
    CAIRO_OPERATOR_OUT,
    CAIRO_OPERATOR_ATOP,

    CAIRO_OPERATOR_DEST,
    CAIRO_OPERATOR_DEST_OVER,
    CAIRO_OPERATOR_DEST_IN,
    CAIRO_OPERATOR_DEST_OUT,
    CAIRO_OPERATOR_DEST_ATOP,

    CAIRO_OPERATOR_XOR,
    CAIRO_OPERATOR_ADD,
    CAIRO_OPERATOR_SATURATE
} cairo_operator_t;

cairo_public void
cairo_set_operator (cairo_t *cr, cairo_operator_t op);

cairo_public void
cairo_set_source (cairo_t *cr, cairo_pattern_t *source);

cairo_public void
cairo_set_source_rgb (cairo_t *cr, double red, double green, double blue);

cairo_public void
cairo_set_source_rgba (cairo_t *cr,
		       double red, double green, double blue,
		       double alpha);

cairo_public void
cairo_set_source_surface (cairo_t	  *cr,
			  cairo_surface_t *surface,
			  double	   x,
			  double	   y);

cairo_public void
cairo_set_tolerance (cairo_t *cr, double tolerance);

/**
 * cairo_antialias_t:
 * @CAIRO_ANTIALIAS_DEFAULT: Use the default antialiasing for
 *   the subsystem and target device
 * @CAIRO_ANTIALIAS_NONE: Use a bilevel alpha mask
 * @CAIRO_ANTIALIAS_GRAY: Perform single-color antialiasing (using
 *  shades of gray for black text on a white background, for example).
 * @CAIRO_ANTIALIAS_SUBPIXEL: Perform antialiasing by taking
 *  advantage of the order of subpixel elements on devices
 *  such as LCD panels
 *
 * Specifies the type of antialiasing to do when rendering text or shapes.
 **/
typedef enum _cairo_antialias {
    CAIRO_ANTIALIAS_DEFAULT,
    CAIRO_ANTIALIAS_NONE,
    CAIRO_ANTIALIAS_GRAY,
    CAIRO_ANTIALIAS_SUBPIXEL
} cairo_antialias_t;

cairo_public void
cairo_set_antialias (cairo_t *cr, cairo_antialias_t antialias);

/**
 * cairo_fill_rule_t:
 * @CAIRO_FILL_RULE_WINDING: If the path crosses the ray from
 * left-to-right, counts +1. If the path crosses the ray
 * from right to left, counts -1. (Left and right are determined
 * from the perspective of looking along the ray from the starting
 * point.) If the total count is non-zero, the point will be filled.
 * @CAIRO_FILL_RULE_EVEN_ODD: Counts the total number of
 * intersections, without regard to the orientation of the contour. If
 * the total number of intersections is odd, the point will be
 * filled.
 *
 * #cairo_fill_rule_t is used to select how paths are filled. For both
 * fill rules, whether or not a point is included in the fill is
 * determined by taking a ray from that point to infinity and looking
 * at intersections with the path. The ray can be in any direction,
 * as long as it doesn't pass through the end point of a segment
 * or have a tricky intersection such as intersecting tangent to the path.
 * (Note that filling is not actually implemented in this way. This
 * is just a description of the rule that is applied.)
 *
 * The default fill rule is %CAIRO_FILL_RULE_WINDING.
 *
 * New entries may be added in future versions.
 **/
typedef enum _cairo_fill_rule {
    CAIRO_FILL_RULE_WINDING,
    CAIRO_FILL_RULE_EVEN_ODD
} cairo_fill_rule_t;

cairo_public void
cairo_set_fill_rule (cairo_t *cr, cairo_fill_rule_t fill_rule);

cairo_public void
cairo_set_line_width (cairo_t *cr, double width);

/**
 * cairo_line_cap_t:
 * @CAIRO_LINE_CAP_BUTT: start(stop) the line exactly at the start(end) point
 * @CAIRO_LINE_CAP_ROUND: use a round ending, the center of the circle is the end point
 * @CAIRO_LINE_CAP_SQUARE: use squared ending, the center of the square is the end point
 *
 * Specifies how to render the endpoints of the path when stroking.
 *
 * The default line cap style is %CAIRO_LINE_CAP_BUTT.
 **/
typedef enum _cairo_line_cap {
    CAIRO_LINE_CAP_BUTT,
    CAIRO_LINE_CAP_ROUND,
    CAIRO_LINE_CAP_SQUARE
} cairo_line_cap_t;

cairo_public void
cairo_set_line_cap (cairo_t *cr, cairo_line_cap_t line_cap);

/**
 * cairo_line_join_t:
 * @CAIRO_LINE_JOIN_MITER: use a sharp (angled) corner, see
 * cairo_set_miter_limit()
 * @CAIRO_LINE_JOIN_ROUND: use a rounded join, the center of the circle is the
 * joint point
 * @CAIRO_LINE_JOIN_BEVEL: use a cut-off join, the join is cut off at half
 * the line width from the joint point
 *
 * Specifies how to render the junction of two lines when stroking.
 *
 * The default line join style is %CAIRO_LINE_JOIN_MITER.
 **/
typedef enum _cairo_line_join {
    CAIRO_LINE_JOIN_MITER,
    CAIRO_LINE_JOIN_ROUND,
    CAIRO_LINE_JOIN_BEVEL
} cairo_line_join_t;

cairo_public void
cairo_set_line_join (cairo_t *cr, cairo_line_join_t line_join);

cairo_public void
cairo_set_dash (cairo_t      *cr,
		const double *dashes,
		int	      num_dashes,
		double	      offset);

cairo_public void
cairo_set_miter_limit (cairo_t *cr, double limit);

cairo_public void
cairo_translate (cairo_t *cr, double tx, double ty);

cairo_public void
cairo_scale (cairo_t *cr, double sx, double sy);

cairo_public void
cairo_rotate (cairo_t *cr, double angle);

cairo_public void
cairo_transform (cairo_t	      *cr,
		 const cairo_matrix_t *matrix);

cairo_public void
cairo_set_matrix (cairo_t	       *cr,
		  const cairo_matrix_t *matrix);

cairo_public void
cairo_identity_matrix (cairo_t *cr);

cairo_public void
cairo_user_to_device (cairo_t *cr, double *x, double *y);

cairo_public void
cairo_user_to_device_distance (cairo_t *cr, double *dx, double *dy);

cairo_public void
cairo_device_to_user (cairo_t *cr, double *x, double *y);

cairo_public void
cairo_device_to_user_distance (cairo_t *cr, double *dx, double *dy);

/* Path creation functions */
cairo_public void
cairo_new_path (cairo_t *cr);

cairo_public void
cairo_move_to (cairo_t *cr, double x, double y);

cairo_public void
cairo_new_sub_path (cairo_t *cr);

cairo_public void
cairo_line_to (cairo_t *cr, double x, double y);

cairo_public void
cairo_curve_to (cairo_t *cr,
		double x1, double y1,
		double x2, double y2,
		double x3, double y3);

cairo_public void
cairo_arc (cairo_t *cr,
	   double xc, double yc,
	   double radius,
	   double angle1, double angle2);

cairo_public void
cairo_arc_negative (cairo_t *cr,
		    double xc, double yc,
		    double radius,
		    double angle1, double angle2);

/* XXX: NYI
cairo_public void
cairo_arc_to (cairo_t *cr,
	      double x1, double y1,
	      double x2, double y2,
	      double radius);
*/

cairo_public void
cairo_rel_move_to (cairo_t *cr, double dx, double dy);

cairo_public void
cairo_rel_line_to (cairo_t *cr, double dx, double dy);

cairo_public void
cairo_rel_curve_to (cairo_t *cr,
		    double dx1, double dy1,
		    double dx2, double dy2,
		    double dx3, double dy3);

cairo_public void
cairo_rectangle (cairo_t *cr,
		 double x, double y,
		 double width, double height);

/* XXX: NYI
cairo_public void
cairo_stroke_to_path (cairo_t *cr);
*/

cairo_public void
cairo_close_path (cairo_t *cr);

cairo_public void
cairo_path_extents (cairo_t *cr,
		    double *x1, double *y1,
		    double *x2, double *y2);

/* Painting functions */
cairo_public void
cairo_paint (cairo_t *cr);

cairo_public void
cairo_paint_with_alpha (cairo_t *cr,
			double   alpha);

cairo_public void
cairo_mask (cairo_t         *cr,
	    cairo_pattern_t *pattern);

cairo_public void
cairo_mask_surface (cairo_t         *cr,
		    cairo_surface_t *surface,
		    double           surface_x,
		    double           surface_y);

cairo_public void
cairo_stroke (cairo_t *cr);

cairo_public void
cairo_stroke_preserve (cairo_t *cr);

cairo_public void
cairo_fill (cairo_t *cr);

cairo_public void
cairo_fill_preserve (cairo_t *cr);

cairo_public void
cairo_copy_page (cairo_t *cr);

cairo_public void
cairo_show_page (cairo_t *cr);

/* Insideness testing */
cairo_public cairo_bool_t
cairo_in_stroke (cairo_t *cr, double x, double y);

cairo_public cairo_bool_t
cairo_in_fill (cairo_t *cr, double x, double y);

/* Rectangular extents */
cairo_public void
cairo_stroke_extents (cairo_t *cr,
		      double *x1, double *y1,
		      double *x2, double *y2);

cairo_public void
cairo_fill_extents (cairo_t *cr,
		    double *x1, double *y1,
		    double *x2, double *y2);

/* Clipping */
cairo_public void
cairo_reset_clip (cairo_t *cr);

cairo_public void
cairo_clip (cairo_t *cr);

cairo_public void
cairo_clip_preserve (cairo_t *cr);

cairo_public void
cairo_clip_extents (cairo_t *cr,
		    double *x1, double *y1,
		    double *x2, double *y2);

/**
 * cairo_rectangle_t:
 * @x: X coordinate of the left side of the rectangle
 * @y: Y coordinate of the the top side of the rectangle
 * @width: width of the rectangle
 * @height: height of the rectangle
 *
 * A data structure for holding a rectangle.
 *
 * Since: 1.4
 **/
typedef struct _cairo_rectangle {
    double x, y, width, height;
} cairo_rectangle_t;

/**
 * cairo_rectangle_list_t:
 * @status: Error status of the rectangle list
 * @rectangles: Array containing the rectangles
 * @num_rectangles: Number of rectangles in this list
 * 
 * A data structure for holding a dynamically allocated
 * array of rectangles.
 *
 * Since: 1.4
 **/
typedef struct _cairo_rectangle_list {
    cairo_status_t     status;
    cairo_rectangle_t *rectangles;
    int                num_rectangles;
} cairo_rectangle_list_t;

cairo_public cairo_rectangle_list_t *
cairo_copy_clip_rectangle_list (cairo_t *cr);

cairo_public void
cairo_rectangle_list_destroy (cairo_rectangle_list_t *rectangle_list);

/* Font/Text functions */

/**
 * cairo_scaled_font_t:
 *
 * A #cairo_scaled_font_t is a font scaled to a particular size and device
 * resolution. A #cairo_scaled_font_t is most useful for low-level font
 * usage where a library or application wants to cache a reference
 * to a scaled font to speed up the computation of metrics.
 *
 * There are various types of scaled fonts, depending on the
 * <firstterm>font backend</firstterm> they use. The type of a
 * scaled font can be queried using cairo_scaled_font_get_type().
 *
 * Memory management of #cairo_scaled_font_t is done with
 * cairo_scaled_font_reference() and cairo_scaled_font_destroy().
 **/
typedef struct _cairo_scaled_font cairo_scaled_font_t;

/**
 * cairo_font_face_t:
 *
 * A #cairo_font_face_t specifies all aspects of a font other
 * than the size or font matrix (a font matrix is used to distort
 * a font by sheering it or scaling it unequally in the two
 * directions) . A font face can be set on a #cairo_t by using
 * cairo_set_font_face(); the size and font matrix are set with
 * cairo_set_font_size() and cairo_set_font_matrix().
 *
 * There are various types of font faces, depending on the
 * <firstterm>font backend</firstterm> they use. The type of a
 * font face can be queried using cairo_font_face_get_type().
 *
 * Memory management of #cairo_font_face_t is done with
 * cairo_font_face_reference() and cairo_font_face_destroy().
 **/
typedef struct _cairo_font_face cairo_font_face_t;

/**
 * cairo_glyph_t:
 * @index: glyph index in the font. The exact interpretation of the
 *      glyph index depends on the font technology being used.
 * @x: the offset in the X direction between the origin used for
 *     drawing or measuring the string and the origin of this glyph.
 * @y: the offset in the Y direction between the origin used for
 *     drawing or measuring the string and the origin of this glyph.
 *
 * The #cairo_glyph_t structure holds information about a single glyph
 * when drawing or measuring text. A font is (in simple terms) a
 * collection of shapes used to draw text. A glyph is one of these
 * shapes. There can be multiple glyphs for a single character
 * (alternates to be used in different contexts, for example), or a
 * glyph can be a <firstterm>ligature</firstterm> of multiple
 * characters. Cairo doesn't expose any way of converting input text
 * into glyphs, so in order to use the Cairo interfaces that take
 * arrays of glyphs, you must directly access the appropriate
 * underlying font system.
 *
 * Note that the offsets given by @x and @y are not cumulative. When
 * drawing or measuring text, each glyph is individually positioned
 * with respect to the overall origin
 **/
typedef struct {
    unsigned long        index;
    double               x;
    double               y;
} cairo_glyph_t;

cairo_public cairo_glyph_t *
cairo_glyph_allocate (int num_glyphs);

cairo_public void
cairo_glyph_free (cairo_glyph_t *glyphs);

/**
 * cairo_text_cluster_t:
 * @num_bytes: the number of bytes of UTF-8 text covered by cluster
 * @num_glyphs: the number of glyphs covered by cluster
 *
 * The #cairo_text_cluster_t structure holds information about a single
 * <firstterm>text cluster</firstterm>.  A text cluster is a minimal
 * mapping of some glyphs corresponding to some UTF-8 text.
 *
 * For a cluster to be valid, both @num_bytes and @num_glyphs should
 * be non-negative, and at least one should be non-zero.
 * Note that clusters with zero glyphs are not as well supported as
 * normal clusters.  For example, PDF rendering applications typically
 * ignore those clusters when PDF text is being selected.
 *
 * See cairo_show_text_glyphs() for how clusters are used in advanced
 * text operations.
 *
 * Since: 1.8
 **/
typedef struct {
    int        num_bytes;
    int        num_glyphs;
} cairo_text_cluster_t;

cairo_public cairo_text_cluster_t *
cairo_text_cluster_allocate (int num_clusters);

cairo_public void
cairo_text_cluster_free (cairo_text_cluster_t *clusters);

/**
 * cairo_text_cluster_flags_t:
 * @CAIRO_TEXT_CLUSTER_FLAG_BACKWARD: The clusters in the cluster array
 * map to glyphs in the glyph array from end to start.
 *
 * Specifies properties of a text cluster mapping.
 *
 * Since: 1.8
 **/
typedef enum _cairo_text_cluster_flags {
    CAIRO_TEXT_CLUSTER_FLAG_BACKWARD = 0x00000001
} cairo_text_cluster_flags_t;

/**
 * cairo_text_extents_t:
 * @x_bearing: the horizontal distance from the origin to the
 *   leftmost part of the glyphs as drawn. Positive if the
 *   glyphs lie entirely to the right of the origin.
 * @y_bearing: the vertical distance from the origin to the
 *   topmost part of the glyphs as drawn. Positive only if the
 *   glyphs lie completely below the origin; will usually be
 *   negative.
 * @width: width of the glyphs as drawn
 * @height: height of the glyphs as drawn
 * @x_advance:distance to advance in the X direction
 *    after drawing these glyphs
 * @y_advance: distance to advance in the Y direction
 *   after drawing these glyphs. Will typically be zero except
 *   for vertical text layout as found in East-Asian languages.
 *
 * The #cairo_text_extents_t structure stores the extents of a single
 * glyph or a string of glyphs in user-space coordinates. Because text
 * extents are in user-space coordinates, they are mostly, but not
 * entirely, independent of the current transformation matrix. If you call
 * <literal>cairo_scale(cr, 2.0, 2.0)</literal>, text will
 * be drawn twice as big, but the reported text extents will not be
 * doubled. They will change slightly due to hinting (so you can't
 * assume that metrics are independent of the transformation matrix),
 * but otherwise will remain unchanged.
 **/
typedef struct {
    double x_bearing;
    double y_bearing;
    double width;
    double height;
    double x_advance;
    double y_advance;
} cairo_text_extents_t;

/**
 * cairo_font_extents_t:
 * @ascent: the distance that the font extends above the baseline.
 *          Note that this is not always exactly equal to the maximum
 *          of the extents of all the glyphs in the font, but rather
 *          is picked to express the font designer's intent as to
 *          how the font should align with elements above it.
 * @descent: the distance that the font extends below the baseline.
 *           This value is positive for typical fonts that include
 *           portions below the baseline. Note that this is not always
 *           exactly equal to the maximum of the extents of all the
 *           glyphs in the font, but rather is picked to express the
 *           font designer's intent as to how the the font should
 *           align with elements below it.
 * @height: the recommended vertical distance between baselines when
 *          setting consecutive lines of text with the font. This
 *          is greater than @ascent+@descent by a
 *          quantity known as the <firstterm>line spacing</firstterm>
 *          or <firstterm>external leading</firstterm>. When space
 *          is at a premium, most fonts can be set with only
 *          a distance of @ascent+@descent between lines.
 * @max_x_advance: the maximum distance in the X direction that
 *         the the origin is advanced for any glyph in the font.
 * @max_y_advance: the maximum distance in the Y direction that
 *         the the origin is advanced for any glyph in the font.
 *         this will be zero for normal fonts used for horizontal
 *         writing. (The scripts of East Asia are sometimes written
 *         vertically.)
 *
 * The #cairo_font_extents_t structure stores metric information for
 * a font. Values are given in the current user-space coordinate
 * system.
 *
 * Because font metrics are in user-space coordinates, they are
 * mostly, but not entirely, independent of the current transformation
 * matrix. If you call <literal>cairo_scale(cr, 2.0, 2.0)</literal>,
 * text will be drawn twice as big, but the reported text extents will
 * not be doubled. They will change slightly due to hinting (so you
 * can't assume that metrics are independent of the transformation
 * matrix), but otherwise will remain unchanged.
 **/
typedef struct {
    double ascent;
    double descent;
    double height;
    double max_x_advance;
    double max_y_advance;
} cairo_font_extents_t;

/**
 * cairo_font_slant_t:
 * @CAIRO_FONT_SLANT_NORMAL: Upright font style
 * @CAIRO_FONT_SLANT_ITALIC: Italic font style
 * @CAIRO_FONT_SLANT_OBLIQUE: Oblique font style
 *
 * Specifies variants of a font face based on their slant.
 **/
typedef enum _cairo_font_slant {
    CAIRO_FONT_SLANT_NORMAL,
    CAIRO_FONT_SLANT_ITALIC,
    CAIRO_FONT_SLANT_OBLIQUE
} cairo_font_slant_t;

/**
 * cairo_font_weight_t:
 * @CAIRO_FONT_WEIGHT_NORMAL: Normal font weight
 * @CAIRO_FONT_WEIGHT_BOLD: Bold font weight
 *
 * Specifies variants of a font face based on their weight.
 **/
typedef enum _cairo_font_weight {
    CAIRO_FONT_WEIGHT_NORMAL,
    CAIRO_FONT_WEIGHT_BOLD
} cairo_font_weight_t;

/**
 * cairo_subpixel_order_t:
 * @CAIRO_SUBPIXEL_ORDER_DEFAULT: Use the default subpixel order for
 *   for the target device
 * @CAIRO_SUBPIXEL_ORDER_RGB: Subpixel elements are arranged horizontally
 *   with red at the left
 * @CAIRO_SUBPIXEL_ORDER_BGR:  Subpixel elements are arranged horizontally
 *   with blue at the left
 * @CAIRO_SUBPIXEL_ORDER_VRGB: Subpixel elements are arranged vertically
 *   with red at the top
 * @CAIRO_SUBPIXEL_ORDER_VBGR: Subpixel elements are arranged vertically
 *   with blue at the top
 *
 * The subpixel order specifies the order of color elements within
 * each pixel on the display device when rendering with an
 * antialiasing mode of %CAIRO_ANTIALIAS_SUBPIXEL.
 **/
typedef enum _cairo_subpixel_order {
    CAIRO_SUBPIXEL_ORDER_DEFAULT,
    CAIRO_SUBPIXEL_ORDER_RGB,
    CAIRO_SUBPIXEL_ORDER_BGR,
    CAIRO_SUBPIXEL_ORDER_VRGB,
    CAIRO_SUBPIXEL_ORDER_VBGR
} cairo_subpixel_order_t;

/**
 * cairo_hint_style_t:
 * @CAIRO_HINT_STYLE_DEFAULT: Use the default hint style for
 *   font backend and target device
 * @CAIRO_HINT_STYLE_NONE: Do not hint outlines
 * @CAIRO_HINT_STYLE_SLIGHT: Hint outlines slightly to improve
 *   contrast while retaining good fidelity to the original
 *   shapes.
 * @CAIRO_HINT_STYLE_MEDIUM: Hint outlines with medium strength
 *   giving a compromise between fidelity to the original shapes
 *   and contrast
 * @CAIRO_HINT_STYLE_FULL: Hint outlines to maximize contrast
 *
 * Specifies the type of hinting to do on font outlines. Hinting
 * is the process of fitting outlines to the pixel grid in order
 * to improve the appearance of the result. Since hinting outlines
 * involves distorting them, it also reduces the faithfulness
 * to the original outline shapes. Not all of the outline hinting
 * styles are supported by all font backends.
 *
 * New entries may be added in future versions.
 **/
typedef enum _cairo_hint_style {
    CAIRO_HINT_STYLE_DEFAULT,
    CAIRO_HINT_STYLE_NONE,
    CAIRO_HINT_STYLE_SLIGHT,
    CAIRO_HINT_STYLE_MEDIUM,
    CAIRO_HINT_STYLE_FULL
} cairo_hint_style_t;

/**
 * cairo_hint_metrics_t:
 * @CAIRO_HINT_METRICS_DEFAULT: Hint metrics in the default
 *  manner for the font backend and target device
 * @CAIRO_HINT_METRICS_OFF: Do not hint font metrics
 * @CAIRO_HINT_METRICS_ON: Hint font metrics
 *
 * Specifies whether to hint font metrics; hinting font metrics
 * means quantizing them so that they are integer values in
 * device space. Doing this improves the consistency of
 * letter and line spacing, however it also means that text
 * will be laid out differently at different zoom factors.
 **/
typedef enum _cairo_hint_metrics {
    CAIRO_HINT_METRICS_DEFAULT,
    CAIRO_HINT_METRICS_OFF,
    CAIRO_HINT_METRICS_ON
} cairo_hint_metrics_t;

/**
 * cairo_font_options_t:
 *
 * An opaque structure holding all options that are used when
 * rendering fonts.
 *
 * Individual features of a #cairo_font_options_t can be set or
 * accessed using functions named
 * cairo_font_options_set_<emphasis>feature_name</emphasis> and
 * cairo_font_options_get_<emphasis>feature_name</emphasis>, like
 * cairo_font_options_set_antialias() and
 * cairo_font_options_get_antialias().
 *
 * New features may be added to a #cairo_font_options_t in the
 * future.  For this reason, cairo_font_options_copy(),
 * cairo_font_options_equal(), cairo_font_options_merge(), and
 * cairo_font_options_hash() should be used to copy, check
 * for equality, merge, or compute a hash value of
 * #cairo_font_options_t objects.
 **/
typedef struct _cairo_font_options cairo_font_options_t;

cairo_public cairo_font_options_t *
cairo_font_options_create (void);

cairo_public cairo_font_options_t *
cairo_font_options_copy (const cairo_font_options_t *original);

cairo_public void
cairo_font_options_destroy (cairo_font_options_t *options);

cairo_public cairo_status_t
cairo_font_options_status (cairo_font_options_t *options);

cairo_public void
cairo_font_options_merge (cairo_font_options_t       *options,
			  const cairo_font_options_t *other);
cairo_public cairo_bool_t
cairo_font_options_equal (const cairo_font_options_t *options,
			  const cairo_font_options_t *other);

cairo_public unsigned long
cairo_font_options_hash (const cairo_font_options_t *options);

cairo_public void
cairo_font_options_set_antialias (cairo_font_options_t *options,
				  cairo_antialias_t     antialias);
cairo_public cairo_antialias_t
cairo_font_options_get_antialias (const cairo_font_options_t *options);

cairo_public void
cairo_font_options_set_subpixel_order (cairo_font_options_t   *options,
				       cairo_subpixel_order_t  subpixel_order);
cairo_public cairo_subpixel_order_t
cairo_font_options_get_subpixel_order (const cairo_font_options_t *options);

cairo_public void
cairo_font_options_set_hint_style (cairo_font_options_t *options,
				   cairo_hint_style_t     hint_style);
cairo_public cairo_hint_style_t
cairo_font_options_get_hint_style (const cairo_font_options_t *options);

cairo_public void
cairo_font_options_set_hint_metrics (cairo_font_options_t *options,
				     cairo_hint_metrics_t  hint_metrics);
cairo_public cairo_hint_metrics_t
cairo_font_options_get_hint_metrics (const cairo_font_options_t *options);

/* This interface is for dealing with text as text, not caring about the
   font object inside the the cairo_t. */

cairo_public void
cairo_select_font_face (cairo_t              *cr,
			const char           *family,
			cairo_font_slant_t   slant,
			cairo_font_weight_t  weight);

cairo_public void
cairo_set_font_size (cairo_t *cr, double size);

cairo_public void
cairo_set_font_matrix (cairo_t		    *cr,
		       const cairo_matrix_t *matrix);

cairo_public void
cairo_get_font_matrix (cairo_t *cr,
		       cairo_matrix_t *matrix);

cairo_public void
cairo_set_font_options (cairo_t                    *cr,
			const cairo_font_options_t *options);

cairo_public void
cairo_get_font_options (cairo_t              *cr,
			cairo_font_options_t *options);

cairo_public void
cairo_set_font_face (cairo_t *cr, cairo_font_face_t *font_face);

cairo_public cairo_font_face_t *
cairo_get_font_face (cairo_t *cr);

cairo_public void
cairo_set_scaled_font (cairo_t                   *cr,
		       const cairo_scaled_font_t *scaled_font);

cairo_public cairo_scaled_font_t *
cairo_get_scaled_font (cairo_t *cr);

cairo_public void
cairo_show_text (cairo_t *cr, const char *utf8);

cairo_public void
cairo_show_glyphs (cairo_t *cr, const cairo_glyph_t *glyphs, int num_glyphs);

cairo_public void
cairo_show_text_glyphs (cairo_t			   *cr,
			const char		   *utf8,
			int			    utf8_len,
			const cairo_glyph_t	   *glyphs,
			int			    num_glyphs,
			const cairo_text_cluster_t *clusters,
			int			    num_clusters,
			cairo_text_cluster_flags_t  cluster_flags);

cairo_public void
cairo_text_path  (cairo_t *cr, const char *utf8);

cairo_public void
cairo_glyph_path (cairo_t *cr, const cairo_glyph_t *glyphs, int num_glyphs);

cairo_public void
cairo_text_extents (cairo_t              *cr,
		    const char    	 *utf8,
		    cairo_text_extents_t *extents);

cairo_public void
cairo_glyph_extents (cairo_t               *cr,
		     const cairo_glyph_t   *glyphs,
		     int                   num_glyphs,
		     cairo_text_extents_t  *extents);

cairo_public void
cairo_font_extents (cairo_t              *cr,
		    cairo_font_extents_t *extents);

/* Generic identifier for a font style */

cairo_public cairo_font_face_t *
cairo_font_face_reference (cairo_font_face_t *font_face);

cairo_public void
cairo_font_face_destroy (cairo_font_face_t *font_face);

cairo_public unsigned int
cairo_font_face_get_reference_count (cairo_font_face_t *font_face);

cairo_public cairo_status_t
cairo_font_face_status (cairo_font_face_t *font_face);


/**
 * cairo_font_type_t:
 * @CAIRO_FONT_TYPE_TOY: The font was created using cairo's toy font api
 * @CAIRO_FONT_TYPE_FT: The font is of type FreeType
 * @CAIRO_FONT_TYPE_WIN32: The font is of type Win32
 * @CAIRO_FONT_TYPE_QUARTZ: The font is of type Quartz (Since: 1.6)
 * @CAIRO_FONT_TYPE_USER: The font was create using cairo's user font api (Since: 1.8)
 *
 * #cairo_font_type_t is used to describe the type of a given font
 * face or scaled font. The font types are also known as "font
 * backends" within cairo.
 *
 * The type of a font face is determined by the function used to
 * create it, which will generally be of the form
 * cairo_<emphasis>type</emphasis>_font_face_create(). The font face type can be queried
 * with cairo_font_face_get_type()
 *
 * The various #cairo_font_face_t functions can be used with a font face
 * of any type.
 *
 * The type of a scaled font is determined by the type of the font
 * face passed to cairo_scaled_font_create(). The scaled font type can
 * be queried with cairo_scaled_font_get_type()
 *
 * The various #cairo_scaled_font_t functions can be used with scaled
 * fonts of any type, but some font backends also provide
 * type-specific functions that must only be called with a scaled font
 * of the appropriate type. These functions have names that begin with
 * cairo_<emphasis>type</emphasis>_scaled_font() such as cairo_ft_scaled_font_lock_face().
 *
 * The behavior of calling a type-specific function with a scaled font
 * of the wrong type is undefined.
 *
 * New entries may be added in future versions.
 *
 * Since: 1.2
 **/
typedef enum _cairo_font_type {
    CAIRO_FONT_TYPE_TOY,
    CAIRO_FONT_TYPE_FT,
    CAIRO_FONT_TYPE_WIN32,
    CAIRO_FONT_TYPE_QUARTZ,
    CAIRO_FONT_TYPE_USER
} cairo_font_type_t;

cairo_public cairo_font_type_t
cairo_font_face_get_type (cairo_font_face_t *font_face);

cairo_public void *
cairo_font_face_get_user_data (cairo_font_face_t	   *font_face,
			       const cairo_user_data_key_t *key);

cairo_public cairo_status_t
cairo_font_face_set_user_data (cairo_font_face_t	   *font_face,
			       const cairo_user_data_key_t *key,
			       void			   *user_data,
			       cairo_destroy_func_t	    destroy);

/* Portable interface to general font features. */

cairo_public cairo_scaled_font_t *
cairo_scaled_font_create (cairo_font_face_t          *font_face,
			  const cairo_matrix_t       *font_matrix,
			  const cairo_matrix_t       *ctm,
			  const cairo_font_options_t *options);

cairo_public cairo_scaled_font_t *
cairo_scaled_font_reference (cairo_scaled_font_t *scaled_font);

cairo_public void
cairo_scaled_font_destroy (cairo_scaled_font_t *scaled_font);

cairo_public unsigned int
cairo_scaled_font_get_reference_count (cairo_scaled_font_t *scaled_font);

cairo_public cairo_status_t
cairo_scaled_font_status (cairo_scaled_font_t *scaled_font);

cairo_public cairo_font_type_t
cairo_scaled_font_get_type (cairo_scaled_font_t *scaled_font);

cairo_public void *
cairo_scaled_font_get_user_data (cairo_scaled_font_t         *scaled_font,
				 const cairo_user_data_key_t *key);

cairo_public cairo_status_t
cairo_scaled_font_set_user_data (cairo_scaled_font_t         *scaled_font,
				 const cairo_user_data_key_t *key,
				 void                        *user_data,
				 cairo_destroy_func_t	      destroy);

cairo_public void
cairo_scaled_font_extents (cairo_scaled_font_t  *scaled_font,
			   cairo_font_extents_t *extents);

cairo_public void
cairo_scaled_font_text_extents (cairo_scaled_font_t  *scaled_font,
				const char  	     *utf8,
				cairo_text_extents_t *extents);

cairo_public void
cairo_scaled_font_glyph_extents (cairo_scaled_font_t   *scaled_font,
				 const cairo_glyph_t   *glyphs,
				 int                   num_glyphs,
				 cairo_text_extents_t  *extents);

cairo_public cairo_status_t
cairo_scaled_font_text_to_glyphs (cairo_scaled_font_t        *scaled_font,
				  double		      x,
				  double		      y,
				  const char	             *utf8,
				  int		              utf8_len,
				  cairo_glyph_t	            **glyphs,
				  int		             *num_glyphs,
				  cairo_text_cluster_t      **clusters,
				  int		             *num_clusters,
				  cairo_text_cluster_flags_t *cluster_flags);

cairo_public cairo_font_face_t *
cairo_scaled_font_get_font_face (cairo_scaled_font_t *scaled_font);

cairo_public void
cairo_scaled_font_get_font_matrix (cairo_scaled_font_t	*scaled_font,
				   cairo_matrix_t	*font_matrix);

cairo_public void
cairo_scaled_font_get_ctm (cairo_scaled_font_t	*scaled_font,
			   cairo_matrix_t	*ctm);

cairo_public void
cairo_scaled_font_get_scale_matrix (cairo_scaled_font_t	*scaled_font,
				    cairo_matrix_t	*scale_matrix);

cairo_public void
cairo_scaled_font_get_font_options (cairo_scaled_font_t		*scaled_font,
				    cairo_font_options_t	*options);


/* Toy fonts */

cairo_public cairo_font_face_t *
cairo_toy_font_face_create (const char           *family,
			    cairo_font_slant_t    slant,
			    cairo_font_weight_t   weight);

cairo_public const char *
cairo_toy_font_face_get_family (cairo_font_face_t *font_face);

cairo_public cairo_font_slant_t
cairo_toy_font_face_get_slant (cairo_font_face_t *font_face);

cairo_public cairo_font_weight_t
cairo_toy_font_face_get_weight (cairo_font_face_t *font_face);


/* User fonts */

cairo_public cairo_font_face_t *
cairo_user_font_face_create (void);

/* User-font method signatures */

/**
 * cairo_user_scaled_font_init_func_t:
 * @scaled_font: the scaled-font being created
 * @cr: a cairo context, in font space
 * @extents: font extents to fill in, in font space
 *
 * #cairo_user_scaled_font_init_func_t is the type of function which is
 * called when a scaled-font needs to be created for a user font-face.
 *
 * The cairo context @cr is not used by the caller, but is prepared in font
 * space, similar to what the cairo contexts passed to the render_glyph
 * method will look like.  The callback can use this context for extents
 * computation for example.  After the callback is called, @cr is checked
 * for any error status.
 *
 * The @extents argument is where the user font sets the font extents for
 * @scaled_font.  It is in font space, which means that for most cases its
 * ascent and descent members should add to 1.0.  @extents is preset to
 * hold a value of 1.0 for ascent, height, and max_x_advance, and 0.0 for
 * descent and max_y_advance members.
 *
 * The callback is optional.  If not set, default font extents as described
 * in the previous paragraph will be used.
 *
 * Note that @scaled_font is not fully initialized at this
 * point and trying to use it for text operations in the callback will result
 * in deadlock.
 *
 * Returns: %CAIRO_STATUS_SUCCESS upon success, or
 * %CAIRO_STATUS_USER_FONT_ERROR or any other error status on error.
 *
 * Since: 1.8
 **/
typedef cairo_status_t (*cairo_user_scaled_font_init_func_t) (cairo_scaled_font_t  *scaled_font,
							      cairo_t              *cr,
							      cairo_font_extents_t *extents);

/**
 * cairo_user_scaled_font_render_glyph_func_t:
 * @scaled_font: user scaled-font
 * @glyph: glyph code to render
 * @cr: cairo context to draw to, in font space
 * @extents: glyph extents to fill in, in font space
 *
 * #cairo_user_scaled_font_render_glyph_func_t is the type of function which
 * is called when a user scaled-font needs to render a glyph.
 *
 * The callback is mandatory, and expected to draw the glyph with code @glyph to
 * the cairo context @cr.  @cr is prepared such that the glyph drawing is done in
 * font space.  That is, the matrix set on @cr is the scale matrix of @scaled_font,
 * The @extents argument is where the user font sets the font extents for
 * @scaled_font.  However, if user prefers to draw in user space, they can
 * achieve that by changing the matrix on @cr.  All cairo rendering operations
 * to @cr are permitted, however, the result is undefined if any source other
 * than the default source on @cr is used.  That means, glyph bitmaps should
 * be rendered using cairo_mask() instead of cairo_paint().
 *
 * Other non-default settings on @cr include a font size of 1.0 (given that
 * it is set up to be in font space), and font options corresponding to
 * @scaled_font.
 *
 * The @extents argument is preset to have <literal>x_bearing</literal>,
 * <literal>width</literal>, and <literal>y_advance</literal> of zero,
 * <literal>y_bearing</literal> set to <literal>-font_extents.ascent</literal>,
 * <literal>height</literal> to <literal>font_extents.ascent+font_extents.descent</literal>,
 * and <literal>x_advance</literal> to <literal>font_extents.max_x_advance</literal>.
 * The only field user needs to set in majority of cases is
 * <literal>x_advance</literal>.
 * If the <literal>width</literal> field is zero upon the callback returning
 * (which is its preset value), the glyph extents are automatically computed
 * based on the drawings done to @cr.  This is in most cases exactly what the
 * desired behavior is.  However, if for any reason the callback sets the
 * extents, it must be ink extents, and include the extents of all drawing
 * done to @cr in the callback.
 *
 * Returns: %CAIRO_STATUS_SUCCESS upon success, or
 * %CAIRO_STATUS_USER_FONT_ERROR or any other error status on error.
 *
 * Since: 1.8
 **/
typedef cairo_status_t (*cairo_user_scaled_font_render_glyph_func_t) (cairo_scaled_font_t  *scaled_font,
								      unsigned long         glyph,
								      cairo_t              *cr,
								      cairo_text_extents_t *extents);

/**
 * cairo_user_scaled_font_text_to_glyphs_func_t:
 * @scaled_font: the scaled-font being created
 * @utf8: a string of text encoded in UTF-8
 * @utf8_len: length of @utf8 in bytes
 * @glyphs: pointer to array of glyphs to fill, in font space
 * @num_glyphs: pointer to number of glyphs
 * @clusters: pointer to array of cluster mapping information to fill, or %NULL
 * @num_clusters: pointer to number of clusters
 * @cluster_flags: pointer to location to store cluster flags corresponding to the
 *                 output @clusters
 *
 * #cairo_user_scaled_font_text_to_glyphs_func_t is the type of function which
 * is called to convert input text to an array of glyphs.  This is used by the
 * cairo_show_text() operation.
 *
 * Using this callback the user-font has full control on glyphs and their
 * positions.  That means, it allows for features like ligatures and kerning,
 * as well as complex <firstterm>shaping</firstterm> required for scripts like
 * Arabic and Indic.
 *
 * The @num_glyphs argument is preset to the number of glyph entries available
 * in the @glyphs buffer. If the @glyphs buffer is %NULL, the value of
 * @num_glyphs will be zero.  If the provided glyph array is too short for
 * the conversion (or for convenience), a new glyph array may be allocated
 * using cairo_glyph_allocate() and placed in @glyphs.  Upon return,
 * @num_glyphs should contain the number of generated glyphs.  If the value
 * @glyphs points at has changed after the call, the caller will free the
 * allocated glyph array using cairo_glyph_free().
 * The callback should populate the glyph indices and positions (in font space)
 * assuming that the text is to be shown at the origin.
 *
 * If @clusters is not %NULL, @num_clusters and @cluster_flags are also
 * non-%NULL, and cluster mapping should be computed. The semantics of how
 * cluster array allocation works is similar to the glyph array.  That is,
 * if @clusters initially points to a non-%NULL value, that array may be used
 * as a cluster buffer, and @num_clusters points to the number of cluster
 * entries available there.  If the provided cluster array is too short for
 * the conversion (or for convenience), a new cluster array may be allocated
 * using cairo_text_cluster_allocate() and placed in @clusters.  Upon return,
 * @num_clusters should contain the number of generated clusters.
 * If the value @clusters points at has changed after the call, the caller
 * will free the allocated cluster array using cairo_text_cluster_free().
 *
 * The callback is optional.  If @num_glyphs is negative upon
 * the callback returning, the unicode_to_glyph callback
 * is tried.  See #cairo_user_scaled_font_unicode_to_glyph_func_t.
 *
 * Note: While cairo does not impose any limitation on glyph indices,
 * some applications may assume that a glyph index fits in a 16-bit
 * unsigned integer.  As such, it is advised that user-fonts keep their
 * glyphs in the 0 to 65535 range.  Furthermore, some applications may
 * assume that glyph 0 is a special glyph-not-found glyph.  User-fonts
 * are advised to use glyph 0 for such purposes and do not use that
 * glyph value for other purposes.
 *
 * Returns: %CAIRO_STATUS_SUCCESS upon success, or
 * %CAIRO_STATUS_USER_FONT_ERROR or any other error status on error.
 *
 * Since: 1.8
 **/
typedef cairo_status_t (*cairo_user_scaled_font_text_to_glyphs_func_t) (cairo_scaled_font_t        *scaled_font,
									const char	           *utf8,
									int		            utf8_len,
									cairo_glyph_t	          **glyphs,
									int		           *num_glyphs,
									cairo_text_cluster_t      **clusters,
									int		           *num_clusters,
									cairo_text_cluster_flags_t *cluster_flags);

/**
 * cairo_user_scaled_font_unicode_to_glyph_func_t:
 * @scaled_font: the scaled-font being created
 * @unicode: input unicode character code-point
 * @glyph_index: output glyph index
 *
 * #cairo_user_scaled_font_unicode_to_glyph_func_t is the type of function which
 * is called to convert an input Unicode character to a single glyph.
 * This is used by the cairo_show_text() operation.
 *
 * This callback is used to provide the same functionality as the
 * text_to_glyphs callback does (see #cairo_user_scaled_font_text_to_glyphs_func_t)
 * but has much less control on the output,
 * in exchange for increased ease of use.  The inherent assumption to using
 * this callback is that each character maps to one glyph, and that the
 * mapping is context independent.  It also assumes that glyphs are positioned
 * according to their advance width.  These mean no ligatures, kerning, or
 * complex scripts can be implemented using this callback.
 *
 * The callback is optional, and only used if text_to_glyphs callback is not
 * set or fails to return glyphs.  If this callback is not set, an identity
 * mapping from Unicode code-points to glyph indices is assumed.
 *
 * Note: While cairo does not impose any limitation on glyph indices,
 * some applications may assume that a glyph index fits in a 16-bit
 * unsigned integer.  As such, it is advised that user-fonts keep their
 * glyphs in the 0 to 65535 range.  Furthermore, some applications may
 * assume that glyph 0 is a special glyph-not-found glyph.  User-fonts
 * are advised to use glyph 0 for such purposes and do not use that
 * glyph value for other purposes.
 *
 * Returns: %CAIRO_STATUS_SUCCESS upon success, or
 * %CAIRO_STATUS_USER_FONT_ERROR or any other error status on error.
 *
 * Since: 1.8
 **/
typedef cairo_status_t (*cairo_user_scaled_font_unicode_to_glyph_func_t) (cairo_scaled_font_t *scaled_font,
									  unsigned long        unicode,
									  unsigned long       *glyph_index);

/* User-font method setters */

cairo_public void
cairo_user_font_face_set_init_func (cairo_font_face_t                  *font_face,
				    cairo_user_scaled_font_init_func_t  init_func);

cairo_public void
cairo_user_font_face_set_render_glyph_func (cairo_font_face_t                          *font_face,
					    cairo_user_scaled_font_render_glyph_func_t  render_glyph_func);

cairo_public void
cairo_user_font_face_set_text_to_glyphs_func (cairo_font_face_t                            *font_face,
					      cairo_user_scaled_font_text_to_glyphs_func_t  text_to_glyphs_func);

cairo_public void
cairo_user_font_face_set_unicode_to_glyph_func (cairo_font_face_t                              *font_face,
					        cairo_user_scaled_font_unicode_to_glyph_func_t  unicode_to_glyph_func);

/* User-font method getters */

cairo_public cairo_user_scaled_font_init_func_t
cairo_user_font_face_get_init_func (cairo_font_face_t *font_face);

cairo_public cairo_user_scaled_font_render_glyph_func_t
cairo_user_font_face_get_render_glyph_func (cairo_font_face_t *font_face);

cairo_public cairo_user_scaled_font_text_to_glyphs_func_t
cairo_user_font_face_get_text_to_glyphs_func (cairo_font_face_t *font_face);

cairo_public cairo_user_scaled_font_unicode_to_glyph_func_t
cairo_user_font_face_get_unicode_to_glyph_func (cairo_font_face_t *font_face);


/* Query functions */

cairo_public cairo_operator_t
cairo_get_operator (cairo_t *cr);

cairo_public cairo_pattern_t *
cairo_get_source (cairo_t *cr);

cairo_public double
cairo_get_tolerance (cairo_t *cr);

cairo_public cairo_antialias_t
cairo_get_antialias (cairo_t *cr);

cairo_public cairo_bool_t
cairo_has_current_point (cairo_t *cr);

cairo_public void
cairo_get_current_point (cairo_t *cr, double *x, double *y);

cairo_public cairo_fill_rule_t
cairo_get_fill_rule (cairo_t *cr);

cairo_public double
cairo_get_line_width (cairo_t *cr);

cairo_public cairo_line_cap_t
cairo_get_line_cap (cairo_t *cr);

cairo_public cairo_line_join_t
cairo_get_line_join (cairo_t *cr);

cairo_public double
cairo_get_miter_limit (cairo_t *cr);

cairo_public int
cairo_get_dash_count (cairo_t *cr);

cairo_public void
cairo_get_dash (cairo_t *cr, double *dashes, double *offset);

cairo_public void
cairo_get_matrix (cairo_t *cr, cairo_matrix_t *matrix);

cairo_public cairo_surface_t *
cairo_get_target (cairo_t *cr);

cairo_public cairo_surface_t *
cairo_get_group_target (cairo_t *cr);

/**
 * cairo_path_data_type_t:
 * @CAIRO_PATH_MOVE_TO: A move-to operation
 * @CAIRO_PATH_LINE_TO: A line-to operation
 * @CAIRO_PATH_CURVE_TO: A curve-to operation
 * @CAIRO_PATH_CLOSE_PATH: A close-path operation
 *
 * #cairo_path_data_t is used to describe the type of one portion
 * of a path when represented as a #cairo_path_t.
 * See #cairo_path_data_t for details.
 **/
typedef enum _cairo_path_data_type {
    CAIRO_PATH_MOVE_TO,
    CAIRO_PATH_LINE_TO,
    CAIRO_PATH_CURVE_TO,
    CAIRO_PATH_CLOSE_PATH
} cairo_path_data_type_t;

/**
 * cairo_path_data_t:
 *
 * #cairo_path_data_t is used to represent the path data inside a
 * #cairo_path_t.
 *
 * The data structure is designed to try to balance the demands of
 * efficiency and ease-of-use. A path is represented as an array of
 * #cairo_path_data_t, which is a union of headers and points.
 *
 * Each portion of the path is represented by one or more elements in
 * the array, (one header followed by 0 or more points). The length
 * value of the header is the number of array elements for the current
 * portion including the header, (ie. length == 1 + # of points), and
 * where the number of points for each element type is as follows:
 *
 * <programlisting>
 *     %CAIRO_PATH_MOVE_TO:     1 point
 *     %CAIRO_PATH_LINE_TO:     1 point
 *     %CAIRO_PATH_CURVE_TO:    3 points
 *     %CAIRO_PATH_CLOSE_PATH:  0 points
 * </programlisting>
 *
 * The semantics and ordering of the coordinate values are consistent
 * with cairo_move_to(), cairo_line_to(), cairo_curve_to(), and
 * cairo_close_path().
 *
 * Here is sample code for iterating through a #cairo_path_t:
 *
 * <informalexample><programlisting>
 *      int i;
 *      cairo_path_t *path;
 *      cairo_path_data_t *data;
 * &nbsp;
 *      path = cairo_copy_path (cr);
 * &nbsp;
 *      for (i=0; i < path->num_data; i += path->data[i].header.length) {
 *          data = &amp;path->data[i];
 *          switch (data->header.type) {
 *          case CAIRO_PATH_MOVE_TO:
 *              do_move_to_things (data[1].point.x, data[1].point.y);
 *              break;
 *          case CAIRO_PATH_LINE_TO:
 *              do_line_to_things (data[1].point.x, data[1].point.y);
 *              break;
 *          case CAIRO_PATH_CURVE_TO:
 *              do_curve_to_things (data[1].point.x, data[1].point.y,
 *                                  data[2].point.x, data[2].point.y,
 *                                  data[3].point.x, data[3].point.y);
 *              break;
 *          case CAIRO_PATH_CLOSE_PATH:
 *              do_close_path_things ();
 *              break;
 *          }
 *      }
 *      cairo_path_destroy (path);
 * </programlisting></informalexample>
 *
 * As of cairo 1.4, cairo does not mind if there are more elements in
 * a portion of the path than needed.  Such elements can be used by
 * users of the cairo API to hold extra values in the path data
 * structure.  For this reason, it is recommended that applications
 * always use <literal>data->header.length</literal> to
 * iterate over the path data, instead of hardcoding the number of
 * elements for each element type.
 **/
typedef union _cairo_path_data_t cairo_path_data_t;
union _cairo_path_data_t {
    struct {
	cairo_path_data_type_t type;
	int length;
    } header;
    struct {
	double x, y;
    } point;
};

/**
 * cairo_path_t:
 * @status: the current error status
 * @data: the elements in the path
 * @num_data: the number of elements in the data array
 *
 * A data structure for holding a path. This data structure serves as
 * the return value for cairo_copy_path() and
 * cairo_copy_path_flat() as well the input value for
 * cairo_append_path().
 *
 * See #cairo_path_data_t for hints on how to iterate over the
 * actual data within the path.
 *
 * The num_data member gives the number of elements in the data
 * array. This number is larger than the number of independent path
 * portions (defined in #cairo_path_data_type_t), since the data
 * includes both headers and coordinates for each portion.
 **/
typedef struct cairo_path {
    cairo_status_t status;
    cairo_path_data_t *data;
    int num_data;
} cairo_path_t;

cairo_public cairo_path_t *
cairo_copy_path (cairo_t *cr);

cairo_public cairo_path_t *
cairo_copy_path_flat (cairo_t *cr);

cairo_public void
cairo_append_path (cairo_t		*cr,
		   const cairo_path_t	*path);

cairo_public void
cairo_path_destroy (cairo_path_t *path);

/* Error status queries */

cairo_public cairo_status_t
cairo_status (cairo_t *cr);

cairo_public const char *
cairo_status_to_string (cairo_status_t status);

/* Surface manipulation */

cairo_public cairo_surface_t *
cairo_surface_create_similar (cairo_surface_t  *other,
			      cairo_content_t	content,
			      int		width,
			      int		height);

cairo_public cairo_surface_t *
cairo_surface_reference (cairo_surface_t *surface);

cairo_public void
cairo_surface_finish (cairo_surface_t *surface);

cairo_public void
cairo_surface_destroy (cairo_surface_t *surface);

cairo_public unsigned int
cairo_surface_get_reference_count (cairo_surface_t *surface);

cairo_public cairo_status_t
cairo_surface_status (cairo_surface_t *surface);

/**
 * cairo_surface_type_t:
 * @CAIRO_SURFACE_TYPE_IMAGE: The surface is of type image
 * @CAIRO_SURFACE_TYPE_PDF: The surface is of type pdf
 * @CAIRO_SURFACE_TYPE_PS: The surface is of type ps
 * @CAIRO_SURFACE_TYPE_XLIB: The surface is of type xlib
 * @CAIRO_SURFACE_TYPE_XCB: The surface is of type xcb
 * @CAIRO_SURFACE_TYPE_GLITZ: The surface is of type glitz
 * @CAIRO_SURFACE_TYPE_QUARTZ: The surface is of type quartz
 * @CAIRO_SURFACE_TYPE_WIN32: The surface is of type win32
 * @CAIRO_SURFACE_TYPE_BEOS: The surface is of type beos
 * @CAIRO_SURFACE_TYPE_DIRECTFB: The surface is of type directfb
 * @CAIRO_SURFACE_TYPE_SVG: The surface is of type svg
 * @CAIRO_SURFACE_TYPE_OS2: The surface is of type os2
 * @CAIRO_SURFACE_TYPE_WIN32_PRINTING: The surface is a win32 printing surface
 * @CAIRO_SURFACE_TYPE_QUARTZ_IMAGE: The surface is of type quartz_image
 * @CAIRO_SURFACE_TYPE_SCRIPT: The surface is of type script, since 1.10
 * @CAIRO_SURFACE_TYPE_QPAINTER: The surface is of type qpainter
 *
 * #cairo_surface_type_t is used to describe the type of a given
 * surface. The surface types are also known as "backends" or "surface
 * backends" within cairo.
 *
 * The type of a surface is determined by the function used to create
 * it, which will generally be of the form cairo_<emphasis>type</emphasis>_surface_create(),
 * (though see cairo_surface_create_similar() as well).
 *
 * The surface type can be queried with cairo_surface_get_type()
 *
 * The various #cairo_surface_t functions can be used with surfaces of
 * any type, but some backends also provide type-specific functions
 * that must only be called with a surface of the appropriate
 * type. These functions have names that begin with
 * cairo_<emphasis>type</emphasis>_surface<!-- --> such as cairo_image_surface_get_width().
 *
 * The behavior of calling a type-specific function with a surface of
 * the wrong type is undefined.
 *
 * New entries may be added in future versions.
 *
 * Since: 1.2
 **/
typedef enum _cairo_surface_type {
    CAIRO_SURFACE_TYPE_IMAGE,
    CAIRO_SURFACE_TYPE_PDF,
    CAIRO_SURFACE_TYPE_PS,
    CAIRO_SURFACE_TYPE_XLIB,
    CAIRO_SURFACE_TYPE_XCB,
    CAIRO_SURFACE_TYPE_GLITZ,
    CAIRO_SURFACE_TYPE_QUARTZ,
    CAIRO_SURFACE_TYPE_WIN32,
    CAIRO_SURFACE_TYPE_BEOS,
    CAIRO_SURFACE_TYPE_DIRECTFB,
    CAIRO_SURFACE_TYPE_SVG,
    CAIRO_SURFACE_TYPE_OS2,
    CAIRO_SURFACE_TYPE_WIN32_PRINTING,
    CAIRO_SURFACE_TYPE_QUARTZ_IMAGE,
    CAIRO_SURFACE_TYPE_SCRIPT,
    CAIRO_SURFACE_TYPE_QPAINTER
} cairo_surface_type_t;

cairo_public cairo_surface_type_t
cairo_surface_get_type (cairo_surface_t *surface);

cairo_public cairo_content_t
cairo_surface_get_content (cairo_surface_t *surface);

#if CAIRO_HAS_PNG_FUNCTIONS

cairo_public cairo_status_t
cairo_surface_write_to_png (cairo_surface_t	*surface,
			    const char		*filename);

cairo_public cairo_status_t
cairo_surface_write_to_png_stream (cairo_surface_t	*surface,
				   cairo_write_func_t	write_func,
				   void			*closure);

#endif

cairo_public void *
cairo_surface_get_user_data (cairo_surface_t		 *surface,
			     const cairo_user_data_key_t *key);

cairo_public cairo_status_t
cairo_surface_set_user_data (cairo_surface_t		 *surface,
			     const cairo_user_data_key_t *key,
			     void			 *user_data,
			     cairo_destroy_func_t	 destroy);

#define CAIRO_MIME_TYPE_JPEG "image/jpeg"
#define CAIRO_MIME_TYPE_PNG "image/png"
#define CAIRO_MIME_TYPE_JP2 "image/jp2"

cairo_public void
cairo_surface_get_mime_data (cairo_surface_t		*surface,
                             const char			*mime_type,
                             const unsigned char       **data,
                             unsigned int		*length);

cairo_public cairo_status_t
cairo_surface_set_mime_data (cairo_surface_t		*surface,
                             const char			*mime_type,
                             const unsigned char	*data,
                             unsigned int		 length,
			     cairo_destroy_func_t	 destroy,
			     void			*closure);

cairo_public void
cairo_surface_get_font_options (cairo_surface_t      *surface,
				cairo_font_options_t *options);

cairo_public void
cairo_surface_flush (cairo_surface_t *surface);

cairo_public void
cairo_surface_mark_dirty (cairo_surface_t *surface);

cairo_public void
cairo_surface_mark_dirty_rectangle (cairo_surface_t *surface,
				    int              x,
				    int              y,
				    int              width,
				    int              height);

cairo_public void
cairo_surface_set_device_offset (cairo_surface_t *surface,
				 double           x_offset,
				 double           y_offset);

cairo_public void
cairo_surface_get_device_offset (cairo_surface_t *surface,
				 double          *x_offset,
				 double          *y_offset);

cairo_public void
cairo_surface_set_fallback_resolution (cairo_surface_t	*surface,
				       double		 x_pixels_per_inch,
				       double		 y_pixels_per_inch);

cairo_public void
cairo_surface_get_fallback_resolution (cairo_surface_t	*surface,
				       double		*x_pixels_per_inch,
				       double		*y_pixels_per_inch);

cairo_public void
cairo_surface_copy_page (cairo_surface_t *surface);

cairo_public void
cairo_surface_show_page (cairo_surface_t *surface);

cairo_public cairo_bool_t
cairo_surface_has_show_text_glyphs (cairo_surface_t *surface);

/* Image-surface functions */

/**
 * cairo_format_t:
 * @CAIRO_FORMAT_ARGB32: each pixel is a 32-bit quantity, with
 *   alpha in the upper 8 bits, then red, then green, then blue.
 *   The 32-bit quantities are stored native-endian. Pre-multiplied
 *   alpha is used. (That is, 50% transparent red is 0x80800000,
 *   not 0x80ff0000.)
 * @CAIRO_FORMAT_RGB24: each pixel is a 32-bit quantity, with
 *   the upper 8 bits unused. Red, Green, and Blue are stored
 *   in the remaining 24 bits in that order.
 * @CAIRO_FORMAT_A8: each pixel is a 8-bit quantity holding
 *   an alpha value.
 * @CAIRO_FORMAT_A1: each pixel is a 1-bit quantity holding
 *   an alpha value. Pixels are packed together into 32-bit
 *   quantities. The ordering of the bits matches the
 *   endianess of the platform. On a big-endian machine, the
 *   first pixel is in the uppermost bit, on a little-endian
 *   machine the first pixel is in the least-significant bit.
 * @CAIRO_FORMAT_RGB16_565: This format value is deprecated. It has
 *   never been properly implemented in cairo and should not be used
 *   by applications. (since 1.2)
 *
 * #cairo_format_t is used to identify the memory format of
 * image data.
 *
 * New entries may be added in future versions.
 **/
typedef enum _cairo_format {
    CAIRO_FORMAT_ARGB32,
    CAIRO_FORMAT_RGB24,
    CAIRO_FORMAT_A8,
    CAIRO_FORMAT_A1
    /* The value of 4 is reserved by a deprecated enum value.
     * The next format added must have an explicit value of 5.
    CAIRO_FORMAT_RGB16_565 = 4,
    */
} cairo_format_t;

cairo_public cairo_surface_t *
cairo_image_surface_create (cairo_format_t	format,
			    int			width,
			    int			height);

cairo_public int
cairo_format_stride_for_width (cairo_format_t	format,
			       int		width);

cairo_public cairo_surface_t *
cairo_image_surface_create_for_data (unsigned char	       *data,
				     cairo_format_t		format,
				     int			width,
				     int			height,
				     int			stride);

cairo_public unsigned char *
cairo_image_surface_get_data (cairo_surface_t *surface);

cairo_public cairo_format_t
cairo_image_surface_get_format (cairo_surface_t *surface);

cairo_public int
cairo_image_surface_get_width (cairo_surface_t *surface);

cairo_public int
cairo_image_surface_get_height (cairo_surface_t *surface);

cairo_public int
cairo_image_surface_get_stride (cairo_surface_t *surface);

#if CAIRO_HAS_PNG_FUNCTIONS

cairo_public cairo_surface_t *
cairo_image_surface_create_from_png (const char	*filename);

cairo_public cairo_surface_t *
cairo_image_surface_create_from_png_stream (cairo_read_func_t	read_func,
					    void		*closure);

#endif

/* Pattern creation functions */

cairo_public cairo_pattern_t *
cairo_pattern_create_rgb (double red, double green, double blue);

cairo_public cairo_pattern_t *
cairo_pattern_create_rgba (double red, double green, double blue,
			   double alpha);

cairo_public cairo_pattern_t *
cairo_pattern_create_for_surface (cairo_surface_t *surface);

cairo_public cairo_pattern_t *
cairo_pattern_create_linear (double x0, double y0,
			     double x1, double y1);

cairo_public cairo_pattern_t *
cairo_pattern_create_radial (double cx0, double cy0, double radius0,
			     double cx1, double cy1, double radius1);

cairo_public cairo_pattern_t *
cairo_pattern_reference (cairo_pattern_t *pattern);

cairo_public void
cairo_pattern_destroy (cairo_pattern_t *pattern);

cairo_public unsigned int
cairo_pattern_get_reference_count (cairo_pattern_t *pattern);

cairo_public cairo_status_t
cairo_pattern_status (cairo_pattern_t *pattern);

cairo_public void *
cairo_pattern_get_user_data (cairo_pattern_t		 *pattern,
			     const cairo_user_data_key_t *key);

cairo_public cairo_status_t
cairo_pattern_set_user_data (cairo_pattern_t		 *pattern,
			     const cairo_user_data_key_t *key,
			     void			 *user_data,
			     cairo_destroy_func_t	  destroy);

/**
 * cairo_pattern_type_t:
 * @CAIRO_PATTERN_TYPE_SOLID: The pattern is a solid (uniform)
 * color. It may be opaque or translucent.
 * @CAIRO_PATTERN_TYPE_SURFACE: The pattern is a based on a surface (an image).
 * @CAIRO_PATTERN_TYPE_LINEAR: The pattern is a linear gradient.
 * @CAIRO_PATTERN_TYPE_RADIAL: The pattern is a radial gradient.
 *
 * #cairo_pattern_type_t is used to describe the type of a given pattern.
 *
 * The type of a pattern is determined by the function used to create
 * it. The cairo_pattern_create_rgb() and cairo_pattern_create_rgba()
 * functions create SOLID patterns. The remaining
 * cairo_pattern_create<!-- --> functions map to pattern types in obvious
 * ways.
 *
 * The pattern type can be queried with cairo_pattern_get_type()
 *
 * Most #cairo_pattern_t functions can be called with a pattern of any
 * type, (though trying to change the extend or filter for a solid
 * pattern will have no effect). A notable exception is
 * cairo_pattern_add_color_stop_rgb() and
 * cairo_pattern_add_color_stop_rgba() which must only be called with
 * gradient patterns (either LINEAR or RADIAL). Otherwise the pattern
 * will be shutdown and put into an error state.
 *
 * New entries may be added in future versions.
 *
 * Since: 1.2
 **/
typedef enum _cairo_pattern_type {
    CAIRO_PATTERN_TYPE_SOLID,
    CAIRO_PATTERN_TYPE_SURFACE,
    CAIRO_PATTERN_TYPE_LINEAR,
    CAIRO_PATTERN_TYPE_RADIAL
} cairo_pattern_type_t;

cairo_public cairo_pattern_type_t
cairo_pattern_get_type (cairo_pattern_t *pattern);

cairo_public void
cairo_pattern_add_color_stop_rgb (cairo_pattern_t *pattern,
				  double offset,
				  double red, double green, double blue);

cairo_public void
cairo_pattern_add_color_stop_rgba (cairo_pattern_t *pattern,
				   double offset,
				   double red, double green, double blue,
				   double alpha);

cairo_public void
cairo_pattern_set_matrix (cairo_pattern_t      *pattern,
			  const cairo_matrix_t *matrix);

cairo_public void
cairo_pattern_get_matrix (cairo_pattern_t *pattern,
			  cairo_matrix_t  *matrix);

/**
 * cairo_extend_t:
 * @CAIRO_EXTEND_NONE: pixels outside of the source pattern
 *   are fully transparent
 * @CAIRO_EXTEND_REPEAT: the pattern is tiled by repeating
 * @CAIRO_EXTEND_REFLECT: the pattern is tiled by reflecting
 *   at the edges (Implemented for surface patterns since 1.6)
 * @CAIRO_EXTEND_PAD: pixels outside of the pattern copy
 *   the closest pixel from the source (Since 1.2; but only
 *   implemented for surface patterns since 1.6)
 *
 * #cairo_extend_t is used to describe how pattern color/alpha will be
 * determined for areas "outside" the pattern's natural area, (for
 * example, outside the surface bounds or outside the gradient
 * geometry).
 *
 * The default extend mode is %CAIRO_EXTEND_NONE for surface patterns
 * and %CAIRO_EXTEND_PAD for gradient patterns.
 *
 * New entries may be added in future versions.
 **/
typedef enum _cairo_extend {
    CAIRO_EXTEND_NONE,
    CAIRO_EXTEND_REPEAT,
    CAIRO_EXTEND_REFLECT,
    CAIRO_EXTEND_PAD
} cairo_extend_t;

cairo_public void
cairo_pattern_set_extend (cairo_pattern_t *pattern, cairo_extend_t extend);

cairo_public cairo_extend_t
cairo_pattern_get_extend (cairo_pattern_t *pattern);

/**
 * cairo_filter_t:
 * @CAIRO_FILTER_FAST: A high-performance filter, with quality similar
 *     to %CAIRO_FILTER_NEAREST
 * @CAIRO_FILTER_GOOD: A reasonable-performance filter, with quality
 *     similar to %CAIRO_FILTER_BILINEAR
 * @CAIRO_FILTER_BEST: The highest-quality available, performance may
 *     not be suitable for interactive use.
 * @CAIRO_FILTER_NEAREST: Nearest-neighbor filtering
 * @CAIRO_FILTER_BILINEAR: Linear interpolation in two dimensions
 * @CAIRO_FILTER_GAUSSIAN: This filter value is currently
 *     unimplemented, and should not be used in current code.
 *
 * #cairo_filter_t is used to indicate what filtering should be
 * applied when reading pixel values from patterns. See
 * cairo_pattern_set_source() for indicating the desired filter to be
 * used with a particular pattern.
 */
typedef enum _cairo_filter {
    CAIRO_FILTER_FAST,
    CAIRO_FILTER_GOOD,
    CAIRO_FILTER_BEST,
    CAIRO_FILTER_NEAREST,
    CAIRO_FILTER_BILINEAR,
    CAIRO_FILTER_GAUSSIAN
} cairo_filter_t;

cairo_public void
cairo_pattern_set_filter (cairo_pattern_t *pattern, cairo_filter_t filter);

cairo_public cairo_filter_t
cairo_pattern_get_filter (cairo_pattern_t *pattern);

cairo_public cairo_status_t
cairo_pattern_get_rgba (cairo_pattern_t *pattern,
			double *red, double *green,
			double *blue, double *alpha);

cairo_public cairo_status_t
cairo_pattern_get_surface (cairo_pattern_t *pattern,
			   cairo_surface_t **surface);


cairo_public cairo_status_t
cairo_pattern_get_color_stop_rgba (cairo_pattern_t *pattern,
				   int index, double *offset,
				   double *red, double *green,
				   double *blue, double *alpha);

cairo_public cairo_status_t
cairo_pattern_get_color_stop_count (cairo_pattern_t *pattern,
				    int *count);

cairo_public cairo_status_t
cairo_pattern_get_linear_points (cairo_pattern_t *pattern,
				 double *x0, double *y0,
				 double *x1, double *y1);

cairo_public cairo_status_t
cairo_pattern_get_radial_circles (cairo_pattern_t *pattern,
				  double *x0, double *y0, double *r0,
				  double *x1, double *y1, double *r1);

/* Matrix functions */

cairo_public void
cairo_matrix_init (cairo_matrix_t *matrix,
		   double  xx, double  yx,
		   double  xy, double  yy,
		   double  x0, double  y0);

cairo_public void
cairo_matrix_init_identity (cairo_matrix_t *matrix);

cairo_public void
cairo_matrix_init_translate (cairo_matrix_t *matrix,
			     double tx, double ty);

cairo_public void
cairo_matrix_init_scale (cairo_matrix_t *matrix,
			 double sx, double sy);

cairo_public void
cairo_matrix_init_rotate (cairo_matrix_t *matrix,
			  double radians);

cairo_public void
cairo_matrix_translate (cairo_matrix_t *matrix, double tx, double ty);

cairo_public void
cairo_matrix_scale (cairo_matrix_t *matrix, double sx, double sy);

cairo_public void
cairo_matrix_rotate (cairo_matrix_t *matrix, double radians);

cairo_public cairo_status_t
cairo_matrix_invert (cairo_matrix_t *matrix);

cairo_public void
cairo_matrix_multiply (cairo_matrix_t	    *result,
		       const cairo_matrix_t *a,
		       const cairo_matrix_t *b);

cairo_public void
cairo_matrix_transform_distance (const cairo_matrix_t *matrix,
				 double *dx, double *dy);

cairo_public void
cairo_matrix_transform_point (const cairo_matrix_t *matrix,
			      double *x, double *y);

/* Functions to be used while debugging (not intended for use in production code) */
cairo_public void
cairo_debug_reset_static_data (void);

CAIRO_END_DECLS

#endif /* CAIRO_H */
