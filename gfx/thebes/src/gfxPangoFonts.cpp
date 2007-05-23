/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Foundation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@mozilla.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *
 * based on nsFontMetricsPango.cpp by
 *   Christopher Blizzard <blizzard@mozilla.org>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifdef XP_BEOS
#define THEBES_USE_PANGO_CAIRO
#endif

#define PANGO_ENABLE_ENGINE
#define PANGO_ENABLE_BACKEND

#include "prtypes.h"
#include "prlink.h"
#include "gfxTypes.h"

#include "nsUnicodeRange.h"

#include "nsIPref.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsServiceManagerUtils.h"
#include "nsMathUtils.h"

#include "nsVoidArray.h"
#include "nsPromiseFlatString.h"

#include "gfxContext.h"
#include "gfxPangoFonts.h"

#include "nsCRT.h"

#include "cairo.h"

#ifndef THEBES_USE_PANGO_CAIRO
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkpango.h>


#include <freetype/tttables.h>
#include <fontconfig/fontconfig.h>

#include <pango/pango-font.h>
#include <pango/pangoxft.h>

#include "cairo-ft.h"

#include "gfxPlatformGtk.h"

#else // THEBES_USE_PANGO_CAIRO

#include <pango/pangocairo.h>

#endif // THEBES_USE_PANGO_CAIRO

#include <math.h>

#define FLOAT_PANGO_SCALE ((gfxFloat)PANGO_SCALE)

#define IS_MISSING_GLYPH(g) (((g) & 0x10000000) || (g) == 0x0FFFFFFF)

static PangoLanguage *GetPangoLanguage(const nsACString& aLangGroup);
static void GetMozLanguage(const PangoLanguage *aLang, nsACString &aMozLang);

/* static */ gfxPangoFontCache* gfxPangoFontCache::sPangoFontCache = nsnull;
/* static */ gfxPangoFontNameMap* gfxPangoFontNameMap::sPangoFontNameMap = nsnull;

/**
 ** gfxPangoFontGroup
 **/

static int
FFRECountHyphens (const nsAString &aFFREName)
{
    int h = 0;
    PRInt32 hyphen = 0;
    while ((hyphen = aFFREName.FindChar('-', hyphen)) >= 0) {
        ++h;
        ++hyphen;
    }
    return h;
}

PRBool
gfxPangoFontGroup::FontCallback (const nsAString& fontName,
                                 const nsACString& genericName,
                                 void *closure)
{
    nsStringArray *sa = NS_STATIC_CAST(nsStringArray*, closure);

    if (FFRECountHyphens(fontName) < 3 && sa->IndexOf(fontName) < 0) {
        sa->AppendString(fontName);
    }

    return PR_TRUE;
}

/**
 * Look up the font in the gfxFont cache. If we don't find it, create one.
 * In either case, add a ref, append it to the aFonts array, and return it ---
 * except for OOM in which case we do nothing and return null.
 */
static already_AddRefed<gfxPangoFont>
GetOrMakeFont(const nsAString& aName, const gfxFontStyle *aStyle)
{
    nsRefPtr<gfxFont> font = gfxFontCache::GetCache()->Lookup(aName, aStyle);
    if (!font) {
        font = new gfxPangoFont(aName, aStyle);
        if (!font)
            return nsnull;
        gfxFontCache::GetCache()->AddNew(font);
    }
    gfxFont *f = nsnull;
    font.swap(f);
    return static_cast<gfxPangoFont *>(f);
}

gfxPangoFontGroup::gfxPangoFontGroup (const nsAString& families,
                                      const gfxFontStyle *aStyle)
    : gfxFontGroup(families, aStyle)
{
    g_type_init();

    nsStringArray familyArray;

    ForEachFont (FontCallback, &familyArray);

    FindGenericFontFromStyle (FontCallback, &familyArray);

    // XXX If there are no actual fonts, we should use dummy family.
    // Pango will resolve from this.
    if (familyArray.Count() == 0) {
        // printf("%s(%s)\n", NS_ConvertUTF16toUTF8(families).get(),
        //                    aStyle->langGroup.get());
        familyArray.AppendString(NS_LITERAL_STRING("sans-serif"));
    }

    for (int i = 0; i < familyArray.Count(); i++) {
        nsRefPtr<gfxPangoFont> font = GetOrMakeFont(*familyArray[i], &mStyle);
        if (font) {
            mFonts.AppendElement(font);
        }
    }
}

gfxPangoFontGroup::~gfxPangoFontGroup()
{
}

gfxFontGroup *
gfxPangoFontGroup::Copy(const gfxFontStyle *aStyle)
{
    return new gfxPangoFontGroup(mFamilies, aStyle);
}

/**
 ** gfxPangoFont
 **/

// Glue to avoid build/runtime dependencies on Pango > 1.6,
// because we like living in 1999

#ifndef THEBES_USE_PANGO_CAIRO
static void
(* PTR_pango_font_description_set_absolute_size)(PangoFontDescription*, double)
    = nsnull;

static void InitPangoLib()
{
    static PRBool initialized = PR_FALSE;
    if (initialized)
        return;
    initialized = PR_TRUE;

    g_type_init();

    PRLibrary *pangoLib = nsnull;
    PTR_pango_font_description_set_absolute_size =
        (void (*)(PangoFontDescription*, double))
        PR_FindFunctionSymbolAndLibrary("pango_font_description_set_absolute_size",
                                        &pangoLib);
    if (pangoLib)
        PR_UnloadLibrary(pangoLib);

    PRLibrary *xftLib = nsnull;
    int *xft_max_freetype_files_ptr = nsnull;
    xft_max_freetype_files_ptr = (int*) PR_FindSymbolAndLibrary("XftMaxFreeTypeFiles", &xftLib);
    if (xft_max_freetype_files_ptr && *xft_max_freetype_files_ptr < 50)
        *xft_max_freetype_files_ptr = 50;
    if (xftLib)
        PR_UnloadLibrary(xftLib);
}

static void
ShutdownPangoLib()
{
}

static void
MOZ_pango_font_description_set_absolute_size(PangoFontDescription *desc,
                                             double size)
{
    if (PTR_pango_font_description_set_absolute_size) {
        PTR_pango_font_description_set_absolute_size(desc, size);
    } else {
        pango_font_description_set_size(desc,
                                        (gint)(size * 72.0 /
                                               gfxPlatformGtk::DPI()));
    }
}
#else
static inline void InitPangoLib()
{
}

static inline void ShutdownPangoLib()
{
}

static inline void
MOZ_pango_font_description_set_absolute_size(PangoFontDescription *desc, double size)
{
    pango_font_description_set_absolute_size(desc, size);
}
#endif

gfxPangoFont::gfxPangoFont(const nsAString &aName,
                           const gfxFontStyle *aFontStyle)
    : gfxFont(aName, aFontStyle),
    mPangoFontDesc(nsnull), mPangoCtx(nsnull),
    mXftFont(nsnull), mPangoFont(nsnull), mGlyphTestingFont(nsnull),
    mCairoFont(nsnull), mHasMetrics(PR_FALSE),
    mAdjustedSize(0)
{
    InitPangoLib();
}

gfxPangoFont::~gfxPangoFont()
{
    if (mPangoCtx)
        g_object_unref(mPangoCtx);

    if (mPangoFont)
        g_object_unref(mPangoFont);

    if (mGlyphTestingFont)
        g_object_unref(mGlyphTestingFont);

    if (mPangoFontDesc)
        pango_font_description_free(mPangoFontDesc);

    if (mCairoFont)
        cairo_scaled_font_destroy(mCairoFont);
}

/* static */ void
gfxPangoFont::Shutdown()
{
    ShutdownPangoLib();
    gfxPangoFontCache::Shutdown();
    gfxPangoFontNameMap::Shutdown();
}

static PangoStyle
ThebesStyleToPangoStyle (const gfxFontStyle *fs)
{
    if (fs->style == FONT_STYLE_ITALIC)
        return PANGO_STYLE_ITALIC;
    if (fs->style == FONT_STYLE_OBLIQUE)
        return PANGO_STYLE_OBLIQUE;

    return PANGO_STYLE_NORMAL;
}

static PangoWeight
ThebesStyleToPangoWeight (const gfxFontStyle *fs)
{
    PRInt32 w = fs->weight;

    /*
     * weights come in two parts crammed into one
     * integer -- the "base" weight is weight / 100,
     * the rest of the value is the "offset" from that
     * weight -- the number of steps to move to adjust
     * the weight in the list of supported font weights,
     * this value can be negative or positive.
     */
    PRInt32 baseWeight = (w + 50) / 100;
    PRInt32 offset = w - baseWeight * 100;

    /* clip weights to range 0 to 9 */
    if (baseWeight < 0)
        baseWeight = 0;
    if (baseWeight > 9)
        baseWeight = 9;

    /* Map from weight value to fcWeights index */
    static int fcWeightLookup[10] = {
        0, 0, 0, 0, 1, 1, 2, 3, 3, 4,
    };

    PRInt32 fcWeight = fcWeightLookup[baseWeight];

    /*
     * adjust by the offset value, make sure we stay inside the 
     * fcWeights table
     */
    fcWeight += offset;

    if (fcWeight < 0)
        fcWeight = 0;
    if (fcWeight > 4)
        fcWeight = 4;

    /* Map to final PANGO_WEIGHT value */
    static int fcWeights[5] = {
        349,
        499,
        649,
        749,
        999
    };

    return (PangoWeight)fcWeights[fcWeight];
}

