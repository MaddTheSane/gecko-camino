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
 * The Original Code is Download Manager Test Code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Shawn Wilsher <me@shawnwilsher.com> (Original Author)
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

// This tests the migration code to make sure we properly migrate downloads.rdf
// Also tests cleanUp and getDownload (from the database) since we have a good
// number of entries in the database after importing.

importDownloadsFile("downloads.rdf");

const nsIDownloadManager = Ci.nsIDownloadManager;
const dm = Cc["@mozilla.org/download-manager;1"].getService(nsIDownloadManager);

function test_count_entries()
{
  var stmt = dm.DBConnection.createStatement("SELECT COUNT(*) " +
                                             "FROM moz_downloads");
  stmt.executeStep();

  do_check_eq(7, stmt.getInt32(0));

  stmt.reset();
}

function test_random_download()
{
  var stmt = dm.DBConnection.createStatement("SELECT COUNT(*), source, target," +
                                           "state " +
                                           "FROM moz_downloads " +
                                           "WHERE name = ?1");
  stmt.bindStringParameter(0, "Firefox 2.0.0.3.dmg");
  stmt.executeStep();

  do_check_eq(1, stmt.getInt32(0));
  do_check_eq("http://ftp-mozilla.netscape.com/pub/mozilla.org/firefox/releases/2.0.0.3/mac/en-US/Firefox%202.0.0.3.dmg", stmt.getUTF8String(1));
  do_check_eq("file:///Users/sdwilsh/Desktop/Firefox 2.0.0.3.dmg", stmt.getUTF8String(2));
  do_check_eq(1, stmt.getInt32(3));

  stmt.reset();
}

// This provides us with a lot of download entries to test the cleanup function
function test_dm_cleanup()
{
  dm.cleanUp();

  var stmt = dm.DBConnection.createStatement("SELECT COUNT(*) " +
                                             "FROM moz_downloads");
  stmt.executeStep();

  do_check_eq(0, stmt.getInt32(0));

  stmt.reset();
}

var tests = [test_count_entries, test_random_download, test_dm_cleanup];

function run_test()
{
  for (var i = 0; i < tests.length; i++)
    tests[i]();
  
  cleanup();
}
