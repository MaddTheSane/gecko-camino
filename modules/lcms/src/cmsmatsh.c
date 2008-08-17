//
//  Little cms
//  Copyright (C) 1998-2007 Marti Maria
//
// Permission is hereby granted, free of charge, to any person obtaining 
// a copy of this software and associated documentation files (the "Software"), 
// to deal in the Software without restriction, including without limitation 
// the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the Software 
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in 
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO 
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE 
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION 
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include "lcms.h"


// Shaper/Matrix handling
// This routines handles the matrix-shaper method. A note about domain
// is here required. If the shaper-matrix is invoked on INPUT profiles,
// after the shaper process, we have a value between 0 and 0xFFFF. Thus,
// for proper matrix handling, we must convert it to 15fix16, so
// ToFixedDomain might be called. But cmsLinearInterpFixed() returns
// data yet in fixed point, so no additional process is required.
// Then, we obtain data on 15.16, so we need to shift >> by 1 to
// obtain 1.15 PCS format.

// On OUTPUT profiles, things are inverse, we must first expand 1 bit
// by shifting left, and then convert result between 0 and 1.000 to
// RGB, so FromFixedDomain() must be called before pass values to
// shaper. Trickly, there is a situation where this shifts works
// little different. Sometimes, lcms smelts input/output
// matrices into a single, one shaper, process. In such cases, since
// input is encoded from 0 to 0xffff, we must first use the shaper and
// then the matrix, an additional FromFixedDomain() must be used to
// accomodate output values.

// For a sake of simplicity, I will handle this three behaviours
// with different routines, so the flags MATSHAPER_INPUT and MATSHAPER_OUTPUT
// can be conbined to signal smelted matrix-shapers



static
int ComputeTables(LPGAMMATABLE Table[3], LPWORD Out[3], LPL16PARAMS p16)
{
    int i, AllLinear;

       cmsCalcL16Params(Table[0] -> nEntries, p16);

       AllLinear = 0;
       for (i=0; i < 3; i++)
       {
        LPWORD PtrW;

        PtrW = (LPWORD) _cmsMalloc(sizeof(WORD) * p16 -> nSamples);

        if (PtrW == NULL) return -1;  // Signal error            
              
        CopyMemory(PtrW, Table[i] -> GammaTable, sizeof(WORD) * Table[i] -> nEntries);

        Out[i] = PtrW;      // Set table pointer

        // Linear after all?

        AllLinear   += cmsIsLinear(PtrW, p16 -> nSamples);
       }

       // If is all linear, then supress table interpolation (this
       // will speed greately some trivial operations. 
       // Return 1 if present, 0 if all linear
       

       if (AllLinear != 3) return 1;

       return 0;

}


LPMATSHAPER cmsAllocMatShaper2(LPMAT3 Matrix, LPGAMMATABLE In[], LPLCMSPRECACHE InPrecache,
                               LPGAMMATABLE Out[], LPLCMSPRECACHE OutPrecache, 
                               DWORD Behaviour)
{
       LPMATSHAPER NewMatShaper;
       int rc;

       NewMatShaper = (LPMATSHAPER) _cmsMalloc(sizeof(MATSHAPER));
       if (NewMatShaper)
              ZeroMemory(NewMatShaper, sizeof(MATSHAPER));

       NewMatShaper->dwFlags = Behaviour;

       // Fill matrix part
       if (Behaviour & MATSHAPER_FLOATMAT) {
              FMAT3ASetup(&NewMatShaper->Matrix.FA);
              MAT3toFloat(NewMatShaper -> Matrix.FA.F, Matrix);
              if (!FMAT3isIdentity(NewMatShaper -> Matrix.FA.F, 0.00001f))
                            NewMatShaper -> dwFlags |= MATSHAPER_HASMATRIX;
       }
       else {
              MAT3toFix(&NewMatShaper -> Matrix.W, Matrix);
              if (!MAT3isIdentity(&NewMatShaper -> Matrix.W, 0.00001))
                            NewMatShaper -> dwFlags |= MATSHAPER_HASMATRIX;
       }

       // Now, on the table characteristics

       // If we have an output precache, use that
       if (OutPrecache != NULL) {
              PRECACHE_ADDREF(OutPrecache);
              NewMatShaper->L_Precache = OutPrecache;
              NewMatShaper -> dwFlags |= MATSHAPER_HASSHAPER;
       }

       else {
              rc = ComputeTables(Out, NewMatShaper ->L, &NewMatShaper ->p16);
              if (rc < 0) {
                     cmsFreeMatShaper(NewMatShaper);
                     return NULL;
              }
              if (rc == 1) NewMatShaper -> dwFlags |= MATSHAPER_HASSHAPER;        
       }

       // If we have an input precache, use that
       if (InPrecache != NULL) {
              PRECACHE_ADDREF(InPrecache);
              NewMatShaper->L2_Precache = InPrecache;
              NewMatShaper-> dwFlags |= MATSHAPER_HASINPSHAPER;
       }

       else {

              rc = ComputeTables(In, NewMatShaper ->L2, &NewMatShaper ->p2_16);
              if (rc < 0) {
                     cmsFreeMatShaper(NewMatShaper);
                     return NULL;
              }
              if (rc == 1) NewMatShaper -> dwFlags |= MATSHAPER_HASINPSHAPER;     
       }

       return NewMatShaper;

}