void
gfxPangoFont::RealizeFont(PRBool force)
{
    // already realized?
    if (!force && mPangoFontDesc)
        return;

    if (mPangoCtx)
        g_object_unref(mPangoCtx);
    if (mPangoFontDesc)
        pango_font_description_free(mPangoFontDesc);
    if (mPangoFont) {
        g_object_unref(mPangoFont);
        mPangoFont = nsnull;
        mXftFont = nsnull;
        // XXX we don't need to reset mGlyphTestingFont
    }

    mPangoFontDesc = pango_font_description_new();

    pango_font_description_set_family(mPangoFontDesc, NS_ConvertUTF16toUTF8(mName).get());
    gfxFloat size = mAdjustedSize ? mAdjustedSize : GetStyle()->size;
    MOZ_pango_font_description_set_absolute_size(mPangoFontDesc, size * PANGO_SCALE);
    pango_font_description_set_style(mPangoFontDesc, ThebesStyleToPangoStyle(GetStyle()));
    pango_font_description_set_weight(mPangoFontDesc, ThebesStyleToPangoWeight(GetStyle()));

    //printf ("%s, %f, %d, %d\n", NS_ConvertUTF16toUTF8(mName).get(), GetStyle()->size, ThebesStyleToPangoStyle(GetStyle()), ThebesStyleToPangoWeight(GetStyle()));
#ifndef THEBES_USE_PANGO_CAIRO
    mPangoCtx = pango_xft_get_context(GDK_DISPLAY(), 0);
    gdk_pango_context_set_colormap(mPangoCtx, gdk_rgb_get_cmap());
#else
    mPangoCtx = pango_cairo_font_map_create_context(PANGO_CAIRO_FONT_MAP(pango_cairo_font_map_get_default()));
#endif

    if (!GetStyle()->langGroup.IsEmpty())
        pango_context_set_language(mPangoCtx, GetPangoLanguage(GetStyle()->langGroup));

    pango_context_set_font_description(mPangoCtx, mPangoFontDesc);

    mHasMetrics = PR_FALSE;

    if (mAdjustedSize != 0)
        return;

    mAdjustedSize = GetStyle()->size;
    if (GetStyle()->sizeAdjust == 0)
        return;

    gfxSize isz, lsz;
    GetCharSize('x', isz, lsz);
    gfxFloat aspect = isz.height / GetStyle()->size;
    mAdjustedSize =
        PR_MAX(NS_round(GetStyle()->size*(GetStyle()->sizeAdjust/aspect)), 1.0);
    RealizeFont(PR_TRUE);
}

void
gfxPangoFont::RealizeXftFont(PRBool force)
{
    // already realized?
    if (!force && mXftFont)
        return;
    if (GDK_DISPLAY() == 0) {
        mXftFont = nsnull;
        return;
    }

    mXftFont = pango_xft_font_get_font(GetPangoFont());
}

void
gfxPangoFont::RealizePangoFont(PRBool aForce)
{
    if (!aForce && mPangoFont)
        return;
    if (mPangoFont) {
        g_object_unref(mPangoFont);
        mPangoFont = nsnull;
        mXftFont = nsnull;
    }
    RealizeFont();
    gfxPangoFontCache *cache = gfxPangoFontCache::GetPangoFontCache();
    if (!cache)
        return; // Error
    mPangoFont = cache->Get(mPangoFontDesc);
    if (mPangoFont)
        return;
    mPangoFont = pango_context_load_font(mPangoCtx, mPangoFontDesc);
    if (!mPangoFont)
        return; // Error
    cache->Put(mPangoFontDesc, mPangoFont);

    if (mGlyphTestingFont)
        return;

    // Append this to font name map
    gfxPangoFontNameMap *fontNameMap = gfxPangoFontNameMap::GetPangoFontNameMap();
    if (!fontNameMap)
        return; // Error
    NS_ConvertUTF16toUTF8 name(mName);
    mGlyphTestingFont = fontNameMap->Get(name);
    if (mGlyphTestingFont)
        return;
    fontNameMap->Put(name, mPangoFont);
}

void
gfxPangoFont::GetCharSize(char aChar, gfxSize& aInkSize, gfxSize& aLogSize,
                          PRUint32 *aGlyphID)
{
    PangoAnalysis analysis;
    analysis.font = GetPangoFont();
    analysis.level = 0;
    analysis.lang_engine = nsnull;
    analysis.extra_attrs = nsnull;
    analysis.language = pango_language_from_string("en");
    analysis.shape_engine = pango_font_find_shaper(analysis.font, analysis.language, aChar);

    PangoGlyphString *glstr = pango_glyph_string_new();
    pango_shape (&aChar, 1, &analysis, glstr);

    if (aGlyphID) {
        *aGlyphID = 0;
        if (glstr->num_glyphs == 1) {
            PangoGlyph glyph = glstr->glyphs[0].glyph;
            if (!IS_MISSING_GLYPH(glyph)) {
                *aGlyphID = glyph;
            }
        }
    }

    PangoRectangle ink_rect, log_rect;
    pango_glyph_string_extents(glstr, analysis.font, &ink_rect, &log_rect);

    aInkSize.width = ink_rect.width / FLOAT_PANGO_SCALE;
    aInkSize.height = ink_rect.height / FLOAT_PANGO_SCALE;

    aLogSize.width = log_rect.width / FLOAT_PANGO_SCALE;
    aLogSize.height = log_rect.height / FLOAT_PANGO_SCALE;

    pango_glyph_string_free(glstr);
}

// rounding and truncation functions for a Freetype floating point number 
// (FT26Dot6) stored in a 32bit integer with high 26 bits for the integer
// part and low 6 bits for the fractional part. 
#define MOZ_FT_ROUND(x) (((x) + 32) & ~63) // 63 = 2^6 - 1
#define MOZ_FT_TRUNC(x) ((x) >> 6)
#define CONVERT_DESIGN_UNITS_TO_PIXELS(v, s) \
        MOZ_FT_TRUNC(MOZ_FT_ROUND(FT_MulFix((v) , (s))))

