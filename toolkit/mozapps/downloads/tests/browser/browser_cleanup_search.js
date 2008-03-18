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
 * The Original Code is Download Manager UI Test Code.
 *
 * The Initial Developer of the Original Code is
 * Edward Lee <edward.lee@engineering.uiuc.edu>.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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
 * Test for bug 414850 to make sure only downloads that are shown when
 * searching are cleared and afterwards, the default list is shown.
 */

function test()
{
  let dm = Cc["@mozilla.org/download-manager;1"].
           getService(Ci.nsIDownloadManager);
  let db = dm.DBConnection;

  // Empty any old downloads
  db.executeSimpleSQL("DELETE FROM moz_downloads");

  let stmt = db.createStatement(
    "INSERT INTO moz_downloads (name, target, source, state, endTime, maxBytes) " +
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");

  try {
    for each (let site in ["delete.me", "i.live"]) {
      stmt.bindStringParameter(0, "Super Pimped Download");
      stmt.bindStringParameter(1, "file://dummy/file");
      stmt.bindStringParameter(2, "http://" + site + "/file");
      stmt.bindInt32Parameter(3, dm.DOWNLOAD_FINISHED);
      stmt.bindInt64Parameter(4, new Date(1985, 7, 2) * 1000);
      stmt.bindInt64Parameter(5, 111222333444);

      // Add it!
      stmt.execute();
    }
  } finally {
    stmt.reset();
    stmt.finalize();
  }

  // Close the UI if necessary
  let wm = Cc["@mozilla.org/appshell/window-mediator;1"].
           getService(Ci.nsIWindowMediator);
  let win = wm.getMostRecentWindow("Download:Manager");
  if (win) win.close();

  // Start the test when the download manager window loads
  let ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].
           getService(Ci.nsIWindowWatcher);
  ww.registerNotification({
    observe: function(aSubject, aTopic, aData) {
      ww.unregisterNotification(this);
      aSubject.QueryInterface(Ci.nsIDOMEventTarget).
      addEventListener("DOMContentLoaded", doTest, false);
    }
  });

  let testPhase = 0;

  // Let the Startup method of the download manager UI finish before we test
  let doTest = function() setTimeout(function() {
    win = wm.getMostRecentWindow("Download:Manager");
    let $ = function(id) win.document.getElementById(id);

    let downloadView = $("downloadView");
    let searchbox = $("searchbox");

    // Try again if selectedIndex is -1
    if (downloadView.selectedIndex)
      return doTest();

    // The list must have built, so figure out what test to do
    switch (testPhase) {
      case 0:
        // Search for multiple words in any order in all places
        searchbox.value = "delete me";
        searchbox.doCommand();

        // Next phase checks for the download to delete
        testPhase++;
        return doTest();
      case 1:
        // Got it!
        ok(downloadView.itemCount == 1, "Search found the item to delete");

        // Clear the list that has the single matched item
        $("menuitem_clearList").doCommand();

        // Make sure the default list is built
        testPhase++;
        return doTest();
      case 2:
        // Done rebuilding with one item left
        ok(downloadView.itemCount == 1, "Clear list rebuilt the list with one");

        // We're done!
        return finish();
    }
  }, 0);
 
  // Show the Download Manager UI
  Cc["@mozilla.org/download-manager-ui;1"].
  getService(Ci.nsIDownloadManagerUI).show();

  waitForExplicitFinish();
}
