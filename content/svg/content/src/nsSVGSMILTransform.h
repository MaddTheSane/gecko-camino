/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is Brian Birtles.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian Birtles <birtles@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef NS_SVGSMILTRANSFORM_H_
#define NS_SVGSMILTRANSFORM_H_

////////////////////////////////////////////////////////////////////////
// nsSVGSMILTransform
//
// A pared-down representation of an SVG transform used in SMIL animation. We
// just store the most basic facts about the transform such that we can add the
// transform parameters together and later reconstruct a full SVG transform from
// this information.
//
// The meaning of the mParams array depends on the transform type as follows:
//
// Type                | mParams[0], mParams[1], mParams[2], ...
// --------------------+-----------------------------------------
// TRANSFORM_TRANSLATE | tx, ty
// TRANSFORM_SCALE     | sx, sy
// TRANSFORM_ROTATE    | rotation-angle (in degrees), cx, cy
// TRANSFORM_SKEWX     | skew-angle (in degrees)
// TRANSFORM_SKEWY     | skew-angle (in degrees)
// TRANSFORM_MATRIX    | a, b, c, d, e, f
//
// TRANSFORM_MATRIX is never generated by animation code (it is only produced
// when the user inserts one via the DOM) and often requires special handling
// when we do encounter it. Therefore many users of this class are only
// interested in the first three parameters and so we provide a special
// constructor for setting those parameters only.
class nsSVGSMILTransform
{
public:
  enum TransformType
  {
    TRANSFORM_TRANSLATE,
    TRANSFORM_SCALE,
    TRANSFORM_ROTATE,
    TRANSFORM_SKEWX,
    TRANSFORM_SKEWY,
    TRANSFORM_MATRIX
  };

  nsSVGSMILTransform(TransformType aType)
  : mTransformType(aType)
  {
    for (int i = 0; i < 6; ++i) {
      mParams[i] = 0;
    }
  }

  nsSVGSMILTransform(TransformType aType, float (&aParams)[3])
  : mTransformType(aType)
  {
    for (int i = 0; i < 3; ++i) {
      mParams[i] = aParams[i];
    }
    for (int i = 3; i < 6; ++i) {
      mParams[i] = 0;
    }
  }

  nsSVGSMILTransform(float (&aParams)[6])
  : mTransformType(TRANSFORM_MATRIX)
  {
    for (int i = 0; i < 6; ++i) {
      mParams[i] = aParams[i];
    }
  }
    
  TransformType mTransformType;
  
  float mParams[6];
};

#endif // NS_SVGSMILTRANSFORM_H_