const gfxFont::Metrics&
gfxPangoFont::GetMetrics()
{
    if (mHasMetrics)
        return mMetrics;

#ifndef THEBES_USE_PANGO_CAIRO
    float val;

    XftFont *xftFont = GetXftFont(); // RealizeFont is called here.
    if (!xftFont)
        return mMetrics;        // XXX error

    FT_Face face = XftLockFace(xftFont);
    if (!face)
        return mMetrics;        // XXX error

    int size;
    PangoFcFont *fcfont = PANGO_FC_FONT(mPangoFont);
    if (FcPatternGetInteger(fcfont->font_pattern, FC_PIXEL_SIZE, 0, &size) != FcResultMatch)
        size = 12;
    mMetrics.emHeight = PR_MAX(1, size);

    mMetrics.maxAscent = xftFont->ascent;
    mMetrics.maxDescent = xftFont->descent;

    double lineHeight = mMetrics.maxAscent + mMetrics.maxDescent;

    if (lineHeight > mMetrics.emHeight)
        mMetrics.internalLeading = lineHeight - mMetrics.emHeight;
    else
        mMetrics.internalLeading = 0;
    mMetrics.externalLeading = 0;

    mMetrics.maxHeight = lineHeight;
    mMetrics.emAscent = mMetrics.maxAscent * mMetrics.emHeight / lineHeight;
    mMetrics.emDescent = mMetrics.emHeight - mMetrics.emAscent;
    mMetrics.maxAdvance = xftFont->max_advance_width;

    gfxSize isz, lsz;
    GetCharSize(' ', isz, lsz, &mSpaceGlyph);
    mMetrics.spaceWidth = lsz.width;

    // XXX do some FcCharSetHasChar work here to make sure
    // we have an "x"
    GetCharSize('x', isz, lsz);
    mMetrics.xHeight = isz.height;
    mMetrics.aveCharWidth = isz.width;

    val = CONVERT_DESIGN_UNITS_TO_PIXELS(face->underline_position,
                                         face->size->metrics.y_scale);
    if (!val)
        val = - PR_MAX(1, floor(0.1 * xftFont->height + 0.5));

    mMetrics.underlineOffset = val;

    val = CONVERT_DESIGN_UNITS_TO_PIXELS(face->underline_thickness,
                                         face->size->metrics.y_scale);
    if (!val)
        val = floor(0.05 * xftFont->height + 0.5);

    mMetrics.underlineSize = PR_MAX(1, val);

    TT_OS2 *os2 = (TT_OS2 *) FT_Get_Sfnt_Table(face, ft_sfnt_os2);

    if (os2 && os2->ySuperscriptYOffset) {
        val = CONVERT_DESIGN_UNITS_TO_PIXELS(os2->ySuperscriptYOffset,
                                             face->size->metrics.y_scale);
        mMetrics.superscriptOffset = PR_MAX(1, val);
    } else {
        mMetrics.superscriptOffset = mMetrics.xHeight;
    }

    // mSubscriptOffset
    if (os2 && os2->ySubscriptYOffset) {
        val = CONVERT_DESIGN_UNITS_TO_PIXELS(os2->ySubscriptYOffset,
                                             face->size->metrics.y_scale);
        // some fonts have the incorrect sign. 
        val = (val < 0) ? -val : val;
        mMetrics.subscriptOffset = PR_MAX(1, val);
    } else {
        mMetrics.subscriptOffset = mMetrics.xHeight;
    }

    mMetrics.strikeoutOffset = mMetrics.xHeight / 2.0;
    mMetrics.strikeoutSize = mMetrics.underlineSize;

    XftUnlockFace(xftFont);
#else
    /* pango_cairo case; try to get all the metrics from pango itself */
    PangoFont *font = GetPangoFont(); // RealizeFont is called here.

    PangoFontMetrics *pfm = pango_font_get_metrics (font, NULL);

    // ??
    mMetrics.emHeight = mAdjustedSize ? mAdjustedSize : GetStyle()->size;

    mMetrics.maxAscent = pango_font_metrics_get_ascent(pfm) / FLOAT_PANGO_SCALE;
    mMetrics.maxDescent = pango_font_metrics_get_descent(pfm) / FLOAT_PANGO_SCALE;

    gfxFloat lineHeight = mMetrics.maxAscent + mMetrics.maxDescent;

    if (lineHeight > mMetrics.emHeight)
        mMetrics.externalLeading = lineHeight - mMetrics.emHeight;
    else
        mMetrics.externalLeading = 0;
    mMetrics.internalLeading = 0;

    mMetrics.maxHeight = lineHeight;

    mMetrics.emAscent = mMetrics.maxAscent * mMetrics.emHeight / lineHeight;
    mMetrics.emDescent = mMetrics.emHeight - mMetrics.emAscent;

    mMetrics.maxAdvance = pango_font_metrics_get_approximate_char_width(pfm) / FLOAT_PANGO_SCALE; // XXX

    gfxSize isz, lsz;
    GetCharSize(' ', isz, lsz, &mSpaceGlyph);
    mMetrics.spaceWidth = lsz.width;
    GetCharSize('x', isz, lsz);
    mMetrics.xHeight = isz.height;

    mMetrics.aveCharWidth = pango_font_metrics_get_approximate_char_width(pfm) / FLOAT_PANGO_SCALE;

    mMetrics.underlineOffset = pango_font_metrics_get_underline_position(pfm) / FLOAT_PANGO_SCALE;
    mMetrics.underlineSize = pango_font_metrics_get_underline_thickness(pfm) / FLOAT_PANGO_SCALE;

    mMetrics.strikeoutOffset = pango_font_metrics_get_strikethrough_position(pfm) / FLOAT_PANGO_SCALE;
    mMetrics.strikeoutSize = pango_font_metrics_get_strikethrough_thickness(pfm) / FLOAT_PANGO_SCALE;

    // these are specified by the so-called OS2 SFNT info, but
    // pango doesn't expose this to us.  This really sucks,
    // so we just assume it's the xHeight
    mMetrics.superscriptOffset = mMetrics.xHeight;
    mMetrics.subscriptOffset = mMetrics.xHeight;

    pango_font_metrics_unref (pfm);
#endif

#if 0
    fprintf (stderr, "Font: %s\n", NS_ConvertUTF16toUTF8(mName).get());
    fprintf (stderr, "    emHeight: %f emAscent: %f emDescent: %f\n", mMetrics.emHeight, mMetrics.emAscent, mMetrics.emDescent);
    fprintf (stderr, "    maxAscent: %f maxDescent: %f\n", mMetrics.maxAscent, mMetrics.maxDescent);
    fprintf (stderr, "    internalLeading: %f externalLeading: %f\n", mMetrics.externalLeading, mMetrics.internalLeading);
    fprintf (stderr, "    spaceWidth: %f aveCharWidth: %f xHeight: %f\n", mMetrics.spaceWidth, mMetrics.aveCharWidth, mMetrics.xHeight);
    fprintf (stderr, "    uOff: %f uSize: %f stOff: %f stSize: %f suOff: %f suSize: %f\n", mMetrics.underlineOffset, mMetrics.underlineSize, mMetrics.strikeoutOffset, mMetrics.strikeoutSize, mMetrics.superscriptOffset, mMetrics.subscriptOffset);
#endif

    mHasMetrics = PR_TRUE;
    return mMetrics;
}

// XXX we should replace this to |pango_is_zero_width| after we don't support pre pango 1.10
static PRBool MOZ_pango_is_zero_width(PRUint32 aChar)
{
    if (aChar == 0x00AD)
        return PR_TRUE;
    if (aChar < 0x200B)
        return PR_FALSE;
    if (aChar <= 0x200F || aChar == 0x2028)
        return PR_TRUE;
    if (aChar < 0x202A)
        return PR_FALSE;
    if (aChar <= 0x202E)
        return PR_TRUE;
    if (aChar < 0x2060)
        return PR_FALSE;
    if (aChar <= 0x2063 || aChar == 0xFEFF)
        return PR_TRUE;
    return PR_FALSE;
}
PRBool
gfxPangoFont::HasGlyph(PRUint32 aChar)
{
    // Ensure that null character should be missing.
    if (aChar == 0)
        return PR_FALSE;

    if (MOZ_pango_is_zero_width(aChar))
        return PR_TRUE;

    PangoFont *font = nsnull;
    if (mPangoFont)
        font = mPangoFont;
    else if (mGlyphTestingFont)
        font = mGlyphTestingFont;
    else {
        gfxPangoFontNameMap *fontNameMap = gfxPangoFontNameMap::GetPangoFontNameMap();
        NS_ENSURE_TRUE(fontNameMap, PR_FALSE);
        // XXX in a prinsiple, we need to add weight and style for the key.
        // But this method should be independent from pango for the performance.
        // For the temporary, the name is enough for the key. The members of
        // a font-family should have same glyphs.
        NS_ConvertUTF16toUTF8 name(mName);
        mGlyphTestingFont = fontNameMap->Get(name);
        if (!mGlyphTestingFont) {
            font = GetPangoFont();
            NS_ENSURE_TRUE(font, PR_FALSE);
        } else
            font = mGlyphTestingFont;
    }
    return pango_fc_font_has_char(PANGO_FC_FONT(font), aChar) ? PR_TRUE : PR_FALSE;
}

PRUint32
gfxPangoFont::GetGlyph(const PRUint32 aChar)
{
    // Ensure that null character should be missing.
    if (aChar == 0)
        return 0;
    RealizePangoFont();
    return pango_fc_font_get_glyph(PANGO_FC_FONT(mPangoFont), aChar);
}

nsString
gfxPangoFont::GetUniqueName()
{
    PangoFont *font = GetPangoFont();
    PangoFontDescription *desc = pango_font_describe(font);
    char *str = pango_font_description_to_string(desc);

    // chop off the trailing size, e.g. "Albany AMT 15.359375" -> "Albany AMT"
    PRUint32 end = strlen(str);
    while (end > 0) {
        --end;
        if (str[end] == ' ')
            break;
    }
    str[end] = 0;

    nsString result;
    CopyUTF8toUTF16(str, result);
    g_free(str);
    return result;
}

static const char *sCJKLangGroup[] = {
    "ja",
    "ko",
    "zh-CN",
    "zh-HK",
    "zh-TW"
};

#define COUNT_OF_CJK_LANG_GROUP 5
#define CJK_LANG_JA    sCJKLangGroup[0]
#define CJK_LANG_KO    sCJKLangGroup[1]
#define CJK_LANG_ZH_CN sCJKLangGroup[2]
#define CJK_LANG_ZH_HK sCJKLangGroup[3]
#define CJK_LANG_ZH_TW sCJKLangGroup[4]

static PRInt32
GetCJKLangGroupIndex(const char *aLangGroup)
{
    PRInt32 i;
    for (i = 0; i < COUNT_OF_CJK_LANG_GROUP; i++) {
        if (!PL_strcasecmp(aLangGroup, sCJKLangGroup[i]))
            return i;
    }
    return -1;
}

/**
 ** gfxTextRun
 * 
 * Some serious problems:
 *
 * -- We draw with a font that's hinted for the CTM, but we measure with a font
 * hinted to the identity matrix, so our "bounding metrics" may not be accurate.
 * 
 * -- CreateScaledFont doesn't necessarily give us the font that the Pango
 * metrics assume.
 * 
 **/

/**
 * We use this to append an LTR or RTL Override character to the start of the
 * string. This forces Pango to honour our direction even if there are neutral characters
 * in the string.
 */
static PRInt32 AppendDirectionalIndicatorUTF8(PRBool aIsRTL, nsACString& aString)
{
    static const PRUnichar overrides[2][2] =
      { { 0x202d, 0 }, { 0x202e, 0 }}; // LRO, RLO
    AppendUTF16toUTF8(overrides[aIsRTL], aString);
    return 3; // both overrides map to 3 bytes in UTF8
}