// Creation & Destruction

LPMATSHAPER cmsAllocMatShaper(LPMAT3 Matrix, LPGAMMATABLE Tables[], DWORD Behaviour)
{
       LPMATSHAPER NewMatShaper;
       int i, AllLinear;

       NewMatShaper = (LPMATSHAPER) _cmsMalloc(sizeof(MATSHAPER));
       if (NewMatShaper)
              ZeroMemory(NewMatShaper, sizeof(MATSHAPER));

       NewMatShaper->dwFlags = Behaviour & (MATSHAPER_ALLSMELTED);

       // Fill matrix part

       MAT3toFix(&NewMatShaper -> Matrix.W, Matrix);

       // Reality check

       if (!MAT3isIdentity(&NewMatShaper -> Matrix.W, 0.00001))
                     NewMatShaper -> dwFlags |= MATSHAPER_HASMATRIX;

       // Now, on the table characteristics

       cmsCalcL16Params(Tables[0] -> nEntries, &NewMatShaper -> p16);

       // Copy tables

       AllLinear = 0;
       for (i=0; i < 3; i++)
       {
        LPWORD PtrW;

        PtrW = (LPWORD) _cmsMalloc(sizeof(WORD) * NewMatShaper -> p16.nSamples);

        if (PtrW == NULL) {
              cmsFreeMatShaper(NewMatShaper);
              return NULL;
        }

        CopyMemory(PtrW, Tables[i] -> GammaTable,
                            sizeof(WORD) * Tables[i] -> nEntries);

        NewMatShaper -> L[i] = PtrW;      // Set table pointer

        // Linear after all?

        AllLinear   += cmsIsLinear(PtrW, NewMatShaper -> p16.nSamples);
       }

       // If is all linear, then supress table interpolation (this
       // will speed greately some trivial operations

       if (AllLinear != 3)
              NewMatShaper -> dwFlags |= MATSHAPER_HASSHAPER;

       return NewMatShaper;
}



// Free associated memory

void cmsFreeMatShaper(LPMATSHAPER MatShaper)
{
       int i;

       if (!MatShaper) return;

       // Release references to the precaches if we have them
       if (MatShaper->L_Precache != NULL)
              PRECACHE_RELEASE(MatShaper->L_Precache);
       if (MatShaper->L2_Precache != NULL)
              PRECACHE_RELEASE(MatShaper->L2_Precache);

       for (i=0; i < 3; i++)
       {
              // These are never initialized from their zeroed state if we
              // were using a cache
              if (MatShaper -> L[i]) _cmsFree(MatShaper ->L[i]);
              if (MatShaper -> L2[i]) _cmsFree(MatShaper ->L2[i]);
       }

       _cmsFree(MatShaper);
}


// All smelted must postpose gamma to last stage.

