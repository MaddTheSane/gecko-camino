/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is the Places Bookmark Properties dialog.
 *
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Joe Hughes <jhughes@google.com>
 *   Dietrich Ayala <dietrich@mozilla.com>
 *   Asaf Romano <mano@mozilla.com>
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

/**
 * The panel is initialized based on data given in the js object passed
 * as window.arguments[0]. The object must have the following fields set:
 *   @ action (String). Possible values:
 *     - "add" - for adding a new item.
 *       @ type (String). Possible values:
 *         - "bookmark"
 *           @ loadBookmarkInSidebar - optional, the default state for the
 *             "Load this bookmark in the sidebar" field.
 *         - "folder"
 *           @ URIList (Array of nsIURI objects) - optional, list of uris to
 *             be bookmarked under the new folder.
 *         - "livemark"
 *       @ uri (nsIURI object) - optional, the default uri for the new item.
 *         The property is not used for the "folder with items" type.
 *       @ title (String) - optional, the defualt title for the new item.
 *       @ description (String) - optional, the default description for the new
 *         item.
 *       @ defaultInsertionPoint (InsertionPoint JS object) - optional, the
 *         default insertion point for the new item.
 *       @ keyword (String) - optional, the default keyword for the new item.
 *       @ postData (String) - optional, POST data to accompany the keyword.
 *      Notes:
 *        1) If |uri| is set for a bookmark/livemark item and |title| isn't,
 *           the dialog will query the history tables for the title associated
 *           with the given uri. If the dialog is set to adding a folder with
 *           bookmark items under it (see URIList), a default static title is
 *           used ("[Folder Name]").
 *        2) The index field of the the default insertion point is ignored if
 *           the folder picker is shown.
 *     - "edit" - for editing a bookmark item or a folder.
 *       @ type (String). Possible values:
 *         - "bookmark"
 *           @ bookmarkId (Integer) - the id of the bookmark item.
 *         - "folder" (also applies to livemarks)
 *           @ folderId (Integer) - the id of the folder.
 *   @ hiddenRows (Strings array) - optional, list of rows to be hidden
 *     regardless of the item edited or added by the dialog.
 *     Possible values:
 *     - "title"
 *     - "location"
 *     - "description"
 *     - "keyword"
 *     - "load in sidebar"
 *     - "feedURI"
 *     - "siteURI"
 *     - "folder picker" - hides both the tree and the menu.
 *
 * window.arguments[0].performed is set to true if any transaction has
 * been performed by the dialog.
 */

const LAST_USED_ANNO = "bookmarkPropertiesDialog/lastUsed";

// This doesn't include "static" special folders (first two menu items)
const MAX_FOLDER_ITEM_IN_MENU_LIST = 5;

const BOOKMARK_ITEM = 0;
const BOOKMARK_FOLDER = 1;
const LIVEMARK_CONTAINER = 2;

const ACTION_EDIT = 0;
const ACTION_ADD = 1;