gfxTextRun *
gfxPangoFontGroup::MakeTextRun(const PRUint8 *aString, PRUint32 aLength,
                               const Parameters *aParams, PRUint32 aFlags)
{
    NS_ASSERTION(aFlags & TEXT_IS_8BIT, "8bit should have been set");
    gfxTextRun *run = new gfxTextRun(aParams, aString, aLength, this, aFlags);
    if (!run)
        return nsnull;

    PRBool isRTL = run->IsRightToLeft();
    if ((aFlags & TEXT_IS_ASCII) && !isRTL) {
        // We don't need to send an override character here, the characters must be all
        // LTR
        const gchar *utf8Chars = NS_REINTERPRET_CAST(const gchar*, aString);
        InitTextRun(run, utf8Chars, aLength, 0);
    } else {
        const char *chars = NS_REINTERPRET_CAST(const char*, aString);
        // XXX this could be more efficient.
        // Although chars in not necessarily ASCII (as it may point to the low
        // bytes of any UCS-2 characters < 256), NS_ConvertASCIItoUTF16 seems
        // to DTRT.
        NS_ConvertASCIItoUTF16 unicodeString(chars, aLength);
        nsCAutoString utf8;
        PRInt32 headerLen = AppendDirectionalIndicatorUTF8(isRTL, utf8);
        AppendUTF16toUTF8(unicodeString, utf8);
        InitTextRun(run, utf8.get(), utf8.Length(), headerLen);
    }
    return run;
}

gfxTextRun *
gfxPangoFontGroup::MakeTextRun(const PRUnichar *aString, PRUint32 aLength,
                               const Parameters *aParams, PRUint32 aFlags)
{
    gfxTextRun *run = new gfxTextRun(aParams, aString, aLength, this, aFlags);
    if (!run)
        return nsnull;

    run->RecordSurrogates(aString);

    nsCAutoString utf8;
    PRInt32 headerLen = AppendDirectionalIndicatorUTF8(run->IsRightToLeft(), utf8);
    AppendUTF16toUTF8(Substring(aString, aString + aLength), utf8);
    InitTextRun(run, utf8.get(), utf8.Length(), headerLen);
    return run;
}

void
gfxPangoFontGroup::InitTextRun(gfxTextRun *aTextRun, const gchar *aUTF8Text,
                               PRUint32 aUTF8Length, PRUint32 aUTF8HeaderLength)
{
#if defined(ENABLE_XFT_FAST_PATH_ALWAYS)
    CreateGlyphRunsXft(aTextRun, aUTF8Text + aUTF8HeaderLength, aUTF8Length - aUTF8HeaderLength);
#else
#if defined(ENABLE_XFT_FAST_PATH_8BIT)
    if (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_IS_8BIT) {
        CreateGlyphRunsXft(aTextRun, aUTF8Text + aUTF8HeaderLength, aUTF8Length - aUTF8HeaderLength);
        return;
    }
#endif

    pango_context_set_base_dir(GetFontAt(0)->GetPangoContext(),
                               (aTextRun->IsRightToLeft()
                                  ? PANGO_DIRECTION_RTL : PANGO_DIRECTION_LTR));

    CreateGlyphRunsItemizing(aTextRun, aUTF8Text, aUTF8Length, aUTF8HeaderLength);
#endif
}

static cairo_scaled_font_t*
CreateScaledFont(cairo_t *aCR, cairo_matrix_t *aCTM, PangoFont *aPangoFont)
{
    // XXX is this safe really? We should probably check the font type or something.
    // XXX does this really create the same font that Pango used for measurement?
    // We probably need to work harder here. We should pay particular attention
    // to the font options.
    PangoFcFont *fcfont = PANGO_FC_FONT(aPangoFont);
    cairo_font_face_t *face = cairo_ft_font_face_create_for_pattern(fcfont->font_pattern);
    double size;
    if (FcPatternGetDouble(fcfont->font_pattern, FC_PIXEL_SIZE, 0, &size) != FcResultMatch)
        size = 12.0;
    cairo_matrix_t fontMatrix;
    cairo_matrix_init_scale(&fontMatrix, size, size);
    cairo_font_options_t *fontOptions = cairo_font_options_create();
    cairo_get_font_options(aCR, fontOptions);
    cairo_scaled_font_t *scaledFont =
        cairo_scaled_font_create(face, &fontMatrix, aCTM, fontOptions);
    cairo_font_options_destroy(fontOptions);
    cairo_font_face_destroy(face);
    return scaledFont;
}

void
gfxPangoFont::SetupCairoFont(cairo_t *aCR)
{
    cairo_matrix_t currentCTM;
    cairo_get_matrix(aCR, &currentCTM);

    if (mCairoFont) {
        // Need to validate that its CTM is OK
        cairo_matrix_t fontCTM;
        cairo_scaled_font_get_ctm(mCairoFont, &fontCTM);
        if (fontCTM.xx == currentCTM.xx && fontCTM.yy == currentCTM.yy &&
            fontCTM.xy == currentCTM.xy && fontCTM.yx == currentCTM.yx) {
            cairo_set_scaled_font(aCR, mCairoFont);
            return;
        }

        // Just recreate it from scratch, simplest way
        cairo_scaled_font_destroy(mCairoFont);
    }

    mCairoFont = CreateScaledFont(aCR, &currentCTM, GetPangoFont());
    cairo_set_scaled_font(aCR, mCairoFont);
}

static void
SetupClusterBoundaries(gfxTextRun* aTextRun, const gchar *aUTF8, PRUint32 aUTF8Length,
                       PRUint32 aUTF16Offset, PangoAnalysis *aAnalysis)
{
    if (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_IS_8BIT) {
        // 8-bit text doesn't have clusters.
        // XXX is this true in all languages???
        return;
    }

    // Pango says "the array of PangoLogAttr passed in must have at least N+1
    // elements, if there are N characters in the text being broken".
    // Could use g_utf8_strlen(aUTF8, aUTF8Length) + 1 but the memory savings
    // may not be worth the call.
    nsAutoTArray<PangoLogAttr,2000> buffer;
    if (!buffer.AppendElements(aUTF8Length + 1))
        return;

    const gchar *p = aUTF8;
    const gchar *end = aUTF8 + aUTF8Length;
    gfxTextRun::CompressedGlyph g;

    while (p < end) {
        PangoLogAttr *attr = buffer.Elements();
        pango_break(p, end - p, aAnalysis, attr, buffer.Length());

        while (p < end) {
            if (!attr->is_cursor_position) {
                aTextRun->SetCharacterGlyph(aUTF16Offset, g.SetClusterContinuation());
            }
            ++aUTF16Offset;
        
            gunichar ch = g_utf8_get_char(p);
            NS_ASSERTION(!IS_SURROGATE(ch), "Shouldn't have surrogates in UTF8");
            if (ch >= 0x10000) {
                ++aUTF16Offset;
            }
            // We produced this utf8 so we don't need to worry about malformed stuff
            p = g_utf8_next_char(p);
            ++attr;

            if (ch == 0) {
                // pango_break (pango 1.16.2) only analyses text before the
                // first NUL (but sets one extra attr), so call pango_break
                // again to analyse after the NUL.
                break;
            }
        }
    }
}

static PRInt32
ConvertPangoToAppUnits(PRInt32 aCoordinate, PRUint32 aAppUnitsPerDevUnit)
{
    PRInt64 v = (PRInt64(aCoordinate)*aAppUnitsPerDevUnit + PANGO_SCALE/2)/PANGO_SCALE;
    return PRInt32(v);
}

static void
SetMissingGlyphForUCS4(gfxTextRun *aTextRun, PRUint32 aIndex, gunichar aCh)
{
    if (aCh < 0x10000) {
        aTextRun->SetMissingGlyph(aIndex, PRUnichar(aCh));
        return;
    }

    // Display non-BMP characters as a surrogate pair
    aTextRun->SetMissingGlyph(aIndex, H_SURROGATE(aCh));
    if (aIndex + 1 < aTextRun->GetLength()) {
        aTextRun->SetMissingGlyph(aIndex + 1, L_SURROGATE(aCh));
    }
}

/**
 * Given a run of Pango glyphs that should be treated as a single
 * cluster/ligature, store them in the textrun at the appropriate character
 * and set the other characters involved to be ligature/cluster continuations
 * as appropriate.
 */ 
