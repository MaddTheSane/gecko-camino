# -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Mozilla.org Code.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 2001
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Blake Ross <blakeross@telocity.com> (Original Author)
#   Ben Goodger <ben@bengoodger.com> (v2.0)
#   Dan Mosedale <dmose@mozilla.org>
#   Fredrik Holmqvist <thesuckiestemail@yahoo.se>
#   Josh Aas <josh@mozilla.com>
#   Shawn Wilsher <me@shawnwilsher.com> (v3.0)
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

///////////////////////////////////////////////////////////////////////////////
// Globals

const PREF_BDM_CLOSEWHENDONE = "browser.download.manager.closeWhenDone";
const PREF_BDM_ALERTONEXEOPEN = "browser.download.manager.alertOnEXEOpen";
const PREF_BDM_RETENTION = "browser.download.manager.retention";
const PREF_BDM_DISPLAYEDHISTORYDAYS =
  "browser.download.manager.displayedHistoryDays";

const nsLocalFile = Components.Constructor("@mozilla.org/file/local;1",
                                           "nsILocalFile", "initWithPath");

var Cc = Components.classes;
var Ci = Components.interfaces;

var gDownloadManager  = Cc["@mozilla.org/download-manager;1"].
                        getService(Ci.nsIDownloadManager);
var gDownloadListener     = null;
var gDownloadsView        = null;
var gDownloadsActiveTitle = null;
var gDownloadsOtherLabel  = null;
var gDownloadsOtherTitle  = null;
var gDownloadInfoPopup    = null;
var gUserInterfered       = false;
var gSearching            = false;

// If the user has interacted with the window in a significant way, we should
// not auto-close the window. Tough UI decisions about what is "significant."
var gUserInteracted = false;

///////////////////////////////////////////////////////////////////////////////
// Utility Functions 
function fireEventForElement(aElement, aEventType)
{
  var e = document.createEvent("Events");
  e.initEvent("download-" + aEventType, true, true);
  
  aElement.dispatchEvent(e);
}

function createDownloadItem(aID, aFile, aTarget, aURI, aState,
                            aStatus, aProgress, aStartTime)
{
  var dl = document.createElement("richlistitem");
  dl.setAttribute("type", "download");
  dl.setAttribute("id", "dl" + aID);
  dl.setAttribute("dlid", aID);
  dl.setAttribute("image", "moz-icon://" + aFile + "?size=32");
  dl.setAttribute("file", aFile);
  dl.setAttribute("target", aTarget);
  dl.setAttribute("uri", aURI);
  dl.setAttribute("state", aState);
  dl.setAttribute("status", aStatus);
  dl.setAttribute("progress", aProgress);
  dl.setAttribute("startTime", aStartTime);

  var ioSvc = Cc["@mozilla.org/network/io-service;1"].
              getService(Ci.nsIIOService);
  var file = ioSvc.newURI(aFile, null, null).QueryInterface(Ci.nsIFileURL).file;
  dl.setAttribute("path", file.nativePath || file.path);
  
  return dl;
}

function getDownload(aID)
{
  return document.getElementById("dl" + aID);
}

///////////////////////////////////////////////////////////////////////////////
// Start/Stop Observers
function downloadCompleted(aDownload) 
{
  // Wrap this in try...catch since this can be called while shutting down... 
  // it doesn't really matter if it fails then since well.. we're shutting down
  // and there's no UI to update!
  try {
    let dl = getDownload(aDownload.id);

    // If we are displaying search results, we do not want to add it to the list
    // of completed downloads
    if (!gSearching)
      gDownloadsView.insertBefore(dl, gDownloadsOtherTitle.nextSibling);
    else
      gDownloadsView.removeChild(dl);

    // getTypeFromFile fails if it can't find a type for this file.
    try {
      // Refresh the icon, so that executable icons are shown.
      const kExternalHelperAppServContractID =
        "@mozilla.org/uriloader/external-helper-app-service;1";
      var mimeService = Cc[kExternalHelperAppServContractID].
                        getService(Ci.nsIMIMEService);
      var contentType = mimeService.getTypeFromFile(aDownload.targetFile);

      var listItem = getDownload(aDownload.id)
      var oldImage = listItem.getAttribute("image");
      // Tacking on contentType bypasses cache
      listItem.setAttribute("image", oldImage + "&contentType=" + contentType);
    } catch (e) { }

    if (gDownloadManager.activeDownloadCount == 0) {
      gDownloadsActiveTitle.hidden = true;
      document.title = document.documentElement.getAttribute("statictitle");
    }
  }
  catch (e) { }
}

