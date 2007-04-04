/* $Id: cairo-analysis-surface-private.h,v 1.5 2007/04/04 01:09:15 vladimir%pobox.com Exp $
 *
 * Copyright © 2005 Keith Packard
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
 * The Initial Developer of the Original Code is Keith Packard
 *
 * Contributor(s):
 *      Keith Packard <keithp@keithp.com>
 */

#ifndef CAIRO_ANALYSIS_SURFACE_H
#define CAIRO_ANALYSIS_SURFACE_H

#include "cairoint.h"

cairo_private cairo_surface_t *
_cairo_analysis_surface_create (cairo_surface_t		*target,
				int			 width,
				int			 height);

cairo_private pixman_region16_t *
_cairo_analysis_surface_get_supported (cairo_surface_t *surface);

cairo_private pixman_region16_t *
_cairo_analysis_surface_get_unsupported (cairo_surface_t *unsupported);

cairo_private cairo_bool_t
_cairo_analysis_surface_has_unsupported (cairo_surface_t *unsupported);

#endif /* CAIRO_ANALYSIS_SURFACE_H */