static
void AllSmeltedBehaviour(LPMATSHAPER MatShaper, WORD In[], WORD Out[])
{

       WORD tmp[3];
       WVEC3 InVect, OutVect;

       if (MatShaper -> dwFlags & MATSHAPER_HASINPSHAPER)
       {
              if (MatShaper->L2_Precache != NULL) 
              {
              InVect.n[VX] = MatShaper->L2_Precache->Impl.LI16W_FORWARD.Cache[0][In[0]];
              InVect.n[VY] = MatShaper->L2_Precache->Impl.LI16W_FORWARD.Cache[1][In[1]];
              InVect.n[VZ] = MatShaper->L2_Precache->Impl.LI16W_FORWARD.Cache[2][In[2]];
              }
              else 
              {
              InVect.n[VX] = cmsLinearInterpFixed(In[0], MatShaper -> L2[0], &MatShaper -> p2_16);
              InVect.n[VY] = cmsLinearInterpFixed(In[1], MatShaper -> L2[1], &MatShaper -> p2_16);
              InVect.n[VZ] = cmsLinearInterpFixed(In[2], MatShaper -> L2[2], &MatShaper -> p2_16);
              }
       }
       else
       {
       InVect.n[VX] = ToFixedDomain(In[0]);
       InVect.n[VY] = ToFixedDomain(In[1]);
       InVect.n[VZ] = ToFixedDomain(In[2]);
       }


       if (MatShaper -> dwFlags & MATSHAPER_HASMATRIX)
       {       
                         
             MAT3evalW(&OutVect, &MatShaper -> Matrix.W, &InVect);
       }
       else 
       {
       OutVect.n[VX] = InVect.n[VX];
       OutVect.n[VY] = InVect.n[VY];
       OutVect.n[VZ] = InVect.n[VZ];
       }

             
       tmp[0] = _cmsClampWord(FromFixedDomain(OutVect.n[VX]));
       tmp[1] = _cmsClampWord(FromFixedDomain(OutVect.n[VY]));
       tmp[2] = _cmsClampWord(FromFixedDomain(OutVect.n[VZ]));

       
           
       if (MatShaper -> dwFlags & MATSHAPER_HASSHAPER)
       {
              if (MatShaper->L_Precache != NULL) 
              {
              Out[0] = MatShaper->L_Precache->Impl.LI1616_REVERSE.Cache[0][tmp[0]];
              Out[1] = MatShaper->L_Precache->Impl.LI1616_REVERSE.Cache[1][tmp[1]];
              Out[2] = MatShaper->L_Precache->Impl.LI1616_REVERSE.Cache[2][tmp[2]];
              }
              else 
              {
              Out[0] = cmsLinearInterpLUT16(tmp[0], MatShaper -> L[0], &MatShaper -> p16);
              Out[1] = cmsLinearInterpLUT16(tmp[1], MatShaper -> L[1], &MatShaper -> p16);
              Out[2] = cmsLinearInterpLUT16(tmp[2], MatShaper -> L[2], &MatShaper -> p16);
              }
       }
       else
       {
       Out[0] = tmp[0];
       Out[1] = tmp[1];
       Out[2] = tmp[2];
       }
        
}


static
void InputBehaviour(LPMATSHAPER MatShaper, WORD In[], WORD Out[])
{
       WVEC3 InVect, OutVect;

       
       if (MatShaper -> dwFlags & MATSHAPER_HASSHAPER)
       {
       InVect.n[VX] = cmsLinearInterpFixed(In[0], MatShaper -> L[0], &MatShaper -> p16);
       InVect.n[VY] = cmsLinearInterpFixed(In[1], MatShaper -> L[1], &MatShaper -> p16);
       InVect.n[VZ] = cmsLinearInterpFixed(In[2], MatShaper -> L[2], &MatShaper -> p16);
       }
       else
       {
       InVect.n[VX] = ToFixedDomain(In[0]);
       InVect.n[VY] = ToFixedDomain(In[1]);
       InVect.n[VZ] = ToFixedDomain(In[2]);
       }

       if (MatShaper -> dwFlags & MATSHAPER_HASMATRIX)
       {
              MAT3evalW(&OutVect, &MatShaper -> Matrix.W, &InVect);
       }
       else
       {
       OutVect =  InVect;
       }

       // PCS in 1Fixed15 format, adjusting

       Out[0] = _cmsClampWord((OutVect.n[VX]) >> 1);
       Out[1] = _cmsClampWord((OutVect.n[VY]) >> 1);
       Out[2] = _cmsClampWord((OutVect.n[VZ]) >> 1);

}


static
void OutputBehaviour(LPMATSHAPER MatShaper, WORD In[], WORD Out[])
{
       WVEC3 InVect, OutVect;
       int i;

       // We need to convert from XYZ to RGB, here we must
       // shift << 1 to pass between 1.15 to 15.16 formats

       InVect.n[VX] = (Fixed32) In[0] << 1;
       InVect.n[VY] = (Fixed32) In[1] << 1;
       InVect.n[VZ] = (Fixed32) In[2] << 1;

       if (MatShaper -> dwFlags & MATSHAPER_HASMATRIX)
       {
              MAT3evalW(&OutVect, &MatShaper -> Matrix.W, &InVect);
       }
       else
       {
       OutVect = InVect;
       }


       if (MatShaper -> dwFlags & MATSHAPER_HASSHAPER)
       {
              for (i=0; i < 3; i++)
              {

              Out[i] = cmsLinearInterpLUT16(
                     _cmsClampWord(FromFixedDomain(OutVect.n[i])),
                     MatShaper -> L[i],
                     &MatShaper ->p16);
              }
       }
       else
       {
       // Result from fixed domain to RGB

       Out[0] = _cmsClampWord(FromFixedDomain(OutVect.n[VX]));
       Out[1] = _cmsClampWord(FromFixedDomain(OutVect.n[VY]));
       Out[2] = _cmsClampWord(FromFixedDomain(OutVect.n[VZ]));
       }

}