function autoRemoveAndClose(aDownload)
{
  var pref = Cc["@mozilla.org/preferences-service;1"].
             getService(Ci.nsIPrefBranch);

  if (aDownload && (pref.getIntPref(PREF_BDM_RETENTION) == 0)) {
    // The download manager backend removes this, but we have to update the UI!
    var dl = getDownload(aDownload.id);
    if (dl)
      dl.parentNode.removeChild(dl);
  }
  
  if (gDownloadManager.activeDownloadCount == 0) {
    // For the moment, just use the simple heuristic that if this window was
    // opened by the download process, rather than by the user, it should
    // auto-close if the pref is set that way. If the user opened it themselves,
    // it should not close until they explicitly close it.
    var autoClose = pref.getBoolPref(PREF_BDM_CLOSEWHENDONE);
    var autoOpened =
      !window.opener || window.opener.location.href == window.location.href;
    if (autoClose && autoOpened && !gUserInteracted) {
      gCloseDownloadManager();
      return true;
    }
  }
  
  return false;
}

// This function can be overwritten by extensions that wish to place the
// Download Window in another part of the UI. 
function gCloseDownloadManager()
{
  window.close();
}

///////////////////////////////////////////////////////////////////////////////
//// Download Event Handlers

function cancelDownload(aDownload)
{
  gDownloadManager.cancelDownload(aDownload.getAttribute("dlid"));

  // XXXben - 
  // If we got here because we resumed the download, we weren't using a temp file
  // because we used saveURL instead. (this is because the proper download mechanism
  // employed by the helper app service isn't fully accessible yet... should be fixed...
  // talk to bz...)
  // the upshot is we have to delete the file if it exists. 
  var f = getLocalFileFromNativePathOrUrl(aDownload.getAttribute("file"));

  if (f.exists()) 
    f.remove(false);
}

function pauseDownload(aDownload)
{
  var id = aDownload.getAttribute("dlid");
  gDownloadManager.pauseDownload(id);
}

function resumeDownload(aDownload)
{
  gDownloadManager.resumeDownload(aDownload.getAttribute("dlid"));
}

function removeDownload(aDownload)
{
  gDownloadManager.removeDownload(aDownload.getAttribute("dlid"));
  var newIndex = Math.max(gDownloadsView.selectedIndex - 1, 0);
  gDownloadsView.removeChild(aDownload);
  gDownloadsView.selectedIndex = newIndex;
}

function showDownload(aDownload)
{
  var f = getLocalFileFromNativePathOrUrl(aDownload.getAttribute("file"));

  try {
    f.reveal();
  } catch (ex) {
    // if reveal failed for some reason (eg on unix it's not currently
    // implemented), send the file: URL window rooted at the parent to 
    // the OS handler for that protocol
    var parent = f.parent;
    if (parent)
      openExternal(parent);
  }
}

function onDownloadDblClick(aEvent)
{
  var item = aEvent.target;
  if (item.getAttribute("type") == "download" && aEvent.button == 0) {
    var state = parseInt(item.getAttribute("state"));
    switch (state) {
      case Ci.nsIDownloadManager.DOWNLOAD_FINISHED:
        gDownloadViewController.doCommand("cmd_open");
        break;
      case Ci.nsIDownloadManager.DOWNLOAD_DOWNLOADING:  
        gDownloadViewController.doCommand("cmd_pause");
        break;
      case Ci.nsIDownloadManager.DOWNLOAD_PAUSED:
        gDownloadViewController.doCommand("cmd_resume");
        break;
      case Ci.nsIDownloadManager.DOWNLOAD_CANCELED:
      case Ci.nsIDownloadManager.DOWNLOAD_FAILED:
        gDownloadViewController.doCommand("cmd_retry");
        break;
    }
  }
}

