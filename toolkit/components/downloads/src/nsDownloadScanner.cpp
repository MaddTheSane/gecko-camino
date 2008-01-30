/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: se cin sw=2 ts=2 et : */
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
 * The Original Code is download manager code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation
 *
 * Contributor(s):
 *   Rob Arnold <robarnold@mozilla.com> (Original Author)
 *   Masatoshi Kimura <VYV03354@nifty.ne.jp>
 *   Jim Mathies <jmathies@mozilla.com>
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
 
#include "nsDownloadScanner.h"
#include <comcat.h>
#include <process.h>
#include "nsDownloadManager.h"
#include "nsIXULAppInfo.h"
#include "nsXULAppAPI.h"
#include "nsIPrefService.h"
#include "nsNetUtil.h"
#include "AttachmentServices.h"

/**
 * Code overview
 *
 * Download scanner attempts to make use of one of two different virus
 * scanning interfaces available on Windows - IOfficeAntiVirus (Windows
 * 95/NT 4 and IE 5) and IAttachmentExecute (XPSP2 and up).  The latter
 * interface supports calling IOfficeAntiVirus internally, while also
 * adding support for XPSP2+ ADS forks which define security related
 * prompting on downloaded content.  
 *
 * Both interfaces are synchronous and can take a while, so it is not a
 * good idea to call either from the main thread. Some antivirus scanners can
 * take a long time to scan or the call might block while the scanner shows
 * its UI so if the user were to download many files that finished around the
 * same time, they would have to wait a while if the scanning were done on
 * exactly one other thread. Since the overhead of creating a thread is
 * relatively small compared to the time it takes to download a file and scan
 * it, a new thread is spawned for each download that is to be scanned. Since
 * most of the mozilla codebase is not threadsafe, all the information needed
 * for the scanner is gathered in the main thread in nsDownloadScanner::Scan::Start.
 * The only function of nsDownloadScanner::Scan which is invoked on another
 * thread is DoScan.
 *
 * There are 4 possible outcomes of the virus scan:
 *    AVSCAN_GOOD   => the file is clean
 *    AVSCAN_BAD    => the file has a virus
 *    AVSCAN_UGLY   => the file had a virus, but it was cleaned
 *    AVSCAN_FAILED => something else went wrong with the virus scanner.
 *
 * Both the good and ugly states leave the user with a benign file, so they
 * transition to the finished state. Bad files are sent to the blocked state.
 * Failed states transition to finished downloads.
 *
 * Possible Future enhancements:
 *  * Create an interface for scanning files in general
 *  * Make this a service
 *  * Get antivirus scanner status via WMI/registry
 */

#define PREF_BDA_DONTCLEAN "browser.download.antivirus.dontclean"

// IAttachmentExecute supports user definable settings for certain
// security related prompts. This defines a general GUID for use in
// all projects. Individual projects can define an individual guid
// if they want to.
#ifndef MOZ_VIRUS_SCANNER_PROMPT_GUID
#define MOZ_VIRUS_SCANNER_PROMPT_GUID \
  { 0xb50563d1, 0x16b6, 0x43c2, { 0xa6, 0x6a, 0xfa, 0xe6, 0xd2, 0x11, 0xf2, \
  0xea } }
#endif
static const GUID GUID_MozillaVirusScannerPromptGeneric =
  MOZ_VIRUS_SCANNER_PROMPT_GUID;

nsDownloadScanner::nsDownloadScanner()
  : mHaveAVScanner(PR_FALSE), mHaveAttachmentExecute(PR_FALSE)
{
}

nsresult
nsDownloadScanner::Init()
{
  // This CoInitialize/CoUninitialize pattern seems to be common in the Mozilla
  // codebase. All other COM calls/objects are made on different threads.
  nsresult rv = NS_OK;
  CoInitialize(NULL);
  if (!IsAESAvailable() && ListCLSID() < 0)
    rv = NS_ERROR_NOT_AVAILABLE;
  CoUninitialize();

  return rv;
}

PRBool
nsDownloadScanner::IsAESAvailable()
{
  nsRefPtr<IAttachmentExecute> ae;
  HRESULT hr;
  hr = CoCreateInstance(CLSID_AttachmentServices, NULL, CLSCTX_INPROC,
                        IID_IAttachmentExecute, getter_AddRefs(ae));
  if (FAILED(hr)) {
    NS_WARNING("Could not instantiate attachment execution service\n");
    return PR_FALSE;
  }

  mHaveAVScanner = PR_TRUE;
  mHaveAttachmentExecute = PR_TRUE;
  return PR_TRUE;
}