void cmsEvalMatShaperFloat(LPMATSHAPER MatShaper, BYTE In[], BYTE Out[])
{
       WORD tmp[3];
       FVEC3 OutVect;
       LPFVEC3 FloatVals = &MatShaper -> Matrix.FA.F->v[3]; // Access our secret aligned temp buffer

       if (MatShaper -> dwFlags & MATSHAPER_HASINPSHAPER)
       {
              if (MatShaper->L2_Precache != NULL) 
              {
              FloatVals->n[VX] = MatShaper->L2_Precache->Impl.LI16F_FORWARD.Cache[0][In[0]];
              FloatVals->n[VY] = MatShaper->L2_Precache->Impl.LI16F_FORWARD.Cache[1][In[1]];
              FloatVals->n[VZ] = MatShaper->L2_Precache->Impl.LI16F_FORWARD.Cache[2][In[2]];
              }
              else
              {
              FloatVals->n[VX] = ToFloatDomain(cmsLinearInterpLUT16(RGB_8_TO_16(In[0]), MatShaper -> L2[0], &MatShaper -> p2_16));
              FloatVals->n[VY] = ToFloatDomain(cmsLinearInterpLUT16(RGB_8_TO_16(In[1]), MatShaper -> L2[1], &MatShaper -> p2_16));
              FloatVals->n[VZ] = ToFloatDomain(cmsLinearInterpLUT16(RGB_8_TO_16(In[2]), MatShaper -> L2[2], &MatShaper -> p2_16));
              }
       }
       else
       {
       FloatVals->n[VX] = ToFloatDomain(In[0]);
       FloatVals->n[VY] = ToFloatDomain(In[1]);
       FloatVals->n[VZ] = ToFloatDomain(In[2]);
       }


       if (MatShaper -> dwFlags & MATSHAPER_HASMATRIX)
       {       
                         
       MAT3evalF(&OutVect, MatShaper -> Matrix.FA.F, FloatVals);
       }
       else 
       {
       OutVect.n[VX] = FloatVals->n[VX];
       OutVect.n[VY] = FloatVals->n[VY];
       OutVect.n[VZ] = FloatVals->n[VZ];
       }

             
       tmp[0] = _cmsClampWord(FromFloatDomain(OutVect.n[VX]));
       tmp[1] = _cmsClampWord(FromFloatDomain(OutVect.n[VY]));
       tmp[2] = _cmsClampWord(FromFloatDomain(OutVect.n[VZ]));

       
           
       if (MatShaper -> dwFlags & MATSHAPER_HASSHAPER)
       {
              if (MatShaper->L_Precache != NULL) 
              {
              Out[0] = MatShaper->L_Precache->Impl.LI168_REVERSE.Cache[0][tmp[0]];
              Out[1] = MatShaper->L_Precache->Impl.LI168_REVERSE.Cache[1][tmp[1]];
              Out[2] = MatShaper->L_Precache->Impl.LI168_REVERSE.Cache[2][tmp[2]];
              }
              else 
              {
              Out[0] = RGB_16_TO_8(cmsLinearInterpLUT16(tmp[0], MatShaper -> L[0], &MatShaper -> p16));
              Out[1] = RGB_16_TO_8(cmsLinearInterpLUT16(tmp[1], MatShaper -> L[1], &MatShaper -> p16));
              Out[2] = RGB_16_TO_8(cmsLinearInterpLUT16(tmp[2], MatShaper -> L[2], &MatShaper -> p16));
              }
       }
       else
       {
       Out[0] = RGB_16_TO_8(tmp[0]);
       Out[1] = RGB_16_TO_8(tmp[1]);
       Out[2] = RGB_16_TO_8(tmp[2]);
       }
}

// Master on evaluating shapers, 3 different behaviours

void cmsEvalMatShaper(LPMATSHAPER MatShaper, WORD In[], WORD Out[])
{
       if ((MatShaper -> dwFlags & MATSHAPER_ALLSMELTED) == MATSHAPER_ALLSMELTED)
       {
              AllSmeltedBehaviour(MatShaper, In, Out);
              return;
       }
       if (MatShaper -> dwFlags & MATSHAPER_INPUT)
       {
              InputBehaviour(MatShaper, In, Out);
              return;
       }

       OutputBehaviour(MatShaper, In, Out);
}