function openDownload(aDownload)
{
  var f = getLocalFileFromNativePathOrUrl(aDownload.getAttribute("file"));
  if (f.isExecutable()) {
    var dontAsk = false;
    var pref = Cc["@mozilla.org/preferences-service;1"].
               getService(Ci.nsIPrefBranch);
    try {
      dontAsk = !pref.getBoolPref(PREF_BDM_ALERTONEXEOPEN);
    } catch (e) { }

    if (!dontAsk) {
      var strings = document.getElementById("downloadStrings");
      var name = aDownload.getAttribute("target");
      var message = strings.getFormattedString("fileExecutableSecurityWarning", [name, name]);

      var title = strings.getString("fileExecutableSecurityWarningTitle");
      var dontAsk = strings.getString("fileExecutableSecurityWarningDontAsk");

      var promptSvc = Cc["@mozilla.org/embedcomp/prompt-service;1"].
                      getService(Ci.nsIPromptService);
      var checkbox = { value: false };
      var open = promptSvc.confirmCheck(window, title, message, dontAsk, checkbox);

      if (!open) 
        return;
      pref.setBoolPref(PREF_BDM_ALERTONEXEOPEN, !checkbox.value);
    }       
  }
  try {
    f.launch();
  } catch (ex) {
    // if launch fails, try sending it through the system's external
    // file: URL handler
    openExternal(f);
  }
}

function showDownloadInfo(aDownload)
{
  gUserInteracted = true;

  var popupTitle    = document.getElementById("information-title");
  var uriLabel      = document.getElementById("information-uri");
  var locationLabel = document.getElementById("information-location");

  // Generate the proper title (the start time of the download)
  var dts = Cc["@mozilla.org/intl/scriptabledateformat;1"].
            getService(Ci.nsIScriptableDateFormat);  
  var dateStarted = new Date(parseInt(aDownload.getAttribute("startTime")));
  dateStarted = dts.FormatDateTime("",
                                   dts.dateFormatLong,
                                   dts.timeFormatNoSeconds,
                                   dateStarted.getFullYear(),
                                   dateStarted.getMonth() + 1,
                                   dateStarted.getDate(),
                                   dateStarted.getHours(),
                                   dateStarted.getMinutes(), 0);
  popupTitle.setAttribute("value", dateStarted);
  // Add proper uri and path
  var uri = aDownload.getAttribute("uri");
  uriLabel.label = uri;
  uriLabel.setAttribute("tooltiptext", uri);
  var path = aDownload.getAttribute("path");
  locationLabel.label = path;
  locationLabel.setAttribute("tooltiptext", path);

  var button = document.getAnonymousElementByAttribute(aDownload, "anonid", "info");
  gDownloadInfoPopup.openPopup(button, "after_end", 0, 0, false, false);
}

function retryDownload(aDownload)
{
  gDownloadManager.retryDownload(aDownload.getAttribute("dlid"));
}

