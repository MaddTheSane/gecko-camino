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
 * The Original Code is the Firefox Browser Glue Service.
 *
 * The Initial Developer of the Original Code is
 * Giorgio Maone
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Giorgio Maone <g.maone@informaction.com>
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

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;

// Constructor

function BrowserGlue() {
  this._init();
  this._profileStarted = false;
}

BrowserGlue.prototype = {
  _saveSession: false,

  QueryInterface: function(iid) 
  {
     xpcomCheckInterfaces(iid, kServiceIIds, Cr.NS_ERROR_NO_INTERFACE);
     return this;
  }
,
  // nsIObserver implementation 
  observe: function(subject, topic, data) 
  {
    switch(topic) {
      case "xpcom-shutdown":
        this._dispose();
        break;
      case "profile-before-change":
        this._onProfileChange();
        break;
      case "profile-change-teardown": 
        this._onProfileShutdown();
        break;
      case "final-ui-startup":
        this._onProfileStartup();
        break;
      case "browser:purge-session-history":
        // reset the console service's error buffer
        const cs = Cc["@mozilla.org/consoleservice;1"].
                   getService(Ci.nsIConsoleService);
        cs.logStringMessage(null); // clear the console (in case it's open)
        cs.reset();
        break;
      case "quit-application-requested":
        this._onQuitRequest(subject);
        break;
      case "quit-application-granted":
        if (this._saveSession) {
          var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                           getService(Ci.nsIPrefBranch);
          prefBranch.setBoolPref("browser.sessionstore.resume_session_once", true);
        }
        break;
    }
  }
, 
  // initialization (called on application startup) 
  _init: function() 
  {
    // observer registration
    const osvr = Cc['@mozilla.org/observer-service;1'].
                 getService(Ci.nsIObserverService);
    osvr.addObserver(this, "profile-before-change", false);
    osvr.addObserver(this, "profile-change-teardown", false);
    osvr.addObserver(this, "xpcom-shutdown", false);
    osvr.addObserver(this, "final-ui-startup", false);
    osvr.addObserver(this, "browser:purge-session-history", false);
    osvr.addObserver(this, "quit-application-requested", false);
    osvr.addObserver(this, "quit-application-granted", false);
  },

  // cleanup (called on application shutdown)
  _dispose: function() 
  {
    // observer removal 
    const osvr = Cc['@mozilla.org/observer-service;1'].
                 getService(Ci.nsIObserverService);
    osvr.removeObserver(this, "profile-before-change");
    osvr.removeObserver(this, "profile-change-teardown");
    osvr.removeObserver(this, "xpcom-shutdown");
    osvr.removeObserver(this, "final-ui-startup");
    osvr.removeObserver(this, "browser:purge-session-history");
    osvr.removeObserver(this, "quit-application-requested");
    osvr.removeObserver(this, "quit-application-granted");
  },

  // profile startup handler (contains profile initialization routines)
  _onProfileStartup: function() 
  {
    // check to see if the EULA must be shown on startup
    try {
      var mustDisplayEULA = true;
      var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                       getService(Ci.nsIPrefBranch);
      var EULAVersion = prefBranch.getIntPref("browser.EULA.version");
      mustDisplayEULA = !prefBranch.getBoolPref("browser.EULA." + EULAVersion + ".accepted");
    } catch(ex) {
    }

    if (mustDisplayEULA) {
      var ww2 = Cc["@mozilla.org/embedcomp/window-watcher;1"].
                getService(Ci.nsIWindowWatcher);
      ww2.openWindow(null, "chrome://browser/content/EULA.xul", 
                     "_blank", "chrome,centerscreen,modal,resizable=yes", null);
    }

    this.Sanitizer.onStartup();
    // check if we're in safe mode
    var app = Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULAppInfo).
              QueryInterface(Ci.nsIXULRuntime);
    if (app.inSafeMode) {
      var ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].
               getService(Ci.nsIWindowWatcher);
      ww.openWindow(null, "chrome://browser/content/safeMode.xul", 
                    "_blank", "chrome,centerscreen,modal,resizable=no", null);
    }

    // initialize Places
    this._initPlaces();

    // indicate that the profile was initialized
    this._profileStarted = true;
  },

  _onProfileChange: function()
  {
    // this block is for code that depends on _onProfileStartup() having 
    // been called.
    if (this._profileStarted) {
      // final places cleanup
      this._shutdownPlaces();
    }
  },

  // profile shutdown handler (contains profile cleanup routines)
  _onProfileShutdown: function() 
  {
    // here we enter last survival area, in order to avoid multiple
    // "quit-application" notifications caused by late window closings
    const appStartup = Cc['@mozilla.org/toolkit/app-startup;1'].
                       getService(Ci.nsIAppStartup);
    try {
      appStartup.enterLastWindowClosingSurvivalArea();

      this.Sanitizer.onShutdown();

    } catch(ex) {
    } finally {
      appStartup.exitLastWindowClosingSurvivalArea();
    }
  },

  _onQuitRequest: function(aCancelQuit)
  {
    var wm = Cc["@mozilla.org/appshell/window-mediator;1"].
             getService(Ci.nsIWindowMediator);
    var windowcount = 0;
    var pagecount = 0;
    var browserEnum = wm.getEnumerator("navigator:browser");
    while (browserEnum.hasMoreElements()) {
      windowcount++;

      var browser = browserEnum.getNext();
      var tabbrowser = browser.document.getElementById("content");
      if (tabbrowser)
        pagecount += tabbrowser.browsers.length;
    }

    this._saveSession = false;
    if (pagecount < 2)
      return;

    var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                     getService(Ci.nsIPrefBranch);
    var showPrompt = true;
    try {
      if (prefBranch.getIntPref("browser.startup.page") == 3 ||
          prefBranch.getBoolPref("browser.sessionstore.resume_session_once"))
        showPrompt = false;
      else
        showPrompt = prefBranch.getBoolPref("browser.warnOnQuit");
    } catch (ex) {}

    var buttonChoice = 0;
    if (showPrompt) {
      var bundleService = Cc["@mozilla.org/intl/stringbundle;1"].
                          getService(Ci.nsIStringBundleService);
      var quitBundle = bundleService.createBundle("chrome://browser/locale/quitDialog.properties");
      var brandBundle = bundleService.createBundle("chrome://branding/locale/brand.properties");

      var appName = brandBundle.GetStringFromName("brandShortName");
      var quitDialogTitle = quitBundle.formatStringFromName("quitDialogTitle",
                                                            [appName], 1);
      var quitTitle = quitBundle.GetStringFromName("quitTitle");
      var cancelTitle = quitBundle.GetStringFromName("cancelTitle");
      var saveTitle = quitBundle.GetStringFromName("saveTitle");
      var neverAskText = quitBundle.GetStringFromName("neverAsk");

      if (windowcount == 1)
        var message = quitBundle.formatStringFromName("messageNoWindows",
                                                      [appName], 1);
      else
        var message = quitBundle.formatStringFromName("message",
                                                      [appName], 1);

      var promptService = Cc["@mozilla.org/embedcomp/prompt-service;1"].
                          getService(Ci.nsIPromptService);

      var flags = promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_0 +
                  promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_1 +
                  promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_2 +
                  promptService.BUTTON_POS_0_DEFAULT;
      var neverAsk = {value:false};
      buttonChoice = promptService.confirmEx(null, quitDialogTitle, message,
                                   flags, quitTitle, cancelTitle, saveTitle,
                                   neverAskText, neverAsk);

      switch (buttonChoice) {
      case 0:
        if (neverAsk.value)
          prefBranch.setBoolPref("browser.warnOnQuit", false);
        break;
      case 1:
        aCancelQuit.QueryInterface(Ci.nsISupportsPRBool);
        aCancelQuit.data = true;
        break;
      case 2:
        // could also set browser.warnOnQuit to false here,
        // but not setting it is a little safer.
        if (neverAsk.value)
          prefBranch.setIntPref("browser.startup.page", 3);
        break;
      }

      this._saveSession = buttonChoice == 2;
    }
  },

  // returns the (cached) Sanitizer constructor
  get Sanitizer() 
  {
    if(typeof(Sanitizer) != "function") { // we should dynamically load the script
      Cc["@mozilla.org/moz/jssubscript-loader;1"].
      getService(Ci.mozIJSSubScriptLoader).
      loadSubScript("chrome://browser/content/sanitize.js", null);
    }
    return Sanitizer;
  },

  /**
   * Initialize Places
   * - imports the bookmarks html file if bookmarks datastore is empty
   */
  _initPlaces: function bg__initPlaces() {
#ifdef MOZ_PLACES_BOOKMARKS
    // we need to instantiate the history service before we check the 
    // the browser.places.importBookmarksHTML pref, as 
    // nsNavHistory::ForceMigrateBookmarksDB() will set that pref
    // if we need to force a migration (due to a schema change)
    var histsvc = Cc["@mozilla.org/browser/nav-history-service;1"].
                  getService(Ci.nsINavHistoryService);

    var importBookmarks = false;
    try {
      var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                       getService(Ci.nsIPrefBranch);
      importBookmarks = prefBranch.getBoolPref("browser.places.importBookmarksHTML");
    } catch(ex) {}

    if (!importBookmarks)
      return;

    var dirService = Cc["@mozilla.org/file/directory_service;1"].
                     getService(Ci.nsIProperties);

    var bookmarksFile = dirService.get("BMarks", Ci.nsILocalFile);

    if (bookmarksFile.exists()) {
      // import the file
      try {
        var importer = 
          Cc["@mozilla.org/browser/places/import-export-service;1"].
          getService(Ci.nsIPlacesImportExportService);
        importer.importHTMLFromFile(bookmarksFile, true);
      } catch(ex) {
      } finally {
        prefBranch.setBoolPref("browser.places.importBookmarksHTML", false);
      }

      // backup pre-places bookmarks.html
      // XXXtodo remove this before betas, after import/export is solid
      var profDir = dirService.get("ProfD", Ci.nsILocalFile);
      var bookmarksBackup = profDir.clone();
      bookmarksBackup.append("bookmarks.preplaces.html");
      if (!bookmarksBackup.exists()) {
        // save old bookmarks.html file as bookmarks.preplaces.html
        try {
          bookmarksFile.copyTo(profDir, "bookmarks.preplaces.html");
        } catch(ex) {
          dump("nsBrowserGlue::_initPlaces(): copy of bookmarks.html to bookmarks.preplaces.html failed: " + ex + "\n");
        }
      }
    }
#endif
  },

  /**
   * Places shut-down tasks
   * - back up and archive bookmarks
   */
  _shutdownPlaces: function bg__shutdownPlaces() {
#ifdef MOZ_PLACES_BOOKMARKS
    // backup bookmarks to bookmarks.html
    var importer =
      Cc["@mozilla.org/browser/places/import-export-service;1"].
      getService(Ci.nsIPlacesImportExportService);
    importer.backupBookmarksFile();
#endif
  },
  
  // ------------------------------
  // public nsIBrowserGlue members
  // ------------------------------
  
  sanitize: function(aParentWindow) 
  {
    this.Sanitizer.sanitize(aParentWindow);
  }
}


