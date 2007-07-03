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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *    Josh Aas <josh@mozilla.com>
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

#include "nsCOMPtr.h"
#include "nsClipboard.h"
#include "nsIClipboardOwner.h"
#include "nsString.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsXPIDLString.h"
#include "nsPrimitiveHelpers.h"
#include "nsMemory.h"
#include "nsIImage.h"
#include "nsILocalFile.h"

nsClipboard::nsClipboard() : nsBaseClipboard()
{
  mChangeCount = 0;
}


nsClipboard::~nsClipboard()
{
}


NS_IMETHODIMP
nsClipboard::SetNativeClipboardData(PRInt32 aWhichClipboard)
{
  if ((aWhichClipboard != kGlobalClipboard) || !mTransferable)
    return NS_ERROR_FAILURE;

  mIgnoreEmptyNotification = PR_TRUE;

  NSDictionary* pasteboardOutputDict = PasteboardDictFromTransferable(mTransferable);
  if (!pasteboardOutputDict)
    return NS_ERROR_FAILURE;

  // write everything out to the general pasteboard
  unsigned int outputCount = [pasteboardOutputDict count];
  NSArray* outputKeys = [pasteboardOutputDict allKeys];
  NSPasteboard* generalPBoard = [NSPasteboard generalPasteboard];
  [generalPBoard declareTypes:outputKeys owner:nil];
  for (unsigned int i = 0; i < outputCount; i++) {
    NSString* currentKey = [outputKeys objectAtIndex:i];
    id currentValue = [pasteboardOutputDict valueForKey:currentKey];
    if (currentKey == NSStringPboardType)
      [generalPBoard setString:currentValue forType:currentKey];
    else
      [generalPBoard setData:currentValue forType:currentKey];
  }

  mChangeCount = [generalPBoard changeCount];

  mIgnoreEmptyNotification = PR_FALSE;

  return NS_OK;
}


NS_IMETHODIMP
nsClipboard::GetNativeClipboardData(nsITransferable* aTransferable, PRInt32 aWhichClipboard)
{
  if ((aWhichClipboard != kGlobalClipboard) || !aTransferable)
    return NS_ERROR_FAILURE;

  NSPasteboard* cocoaPasteboard = [NSPasteboard generalPasteboard];
  if (!cocoaPasteboard)
    return NS_ERROR_FAILURE;

  // get flavor list that includes all acceptable flavors (including ones obtained through conversion)
  nsCOMPtr<nsISupportsArray> flavorList;
  nsresult rv = aTransferable->FlavorsTransferableCanImport(getter_AddRefs(flavorList));
  if (NS_FAILED(rv))
    return NS_ERROR_FAILURE;

  PRUint32 flavorCount;
  flavorList->Count(&flavorCount);

  // If we were the last ones to put something on the pasteboard, then just use the cached
  // transferable. Otherwise clear it because it isn't relevant any more.
  if (mChangeCount == [cocoaPasteboard changeCount]) {
    if (mTransferable) {
      for (PRUint32 i = 0; i < flavorCount; i++) {
        nsCOMPtr<nsISupports> genericFlavor;
        flavorList->GetElementAt(i, getter_AddRefs(genericFlavor));
        nsCOMPtr<nsISupportsCString> currentFlavor(do_QueryInterface(genericFlavor));
        if (!currentFlavor)
          continue;

        nsXPIDLCString flavorStr;
        currentFlavor->ToString(getter_Copies(flavorStr));

        nsCOMPtr<nsISupports> dataSupports;
        PRUint32 dataSize = 0;
        rv = mTransferable->GetTransferData(flavorStr, getter_AddRefs(dataSupports), &dataSize);
        if (NS_SUCCEEDED(rv)) {
          aTransferable->SetTransferData(flavorStr, dataSupports, dataSize);
          return NS_OK; // maybe try to fill in more types? Is there a point?
        }
      }
    }
  }
  else {
    nsBaseClipboard::EmptyClipboard(kGlobalClipboard);
  }

  // at this point we can't satisfy the request from cache data so let's look
  // for things other people put on the system clipboard

  for (PRUint32 i = 0; i < flavorCount; i++) {
    nsCOMPtr<nsISupports> genericFlavor;
    flavorList->GetElementAt(i, getter_AddRefs(genericFlavor));
    nsCOMPtr<nsISupportsCString> currentFlavor(do_QueryInterface(genericFlavor));
    if (!currentFlavor)
      continue;

    nsXPIDLCString flavorStr;
    currentFlavor->ToString(getter_Copies(flavorStr)); // i has a flavr

    // printf("looking for clipboard data of type %s\n", flavorStr.get());

    if (flavorStr.EqualsLiteral(kUnicodeMime)) {
      NSString* pString = [cocoaPasteboard stringForType:NSStringPboardType];
      if (!pString)
        continue;

      NSData* stringData = [pString dataUsingEncoding:NSUnicodeStringEncoding];
      unsigned int dataLength = [stringData length];
      void* clipboardDataPtr = malloc(dataLength);
      if (!clipboardDataPtr)
        return NS_ERROR_OUT_OF_MEMORY;
      [stringData getBytes:clipboardDataPtr];

      // The DOM only wants LF, so convert from MacOS line endings to DOM line endings.
      PRInt32 signedDataLength = dataLength;
      nsLinebreakHelpers::ConvertPlatformToDOMLinebreaks(flavorStr, &clipboardDataPtr, &signedDataLength);
      dataLength = signedDataLength;

      // skip BOM (Byte Order Mark to distinguish little or big endian)      
      PRUnichar* clipboardDataPtrNoBOM = (PRUnichar*)clipboardDataPtr;
      if ((dataLength > 2) &&
          ((clipboardDataPtrNoBOM[0] == 0xFEFF) ||
           (clipboardDataPtrNoBOM[0] == 0xFFFE))) {
        dataLength -= sizeof(PRUnichar);
        clipboardDataPtrNoBOM += 1;
      }

      nsCOMPtr<nsISupports> genericDataWrapper;
      nsPrimitiveHelpers::CreatePrimitiveForData(flavorStr, clipboardDataPtrNoBOM, dataLength,
                                                 getter_AddRefs(genericDataWrapper));
      aTransferable->SetTransferData(flavorStr, genericDataWrapper, dataLength);
      free(clipboardDataPtr);
      break;
    }

    /*
    if (flavorStr.EqualsLiteral(kPNGImageMime) || flavorStr.EqualsLiteral(kJPEGImageMime) ||
        flavorStr.EqualsLiteral(kGIFImageMime)) {
      // We have never supported this on Mac OS X, we could someday but nobody does this.
      break;
    }
    */
  }

  return NS_OK;
}