PRInt32
nsDownloadScanner::ListCLSID()
{
  nsRefPtr<ICatInformation> catInfo;
  HRESULT hr;
  hr = CoCreateInstance(CLSID_StdComponentCategoriesMgr, NULL, CLSCTX_INPROC,
                        IID_ICatInformation, getter_AddRefs(catInfo));
  if (FAILED(hr)) {
    NS_WARNING("Could not create category information class\n");
    return -1;
  }
  nsRefPtr<IEnumCLSID> clsidEnumerator;
  GUID guids [1] = { CATID_MSOfficeAntiVirus };
  hr = catInfo->EnumClassesOfCategories(1, guids, 0, NULL,
      getter_AddRefs(clsidEnumerator));
  if (FAILED(hr)) {
    NS_WARNING("Could not get class enumerator for category\n");
    return -2;
  }

  ULONG nReceived;
  CLSID clsid;
  while(clsidEnumerator->Next(1, &clsid, &nReceived) == S_OK && nReceived == 1)
    mScanCLSID.AppendElement(clsid);

  if (mScanCLSID.Length() == 0) {
    // No installed Anti Virus program
    return -3;
  }

  mHaveAVScanner = PR_TRUE;
  return 0;
}

unsigned int __stdcall
nsDownloadScanner::ScannerThreadFunction(void *p)
{
  NS_ASSERTION(!NS_IsMainThread(), "Antivirus scan should not be run on the main thread");
  nsDownloadScanner::Scan *scan = static_cast<nsDownloadScanner::Scan*>(p);
  scan->DoScan();
  _endthreadex(0);
  return 0;
}

nsDownloadScanner::Scan::Scan(nsDownloadScanner *scanner, nsDownload *download)
  : mDLScanner(scanner), mThread(NULL), 
    mDownload(download), mStatus(AVSCAN_NOTSTARTED)
{
}