static nsresult
SetGlyphsForCharacterGroup(const PangoGlyphInfo *aGlyphs, PRUint32 aGlyphCount,
                           gfxTextRun *aTextRun,
                           const gchar *aUTF8, PRUint32 aUTF8Length,
                           PRUint32 *aUTF16Offset,
                           PangoGlyphUnit aOverrideSpaceWidth)
{
    PRUint32 utf16Offset = *aUTF16Offset;
    PRUint32 textRunLength = aTextRun->GetLength();
    const PRUint32 appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();
    const gfxTextRun::CompressedGlyph *charGlyphs = aTextRun->GetCharacterGlyphs();

    // Override the width of a space, but only for spaces that aren't
    // clustered with something else (like a freestanding diacritical mark)
    PangoGlyphUnit width = aGlyphs[0].geometry.width;
    if (aOverrideSpaceWidth && aUTF8[0] == ' ' &&
        (utf16Offset + 1 == textRunLength ||
         charGlyphs[utf16Offset].IsClusterStart())) {
        width = aOverrideSpaceWidth;
    }
    PRInt32 advance = ConvertPangoToAppUnits(width, appUnitsPerDevUnit);

    gfxTextRun::CompressedGlyph g;
    // See if we fit in the compressed area.
    if (aGlyphCount == 1 && advance >= 0 &&
        aGlyphs[0].geometry.x_offset == 0 &&
        aGlyphs[0].geometry.y_offset == 0 &&
        gfxTextRun::CompressedGlyph::IsSimpleAdvance(advance) &&
        gfxTextRun::CompressedGlyph::IsSimpleGlyphID(aGlyphs[0].glyph)) {
        aTextRun->SetCharacterGlyph(utf16Offset,
                                    g.SetSimpleGlyph(advance, aGlyphs[0].glyph));
    } else {
        nsAutoTArray<gfxTextRun::DetailedGlyph,10> detailedGlyphs;
        if (!detailedGlyphs.AppendElements(aGlyphCount))
            return NS_ERROR_OUT_OF_MEMORY;

        PRUint32 i;
        for (i = 0; i < aGlyphCount; ++i) {
            gfxTextRun::DetailedGlyph *details = &detailedGlyphs[i];
            const PangoGlyphInfo &glyph = aGlyphs[i];
            details->mIsLastGlyph = i == aGlyphCount - 1;
            details->mGlyphID = glyph.glyph;
            NS_ASSERTION(details->mGlyphID == glyph.glyph,
                         "Seriously weird glyph ID detected!");
            details->mAdvance =
                ConvertPangoToAppUnits(glyph.geometry.width,
                                       appUnitsPerDevUnit);
            details->mXOffset =
                float(glyph.geometry.x_offset)*appUnitsPerDevUnit/PANGO_SCALE;
            details->mYOffset =
                float(glyph.geometry.y_offset)*appUnitsPerDevUnit/PANGO_SCALE;
        }
        aTextRun->SetDetailedGlyphs(utf16Offset, detailedGlyphs.Elements(), aGlyphCount);
    }

    // Check for ligatures and set *aUTF16Offset.
    const gchar *p = aUTF8;
    const gchar *end = aUTF8 + aUTF8Length;
    while (1) {
        // Skip the CompressedGlyph that we have added, but check if the
        // character was supposed to be ignored. If it's supposed to be ignored,
        // overwrite the textrun entry with an invisible missing-glyph.
        gunichar ch = g_utf8_get_char(p);
        NS_ASSERTION(!IS_SURROGATE(ch), "surrogates should not appear in UTF8");
        if (ch >= 0x10000) {
            // Skip surrogate
            ++utf16Offset;
        } else {
            if (gfxFontGroup::IsInvisibleChar(PRUnichar(ch))) {
                aTextRun->SetCharacterGlyph(utf16Offset, g.SetMissing());
            }
        }
        ++utf16Offset;

        // We produced this UTF8 so we don't need to worry about malformed stuff
        p = g_utf8_next_char(p);
        if (p >= end)
            break;

        if (utf16Offset >= textRunLength) {
            NS_ERROR("Someone has added too many glyphs!");
            return NS_ERROR_FAILURE;
        }

        if (! charGlyphs[utf16Offset].IsClusterContinuation()) {
            // This is a separate grapheme cluster but it has no glyphs.
            // It must be represented by a ligature with the previous
            // grapheme cluster.
            aTextRun->SetCharacterGlyph(utf16Offset, g.SetLigatureContinuation());
        }
    }
    *aUTF16Offset = utf16Offset;
    return NS_OK;
}

nsresult
gfxPangoFontGroup::SetGlyphs(gfxTextRun *aTextRun, gfxPangoFont *aFont,
                             const gchar *aUTF8, PRUint32 aUTF8Length,
                             PRUint32 *aUTF16Offset, PangoGlyphString *aGlyphs,
                             PangoGlyphUnit aOverrideSpaceWidth,
                             PRBool aAbortOnMissingGlyph)
{
    gint numGlyphs = aGlyphs->num_glyphs;
    PangoGlyphInfo *glyphs = aGlyphs->glyphs;
    const gint *logClusters = aGlyphs->log_clusters;
    // We cannot make any assumptions about the order of glyph clusters
    // provided by pango_shape (see 375864), so we work through the UTF8 text
    // and process the glyph clusters in logical order.

    // logGlyphs is like an inverse of logClusters.  For each UTF8 byte:
    //   >= 0 indicates that the byte is first in a cluster and
    //        gives the position of the starting glyph for the cluster.
    //     -1 indicates that the byte does not start a cluster.
    nsAutoTArray<gint,2000> logGlyphs;
    if (!logGlyphs.AppendElements(aUTF8Length + 1))
        return NS_ERROR_OUT_OF_MEMORY;
    PRUint32 utf8Index = 0;
    for(; utf8Index < aUTF8Length; ++utf8Index)
        logGlyphs[utf8Index] = -1;
    logGlyphs[aUTF8Length] = numGlyphs;

    gint lastCluster = -1; // != utf8Index
    for (gint glyphIndex = 0; glyphIndex < numGlyphs; ++glyphIndex) {
        gint thisCluster = logClusters[glyphIndex];
        if (thisCluster != lastCluster) {
            lastCluster = thisCluster;
            NS_ASSERTION(0 <= thisCluster && thisCluster < gint(aUTF8Length),
                         "garbage from pango_shape - this is bad");
            logGlyphs[thisCluster] = glyphIndex;
        }
    }

    PRUint32 utf16Offset = *aUTF16Offset;
    PRUint32 textRunLength = aTextRun->GetLength();
    utf8Index = 0;
    // The next glyph cluster in logical order. 
    gint nextGlyphClusterStart = logGlyphs[utf8Index];
    while (utf8Index < aUTF8Length) {
        if (utf16Offset >= textRunLength) {
          NS_ERROR("Someone has added too many glyphs!");
          return NS_ERROR_FAILURE;
        }
        gint glyphClusterStart = nextGlyphClusterStart;
        // Find the utf8 text associated with this glyph cluster.
        PRUint32 clusterUTF8Start = utf8Index;
        // Check we are consistent with pango_break data.
        NS_ASSERTION(aTextRun->GetCharacterGlyphs()->IsClusterStart(),
                     "Glyph cluster not aligned on character cluster.");
        do {
            ++utf8Index;
            nextGlyphClusterStart = logGlyphs[utf8Index];
        } while (nextGlyphClusterStart < 0 && aUTF8[utf8Index] != '\0');
        const gchar *clusterUTF8 = &aUTF8[clusterUTF8Start];
        PRUint32 clusterUTF8Length = utf8Index - clusterUTF8Start;

        PRBool haveMissingGlyph = PR_FALSE;
        gint glyphIndex = glyphClusterStart;
        if (glyphClusterStart < 0) {
            // No glyphs - This happens with a NUL: Pango doesn't create
            // glyphs for these, not even missing-glyph glyphIDs.
            // Treat a NUL byte as a missing glyph.
            haveMissingGlyph = PR_TRUE;
            // Any non-NUL UTF8 bytes are unexpected.
            NS_ASSERTION(*clusterUTF8 == '\0' && clusterUTF8Length == 1,
                         "No glyphs and not a NUL");
            if (aAbortOnMissingGlyph &&
                (*clusterUTF8 != '\0' || clusterUTF8Length != 1)) {
                return NS_ERROR_FAILURE;
            }
        } else {
            gunichar ch = g_utf8_get_char(clusterUTF8);
            do { // Does pango ever provide more than one glyph in the cluster
                 // if there is a missing glyph?
                if (IS_MISSING_GLYPH(glyphs[glyphIndex].glyph)) {
                    if (MOZ_pango_is_zero_width(ch)) {
                        // the zero width characters returns empty glyph ID at shaping,
                        // we should override it if the font has the character.
                        glyphs[glyphIndex].glyph = aFont->GetGlyph(' ');
                        glyphs[glyphIndex].geometry.width = 0;
                    } else
                        haveMissingGlyph = PR_TRUE;
                }
                glyphIndex++;
            } while (glyphIndex < numGlyphs && 
                     logClusters[glyphIndex] == gint(clusterUTF8Start));

            if (haveMissingGlyph && aAbortOnMissingGlyph)
                return NS_ERROR_FAILURE;
        }

        nsresult rv;
        if (haveMissingGlyph) {
            rv = SetMissingGlyphs(aTextRun, clusterUTF8, clusterUTF8Length,
                             &utf16Offset);
        } else {
            rv = SetGlyphsForCharacterGroup(&glyphs[glyphClusterStart],
                                            glyphIndex - glyphClusterStart,
                                            aTextRun,
                                            clusterUTF8, clusterUTF8Length,
                                            &utf16Offset, aOverrideSpaceWidth);
        }
        NS_ENSURE_SUCCESS(rv,rv);
    }
    *aUTF16Offset = utf16Offset;
    return NS_OK;
}

nsresult
gfxPangoFontGroup::SetMissingGlyphs(gfxTextRun *aTextRun,
                                    const gchar *aUTF8, PRUint32 aUTF8Length,
                                    PRUint32 *aUTF16Offset)
{
    PRUint32 utf16Offset = *aUTF16Offset;
    PRUint32 textRunLength = aTextRun->GetLength();
    for (PRUint32 index = 0; index < aUTF8Length;) {
        if (utf16Offset >= textRunLength) {
            NS_ERROR("Someone has added too many glyphs!");
            break;
        }
        gunichar ch = g_utf8_get_char(aUTF8 + index);
        SetMissingGlyphForUCS4(aTextRun, utf16Offset, ch);

        ++utf16Offset;
        NS_ASSERTION(!IS_SURROGATE(ch), "surrogates should not appear in UTF8");
        if (ch >= 0x10000)
            ++utf16Offset;
        // We produced this UTF8 so we don't need to worry about malformed stuff
        index = g_utf8_next_char(aUTF8 + index) - aUTF8;
    }

    *aUTF16Offset = utf16Offset;
    return NS_OK;
}