// XPCOM Scaffolding code

// component defined in this file

const kServiceName = "Firefox Browser Glue Service";
const kServiceId = "{eab9012e-5f74-4cbc-b2b5-a590235513cc}";
const kServiceCtrId = "@mozilla.org/browser/browserglue;1";
const kServiceConstructor = BrowserGlue;

const kServiceCId = Components.ID(kServiceId);

// interfaces implemented by this component
const kServiceIIds = [ 
  Ci.nsIObserver,
  Ci.nsISupports,
  Ci.nsISupportsWeakReference,
  Ci.nsIBrowserGlue
  ];

// categories which this component is registered in
const kServiceCats = ["app-startup"];

// Factory object
const kServiceFactory = {
  _instance: null,
  createInstance: function (outer, iid) 
  {
    if (outer != null) throw Cr.NS_ERROR_NO_AGGREGATION;

    xpcomCheckInterfaces(iid, kServiceIIds, 
                          Cr.NS_ERROR_INVALID_ARG);
    return this._instance == null ?
      this._instance = new kServiceConstructor() : this._instance;
  }
};

function xpcomCheckInterfaces(iid, iids, ex) {
  for (var j = iids.length; j-- >0;) {
    if (iid.equals(iids[j])) return true;
  }
  throw ex;
}