// returns true if we have *any* of the passed in flavors available for pasting
NS_IMETHODIMP
nsClipboard::HasDataMatchingFlavors(nsISupportsArray* aFlavorList, PRInt32 aWhichClipboard, PRBool* outResult) 
{
  *outResult = PR_FALSE;

  if ((aWhichClipboard != kGlobalClipboard) || !aFlavorList)
    return NS_OK;

  // first see if we have data for this in our cached transferable
  if (mTransferable) {    
    nsCOMPtr<nsISupportsArray> transferableFlavorList;
    nsresult rv = mTransferable->FlavorsTransferableCanImport(getter_AddRefs(transferableFlavorList));
    if (NS_SUCCEEDED(rv)) {
      PRUint32 transferableFlavorCount;
      transferableFlavorList->Count(&transferableFlavorCount);
      for (PRUint32 j = 0; j < transferableFlavorCount; j++) {
        nsCOMPtr<nsISupports> transferableFlavorSupports;
        transferableFlavorList->GetElementAt(j, getter_AddRefs(transferableFlavorSupports));
        nsCOMPtr<nsISupportsCString> currentTransferableFlavor(do_QueryInterface(transferableFlavorSupports));
        if (!currentTransferableFlavor)
          continue;
        nsXPIDLCString transferableFlavorStr;
        currentTransferableFlavor->ToString(getter_Copies(transferableFlavorStr));
        
        PRUint32 passedFlavorCount;
        aFlavorList->Count(&passedFlavorCount);
        for (PRUint32 k = 0; k < passedFlavorCount; k++) {
          nsCOMPtr<nsISupports> passedFlavorSupports;
          aFlavorList->GetElementAt(j, getter_AddRefs(passedFlavorSupports));
          nsCOMPtr<nsISupportsCString> currentPassedFlavor(do_QueryInterface(passedFlavorSupports));
          if (!currentPassedFlavor)
            continue;
          nsXPIDLCString passedFlavorStr;
          currentPassedFlavor->ToString(getter_Copies(passedFlavorStr));
          if (passedFlavorStr.Equals(transferableFlavorStr)) {
            *outResult = PR_TRUE;
            return NS_OK;
          }
        }
      }      
    }    
  }

  NSPasteboard* generalPBoard = [NSPasteboard generalPasteboard];

  PRUint32 passedFlavorCount;
  aFlavorList->Count(&passedFlavorCount);
  for (PRUint32 i = 0; i < passedFlavorCount; i++) {
    nsCOMPtr<nsISupports> passedFlavorSupports;
    aFlavorList->GetElementAt(i, getter_AddRefs(passedFlavorSupports));
    nsCOMPtr<nsISupportsCString> flavorWrapper(do_QueryInterface(passedFlavorSupports));
    if (flavorWrapper) {
      nsXPIDLCString flavorStr;
      flavorWrapper->ToString(getter_Copies(flavorStr));
      if (flavorStr.EqualsLiteral(kUnicodeMime)) {
        NSString* availableType = [generalPBoard availableTypeFromArray:[NSArray arrayWithObject:NSStringPboardType]];
        if (availableType && [availableType isEqualToString:NSStringPboardType]) {
          *outResult = PR_TRUE;
          break;
        }
      }
    }      
  }

  return NS_OK;
}