#if defined(ENABLE_XFT_FAST_PATH_8BIT) || defined(ENABLE_XFT_FAST_PATH_ALWAYS)
void
gfxPangoFontGroup::CreateGlyphRunsXft(gfxTextRun *aTextRun,
                                      const gchar *aUTF8, PRUint32 aUTF8Length)
{
    const gchar *p = aUTF8;
    Display *dpy = GDK_DISPLAY();
    gfxPangoFont *font = GetFontAt(0);
    XftFont *xfont = font->GetXftFont();
    PRUint32 utf16Offset = 0;
    gfxTextRun::CompressedGlyph g;
    const PRUint32 appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();

    aTextRun->AddGlyphRun(font, 0);

    while (p < aUTF8 + aUTF8Length) {
        // glib-2.12.9: "If p does not point to a valid UTF-8 encoded
        // character, results are undefined." so it is not easy to assert that
        // aUTF8 in fact points to UTF8 data but asserting
        // g_unichar_validate(ch) may be mildly useful.
        gunichar ch = g_utf8_get_char(p);
        p = g_utf8_next_char(p);
        
        if (ch == 0) {
            // treat this null byte as a missing glyph. Pango
            // doesn't create glyphs for these, not even missing-glyphs.
            aTextRun->SetMissingGlyph(utf16Offset, 0);
        } else if (ch < 0x10000 && IsInvisibleChar(PRUnichar(ch))) {
            aTextRun->SetCharacterGlyph(utf16Offset, g.SetMissing());
        } else {
            FT_UInt glyph = XftCharIndex(dpy, xfont, ch);
            XGlyphInfo info;
            XftGlyphExtents(dpy, xfont, &glyph, 1, &info);
            if (info.yOff > 0) {
                NS_WARNING("vertical offsets not supported");
            }

            PRInt32 advance = info.xOff*appUnitsPerDevUnit;
            if (advance >= 0 &&
                gfxTextRun::CompressedGlyph::IsSimpleAdvance(advance) &&
                gfxTextRun::CompressedGlyph::IsSimpleGlyphID(glyph)) {
                aTextRun->SetCharacterGlyph(utf16Offset,
                                            g.SetSimpleGlyph(advance, glyph));
            } else if (IS_MISSING_GLYPH(glyph)) {
                // Note that missing-glyph IDs are not simple glyph IDs, so we'll
                // always get here when a glyph is missing
                SetMissingGlyphForUCS4(aTextRun, utf16Offset, ch);
            } else {
                gfxTextRun::DetailedGlyph details;
                details.mIsLastGlyph = PR_TRUE;
                details.mGlyphID = glyph;
                NS_ASSERTION(details.mGlyphID == glyph,
                             "Seriously weird glyph ID detected!");
                details.mAdvance = advance;
                details.mXOffset = 0;
                details.mYOffset = 0;
                aTextRun->SetDetailedGlyphs(utf16Offset, &details, 1);
            }

            NS_ASSERTION(!IS_SURROGATE(ch), "Surrogates shouldn't appear in UTF8");
            if (ch >= 0x10000) {
                // This character is a surrogate pair in UTF16
                ++utf16Offset;
            }
        }

        ++utf16Offset;
    }
}
#endif

class FontSelector
{
public:
    FontSelector(const gchar *aString, PRInt32 aLength,
                 gfxPangoFontGroup *aGroup, gfxTextRun *aTextRun,
                 PangoItem *aItem, PRUint32 aUTF16Offset, PRPackedBool aIsRTL) :
        mItem(aItem),
        mGroup(aGroup), mTextRun(aTextRun), mString(aString),
        mFontIndex(0), mLength(aLength), mUTF16Offset(aUTF16Offset),
        mTriedPrefFonts(0), mTriedOtherFonts(0), mIsRTL(aIsRTL)
    {
        for (PRUint32 i = 0; i < mGroup->FontListLength(); ++i)
            mFonts.AppendElement(mGroup->GetFontAt(i));
        mSpaceWidth = NS_lround(mGroup->GetFontAt(0)->GetMetrics().spaceWidth * FLOAT_PANGO_SCALE);
    }
    
    nsresult Run()
    {
        return InitSegments(mString, mLength);
    }

    PRUint32 GetUTF16Offset() { return mUTF16Offset; }

    static PRBool ExistsFont(FontSelector *aFs,
                             const nsAString &aName) {
        PRUint32 len = aFs->mFonts.Length();
        for (PRUint32 i = 0; i < len; ++i) {
            if (aName.Equals(aFs->mFonts[i]->GetName()))
                return PR_TRUE;
        }
        return PR_FALSE;
    }

    static PRBool AddFontCallback(const nsAString &aName,
                                  const nsACString &aGenericName,
                                  void *closure) {
        if (aName.IsEmpty())
            return PR_TRUE;

        FontSelector *fs = NS_STATIC_CAST(FontSelector*, closure);

        // XXX do something better than this to remove dups
        if (ExistsFont(fs, aName))
            return PR_TRUE;

        nsRefPtr<gfxPangoFont> font = GetOrMakeFont(aName, fs->mGroup->GetStyle());
        if (font) {
            fs->mFonts.AppendElement(font);
        }

        return PR_TRUE;
    }

private:
    PangoItem *mItem;

    nsTArray< nsRefPtr<gfxPangoFont> > mFonts;

    gfxPangoFontGroup *mGroup;
    gfxTextRun   *mTextRun;
    const char        *mString; // UTF-8
    PRUint32           mFontIndex;
    PRInt32            mLength;
    PRUint32           mUTF16Offset;
    PRUint32           mSpaceWidth;

    PRPackedBool mTriedPrefFonts;
    PRPackedBool mTriedOtherFonts;
    PRPackedBool mIsRTL;

    nsresult InitSegments(const gchar *aUTF8, PRUint32 aLength) {
        if (aLength == 0)
            return NS_OK;
        const gchar *start = aUTF8;
        const gchar *last = start + aLength;

RetryNextFont:
        nsRefPtr<gfxPangoFont> font = GetNextFont();

        // If we cannot found the font that has the current character glyph,
        // we should return default font's missing data.
        if (!font)
            return AppendMissingSegment(start, last - start);

        nsresult rv;
        for (const gchar *c = start; c < last;) {
            // find the first missing glyph
            gunichar u = g_utf8_get_char(c);
            if (font->HasGlyph(PRUint32(u))) {
                c = g_utf8_next_char(c);
                continue;
            }

            // find the next point that can be renderd with current font
            const gchar *missingStart = c;
            const gchar *next;
            for (next = g_utf8_next_char(missingStart); next < last; next = g_utf8_next_char(next)) {
                u = g_utf8_get_char(next);
                if (font->HasGlyph(PRUint32(u)))
                    break;
            }

            // current font has 0 glyphs for current segment, try with next font
            if (missingStart == start && next == last)
                goto RetryNextFont;

            // create the segment for found glyphs
            rv = AppendSegment(font, start, missingStart - start);
            NS_ENSURE_SUCCESS(rv, rv);

            // init the missing glyphs with remains fonts.
            PRUint32 fontIndex = mFontIndex;
            rv = InitSegments(missingStart, next - missingStart);
            mFontIndex = fontIndex;
            NS_ENSURE_SUCCESS(rv, rv);

            start = c = next;
        }

        rv = AppendSegment(font, start, last - start);
        NS_ENSURE_SUCCESS(rv, rv);
        return NS_OK;
    }

    nsresult AppendSegment(gfxPangoFont* aFont, const gchar *aUTF8, PRUint32 aLength) {
        if (aLength == 0)
            return NS_OK;

        PangoFont* pf = aFont->GetPangoFont();

        PangoGlyphString *glyphString = pango_glyph_string_new();
        if (!glyphString)
            return NS_ERROR_OUT_OF_MEMORY;
        PangoFont *tmpFont = mItem->analysis.font;
        mItem->analysis.font = pf;
        pango_shape(aUTF8, aLength, &mItem->analysis, glyphString);
        mItem->analysis.font = tmpFont;

        nsresult rv = mTextRun->AddGlyphRun(aFont, mUTF16Offset);
        if (NS_FAILED(rv)) {
            NS_ERROR("AddGlyphRun Failed");
            pango_glyph_string_free(glyphString);
            return rv;
        }
        PRUint32 utf16Offset = mUTF16Offset;
        rv = mGroup->SetGlyphs(mTextRun, aFont, aUTF8, aLength,
                               &utf16Offset, glyphString, mSpaceWidth, PR_FALSE);
        pango_glyph_string_free(glyphString);
        NS_ENSURE_SUCCESS(rv, rv);

        mUTF16Offset = utf16Offset;
        return NS_OK;
    }

    nsresult AppendMissingSegment(const gchar *aUTF8, PRUint32 aLength) {
        if (aLength == 0)
            return NS_OK;

        nsresult rv = mTextRun->AddGlyphRun(mFonts[0], mUTF16Offset);
        NS_ENSURE_SUCCESS(rv, rv);
        PRUint32 utf16Offset = mUTF16Offset;
        rv = mGroup->SetMissingGlyphs(mTextRun, aUTF8, aLength, &utf16Offset);
        NS_ENSURE_SUCCESS(rv, rv);

        mUTF16Offset = utf16Offset;
        return NS_OK;
    }

