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
# The Original Code is the Browser Search Service.
#
# The Initial Developer of the Original Code is
# Giorgio Maone
# Portions created by the Initial Developer are Copyright (C) 2005
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Giorgio Maone <g.maone@informaction.com>
#   Seth Spitzer <sspitzer@mozilla.com>
#   Asaf Romano <mano@mozilla.com>
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

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource:///modules/distribution.js");

// Factory object
const BrowserGlueServiceFactory = {
  _instance: null,
  createInstance: function (outer, iid) 
  {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    return this._instance == null ?
      this._instance = new BrowserGlue() : this._instance;
  }
};

// Constructor

function BrowserGlue() {
  this._init();
}

BrowserGlue.prototype = {
  _saveSession: false,

  _setPrefToSaveSession: function()
  {
    var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                     getService(Ci.nsIPrefBranch);
    prefBranch.setBoolPref("browser.sessionstore.resume_session_once", true);
  },

  // nsIObserver implementation 
  observe: function(subject, topic, data) 
  {
    switch(topic) {
      case "xpcom-shutdown":
        this._dispose();
        break;
      case "quit-application": 
        this._onProfileShutdown();
        break;
      case "prefservice:after-app-defaults":
        this._onAppDefaults();
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
        this._onQuitRequest(subject, data);
        break;
      case "quit-application-granted":
        if (this._saveSession) {
          this._setPrefToSaveSession();
        }
        break;
      case "session-save":
        this._setPrefToSaveSession();
        subject.QueryInterface(Ci.nsISupportsPRBool);
        subject.data = true;
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
    osvr.addObserver(this, "quit-application", false);
    osvr.addObserver(this, "xpcom-shutdown", false);
    osvr.addObserver(this, "prefservice:after-app-defaults", false);
    osvr.addObserver(this, "final-ui-startup", false);
    osvr.addObserver(this, "browser:purge-session-history", false);
    osvr.addObserver(this, "quit-application-requested", false);
    osvr.addObserver(this, "quit-application-granted", false);
    osvr.addObserver(this, "session-save", false);
  },

  // cleanup (called on application shutdown)
  _dispose: function() 
  {
    // observer removal 
    const osvr = Cc['@mozilla.org/observer-service;1'].
                 getService(Ci.nsIObserverService);
    osvr.removeObserver(this, "quit-application");
    osvr.removeObserver(this, "xpcom-shutdown");
    osvr.removeObserver(this, "prefservice:after-app-defaults");
    osvr.removeObserver(this, "final-ui-startup");
    osvr.removeObserver(this, "browser:purge-session-history");
    osvr.removeObserver(this, "quit-application-requested");
    osvr.removeObserver(this, "quit-application-granted");
    osvr.removeObserver(this, "session-save");
  },

  _onAppDefaults: function()
  {
    // apply distribution customizations (prefs)
    // other customizations are applied in _onProfileStartup()
    var distro = new DistributionCustomizer();
    distro.applyPrefDefaults();
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

    // apply distribution customizations
    // prefs are applied in _onAppDefaults()
    var distro = new DistributionCustomizer();
    distro.applyCustomizations();

    // handle any UI migration
    this._migrateUI();
  },

  // profile shutdown handler (contains profile cleanup routines)
  _onProfileShutdown: function() 
  {
    this._shutdownPlaces();
    this.Sanitizer.onShutdown();
  },

  _onQuitRequest: function(aCancelQuit, aQuitType)
  {
    // If user has already dismissed quit request, then do nothing
    if ((aCancelQuit instanceof Ci.nsISupportsPRBool) && aCancelQuit.data)
      return;

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

    if (aQuitType != "restart")
      aQuitType = "quit";

    var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                     getService(Ci.nsIPrefBranch);
    var showPrompt = true;
    try {
      if (prefBranch.getIntPref("browser.startup.page") == 3 ||
          prefBranch.getBoolPref("browser.sessionstore.resume_session_once"))
        showPrompt = false;
      else
        showPrompt = aQuitType == "restart" ?
                     prefBranch.getBoolPref("browser.warnOnRestart") :
                     prefBranch.getBoolPref("browser.warnOnQuit");
    } catch (ex) {}

    var buttonChoice = 0;
    if (showPrompt) {
      var bundleService = Cc["@mozilla.org/intl/stringbundle;1"].
                          getService(Ci.nsIStringBundleService);
      var quitBundle = bundleService.createBundle("chrome://browser/locale/quitDialog.properties");
      var brandBundle = bundleService.createBundle("chrome://branding/locale/brand.properties");

      var appName = brandBundle.GetStringFromName("brandShortName");
      var quitDialogTitle = quitBundle.formatStringFromName(aQuitType + "DialogTitle",
                                                              [appName], 1);

      var message;
      if (aQuitType == "restart")
        message = quitBundle.formatStringFromName("messageRestart",
                                                  [appName], 1);
      else if (windowcount == 1)
        message = quitBundle.formatStringFromName("messageNoWindows",
                                                  [appName], 1);
      else
        message = quitBundle.formatStringFromName("message",
                                                  [appName], 1);

      var promptService = Cc["@mozilla.org/embedcomp/prompt-service;1"].
                          getService(Ci.nsIPromptService);

      var flags = promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_0 +
                  promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_1 +
                  promptService.BUTTON_POS_0_DEFAULT;

      var neverAsk = {value:false};
      var button0Title, button2Title;
      var button1Title = quitBundle.GetStringFromName("cancelTitle");
      var neverAskText = quitBundle.GetStringFromName("neverAsk");

      if (aQuitType == "restart")
        button0Title = quitBundle.GetStringFromName("restartTitle");
      else {
        flags += promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_2;
        button0Title = quitBundle.GetStringFromName("saveTitle");
        button2Title = quitBundle.GetStringFromName("quitTitle");
      }

      buttonChoice = promptService.confirmEx(null, quitDialogTitle, message,
                                   flags, button0Title, button1Title, button2Title,
                                   neverAskText, neverAsk);

      switch (buttonChoice) {
      case 2:
        if (neverAsk.value)
          prefBranch.setBoolPref("browser.warnOnQuit", false);
        break;
      case 1:
        aCancelQuit.QueryInterface(Ci.nsISupportsPRBool);
        aCancelQuit.data = true;
        break;
      case 0:
        this._saveSession = true;
        if (neverAsk.value) {
          if (aQuitType == "restart")
            prefBranch.setBoolPref("browser.warnOnRestart", false);
          else {
            // could also set browser.warnOnQuit to false here,
            // but not setting it is a little safer.
            prefBranch.setIntPref("browser.startup.page", 3);
          }
        }
        break;
      }
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

    if (!importBookmarks) {
      // Call it here for Fx3 profiles created before the Places folder
      // has been added, otherwise it's called during import.
      this.ensurePlacesDefaultQueriesInitialized();
      return;
    }

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

      // only back up pre-places bookmarks.html if we plan on overwriting it
      if (prefBranch.getBoolPref("browser.bookmarks.overwrite")) {
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
    }
  },

  /**
   * Places shut-down tasks
   * - back up and archive bookmarks
   */
  _shutdownPlaces: function bg__shutdownPlaces() {
    // backup bookmarks to bookmarks.html
    var importer =
      Cc["@mozilla.org/browser/places/import-export-service;1"].
      getService(Ci.nsIPlacesImportExportService);
    importer.backupBookmarksFile();
  },

  _migrateUI: function bg__migrateUI() {
    var prefBranch = Cc["@mozilla.org/preferences-service;1"].getService(Ci.nsIPrefBranch);

    var migration = 0;
    try {
      migration = prefBranch.getIntPref("browser.migration.version");
    } catch(ex) {}

    if (migration == 0) {
      // this code should always migrate pre-FF3 profiles to the current UI state

      // grab the localstore.rdf and make changes needed for new UI
      this._rdf = Cc["@mozilla.org/rdf/rdf-service;1"].getService(Ci.nsIRDFService);
      this._dataSource = this._rdf.GetDataSource("rdf:local-store");
      this._dirty = false;

      var currentSet = this._rdf.GetResource("currentset");
      var collapsed = this._rdf.GetResource("collapsed");
      var target;
      var moveHome;

      // get an nsIRDFResource for the PersonalToolbar item
      var personalBar = this._rdf.GetResource("chrome://browser/content/browser.xul#PersonalToolbar");
      var personalBarCollapsed = this._getPersist(personalBar, collapsed) == "true";

      // get an nsIRDFResource for the nav-bar item
      var navBar = this._rdf.GetResource("chrome://browser/content/browser.xul#nav-bar");
      target = this._getPersist(navBar, currentSet);
      if (target) {
        // move Home if we find it in the nav-bar and the personal toolbar isn't collapsed
        moveHome = !personalBarCollapsed && (target.indexOf("home-button") != -1);
        if (moveHome)
          target = target.replace("home-button", "");

        // add the new combined back and forward button
        target = "unified-back-forward-button," + target;

        this._setPersist(navBar, currentSet, target);
      } else {
        // nav-bar doesn't have a currentset, so the defaultset will be used,
        // which means Home will be moved
        moveHome = true;
      }

      if (moveHome) {
        // If the personal toolbar has a currentset, add Home. The defaultset will be
        // used otherwise.
        target = this._getPersist(personalBar, currentSet);
        if (target && target.indexOf("home-button") == -1)
          this._setPersist(personalBar, currentSet, "home-button," + target);

        // uncollapse the personal toolbar
        if (personalBarCollapsed)
          this._setPersist(personalBar, collapsed, "false");
      }

      // force the RDF to be saved
      if (this._dirty)
        this._dataSource.QueryInterface(Ci.nsIRDFRemoteDataSource).Flush();

      // free up the RDF service
      this._rdf = null;
      this._dataSource = null;

      // update the migration version
      prefBranch.setIntPref("browser.migration.version", 1);
    }
  },

  _getPersist: function bg__getPersist(aSource, aProperty) {
    var target = this._dataSource.GetTarget(aSource, aProperty, true);
    if (target instanceof Ci.nsIRDFLiteral)
      return target.Value;
    return null;
  },

  _setPersist: function bg__setPersist(aSource, aProperty, aTarget) {
    this._dirty = true;
    try {
      var oldTarget = this._dataSource.GetTarget(aSource, aProperty, true);
      if (oldTarget) {
        if (aTarget)
          this._dataSource.Change(aSource, aProperty, oldTarget, this._rdf.GetLiteral(aTarget));
        else
          this._dataSource.Unassert(aSource, aProperty, oldTarget);
      }
      else {
        this._dataSource.Assert(aSource, aProperty, this._rdf.GetLiteral(aTarget), true);
      }
    }
    catch(ex) {}
  },

  // ------------------------------
  // public nsIBrowserGlue members
  // ------------------------------
  
  sanitize: function(aParentWindow) 
  {
    this.Sanitizer.sanitize(aParentWindow);
  },

  ensurePlacesDefaultQueriesInitialized: function() {
    // bail out if the folder is already created
    var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                     getService(Ci.nsIPrefBranch);
    var createdSmartBookmarks = false;
    try {
      createdSmartBookmarks = prefBranch.getBoolPref("browser.places.createdSmartBookmarks");
    } catch(ex) { }

    if (createdSmartBookmarks)
      return;

    var bmsvc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
                getService(Ci.nsINavBookmarksService);

    var callback = {
      _placesBundle: Cc["@mozilla.org/intl/stringbundle;1"].
                     getService(Ci.nsIStringBundleService).
                     createBundle("chrome://browser/locale/places/places.properties"),

      _uri: function(aSpec) {
        return Cc["@mozilla.org/network/io-service;1"].
               getService(Ci.nsIIOService).
               newURI(aSpec, null, null);
      },

      runBatched: function() {
        var smartBookmarksFolderTitle =
          this._placesBundle.GetStringFromName("smartBookmarksFolderTitle");
        var mostVisitedTitle =
          this._placesBundle.GetStringFromName("mostVisitedTitle");
        var recentlyBookmarkedTitle =
          this._placesBundle.GetStringFromName("recentlyBookmarkedTitle");
        var recentTagsTitle =
          this._placesBundle.GetStringFromName("recentTagsTitle");

        var bookmarksMenuFolder = bmsvc.bookmarksMenuFolder;
        var unfiledBookmarksFolder = bmsvc.unfiledBookmarksFolder;
        var toolbarFolder = bmsvc.toolbarFolder;
        var tagsFolder = bmsvc.tagsFolder;
        var defaultIndex = bmsvc.DEFAULT_INDEX;

        // index = 0, make it the first folder
        var placesFolder = bmsvc.createFolder(toolbarFolder, smartBookmarksFolderTitle,
                                              0);

        // XXX should this be a pref?  see bug #399268
        var maxResults = 10;

        var mostVisitedItem = bmsvc.insertBookmark(placesFolder,
          this._uri("place:queryType=" +
              Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY +
              "&sort=" +
              Ci.nsINavHistoryQueryOptions.SORT_BY_VISITCOUNT_DESCENDING +
              "&maxResults=" + maxResults),
              defaultIndex, mostVisitedTitle);

        // excludeQueries=1 so that user created "saved searches" 
        // and these queries (added automatically) are excluded
        var recentlyBookmarkedItem = bmsvc.insertBookmark(placesFolder,
          this._uri("place:folder=" + bookmarksMenuFolder + 
              "&folder=" + unfiledBookmarksFolder +
              "&folder=" + toolbarFolder +
              "&queryType=" + Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS +
              "&sort=" +
              Ci.nsINavHistoryQueryOptions.SORT_BY_DATEADDED_DESCENDING +
              "&excludeItemIfParentHasAnnotation=livemark%2FfeedURI" +
              "&maxResults=" + maxResults +
              "&excludeQueries=1"),
              defaultIndex, recentlyBookmarkedTitle);

        var sep =  bmsvc.insertSeparator(placesFolder, defaultIndex);

        var recentTagsItem = bmsvc.insertBookmark(placesFolder,
          this._uri("place:folder=" + tagsFolder +
              "&group=" + Ci.nsINavHistoryQueryOptions.GROUP_BY_FOLDER +
              "&queryType=" + Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS +
              "&applyOptionsToContainers=1" +
              "&sort=" +
              Ci.nsINavHistoryQueryOptions.SORT_BY_DATEADDED_DESCENDING +
              "&maxResults=" + maxResults),
              defaultIndex, recentTagsTitle);
      }
    };

    try {
      bmsvc.runInBatchMode(callback, null);
    }
    catch(ex) {
      Components.utils.reportError(ex);
    }
    finally {
      prefBranch.setBoolPref("browser.places.createdSmartBookmarks", true);
      prefBranch.QueryInterface(Ci.nsIPrefService).savePrefFile(null);
    }
  },

  // for XPCOM
  classDescription: "Firefox Browser Glue Service",
  classID:          Components.ID("{eab9012e-5f74-4cbc-b2b5-a590235513cc}"),
  contractID:       "@mozilla.org/browser/browserglue;1",

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference,
                                         Ci.nsIBrowserGlue]),

  // redefine the default factory for XPCOMUtils
  _xpcom_factory: BrowserGlueServiceFactory,

  // get this contractID registered for certain categories via XPCOMUtils
  _xpcom_categories: [
    // make BrowserGlue a startup observer
    { category: "app-startup", service: true }
  ]
}

//module initialization
function NSGetModule(aCompMgr, aFileSpec) {
  return XPCOMUtils.generateModule([BrowserGlue]);
}