// This function converts anything that other applications might understand into the system format
// and puts it into a dictionary which it returns.
// static
NSDictionary* 
nsClipboard::PasteboardDictFromTransferable(nsITransferable* aTransferable)
{
  if (!aTransferable)
    return nil;

  NSMutableDictionary* pasteboardOutputDict = [NSMutableDictionary dictionary];

  nsCOMPtr<nsISupportsArray> flavorList;
  nsresult rv = aTransferable->FlavorsTransferableCanExport(getter_AddRefs(flavorList));
  if (NS_FAILED(rv))
    return nil;

  PRUint32 flavorCount;
  flavorList->Count(&flavorCount);
  for (PRUint32 i = 0; i < flavorCount; i++) {
    nsCOMPtr<nsISupports> genericFlavor;
    flavorList->GetElementAt(i, getter_AddRefs(genericFlavor));
    nsCOMPtr<nsISupportsCString> currentFlavor(do_QueryInterface(genericFlavor));
    if (!currentFlavor)
      continue;

    nsXPIDLCString flavorStr;
    currentFlavor->ToString(getter_Copies(flavorStr));

    // printf("writing out clipboard data of type %s\n", flavorStr.get());

    if (flavorStr.EqualsLiteral(kUnicodeMime)) {
      void* data = nsnull;
      PRUint32 dataSize = 0;
      nsCOMPtr<nsISupports> genericDataWrapper;
      rv = aTransferable->GetTransferData(flavorStr, getter_AddRefs(genericDataWrapper), &dataSize);
      nsPrimitiveHelpers::CreateDataFromPrimitive(flavorStr, genericDataWrapper, &data, dataSize);
      
      NSString* nativeString = [NSString stringWithCharacters:(const unichar*)data length:(dataSize / sizeof(PRUnichar))];
      // be nice to Carbon apps, normalize the receiver's contents using Form C.
      nativeString = [nativeString precomposedStringWithCanonicalMapping];
      [pasteboardOutputDict setObject:nativeString forKey:NSStringPboardType];
      
      nsMemory::Free(data);
    }
    else if (flavorStr.EqualsLiteral(kPNGImageMime) || flavorStr.EqualsLiteral(kJPEGImageMime) ||
             flavorStr.EqualsLiteral(kGIFImageMime) || flavorStr.EqualsLiteral(kNativeImageMime)) {
      PRUint32 dataSize = 0;
      nsCOMPtr<nsISupports> transferSupports;
      aTransferable->GetTransferData(flavorStr, getter_AddRefs(transferSupports), &dataSize);
      nsCOMPtr<nsISupportsInterfacePointer> ptrPrimitive(do_QueryInterface(transferSupports));
      if (!ptrPrimitive)
        continue;

      nsCOMPtr<nsISupports> primitiveData;
      ptrPrimitive->GetData(getter_AddRefs(primitiveData));

      nsCOMPtr<nsIImage> image(do_QueryInterface(primitiveData));
      if (!image) {
        NS_WARNING("Image isn't an nsIImage in transferable");
        continue;
      }

      if (NS_FAILED(image->LockImagePixels(PR_FALSE)))
        continue;

      PRInt32 height = image->GetHeight();
      PRInt32 stride = image->GetLineStride();
      PRInt32 width = image->GetWidth();
      if ((stride % 4 != 0) || (height < 1) || (width < 1))
        continue;

      PRUint32* imageData = (PRUint32*)image->GetBits();

      PRUint32* reorderedData = (PRUint32*)malloc(height * stride);
      if (!reorderedData)
        continue;

      // We have to reorder data to have alpha last because only Tiger can handle
      // alpha being first.
      PRUint32 imageLength = ((stride * height) / 4);
      for (PRUint32 i = 0; i < imageLength; i++) {
        PRUint32 pixel = imageData[i];
        reorderedData[i] = CFSwapInt32HostToBig((pixel << 8) | (pixel >> 24));
      }

      PRUint8* planes[2];
      planes[0] = (PRUint8*)reorderedData;
      planes[1] = nsnull;
      NSBitmapImageRep* imageRep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes
                                                                           pixelsWide:width
                                                                           pixelsHigh:height
                                                                        bitsPerSample:8
                                                                      samplesPerPixel:4
                                                                             hasAlpha:YES
                                                                             isPlanar:NO
                                                                       colorSpaceName:NSDeviceRGBColorSpace
                                                                          bytesPerRow:stride
                                                                         bitsPerPixel:32];
      NSData* tiffData = [imageRep TIFFRepresentationUsingCompression:NSTIFFCompressionNone factor:1.0];
      [imageRep release];
      free(reorderedData);

      if (NS_FAILED(image->UnlockImagePixels(PR_FALSE)))
        continue;

      [pasteboardOutputDict setObject:tiffData forKey:NSTIFFPboardType];
    }

    // If it wasn't a type that we recognize as exportable we don't put it on the system
    // clipboard. We'll just access it from our cached transferable when we need it.
  }

  return pasteboardOutputDict;
}