    gfxPangoFont *GetNextFont() {
TRY_AGAIN_HOPE_FOR_THE_BEST_2:
        if (mFontIndex < mFonts.Length()) {
            return mFonts[mFontIndex++];
        } else if (!mTriedPrefFonts) {
            mTriedPrefFonts = PR_TRUE;
            nsCAutoString mozLang;
            GetMozLanguage(mItem->analysis.language, mozLang);
            if (!mozLang.IsEmpty()) {
                PRInt32 index = GetCJKLangGroupIndex(mozLang.get());
                if (index >= 0)
                    AppendCJKPrefFonts();
                else
                    AppendPrefFonts(mozLang.get());
            } else {
                NS_ConvertUTF8toUTF16 str(mString);
                PRBool appenedCJKFonts = PR_FALSE;
                for (PRUint32 i = 0; i < str.Length(); ++i) {
                    const PRUnichar ch = str[i];
                    PRUint32 unicodeRange = FindCharUnicodeRange(ch);

                    /* special case CJK */
                    if (unicodeRange == kRangeSetCJK) {
                        if (!appenedCJKFonts) {
                            appenedCJKFonts = PR_TRUE;
                            AppendCJKPrefFonts();
                        }
                    } else {
                        const char *langGroup =
                            LangGroupFromUnicodeRange(unicodeRange);
                        if (langGroup)
                            AppendPrefFonts(langGroup);
                    }
                }
            }
            goto TRY_AGAIN_HOPE_FOR_THE_BEST_2;
        } else if (!mTriedOtherFonts) {
            mTriedOtherFonts = PR_TRUE;
            // XXX we should try by all system fonts
            goto TRY_AGAIN_HOPE_FOR_THE_BEST_2;
        }
        return nsnull;
    }

    void AppendPrefFonts(const char *aLangGroup) {
        NS_ASSERTION(aLangGroup, "aLangGroup is null");
        gfxPlatform *platform = gfxPlatform::GetPlatform();
        nsString fonts;
        platform->GetPrefFonts(aLangGroup, fonts);
        if (fonts.IsEmpty())
            return;
        gfxFontGroup::ForEachFont(fonts, nsDependentCString(aLangGroup),
                                  FontSelector::AddFontCallback, this);
        return;
   }

   void AppendCJKPrefFonts() {
       nsCOMPtr<nsIPrefService> prefs =
           do_GetService(NS_PREFSERVICE_CONTRACTID);
       if (!prefs)
           return;

       nsCOMPtr<nsIPrefBranch> prefBranch;
       prefs->GetBranch(0, getter_AddRefs(prefBranch));
       if (!prefBranch)
           return;

       // Add the accept languages.
       nsXPIDLCString list;
       nsresult rv = prefBranch->GetCharPref("intl.accept_languages",
                                             getter_Copies(list));
       if (NS_SUCCEEDED(rv) && !list.IsEmpty()) {
           const char kComma = ',';
           const char *p, *p_end;
           list.BeginReading(p);
           list.EndReading(p_end);
           while (p < p_end) {
               while (nsCRT::IsAsciiSpace(*p)) {
                   if (++p == p_end)
                       break;
               }
               if (p == p_end)
                   break;
               const char *start = p;
               while (++p != p_end && *p != kComma)
                   /* nothing */ ;
               nsCAutoString lang(Substring(start, p));
               lang.CompressWhitespace(PR_FALSE, PR_TRUE);
               PRInt32 index = GetCJKLangGroupIndex(lang.get());
               if (index >= 0)
                   AppendPrefFonts(sCJKLangGroup[index]);
               p++;
           }
       }

       // XXX I think that we should append system locale here if it is CJK.

       // last resort...
       AppendPrefFonts(CJK_LANG_JA);
       AppendPrefFonts(CJK_LANG_KO);
       AppendPrefFonts(CJK_LANG_ZH_CN);
       AppendPrefFonts(CJK_LANG_ZH_HK);
       AppendPrefFonts(CJK_LANG_ZH_TW);
    }
};

void 
gfxPangoFontGroup::CreateGlyphRunsItemizing(gfxTextRun *aTextRun,
                                            const gchar *aUTF8, PRUint32 aUTF8Length,
                                            PRUint32 aUTF8HeaderLen)
{
    GList *items = pango_itemize(GetFontAt(0)->GetPangoContext(), aUTF8, 0,
                                 aUTF8Length, nsnull, nsnull);
    
    PRUint32 utf16Offset = 0;
    PRBool isRTL = aTextRun->IsRightToLeft();
    GList *pos = items;
    for (; pos && pos->data; pos = pos->next) {
        PangoItem *item = (PangoItem *)pos->data;
        NS_ASSERTION(isRTL == item->analysis.level % 2, "RTL assumption mismatch");

        PRUint32 offset = item->offset;
        PRUint32 length = item->length;
        if (offset < aUTF8HeaderLen) {
            if (offset + length <= aUTF8HeaderLen) {
                pango_item_free(item);
                continue;
            }
            length -= aUTF8HeaderLen - offset;
            offset = aUTF8HeaderLen;
        }
        
        SetupClusterBoundaries(aTextRun, aUTF8 + offset, length, utf16Offset, &item->analysis);
        FontSelector fs(aUTF8 + offset, length, this, aTextRun, item, utf16Offset, isRTL);
        fs.Run(); // appends GlyphRuns
        utf16Offset = fs.GetUTF16Offset();
        pango_item_free(item);
    }

    NS_ASSERTION(utf16Offset == aTextRun->GetLength(),
                 "Didn't resolve all characters");
  
    if (items)
        g_list_free(items);
}

/**
 ** language group helpers
 **/

struct MozPangoLangGroup {
    const char *mozLangGroup;
    const char *PangoLang;
};

static const MozPangoLangGroup MozPangoLangGroups[] = {
    { "x-western",      "en"    },
    { "x-central-euro", "pl"    },
    { "ja",             "ja"    },
    { "zh-TW",          "zh-tw" },
    { "zh-CN",          "zh-cn" },
    { "zh-HK",          "zh-hk" },
    { "ko",             "ko"    },
    { "x-cyrillic",     "ru"    },
    { "x-baltic",       "lv"    },
    { "el",             "el"    },
    { "tr",             "tr"    },
    { "th",             "th"    },
    { "he",             "he"    },
    { "ar",             "ar"    },
    { "x-devanagari",   "hi"    },
    { "x-tamil",        "ta"    },
    { "x-armn",         "ar"    },
    { "x-beng",         "bn"    },
    { "x-ethi",         "et"    },
    { "x-geor",         "ka"    },
    { "x-gujr",         "gu"    },
    { "x-guru",         "pa"    },
    { "x-khmr",         "km"    },
    { "x-mlym",         "ml"    },
    { "x-cans",         "iu"    },
    { "x-unicode",      0       },
    { "x-user-def",     0       },
};

#define NUM_PANGO_LANG_GROUPS (sizeof (MozPangoLangGroups) / \
                               sizeof (MozPangoLangGroups[0]))

/* static */
PangoLanguage *
GetPangoLanguage(const nsACString& cname)
{
    // see if the lang group needs to be translated from mozilla's
    // internal mapping into fontconfig's
    const struct MozPangoLangGroup *langGroup = nsnull;

    for (unsigned int i=0; i < NUM_PANGO_LANG_GROUPS; ++i) {
        if (cname.Equals(MozPangoLangGroups[i].mozLangGroup,
                         nsCaseInsensitiveCStringComparator())) {
            langGroup = &MozPangoLangGroups[i];
            break;
        }
    }

    // if there's no lang group, just use the lang group as it was
    // passed to us
    //
    // we're casting away the const here for the strings - should be
    // safe.
    if (!langGroup)
        return pango_language_from_string(nsPromiseFlatCString(cname).get());
    else if (langGroup->PangoLang) 
        return pango_language_from_string(langGroup->PangoLang);

    return pango_language_from_string("en");
}