var BookmarkPropertiesPanel = {

  /** UI Text Strings */
  __strings: null,
  get _strings() {
    if (!this.__strings) {
      this.__strings = document.getElementById("stringBundle");
    }
    return this.__strings;
  },

  /**
   * The Microsummary Service for displaying microsummaries.
   */
  __mss: null,
  get _mss() {
    if (!this.__mss)
      this.__mss = Cc["@mozilla.org/microsummary/service;1"].
                  getService(Ci.nsIMicrosummaryService);
    return this.__mss;
  },

  _action: null,
  _itemType: null,
  _folderId: null,
  _bookmarkId: -1,
  _bookmarkURI: null,
  _loadBookmarkInSidebar: false,
  _itemTitle: "",
  _itemDescription: "",
  _microsummaries: null,
  _URIList: null,
  _postData: null,

  // sizeToContent is not usable due to bug 90276, so we'll use resizeTo
  // instead and cache the bookmarks tree view size. See WSucks in the legacy
  // UI code (addBookmark2.js).
  //
  // XXXmano: this doesn't work as expected yet, need to figure out if we're
  // facing cocoa-widget resizeTo issue here.
  _folderTreeHeight: null,

  /**
   * This method returns the correct label for the dialog's "accept"
   * button based on the variant of the dialog.
   */
  _getAcceptLabel: function BPP__getAcceptLabel() {
    if (this._action == ACTION_ADD) {
      if (this._URIList)
        return this._strings.getString("dialogAcceptLabelAddMulti");

      return this._strings.getString("dialogAcceptLabelAddItem");
    }
    return this._strings.getString("dialogAcceptLabelEdit");
  },

  /**
   * This method returns the correct title for the current variant
   * of this dialog.
   */
  _getDialogTitle: function BPP__getDialogTitle() {
    if (this._action == ACTION_ADD) {
      if (this._itemType == BOOKMARK_ITEM)
        return this._strings.getString("dialogTitleAddBookmark");
      if (this._itemType == LIVEMARK_CONTAINER)
        return this._strings.getString("dialogTitleAddLivemark");

      // folder
      NS_ASSERT(this._itemType == BOOKMARK_FOLDER, "bogus item type");
      if (this._URIList)
        return this._strings.getString("dialogTitleAddMulti");

      return this._strings.getString("dialogTitleAddFolder");
    } 
    if (this._action == ACTION_EDIT) {
      return this._strings
                 .getFormattedString("dialogTitleEdit", [this._itemTitle]);
    }
  },

  /**
   * Determines the initial data for the item edited or added by this dialog
   */
  _determineItemInfo: function BPP__determineItemInfo() {
    var dialogInfo = window.arguments[0];
    NS_ASSERT("action" in dialogInfo, "missing action property");
    var action = dialogInfo.action;

    if (action == "add") {
      NS_ASSERT("type" in dialogInfo, "missing type property for add action");

      if ("title" in dialogInfo)
        this._itemTitle = dialogInfo.title;
      if ("defaultInsertionPoint" in dialogInfo)
        this._defaultInsertionPoint = dialogInfo.defaultInsertionPoint;
      else {
        // default to the bookmarks root
        this._defaultInsertionPoint =
          new InsertionPoint(PlacesUtils.bookmarks.bookmarksRoot, -1);
      }

      switch(dialogInfo.type) {
        case "bookmark":
          this._action = ACTION_ADD;
          this._itemType = BOOKMARK_ITEM;
          if ("uri" in dialogInfo) {
            NS_ASSERT(dialogInfo.uri instanceof Ci.nsIURI,
                      "uri property should be a uri object");
            this._bookmarkURI = dialogInfo.uri;
          }
          if (typeof(this._itemTitle) != "string") {
            if (this._bookmarkURI) {
              this._itemTitle =
                this._getURITitleFromHistory(this._bookmarkURI);
              if (!this._itemTitle)
                this._itemTitle = this._bookmarkURI.spec;
            }
            else
              this._itemTitle = this._strings.getString("newBookmarkDefault");
          }

          if ("loadBookmarkInSidebar" in dialogInfo)
            this._loadBookmarkInSidebar = dialogInfo.loadBookmarkInSidebar;

          if ("keyword" in dialogInfo) {
            this._bookmarkKeyword = dialogInfo.keyword;
            if ("postData" in dialogInfo)
              this._postData = dialogInfo.postData;
          }

          break;
        case "folder":
          this._action = ACTION_ADD;
          this._itemType = BOOKMARK_FOLDER;
          if (!this._itemTitle) {
            if ("URIList" in dialogInfo) {
              this._itemTitle =
                this._strings.getString("bookmarkAllTabsDefault");
              this._URIList = dialogInfo.URIList;
            }
            else
              this._itemTitle = this._strings.getString("newFolderDefault");
          }
          break;
        case "livemark":
          this._action = ACTION_ADD;
          this._itemType = LIVEMARK_CONTAINER;
          if ("feedURI" in dialogInfo)
            this._feedURI = dialogInfo.feedURI;
          if ("siteURI" in dialogInfo)
            this._siteURI = dialogInfo.siteURI;

          if (!this._itemTitle) {
            if (this._feedURI) {
              this._itemTitle =
                this._getURITitleFromHistory(this._feedURI);
              if (!this._itemTitle)
                this._itemTitle = this._feedURI.spec;
            }
            else
              this._itemTitle = this._strings.getString("newLivemarkDefault");
          }
      }

      if ("description" in dialogInfo)
        this._itemDescription = dialogInfo.description;
    }
    else { // edit
      const annos = PlacesUtils.annotations;
      const bookmarks = PlacesUtils.bookmarks;

      switch (dialogInfo.type) {
        case "bookmark":
          NS_ASSERT("bookmarkId" in dialogInfo);

          this._action = ACTION_EDIT;
          this._itemType = BOOKMARK_ITEM;
          this._bookmarkId = dialogInfo.bookmarkId;

          this._bookmarkURI = bookmarks.getBookmarkURI(this._bookmarkId);
          this._itemTitle = bookmarks.getItemTitle(this._bookmarkId);

          // keyword
          this._bookmarkKeyword =
            bookmarks.getKeywordForBookmark(this._bookmarkId);

          // Load In Sidebar
          this._loadBookmarkInSidebar =
            annos.itemHasAnnotation(this._bookmarkId, LOAD_IN_SIDEBAR_ANNO);

          break;
        case "folder":
          NS_ASSERT("folderId" in dialogInfo);

          this._action = ACTION_EDIT;
          this._folderId = dialogInfo.folderId;

          const livemarks = PlacesUtils.livemarks;
          if (livemarks.isLivemark(this._folderId)) {
            this._itemType = LIVEMARK_CONTAINER;
            this._feedURI = livemarks.getFeedURI(this._folderId);
            this._siteURI = livemarks.getSiteURI(this._folderId);
          }
          else
            this._itemType = BOOKMARK_FOLDER;
          this._itemTitle = bookmarks.getItemTitle(this._folderId);
          break;
      }

      // Description
      // XXXmano: unify the two id fields
      var itemId = dialogInfo.type == "bookmark" ? this._bookmarkId : this._folderId;
      if (annos.itemHasAnnotation(itemId, DESCRIPTION_ANNO)) {
        this._itemDescription = annos.getItemAnnotationString(itemId,
                                                              DESCRIPTION_ANNO);
      }
    }
  },

  /**
   * This method returns the title string corresponding to a given URI.
   * If none is available from the bookmark service (probably because
   * the given URI doesn't appear in bookmarks or history), we synthesize
   * a title from the first 100 characters of the URI.
   *
   * @param aURI
   *        nsIURI object for which we want the title
   *
   * @returns a title string
   */
  _getURITitleFromHistory: function BPP__getURITitleFromHistory(aURI) {
    NS_ASSERT(aURI instanceof Ci.nsIURI);

    // get the title from History
    return PlacesUtils.history.getPageTitle(aURI);
  },

  /**
   * This method should be called by the onload of the Bookmark Properties
   * dialog to initialize the state of the panel.
   */
  onDialogLoad: function BPP_onDialogLoad() {
    this._tm = window.opener.PlacesUtils.tm;

    this._determineItemInfo();
    this._populateProperties();
    this._forceHideRows();
    this.validateChanges();

    this._folderMenuList = this._element("folderMenuList");
    this._folderTree = this._element("folderTree");
    if (isElementVisible(this._folderMenuList))
      this._initFolderMenuList();

    window.sizeToContent();

    // read the persisted attribute.
    this._folderTreeHeight = parseInt(this._folderTree.getAttribute("height"));
  },

  /**
   * Appends a menu-item representing a bookmarks folder to a menu-popup.
   * @param aMenupopup
   *        The popup to which the menu-item should be added.
   * @param aFolderId
   *        The identifier of the bookmarks folder.
   * @return the new menu item.
   */
  _appendFolderItemToMenupopup:
  function BPP__appendFolderItemToMenuList(aMenupopup, aFolderId) {
    // First make sure the folders-separator is visible
    this._element("foldersSeparator").hidden = false;

    var folderMenuItem = document.createElement("menuitem");
    var folderTitle = PlacesUtils.bookmarks.getItemTitle(aFolderId)
    folderMenuItem.folderId = aFolderId;
    folderMenuItem.setAttribute("label", folderTitle);
    folderMenuItem.className = "menuitem-iconic folder-icon";
    aMenupopup.appendChild(folderMenuItem);
    return folderMenuItem;
  },

  _initFolderMenuList: function BPP__initFolderMenuList() {
    // List of recently used folders:
    var annos = PlacesUtils.annotations;
    var folderIds = annos.getItemsWithAnnotation(LAST_USED_ANNO, { });

    // Hide the folders-separator if no folder is annotated as recently-used
    if (folderIds.length == 0) {
      this._element("foldersSeparator").hidden = true;
      return;
    }

    /**
     * The value of the LAST_USED_ANNO annotation is the time (in the form of
     * Date.getTime) at which the folder has been last used.
     *
     * First we build the annotated folders array, each item has both the
     * folder identifier and the time at which it was last-used by this dialog
     * set. Then we sort it descendingly based on the time field.
     */
    var folders = [];
    for (var i=0; i < folderIds.length; i++) {
      var lastUsed = annos.getItemAnnotationInt64(folderIds[i], LAST_USED_ANNO);
      folders.push({ folderId: folderIds[i], lastUsed: lastUsed });
    }
    folders.sort(function(a, b) {
      if (b.lastUsed < a.lastUsed)
        return -1;
      if (b.lastUsed > a.lastUsed)
        return 1;
      return 0;
    });

    var numberOfItems = Math.min(MAX_FOLDER_ITEM_IN_MENU_LIST, folders.length);
    var menupopup = this._folderMenuList.menupopup;
    for (i=0; i < numberOfItems; i++) {
      this._appendFolderItemToMenupopup(menupopup, folders[i].folderId);
    }

    var defaultItem =
      this._getFolderMenuItem(this._defaultInsertionPoint.itemId, true);
    this._folderMenuList.selectedItem = defaultItem;
  },

  QueryInterface: function BPP_QueryInterface(aIID) {
    if (aIID.equals(Ci.nsIMicrosummaryObserver) ||
        aIID.eqauls(Ci.nsISupports))
      return this;

    throw Cr.NS_ERROR_NO_INTERFACE;
  },

  _element: function BPP__element(aID) {
    return document.getElementById(aID);
  },

  /**
   * Hides fields which were explicitly set hidden by the the dialog opener
   * (see documentation at the top of this file).
   */
  _forceHideRows: function BPP__forceHideRows() {
    var hiddenRows = window.arguments[0].hiddenRows;
    if (!hiddenRows)
      return;

    if (hiddenRows.indexOf("title") != -1)
      this._element("namePicker").hidden = true;
    if (hiddenRows.indexOf("location") != -1)
      this._element("locationRow").hidden = true;
    if (hiddenRows.indexOf("keyword") != -1)
      this._element("keywordRow").hidden = true;
    if (hiddenRows.indexOf("description")!= -1)
      this._element("descriptionRow").hidden = true;
    if (hiddenRows.indexOf("folder picker") != -1)
      this._element("folderRow").hidden = true;
    if (hiddenRows.indexOf("feedURI") != -1)
      this._element("livemarkFeedLocationRow").hidden = true;
    if (hiddenRows.indexOf("siteURI") != -1)
      this._element("livemarkSiteLocationRow").hidden = true;
    if (hiddenRows.indexOf("load in sidebar") != -1)
      this._element("loadInSidebarCheckbox").hidden = true;
  },

  /**
   * This method fills in the data values for the fields in the dialog.
   */
  _populateProperties: function BPP__populateProperties() {
    document.title = this._getDialogTitle();
    document.documentElement.getButton("accept").label = this._getAcceptLabel();

    this._initNamePicker();
    this._element("descriptionTextfield").value = this._itemDescription;

    if (this._itemType == BOOKMARK_ITEM) {
      if (this._bookmarkURI)
        this._element("editURLBar").value = this._bookmarkURI.spec;

      if (typeof(this._bookmarkKeyword) == "string")
        this._element("keywordTextfield").value = this._bookmarkKeyword;

      if (this._loadBookmarkInSidebar)
        this._element("loadInSidebarCheckbox").checked = true;
    }
    else {
      this._element("locationRow").hidden = true;
      this._element("keywordRow").hidden = true;
      this._element("loadInSidebarCheckbox").hidden = true;
    }

    if (this._itemType == LIVEMARK_CONTAINER) {
      if (this._feedURI)
        this._element("feedLocationTextfield").value = this._feedURI.spec;
      if (this._siteURI)
        this._element("feedSiteLocationTextfield").value = this._siteURI.spec;
    }
    else {
      this._element("livemarkFeedLocationRow").hidden = true;
      this._element("livemarkSiteLocationRow").hidden = true;
    }

    if (this._action == ACTION_EDIT)
      this._element("folderRow").hidden = true;
  },

  _createMicrosummaryMenuItem:
  function BPP__createMicrosummaryMenuItem(aMicrosummary) {
    var menuItem = document.createElement("menuitem");

    // Store a reference to the microsummary in the menu item, so we know
    // which microsummary this menu item represents when it's time to
    // save changes or load its content.
    menuItem.microsummary = aMicrosummary;

    // Content may have to be generated asynchronously; we don't necessarily
    // have it now.  If we do, great; otherwise, fall back to the generator
    // name, then the URI, and we trigger a microsummary content update. Once
    // the update completes, the microsummary will notify our observer to
    // update the corresponding menu-item.
    // XXX Instead of just showing the generator name or (heaven forbid)
    // its URI when we don't have content, we should tell the user that
    // we're loading the microsummary, perhaps with some throbbing to let
    // her know it is in progress.
    if (aMicrosummary.content)
      menuItem.setAttribute("label", aMicrosummary.content);
    else {
      menuItem.setAttribute("label", aMicrosummary.generator.name ||
                                     aMicrosummary.generator.uri.spec);
      aMicrosummary.update();
    }

    return menuItem;
  },

  _initNamePicker: function BPP_initNamePicker() {
    var userEnteredNameField = this._element("userEnteredName");
    var namePicker = this._element("namePicker");
    userEnteredNameField.label = this._itemTitle;

    // Non-bookmark items always use the item-title itself
    if (this._itemType != BOOKMARK_ITEM || !this._bookmarkURI) {
      namePicker.selectedItem = userEnteredNameField;
      return;
    }

    var itemToSelect = userEnteredNameField;
    try {
      this._microsummaries = this._mss.getMicrosummaries(this._bookmarkURI,
                                                         this._bookmarkId);
    }
    catch(ex) {
      // getMicrosummaries will throw an exception in at least two cases:
      // 1. the bookmarked URI contains a scheme that the service won't
      //    download for security reasons (currently it only handles http,
      //    https, and file);
      // 2. the page to which the URI refers isn't HTML or XML (the only two
      //    content types the service knows how to summarize).
      this._microsummaries = null;
    }
    if (this._microsummaries) {
      var enumerator = this._microsummaries.Enumerate();

      if (enumerator.hasMoreElements()) {
        // Show the drop marker if there are microsummaries
        namePicker.setAttribute("droppable", "true");

        var menupopup = namePicker.menupopup;
        while (enumerator.hasMoreElements()) {
          var microsummary = enumerator.getNext()
                                       .QueryInterface(Ci.nsIMicrosummary);
          var menuItem = this._createMicrosummaryMenuItem(microsummary);

          if (this._action == ACTION_EDIT &&
              this._mss.isMicrosummary(this._bookmarkId, microsummary))
            itemToSelect = menuItem;

          menupopup.appendChild(menuItem);
        }
      }

      this._microsummaries.addObserver(this);
    }

    namePicker.selectedItem = itemToSelect;
  },

  // nsIMicrosummaryObserver
  onContentLoaded: function BPP_onContentLoaded(aMicrosummary) {
    var namePicker = this._element("namePicker");
    var childNodes = namePicker.menupopup.childNodes;

    // 0: user-entered item; 1: separator
    for (var i = 2; i < childNodes.length; i++) {
      if (childNodes[i].microsummary == aMicrosummary) {
        var newLabel = aMicrosummary.content;
        // XXXmano: non-editable menulist would do this for us, see bug 360220
        // We should fix editable-menulsits to set the DOMAttrModified handler
        // as well.
        //
        // Also note the order importance: if the label of the menu-item is
        // set the something different than the menulist's current value,
        // the menulist no longer has selectedItem set
        if (namePicker.selectedItem == childNodes[i])
          namePicker.value = newLabel;

        childNodes[i].label = newLabel;
        return;
      }
    }
  },

  onElementAppended: function BPP_onElementAppended(aMicrosummary) {
    var namePicker = this._element("namePicker");
    namePicker.menupopup
              .appendChild(this._createMicrosummaryMenuItem(aMicrosummary));

    // Make sure the drop-marker is shown
    namePicker.setAttribute("droppable", "true");
  },

  onDialogUnload: function BPP_onDialogUnload() {
    if (this._microsummaries)
      this._microsummaries.removeObserver(this);

    // persist the folder tree height
    if (!this._folderTree.collapsed) {
      this._folderTree.setAttribute("height",
                                    this._folderTree.boxObject.height);
    }
  },

  onDialogAccept: function BPP_onDialogAccept() {
    if (this._action == ACTION_ADD)
      this._createNewItem();
    else
      this._saveChanges();
  },

  /**
   * This method checks the current state of the input fields in the
   * dialog, and if any of them are in an invalid state, it will disable
   * the submit button.  This method should be called after every
   * significant change to the input.
   */
  validateChanges: function BPP_validateChanges() {
    document.documentElement.getButton("accept").disabled = !this._inputIsValid();
  },

  /**
   * This method checks to see if the input fields are in a valid state.
   *
   * @returns  true if the input is valid, false otherwise
   */
  _inputIsValid: function BPP__inputIsValid() {
    if (this._itemType == BOOKMARK_ITEM && !this._containsValidURI("editURLBar"))
      return false;

    // Feed Location has to be a valid URI;
    // Site Location has to be a valid URI or empty
    if (this._itemType == LIVEMARK_CONTAINER) {
      if (!this._containsValidURI("feedLocationTextfield"))
        return false;
      if (!this._containsValidURI("feedSiteLocationTextfield") &&
          (this._element("feedSiteLocationTextfield").value.length > 0))
        return false;
    }

    return true;
  },

  /**
   * Determines whether the XUL textbox with the given ID contains a
   * string that can be converted into an nsIURI.
   *
   * @param aTextboxID
   *        the ID of the textbox element whose contents we'll test
   *
   * @returns true if the textbox contains a valid URI string, false otherwise
   */
  _containsValidURI: function BPP__containsValidURI(aTextboxID) {
    try {
      var value = this._element(aTextboxID).value;
      if (value) {
        var uri = PlacesUtils._uri(value);
        return true;
      }
    } catch (e) { }
    return false;
  },

  /**
   * Get an edit title transaction for the item edit/added in the dialog
   */
  _getEditTitleTransaction:
  function BPP__getEditTitleTransaction(aItemId, aNewTitle) {
    return new PlacesEditItemTitleTransaction(aItemId, aNewTitle);
  },

  /**
   * XXXmano todo:
   *  1. Make setAnnotationsForURI unset a given annotation if the value field
   *     is not set.
   *  2. Replace PlacesEditItemDescriptionTransaction and
   *     PlacesSetLoadInSidebarTransaction transaction with a generic
   *     transaction to set/unset an annotation object.
   *  3. Use the two helpers below with this new generic transaction in
   *     _saveChanges.
   */

  /**
   * Returns an object which could then be used to set/unset the
   * description annotation for an item (any type).
   *
   * @param aDescription
   *        The description of the item.
   * @returns an object representing the annotation which could then be used
   *          with get/setAnnotationsForURI of PlacesUtils.
   */
  _getDescriptionAnnotation:
  function BPP__getDescriptionAnnotation(aDescription) {
    var anno = { name: DESCRIPTION_ANNO,
                 type: Ci.nsIAnnotationService.TYPE_STRING,
                 flags: 0,
                 value: aDescription,
                 expires: Ci.nsIAnnotationService.EXPIRE_NEVER };

    /**
     * See todo note above
     * if (aDescription)
     *   anno.value = aDescription;
     */
    return anno;
  },

  /**
   * Returns an object which could then be used to set/unset the
   * load-in-sidebar annotation for a bookmark item.
   *
   * @param aLoadInSidebar
   *        Whether to load the bookmark item in the sidebar in default
   *        conditions.
   * @returns an object representing the annotation which could then be used
   *          with get/setAnnotationsForURI of PlacesUtils.
   */
  _getLoadInSidebarAnnotation:
  function BPP__getLoadInSidebarAnnotation(aLoadInSidebar) {
    var anno = { name: LOAD_IN_SIDEBAR_ANNO,
                 type: Ci.nsIAnnotationService.TYPE_INT32,
                 flags: 0,
                 value: aLoadInSidebar,
                 expires: Ci.nsIAnnotationService.EXPIRE_NEVER };

    /**
     * See todo note above
     * if (anno)
     *   anno.value = aLoadInSidebar;
     */
    return anno;
  },

  /**
   * Dialog-accept code path when editing an item (any type).
   *
   * Save any changes that might have been made while the properties dialog
   * was open.
   */
  _saveChanges: function BPP__saveChanges() {
    var itemId;
    if (this._itemType == BOOKMARK_ITEM)
      itemId = this._bookmarkId;
    else
      itemId = this._folderId;

    var transactions = [];

    // title
    var newTitle = this._element("userEnteredName").label;
    if (newTitle != this._itemTitle)
      transactions.push(this._getEditTitleTransaction(itemId, newTitle));

    // description
    var description = this._element("descriptionTextfield").value;
    if (description != this._itemDescription) {
      transactions.push(new PlacesEditItemDescriptionTransaction(
        itemId, description, this._itemType != BOOKMARK_ITEM));
    }

    if (this._itemType == BOOKMARK_ITEM) {
      // location
      var url = PlacesUtils._uri(this._element("editURLBar").value);
      if (!this._bookmarkURI.equals(url))
        transactions.push(new PlacesEditBookmarkURITransaction(itemId, url));

      // keyword transactions
      var newKeyword = this._element("keywordTextfield").value;
      if (newKeyword != this._bookmarkKeyword) {
        transactions.push(
          new PlacesEditBookmarkKeywordTransaction(itemId, newKeyword));
      }

      // microsummaries
      var namePicker = this._element("namePicker");
      var newMicrosummary = namePicker.selectedItem.microsummary;

      // Only add a microsummary update to the transaction if the
      // microsummary has actually changed, i.e. the user selected no
      // microsummary, but the bookmark previously had one, or the user
      // selected a microsummary which is not the one the bookmark previously
      // had.
      if ((newMicrosummary == null && this._mss.hasMicrosummary(itemId)) ||
          (newMicrosummary != null &&
           !this._mss.isMicrosummary(itemId, newMicrosummary))) {
        transactions.push(
          new PlacesEditBookmarkMicrosummaryTransaction(itemId,
                                                        newMicrosummary));
      }

      // load in sidebar
      var loadInSidebarChecked = this._element("loadInSidebarCheckbox").checked;
      if (loadInSidebarChecked != this._loadBookmarkInSidebar) {
        transactions.push(
          new PlacesSetLoadInSidebarTransaction(itemId, loadInSidebarChecked));
      }
    }
    else if (this._itemType == LIVEMARK_CONTAINER) {
      var feedURIString = this._element("feedLocationTextfield").value;
      var feedURI = PlacesUtils._uri(feedURIString);
      if (!this._feedURI.equals(feedURI)) {
        transactions.push(
          new PlacesEditLivemarkFeedURITransaction(this._folderId, feedURI));
      }

      // Site Location is empty, we can set its URI to null
      var siteURIString = this._element("feedSiteLocationTextfield").value;
      var siteURI = null;
      if (siteURIString)
        siteURI = PlacesUtils._uri(siteURIString);

      if ((!siteURI && this._siteURI)  ||
          (siteURI && !this._siteURI.equals(siteURI))) {
        transactions.push(
          new PlacesEditLivemarkSiteURITransaction(this._folderId, siteURI));
      }
    }

    // If we have any changes to perform, do them via the
    // transaction manager passed by the opener so they can be undone.
    if (transactions.length > 0) {
      window.arguments[0].performed = true;
      var aggregate =
        new PlacesAggregateTransaction(this._getDialogTitle(), transactions);
      this._tm.doTransaction(aggregate);
    }
  },

  /**
   * [New Item Mode] Get the insertion point details for the new item, given
   * dialog state and opening arguments.
   *
   * The container-identifier and insertion-index are returned separately in
   * the form of [containerIdentifier, insertionIndex]
   */
  _getInsertionPointDetails: function BPP__getInsertionPointDetails() {
    var containerId, indexInContainer = -1;
    if (isElementVisible(this._folderMenuList))
      containerId = this._getFolderIdFromMenuList();
    else {
      containerId = this._defaultInsertionPoint.itemId;
      indexInContainer = this._defaultInsertionPoint.index;
    }

    return [containerId, indexInContainer];
  },

  /**
   * Returns a transaction for creating a new bookmark item representing the
   * various fields and opening arguments of the dialog.
   */
  _getCreateNewBookmarkTransaction:
  function BPP__getCreateNewBookmarkTransaction(aContainer, aIndex) {
    var uri = PlacesUtils._uri(this._element("editURLBar").value);
    var title = this._element("userEnteredName").label;
    var keyword = this._element("keywordTextfield").value;
    var annotations = [];
    var description = this._element("descriptionTextfield").value;
    if (description)
      annotations.push(this._getDescriptionAnnotation(description));

    var loadInSidebar = this._element("loadInSidebarCheckbox").checked;
    if (loadInSidebar)
      annotations.push(this._getLoadInSidebarAnnotation(true));

    var childTransactions = [];
    var microsummary = this._element("namePicker").selectedItem.microsummary;
    if (microsummary) {
      childTransactions.push(
        new PlacesEditBookmarkMicrosummaryTransaction(-1, microsummary));
    }

    var transactions = [new PlacesCreateItemTransaction(uri, aContainer, aIndex,
                                                        title, keyword,
                                                        annotations,
                                                        childTransactions)];

    if (this._postData) {
      transactions.push(new PlacesEditURIPostDataTransaction(uri,
                                                             this._postData));
    }
    return new PlacesAggregateTransaction(this._getDialogTitle(),
                                          transactions);
  },

  /**
   * Returns a childItems-transactions array representing the URIList with
   * which the dialog has been opened.
   */
  _getTransactionsForURIList: function BPP__getTransactionsForURIList() {
    var transactions = [];
    for (var i = 0; i < this._URIList.length; ++i) {
      var uri = this._URIList[i];
      var title = this._getURITitleFromHistory(uri);
      transactions.push(new PlacesCreateItemTransaction(uri, -1, -1, title));
    }
    return transactions; 
  },

  /**
   * Returns a transaction for creating a new folder item representing the
   * various fields and opening arguments of the dialog.
   */
  _getCreateNewFolderTransaction:
  function BPP__getCreateNewFolderTransaction(aContainer, aIndex) {
    var folderName = this._element("namePicker").value;
    var annotations = [];
    var childItemsTransactions;
    if (this._URIList)
      childItemsTransactions = this._getTransactionsForURIList();
    var description = this._element("descriptionTextfield").value;
    if (description)
      annotations.push(this._getDescriptionAnnotation(description));

    return new PlacesCreateFolderTransaction(folderName, aContainer, aIndex,
                                             annotations,
                                             childItemsTransactions);
  },

  /**
   * Returns a transaction for creating a new live-bookmark item representing
   * the various fields and opening arguments of the dialog.
   */
  _getCreateNewLivemarkTransaction:
  function BPP__getCreateNewLivemarkTransaction(aContainer, aIndex) {
    var feedURIString = this._element("feedLocationTextfield").value;
    var feedURI = PlacesUtils._uri(feedURIString);

    var siteURIString = this._element("feedSiteLocationTextfield").value;
    var siteURI = null;
    if (siteURIString)
      siteURI = PlacesUtils._uri(siteURIString);

    var name = this._element("namePicker").value;
    return new PlacesCreateLivemarkTransaction(feedURI, siteURI,
                                                name, aContainer, 
                                                aIndex);
  },

  /**
   * Dialog-accept code-path for creating a new item (any type)
   */
  _createNewItem: function BPP__getCreateItemTransaction() {
    var [container, index] = this._getInsertionPointDetails();
    var createTxn;
    if (this._itemType == BOOKMARK_FOLDER)
      createTxn = this._getCreateNewFolderTransaction(container, index);
    else if (this._itemType == LIVEMARK_CONTAINER)
      createTxn = this._getCreateNewLivemarkTransaction(container, index);
    else // BOOKMARK_ITEM
      createTxn = this._getCreateNewBookmarkTransaction(container, index);

    // Mark the containing folder as recently-used
    this._markFolderAsRecentlyUsed(container);

    // perfrom our transaction do via the transaction manager passed by the
    // opener so it can be undone.
    window.arguments[0].performed = true;
    this._tm.doTransaction(createTxn);
  },

  onNamePickerInput: function BPP_onNamePickerInput() {
    this._element("userEnteredName").label = this._element("namePicker").value;
  },

  toggleTreeVisibility: function BPP_toggleTreeVisibility() {
    var expander = this._element("expander");
    if (!this._folderTree.collapsed) { // if (willCollapse)
      expander.className = "down";
      expander.setAttribute("tooltiptext",
                            expander.getAttribute("tooltiptextdown"));
      document.documentElement.buttons = "accept,cancel";

      this._folderTreeHeight = this._folderTree.boxObject.height;
      this._folderTree.setAttribute("height", this._folderTreeHeight);
      this._folderTree.collapsed = true;
      resizeTo(window.outerWidth, window.outerHeight - this._folderTreeHeight);
    }
    else {
      expander.className = "up";
      expander.setAttribute("tooltiptext",
                            expander.getAttribute("tooltiptextup"));
      document.documentElement.buttons = "accept,cancel, extra2";

      if (!this._folderTree.treeBoxObject.view.isContainerOpen(0))
        this._folderTree.treeBoxObject.view.toggleOpenState(0);
      this._folderTree.selectFolders([this._getFolderIdFromMenuList()]);
      this._folderTree.focus();

      this._folderTree.collapsed = false;
      resizeTo(window.outerWidth, window.outerHeight + this._folderTreeHeight);
    }
  },

  _getFolderIdFromMenuList:
  function BPP__getFolderIdFromMenuList() {
    var selectedItem = this._folderMenuList.selectedItem
    switch (selectedItem.id) {
      case "bookmarksRootItem":
        return PlacesUtils.bookmarksRootId;
      case "toolbarFolderItem":
        return PlacesUtils.toolbarFolderId;
    }

    NS_ASSERT("folderId" in selectedItem,
              "Invalid menuitem in the folders-menulist");
    return selectedItem.folderId;
  },

  /**
   * Get the corresponding menu-item in the folder-menu-list for a bookmarks
   * folder if such an item exists. Otherwise, this creates a menu-item for the
   * folder. If the items-count limit (see MAX_FOLDERS_IN_MENU_LIST) is reached,
   * the new item replaces the last menu-item.
   * @param aFolderId
   *        The identifier of the bookmarks folder
   * @param aCheckStaticFolderItems
   *        whether or not to also treat the static items at the top of
   *        menu-list. Note dynamic items get precedence even if this is set to
   *        true.
   */
  _getFolderMenuItem:
  function BPP__getFolderMenuItem(aFolderId, aCheckStaticFolderItems) {
    var menupopup = this._folderMenuList.menupopup;

    // 0: Bookmarks root, 1: toolbar folder, 2: separator
    for (var i=3;  i < menupopup.childNodes.length; i++) {
      if (menupopup.childNodes[i].folderId == aFolderId)
        return menupopup.childNodes[i];
    }

    if (aCheckStaticFolderItems) {
      if (aFolderId == PlacesUtils.bookmarksRootId)
        return this._element("bookmarksRootItem")
      if (aFolderId == PlacesUtils.toolbarFolderId)
        return this._element("toolbarFolderItem")
    }

    // 2 special folders + separator + folder-items-count limit
    if (menupopup.childNodes.length == 3 + MAX_FOLDER_ITEM_IN_MENU_LIST)
      menupopup.removeChild(menupopup.lastChild);

    return this._appendFolderItemToMenupopup(menupopup, aFolderId);
  },

  onMenuListFolderSelect: function BPP_onMenuListFolderSelect(aEvent) {
    if (this._folderTree.hidden)
      return;

    this._folderTree.selectFolders([this._getFolderIdFromMenuList()]);
  },

  onFolderTreeSelect: function BPP_onFolderTreeSelect() {
    var selectedNode = this._folderTree.selectedNode;
    if (!selectedNode)
      return;

    var folderId = selectedNode.itemId;
    // Don't set the selected item if the static item for the folder is
    // already selected
    var oldSelectedItem = this._folderMenuList.selectedItem;
    if ((oldSelectedItem.id == "toolbarFolderItem" &&
         folderId == PlacesUtils.bookmarks.toolbarFolder) ||
        (oldSelectedItem.id == "bookmarksRootItem" &&
         folderId == PlacesUtils.bookmarks.bookmarksRoot))
      return;

    var folderItem = this._getFolderMenuItem(folderId, false);
    this._folderMenuList.selectedItem = folderItem;
  },

  _markFolderAsRecentlyUsed:
  function BPP__markFolderAsRecentlyUsed(aFolderId) {
    // We'll figure out when/if to expire the annotation if it turns out
    // we keep this recently-used-folders implementation
    PlacesUtils.annotations
               .setItemAnnotationInt64(aFolderId, LAST_USED_ANNO,
                                       new Date().getTime(), 0,
                                       Ci.nsIAnnotationService.EXPIRE_NEVER);
  },

  newFolder: function BPP_newFolder() {
    // The command is disabled when the tree is not focused
    this._folderTree.focus();
    goDoCommand("placesCmd_new:folder");
  }
};