// Module

var Module = {
  registered: false,
  
  registerSelf: function(compMgr, fileSpec, location, type) 
  {
    if (!this.registered) {
      compMgr.QueryInterface(Ci.nsIComponentRegistrar)
             .registerFactoryLocation(kServiceCId,
                                      kServiceName,
                                      kServiceCtrId, 
                                      fileSpec,
                                      location, 
                                      type);
      const catman = Cc['@mozilla.org/categorymanager;1'].
                     getService(Ci.nsICategoryManager);
      var len = kServiceCats.length;
      for (var j = 0; j < len; j++) {
        catman.addCategoryEntry(kServiceCats[j],
          kServiceCtrId, kServiceCtrId, true, true);
      }
      this.registered = true;
    } 
  },
  
  unregisterSelf: function(compMgr, fileSpec, location) 
  {
    compMgr.QueryInterface(Ci.nsIComponentRegistrar)
           .unregisterFactoryLocation(kServiceCId, fileSpec);
    const catman = Cc['@mozilla.org/categorymanager;1'].
                   getService(Ci.nsICategoryManager);
    var len = kServiceCats.length;
    for (var j = 0; j < len; j++) {
      catman.deleteCategoryEntry(kServiceCats[j], kServiceCtrId, true);
    }
  },
  
  getClassObject: function(compMgr, cid, iid) 
  {
    if(cid.equals(kServiceCId))
      return kServiceFactory;
    
    throw Cr[
      iid.equals(Ci.nsIFactory)
      ? "NS_ERROR_NO_INTERFACE"
      : "NS_ERROR_NOT_IMPLEMENTED"
    ];
    
  },
  
  canUnload: function(compMgr) 
  {
    return true;
  }
};

// entrypoint
function NSGetModule(compMgr, fileSpec) {
  return Module;
}