nsresult
nsDownloadScanner::Scan::Start()
{
  mThread = (HANDLE)_beginthreadex(NULL, 0, ScannerThreadFunction,
      this, CREATE_SUSPENDED, NULL);
  if (!mThread)
    return NS_ERROR_OUT_OF_MEMORY;

  nsresult rv = NS_OK;

  // Default is to try to clean downloads
  mIsReadOnlyRequest = PR_FALSE;

  nsCOMPtr<nsIPrefBranch> pref =
    do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (pref)
    rv = pref->GetBoolPref(PREF_BDA_DONTCLEAN, &mIsReadOnlyRequest);

  // Get the path to the file on disk
  nsCOMPtr<nsILocalFile> file;
  rv = mDownload->GetTargetFile(getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = file->GetPath(mPath);
  NS_ENSURE_SUCCESS(rv, rv);

  // Grab the app name
  nsCOMPtr<nsIXULAppInfo> appinfo =
    do_GetService(XULAPPINFO_SERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString name;
  rv = appinfo->GetName(name);
  NS_ENSURE_SUCCESS(rv, rv);
  CopyUTF8toUTF16(name, mName);

  // Get the origin
  nsCOMPtr<nsIURI> uri;
  rv = mDownload->GetSource(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString origin;
  rv = uri->GetSpec(origin);
  NS_ENSURE_SUCCESS(rv, rv);

  CopyUTF8toUTF16(origin, mOrigin);

  // We count https/ftp/http as an http download
  PRBool isHttp(PR_FALSE), isFtp(PR_FALSE), isHttps(PR_FALSE);
  nsCOMPtr<nsIURI> innerURI = NS_GetInnermostURI(uri);
  (void)innerURI->SchemeIs("http", &isHttp);
  (void)innerURI->SchemeIs("ftp", &isFtp);
  (void)innerURI->SchemeIs("https", &isHttps);
  mIsHttpDownload = isHttp || isFtp || isHttps;

  // ResumeThread returns the previous suspend count
  if (1 != ::ResumeThread(mThread)) {
    CloseHandle(mThread);
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

nsresult
nsDownloadScanner::Scan::Run()
{
  // Cleanup our thread
  WaitForSingleObject(mThread, INFINITE);
  CloseHandle(mThread);

  DownloadState downloadState = 0;
  switch (mStatus) {
    case AVSCAN_BAD:
      downloadState = nsIDownloadManager::DOWNLOAD_DIRTY;
      break;
    default:
    case AVSCAN_FAILED:
    case AVSCAN_GOOD:
    case AVSCAN_UGLY:
      downloadState = nsIDownloadManager::DOWNLOAD_FINISHED;
      break;
  }
  (void)mDownload->SetState(downloadState);

  NS_RELEASE_THIS();
  return NS_OK;
}

void
nsDownloadScanner::Scan::DoScanAES()
{
  HRESULT hr;
  nsRefPtr<IAttachmentExecute> ae;
  hr = CoCreateInstance(CLSID_AttachmentServices, NULL, CLSCTX_ALL,
                        IID_IAttachmentExecute, getter_AddRefs(ae));

  mStatus = AVSCAN_SCANNING;

  if (SUCCEEDED(hr)) {
    (void)ae->SetClientGuid(GUID_MozillaVirusScannerPromptGeneric);
    (void)ae->SetLocalPath(mPath.BeginWriting());
    (void)ae->SetSource(mOrigin.BeginWriting());

    // Save() will invoke the scanner
    hr = ae->Save();

    if (SUCCEEDED(hr)) { // Passed the scan
      mStatus = AVSCAN_GOOD;
    }
    else if (HRESULT_CODE(hr) == ERROR_FILE_NOT_FOUND) {
      NS_WARNING("Downloaded file disappeared before it could be scanned");
      mStatus = AVSCAN_FAILED;
    }
    else { 
      mStatus = AVSCAN_UGLY;
    }
  }
  else {
    mStatus = AVSCAN_FAILED;
  }
}

void
nsDownloadScanner::Scan::DoScanOAV()
{
  HRESULT hr;
  MSOAVINFO info;
  info.cbsize = sizeof(MSOAVINFO);
  info.fPath = TRUE;
  info.fInstalled = FALSE;
  info.fReadOnlyRequest = mIsReadOnlyRequest;
  info.fHttpDownload = mIsHttpDownload;
  info.hwnd = NULL;

  info.pwzHostName = mName.BeginWriting();
  info.u.pwzFullPath = mPath.BeginWriting();
  info.pwzOrigURL = mOrigin.BeginWriting();

  for (PRUint32 i = 0; i < mDLScanner->mScanCLSID.Length(); i++) {
    nsRefPtr<IOfficeAntiVirus> vScanner;
    hr = CoCreateInstance(mDLScanner->mScanCLSID[i], NULL, CLSCTX_ALL,
                          IID_IOfficeAntiVirus, getter_AddRefs(vScanner));
    if (FAILED(hr)) {
      NS_WARNING("Could not instantiate antivirus scanner");
      mStatus = AVSCAN_FAILED;
    } else {
      mStatus = AVSCAN_SCANNING;

      hr = vScanner->Scan(&info);

      if (hr == S_OK) { // Passed the scan
        mStatus = AVSCAN_GOOD;
        continue;
      }
      else if (hr == S_FALSE) { // Failed but cleaned up
        mStatus = AVSCAN_UGLY;
        continue;
      }
      else if (HRESULT_CODE(hr) == ERROR_FILE_NOT_FOUND) {
        NS_WARNING("Downloaded file disappeared before it could be scanned");
        mStatus = AVSCAN_FAILED;
        break;
      }
      else if (hr == E_FAIL) { // Failed
        mStatus = AVSCAN_BAD;
        break;
      }
      else {
        mStatus = AVSCAN_FAILED;
        break;
      }
    }
  }
}

void
nsDownloadScanner::Scan::DoScan()
{
  CoInitialize(NULL);

  if (mDLScanner->mHaveAttachmentExecute)
    DoScanAES();
  else
    DoScanOAV();

  CoUninitialize();

  // We need to do a few more things on the main thread
  NS_DispatchToMainThread(this);
}

nsresult
nsDownloadScanner::ScanDownload(nsDownload *download)
{
  if (!mHaveAVScanner)
    return NS_ERROR_NOT_AVAILABLE;

  // No ref ptr, see comment below
  Scan *scan = new Scan(this, download);
  if (!scan)
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ADDREF(scan);

  nsresult rv = scan->Start();

  // Note that we only release upon error. On success, the scan is passed off
  // to a new thread. It is eventually released in Scan::Run on the main thread.
  if (NS_FAILED(rv))
    NS_RELEASE(scan);

  return rv;
}