// This is called by the progress listener. We don't actually use the event
// system here to minimize time wastage. 
var gLastComputedMean = -1;
var gLastActiveDownloads = 0;
function onUpdateProgress()
{
  if (gDownloadManager.activeDownloads == 0) {
    document.title = document.documentElement.getAttribute("statictitle");
    gLastComputedMean = -1;
    return;
  }

  // Establish the mean transfer speed and amount downloaded.
  var mean = 0;
  var base = 0;
  var numActiveDownloads = 0;
  var dls = gDownloadManager.activeDownloads;
  while (dls.hasMoreElements()) {
    let dl = dls.getNext();
    dl.QueryInterface(Ci.nsIDownload);
    mean += dl.amountTransferred;
    base += dl.size;
    numActiveDownloads++;
  }

  // we're not downloading anything at the moment,
  // but we already downloaded something.
  if (base == 0) {
    mean = 100;
  } else {
    mean = Math.floor((mean / base) * 100);
  }

  // Update title of window
  if (mean != gLastComputedMean || gLastActiveDownloads != numActiveDownloads) {
    gLastComputedMean = mean;
    gLastActiveDownloads = numActiveDownloads;

    let strings = document.getElementById("downloadStrings");
    if (numActiveDownloads > 1) {
      document.title = strings.getFormattedString("downloadsTitleMultiple",
                                                  [mean, numActiveDownloads]);
    } else {
      document.title = strings.getFormattedString("downloadsTitle", [mean]);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
//// Startup, Shutdown
function Startup() 
{
  gDownloadsView        = document.getElementById("downloadView");
  gDownloadsActiveTitle = document.getElementById("active-downloads-title");
  gDownloadsOtherLabel  = document.getElementById("other-downloads");
  gDownloadsOtherTitle  = document.getElementById("other-downloads-title");
  gDownloadInfoPopup    = document.getElementById("information");

  buildDefaultView();

  // View event listeners
  gDownloadsView.addEventListener("dblclick", onDownloadDblClick, false);

  // The DownloadProgressListener (DownloadProgressListener.js) handles progress
  // notifications. 
  gDownloadListener = new DownloadProgressListener();
  gDownloadManager.addListener(gDownloadListener);

  // downloads can finish before Startup() does, so check if the window should
  // close and act accordingly
  if (!autoRemoveAndClose())
    gDownloadsView.focus();
}

function Shutdown() 
{
  gDownloadManager.removeListener(gDownloadListener);
}

///////////////////////////////////////////////////////////////////////////////
// View Context Menus
var gContextMenus = [ 
  ["menuitem_pause", "menuitem_cancel"],
  ["menuitem_open", "menuitem_show", "menuitem_remove"],
  ["menuitem_retry", "menuitem_remove"],
  ["menuitem_retry", "menuitem_remove"],
  ["menuitem_resume", "menuitem_cancel"],
  ["menuitem_cancel"]
];

function buildContextMenu(aEvent)
{
  if (aEvent.target.id != "downloadContextMenu")
    return false;
    
  var popup = document.getElementById("downloadContextMenu");
  while (popup.hasChildNodes())
    popup.removeChild(popup.firstChild);
  
  if (gDownloadsView.selectedItem) {
    var idx = parseInt(gDownloadsView.selectedItem.getAttribute("state"));
    if (idx < 0)
      idx = 0;
    
    var menus = gContextMenus[idx];
    for (var i = 0; i < menus.length; ++i)
      popup.appendChild(document.getElementById(menus[i]).cloneNode(true));
    
    return true;
  }
  
  return false;
}

///////////////////////////////////////////////////////////////////////////////
//// Drag and Drop

var gDownloadDNDObserver =
{
  onDragOver: function (aEvent, aFlavour, aDragSession)
  {
    aDragSession.canDrop = true;
  },
  
  onDrop: function(aEvent, aXferData, aDragSession)
  {
    var split = aXferData.data.split("\n");
    var url = split[0];
    if (url != aXferData.data) {  //do nothing, not a valid URL
      var name = split[1];
      saveURL(url, name, null, true, true);
    }
  },
  _flavourSet: null,  
  getSupportedFlavours: function ()
  {
    if (!this._flavourSet) {
      this._flavourSet = new FlavourSet();
      this._flavourSet.appendFlavour("text/x-moz-url");
      this._flavourSet.appendFlavour("text/unicode");
    }
    return this._flavourSet;
  }
}

///////////////////////////////////////////////////////////////////////////////
//// Command Updating and Command Handlers

var gDownloadViewController = {
  supportsCommand: function(aCommand)
  {
    var commandNode = document.getElementById(aCommand);
    return commandNode && commandNode.parentNode ==
                            document.getElementById("downloadsCommands");
  },
  
  isCommandEnabled: function(aCommand)
  {
    if (!window.gDownloadsView)
      return false;
    
    var dl = gDownloadsView.selectedItem;
    if (!dl)
      return false;

    switch (aCommand) {
      case "cmd_cancel":
        return dl.inProgress;
      case "cmd_open":
      case "cmd_show":
        let file = getLocalFileFromNativePathOrUrl(dl.getAttribute("file"));
        return dl.openable && file.exists();
      case "cmd_pause":
        return dl.inProgress && !dl.paused;
      case "cmd_pauseResume":
        return dl.inProgress || dl.paused;
      case "cmd_resume":
        return dl.paused;
      case "cmd_remove":
      case "cmd_retry":
        return dl.removable;
      case "cmd_showInfo":
        return true;
    }
    return false;
  },
  
  doCommand: function(aCommand)
  {
    if (this.isCommandEnabled(aCommand))
      this.commands[aCommand](gDownloadsView.selectedItem);
  },  
  
  onCommandUpdate: function ()
  {
    var downloadsCommands = document.getElementById("downloadsCommands");
    for (var i = 0; i < downloadsCommands.childNodes.length; ++i)
      this.updateCommand(downloadsCommands.childNodes[i]);
  },
  
  updateCommand: function (command) 
  {
    if (this.isCommandEnabled(command.id))
      command.removeAttribute("disabled");
    else
      command.setAttribute("disabled", "true");
  },
  
  commands: {
    cmd_cancel: function(aSelectedItem) {
      cancelDownload(aSelectedItem);
    },
    cmd_open: function(aSelectedItem) {
      openDownload(aSelectedItem);
    },
    cmd_pause: function(aSelectedItem) {
      pauseDownload(aSelectedItem);
    },
    cmd_pauseResume: function(aSelectedItem) {
      if (aSelectedItem.inProgress)
        this.commands.cmd_pause(aSelectedItem);
      else
        this.commands.cmd_resume(aSelectedItem);
    },
    cmd_remove: function(aSelectedItem) {
      removeDownload(aSelectedItem);
    },
    cmd_resume: function(aSelectedItem) {
      resumeDownload(aSelectedItem);
    },
    cmd_retry: function(aSelectedItem) {
      retryDownload(aSelectedItem);
    },
    cmd_show: function(aSelectedItem) {
      showDownload(aSelectedItem);
    },
    cmd_showInfo: function(aSelectedItem) {
      showDownloadInfo(aSelectedItem);
    }
  }
};

function onDownloadShowInfo()
{
  if (gDownloadsView.selectedItem)
    fireEventForElement(gDownloadsView.selectedItem, "properties");
}

function openExternal(aFile)
{
  var uri = Cc["@mozilla.org/network/io-service;1"].
            getService(Ci.nsIIOService).newFileURI(aFile);

  var protocolSvc = Cc["@mozilla.org/uriloader/external-protocol-service;1"].
                    getService(Ci.nsIExternalProtocolService);
  protocolSvc.loadUrl(uri);

  return;
}

///////////////////////////////////////////////////////////////////////////////
//// Utility functions

/**
 * Builds the default view that the download manager starts out with.
 */
function buildDefaultView()
{
  buildActiveDownloadsList();

  let pref = Cc["@mozilla.org/preferences-service;1"].
             getService(Ci.nsIPrefBranch);
  let days = pref.getIntPref(PREF_BDM_DISPLAYEDHISTORYDAYS);
  buildDownloadListWithTime(Date.now() - days * 24 * 60 * 60 * 1000);

  // select the first visible download item, if any
  var children = gDownloadsView.children;
  if (children.length > 0)
    gDownloadsView.selectedItem = children[0];
}

/**
 * Builds the downloads list with a given statement and reference node.
 *
 * @param aStmt
 *        The compiled SQL statement to build with.  This needs to have the
 *        following columns in this order to work properly:
 *        id, target, name, source, state, startTime
 *        This statement should be ordered on the endTime ASC so that the end
 *        result is a list of downloads with their end time's descending.
 * @param aRef
 *        The node we use for placement of the download objects.  We place each
 *        new node above the previously inserted one.
 */
function buildDownloadList(aStmt, aRef)
{
  while (aRef.nextSibling && aRef.nextSibling.tagName == "richlistitem")
    gDownloadsView.removeChild(aRef.nextSibling);

  while (aStmt.executeStep()) {
    let id = aStmt.getInt64(0);
    let state = aStmt.getInt32(4);
    let percentComplete = 100;
    if (state == Ci.nsIDownloadManager.DOWNLOAD_NOTSTARTED ||
        state == Ci.nsIDownloadManager.DOWNLOAD_DOWNLOADING ||
        state == Ci.nsIDownloadManager.DOWNLOAD_PAUSED) {
      // so we have an in-progress download that we need to determine the
      // proper percentage complete for.  This download will actually be in
      // the active downloads array internally, so calling getDownload is cheap.
      let dl = gDownloadManager.getDownload(id);
      percentComplete = dl.percentComplete;
    }
    let dl = createDownloadItem(id, aStmt.getString(1),
                                aStmt.getString(2), aStmt.getString(3),
                                state, "", percentComplete,
                                Math.round(aStmt.getInt64(5) / 1000));
    gDownloadsView.insertBefore(dl, aRef.nextSibling);
  }
}

var gActiveDownloadsQuery = null;
function buildActiveDownloadsList()
{
  // Are there any active downloads?
  if (gDownloadManager.activeDownloadCount == 0)
    return;

  // unhide the label
  gDownloadsActiveTitle.hidden = false;

  // repopulate the list
  var db = gDownloadManager.DBConnection;
  var stmt = gActiveDownloadsQuery;
  if (!stmt) {
    stmt = gActiveDownloadsQuery =
      db.createStatement("SELECT id, target, name, source, state, startTime " +
                         "FROM moz_downloads " +
                         "WHERE state = ?1 " +
                         "OR state = ?2 " +
                         "OR state = ?3 " +
                         "ORDER BY endTime ASC");
  }

  try {
    stmt.bindInt32Parameter(0, Ci.nsIDownloadManager.DOWNLOAD_NOTSTARTED);
    stmt.bindInt32Parameter(1, Ci.nsIDownloadManager.DOWNLOAD_DOWNLOADING);
    stmt.bindInt32Parameter(2, Ci.nsIDownloadManager.DOWNLOAD_PAUSED);
    buildDownloadList(stmt, gDownloadsActiveTitle);
  } finally {
    stmt.reset();
  }
}

/**
 * Builds the download view with downloads from a given time until now.
 *
 * @param aTime
 *        The time that we want to start displaying downloads from.  This time
 *        is in milliseconds (what is returned from Date.now()).
 */
var gDownloadListWithTimeQuery = null;
function buildDownloadListWithTime(aTime)
{
  var db = gDownloadManager.DBConnection;
  var stmt = gDownloadListWithTimeQuery;
  if (!stmt) {
    stmt = gDownloadListWithTimeQuery =
      db.createStatement("SELECT id, target, name, source, state, startTime " +
                         "FROM moz_downloads " +
                         "WHERE startTime >= ?1 " +
                         "AND (state = ?2 " +
                         "OR state = ?3 " +
                         "OR state = ?4) " +
                         "ORDER BY endTime ASC");
  }

  try {
    stmt.bindInt64Parameter(0, aTime * 1000);
    stmt.bindInt32Parameter(1, Ci.nsIDownloadManager.DOWNLOAD_FINISHED);
    stmt.bindInt32Parameter(2, Ci.nsIDownloadManager.DOWNLOAD_FAILED);
    stmt.bindInt32Parameter(3, Ci.nsIDownloadManager.DOWNLOAD_CANCELED);
    buildDownloadList(stmt, gDownloadsOtherTitle);
  } finally {
    stmt.reset();
  }
}

/**
 * Builds the download list with an array of search terms.  This also changes
 * the label of the second group of downloads to search results (or the locale
 * equivalent).
 *
 * @param aTerms
 *        An array of search terms that will be checked for past downloads.  If
 *        this array is empty, we clear the search results and build the default
 *        view.
 */
function buildDownloadListWithSearch(aTerms)
{
  gSearching = true;
  gDownloadsOtherLabel.value = gDownloadsOtherLabel.getAttribute("searchlabel");

  // remove and trailing or leading whitespace first
  aTerms = aTerms.replace(/^\s+|\s+$/, "");
  if (aTerms.length == 0) {
    gSearching = false;
    gDownloadsOtherLabel.value = gDownloadsOtherLabel.getAttribute("completedlabel");
    buildDefaultView();
    return;
  }

  var sql = "SELECT id, target, name, source, state, startTime " +
            "FROM moz_downloads WHERE name LIKE ?1 ESCAPE '/' " +
            "AND state != ?2 AND state != ?3 ORDER BY endTime ASC";

  var db = gDownloadManager.DBConnection;
  var stmt = db.createStatement(sql);

  try {
    var paramForLike = stmt.escapeStringForLIKE(aTerms, '/');
    stmt.bindStringParameter(0, "%" + paramForLike + "%");
    stmt.bindInt32Parameter(1, Ci.nsIDownloadManager.DOWNLOAD_DOWNLOADING);
    stmt.bindInt32Parameter(2, Ci.nsIDownloadManager.DOWNLOAD_PAUSED);
    buildDownloadList(stmt, gDownloadsOtherTitle);
  } finally {
    stmt.reset();
  }
}

function performSearch() {
  buildDownloadListWithSearch(document.getElementById("searchbox").value);
}

function onSearchboxBlur() {
  var searchbox = document.getElementById("searchbox");
  if (searchbox.value == "") {
    searchbox.setAttribute("empty", "true");
    searchbox.value = searchbox.getAttribute("defaultValue");
  }
}

function onSearchboxFocus() {
  var searchbox = document.getElementById("searchbox");
  if (searchbox.hasAttribute("empty")) {
    searchbox.value = "";
    searchbox.removeAttribute("empty");
  }
}

// we should be using real URLs all the time, but until 
// bug 239948 is fully fixed, this will do...
function getLocalFileFromNativePathOrUrl(aPathOrUrl)
{
  if (aPathOrUrl.substring(0,7) == "file://") {

    // if this is a URL, get the file from that
    let ioSvc = Cc["@mozilla.org/network/io-service;1"].
                getService(Ci.nsIIOService);

    // XXX it's possible that using a null char-set here is bad
    const fileUrl = ioSvc.newURI(aPathOrUrl, null, null).
                    QueryInterface(Ci.nsIFileURL);
    return fileUrl.file.clone().QueryInterface(Ci.nsILocalFile);

  } else {

    // if it's a pathname, create the nsILocalFile directly
    var f = new nsLocalFile(aPathOrUrl);

    return f;
  }
}