// See pango-script-lang-table.h in pango.
static const MozPangoLangGroup PangoAllLangGroup[] = {
    { "x-western",      "aa"    },
    { "x-cyrillic",     "ab"    },
    { "x-western",      "af"    },
    { "x-ethi",         "am"    },
    { "ar",             "ar"    },
    { "x-western",      "ast"   },
    { "x-cyrillic",     "ava"   },
    { "x-western",      "ay"    },
    { "x-western",      "az"    },
    { "x-cyrillic",     "ba"    },
    { "x-western",      "bam"   },
    { "x-cyrillic",     "be"    },
    { "x-cyrillic",     "bg"    },
    { "x-devanagari",   "bh"    },
    { "x-devanagari",   "bho"   },
    { "x-western",      "bi"    },
    { "x-western",      "bin"   },
    { "x-beng",         "bn"    },
    { 0,                "bo"    }, // PANGO_SCRIPT_TIBETAN
    { "x-western",      "br"    },
    { "x-western",      "bs"    },
    { "x-cyrillic",     "bua"   },
    { "x-western",      "ca"    },
    { "x-cyrillic",     "ce"    },
    { "x-western",      "ch"    },
    { "x-cyrillic",     "chm"   },
    { 0,                "chr"   }, // PANGO_SCRIPT_CHEROKEE
    { "x-western",      "co"    },
    { "x-central-euro", "cs"    }, // PANGO_SCRIPT_LATIN
    { "x-cyrillic",     "cu"    },
    { "x-cyrillic",     "cv"    },
    { "x-western",      "cy"    },
    { "x-western",      "da"    },
    { "x-central-euro", "de"    }, // PANGO_SCRIPT_LATIN
    { 0,                "dz"    }, // PANGO_SCRIPT_TIBETAN
    { "el",             "el"    },
    { "x-western",      "en"    },
    { "x-western",      "eo"    },
    { "x-western",      "es"    },
    { "x-western",      "et"    },
    { "x-western",      "eu"    },
    { "ar",             "fa"    },
    { "x-western",      "fi"    },
    { "x-western",      "fj"    },
    { "x-western",      "fo"    },
    { "x-western",      "fr"    },
    { "x-western",      "ful"   },
    { "x-western",      "fur"   },
    { "x-western",      "fy"    },
    { "x-western",      "ga"    },
    { "x-western",      "gd"    },
    { "x-ethi",         "gez"   },
    { "x-western",      "gl"    },
    { "x-western",      "gn"    },
    { "x-gujr",         "gu"    },
    { "x-western",      "gv"    },
    { "x-western",      "ha"    },
    { "x-western",      "haw"   },
    { "he",             "he"    },
    { "x-devanagari",   "hi"    },
    { "x-western",      "ho"    },
    { "x-central-euro", "hr"    }, // PANGO_SCRIPT_LATIN
    { "x-western",      "hu"    },
    { "x-armn",         "hy"    },
    { "x-western",      "ia"    },
    { "x-western",      "ibo"   },
    { "x-western",      "id"    },
    { "x-western",      "ie"    },
    { "x-cyrillic",     "ik"    },
    { "x-western",      "io"    },
    { "x-western",      "is"    },
    { "x-western",      "it"    },
    { "x-cans",         "iu"    },
    { "ja",             "ja"    },
    { "x-geor",         "ka"    },
    { "x-cyrillic",     "kaa"   },
    { "x-western",      "ki"    },
    { "x-cyrillic",     "kk"    },
    { "x-western",      "kl"    },
    { "x-khmr",         "km"    },
    { 0,                "kn"    }, // PANGO_SCRIPT_KANNADA
    { "ko",             "ko"    },
    { "x-devanagari",   "kok"   },
    { "x-devanagari",   "ks"    },
    { "x-cyrillic",     "ku"    },
    { "x-cyrillic",     "kum"   },
    { "x-cyrillic",     "kv"    },
    { "x-western",      "kw"    },
    { "x-cyrillic",     "ky"    },
    { "x-western",      "la"    },
    { "x-western",      "lb"    },
    { "x-cyrillic",     "lez"   },
    { 0,                "lo"    }, // PANGO_SCRIPT_LAO
    { "x-western",      "lt"    },
    { "x-western",      "lv"    },
    { "x-western",      "mg"    },
    { "x-western",      "mh"    },
    { "x-western",      "mi"    },
    { "x-cyrillic",     "mk"    },
    { "x-mlym",         "ml"    },
    { 0,                "mn"    }, // PANGO_SCRIPT_MONGOLIAN
    { "x-western",      "mo"    },
    { "x-devanagari",   "mr"    },
    { "x-western",      "mt"    },
    { 0,                "my"    }, // PANGO_SCRIPT_MYANMAR
    { "x-western",      "nb"    },
    { "x-devanagari",   "ne"    },
    { "x-western",      "nl"    },
    { "x-western",      "nn"    },
    { "x-western",      "no"    },
    { "x-western",      "ny"    },
    { "x-western",      "oc"    },
    { "x-western",      "om"    },
    { 0,                "or"    }, // PANGO_SCRIPT_ORIYA
    { "x-cyrillic",     "os"    },
    { "x-central-euro", "pl"    }, // PANGO_SCRIPT_LATIN
    { "x-western",      "pt"    },
    { "x-western",      "rm"    },
    { "x-western",      "ro"    },
    { "x-cyrillic",     "ru"    },
    { "x-devanagari",   "sa"    },
    { "x-cyrillic",     "sah"   },
    { "x-western",      "sco"   },
    { "x-western",      "se"    },
    { "x-cyrillic",     "sel"   },
    { "x-cyrillic",     "sh"    },
    { 0,                "si"    }, // PANGO_SCRIPT_SINHALA
    { "x-central-euro", "sk"    }, // PANGO_SCRIPT_LATIN
    { "x-central-euro", "sl"    }, // PANGO_SCRIPT_LATIN
    { "x-western",      "sm"    },
    { "x-western",      "sma"   },
    { "x-western",      "smj"   },
    { "x-western",      "smn"   },
    { "x-western",      "sms"   },
    { "x-western",      "so"    },
    { "x-western",      "sq"    },
    { "x-cyrillic",     "sr"    },
    { "x-western",      "sv"    },
    { "x-western",      "sw"    },
    { 0,                "syr"   }, // PANGO_SCRIPT_SYRIAC
    { "x-tamil",        "ta"    },
    { 0,                "te"    }, // PANGO_SCRIPT_TELUGU
    { "x-cyrillic",     "tg"    },
    { "th",             "th"    },
    { "x-ethi",         "ti-er" },
    { "x-ethi",         "ti-et" },
    { "x-ethi",         "tig"   },
    { "x-cyrillic",     "tk"    },
    { 0,                "tl"    }, // PANGO_SCRIPT_TAGALOG
    { "x-western",      "tn"    },
    { "x-western",      "to"    },
    { "x-western",      "tr"    },
    { "x-western",      "ts"    },
    { "x-cyrillic",     "tt"    },
    { "x-western",      "tw"    },
    { "x-cyrillic",     "tyv"   },
    { "ar",             "ug"    },
    { "x-cyrillic",     "uk"    },
    { "ar",             "ur"    },
    { "x-cyrillic",     "uz"    },
    { "x-western",      "ven"   },
    { "x-western",      "vi"    },
    { "x-western",      "vo"    },
    { "x-western",      "vot"   },
    { "x-western",      "wa"    },
    { "x-western",      "wen"   },
    { "x-western",      "wo"    },
    { "x-western",      "xh"    },
    { "x-western",      "yap"   },
    { "he",             "yi"    },
    { "x-western",      "yo"    },
    { "zh-CN",          "zh-cn" },
    { "zh-HK",          "zh-hk" },
    { "zh-HK",          "zh-mo" },
    { "zh-CN",          "zh-sg" },
    { "zh-TW",          "zh-tw" },
    { "x-western",      "zu"    },
};

#define NUM_PANGO_ALL_LANG_GROUPS (sizeof (PangoAllLangGroup) / \
                                   sizeof (PangoAllLangGroup[0]))

/* static */
void
GetMozLanguage(const PangoLanguage *aLang, nsACString &aMozLang)
{
    aMozLang.Truncate();
    if (!aLang)
        return;

    nsCAutoString lang(pango_language_to_string(aLang));
    if (lang.IsEmpty() || lang.Equals("xx"))
        return;

    while (1) {
        for (PRUint32 i = 0; i < NUM_PANGO_ALL_LANG_GROUPS; ++i) {
            if (lang.Equals(PangoAllLangGroup[i].PangoLang)) {
                if (PangoAllLangGroup[i].mozLangGroup)
                    aMozLang.Assign(PangoAllLangGroup[i].mozLangGroup);
                return;
            }
        }

        PRInt32 hyphen = lang.FindChar('-');
        if (hyphen != kNotFound) {
            lang.Cut(hyphen, lang.Length());
            continue;
        }
        break;
    }
}

gfxPangoFontCache::gfxPangoFontCache()
{
    mPangoFonts.Init(500);
}

gfxPangoFontCache::~gfxPangoFontCache()
{
}

void
gfxPangoFontCache::Put(const PangoFontDescription *aFontDesc, PangoFont *aPangoFont)
{
    if (mPangoFonts.Count() > 5000)
        mPangoFonts.Clear();
    PRUint32 key = pango_font_description_hash(aFontDesc);
    gfxPangoFontWrapper *value = new gfxPangoFontWrapper(aPangoFont);
    if (!value)
        return;
    mPangoFonts.Put(key, value);
}

PangoFont*
gfxPangoFontCache::Get(const PangoFontDescription *aFontDesc)
{
    PRUint32 key = pango_font_description_hash(aFontDesc);
    gfxPangoFontWrapper *value;
    if (!mPangoFonts.Get(key, &value))
        return nsnull;
    PangoFont *font = value->Get();
    g_object_ref(font);
    return font;
}

gfxPangoFontNameMap::gfxPangoFontNameMap()
{
    mPangoFonts.Init(100);
}

gfxPangoFontNameMap::~gfxPangoFontNameMap()
{
}

void
gfxPangoFontNameMap::Put(const nsACString &aName, PangoFont *aPangoFont)
{
    nsCAutoString key(aName);
    ToLowerCase(key);
    gfxPangoFontWrapper *value;
    if (!mPangoFonts.Get(key, &value)) {
        value = new gfxPangoFontWrapper(aPangoFont);
        if (!value)
            return;
        mPangoFonts.Put(key, value);
    }
}

PangoFont*
gfxPangoFontNameMap::Get(const nsACString &aName)
{
    nsCAutoString key(aName);
    ToLowerCase(key);
    gfxPangoFontWrapper *value;
    if (!mPangoFonts.Get(key, &value))
        return nsnull;
    PangoFont *font = value->Get();
    g_object_ref(font);
    return font;
}
