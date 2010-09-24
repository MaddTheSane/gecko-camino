/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test Definition
 *
 * Most tests can use an array named TESTS that will perform most if not all of
 * the necessary checks. Each element in the array must be an object with the
 * following possible properties. Additional properties besides the ones listed
 * below can be added as needed.
 *
 * overrideCallback (optional)
 *   The function to call for the next test. This is typically called when the
 *   wizard page changes but can also be called for other events by the previous
 *   test. If this property isn't defined then the defailtCallback function will
 *   be called. If this property is defined then all other properties are
 *   optional.
 *
 * pageid (required unless overrideCallback is specified)
 *   The expected pageid for the wizard. This property is required unless the
 *   overrideCallback property is defined.
 *
 * extraStartFunction (optional)
 *   The function to call at the beginning of the defaultCallback function. If
 *   the function returns true the defaultCallback function will return early
 *   which allows waiting for a specific condition to be evaluated in the
 *   function specified in the extraStartFunction property before continuing
 *   with the test.
 *
 * extraCheckFunction (optional)
 *   The function to call to perform extra checks in the defaultCallback
 *   function.
 *
 * extraDelayedCheckFunction (optional)
 *   The function to call to perform extra checks in the delayedDefaultCallback
 *   function.
 *
 * buttonStates (optional)
 *   A javascript object representing the expected hidden and disabled attribute
 *   values for the buttons of the current wizard page. The values are checked
 *   in the delayedDefaultCallback function. For information about the structure
 *   of this object refer to the getExpectedButtonStates and checkButtonStates
 *   functions.
 *
 * buttonClick (optional)
 *   The current wizard page button to click at the end of the
 *   delayedDefaultCallback function. If the buttonClick property is defined
 *   then the extraDelayedFinishFunction property can't be specified due to race
 *   conditions in some of the tests and if both of them are specified the test
 *   will intentionally throw.
 *
 * extraDelayedFinishFunction (optional)
 *   The function to call at the end of the delayedDefaultCallback function.
 *
 * ranTest (should not be specified)
 *   When delayedDefaultCallback is called a property named ranTest is added to
 *   the current test it is possible to verify that each test in the TESTS
 *   array has ran.
 *
 * prefHasUserValue (optional)
 *   For comparing the expected value defined by this property with the return
 *   value of prefHasUserValue using gPrefToCheck for the preference name in the
 *   checkPrefHasUserValue function.
 *
 * expectedRadioGroupSelectedIndex (optional)
 *   For comparing the expected selectedIndex attribute value of the wizard's
 *   license page radiogroup selectedIndex attribute in the
 *   checkRadioGroupSelectedIndex function.
 *
 * expectedRemoteContentState (optional)
 *   For comparing the expected remotecontent state attribute value of the
 *   wizard's billboard and license pages in the checkRemoteContentState and
 *   waitForRemoteContentLoaded functions.
 */

// The tests have to use the pageid instead of the pageIndex due to the
// app update wizard's access method being random.
const PAGEID_DUMMY            = "dummy";                 // Done
const PAGEID_CHECKING         = "checking";              // Done
const PAGEID_PLUGIN_UPDATES   = "pluginupdatesfound";
const PAGEID_NO_UPDATES_FOUND = "noupdatesfound";        // Done
const PAGEID_MANUAL_UPDATE    = "manualUpdate"; // Tested on license load failure
const PAGEID_INCOMPAT_CHECK   = "incompatibleCheck";
const PAGEID_FOUND_BASIC      = "updatesfoundbasic";     // Done
const PAGEID_FOUND_BILLBOARD  = "updatesfoundbillboard"; // Done
const PAGEID_LICENSE          = "license";               // Done
const PAGEID_INCOMPAT_LIST    = "incompatibleList";
const PAGEID_DOWNLOADING      = "downloading";           // Done
const PAGEID_ERRORS           = "errors";                // Done
const PAGEID_ERROR_PATCHING   = "errorpatching";         // Done
const PAGEID_FINISHED         = "finished";              // Done
const PAGEID_FINISHED_BKGRD   = "finishedBackground";    // Done
const PAGEID_INSTALLED        = "installed";             // Done

const UPDATE_WINDOW_NAME = "Update:Wizard";

const URL_HOST   = "http://example.com/";
const URL_PATH   = "chrome/toolkit/mozapps/update/test/chrome";
const URL_UPDATE = URL_HOST + URL_PATH + "/update.sjs";

const URI_UPDATE_PROMPT_DIALOG  = "chrome://mozapps/content/update/updates.xul";

const CRC_ERROR = 4;

const DEBUG = false;

const TEST_TIMEOUT = 30000; // 30 seconds
var gTimeoutTimer;

// The following vars are for restoring previous preference values (if present)
// when the test finishes.
var gAppUpdateChannel; // app.update.channel (default prefbranch)
var gAppUpdateEnabled; // app.update.enabled
var gAppUpdateURL;     // app.update.url.override

var gTestCounter = -1;
var gWin;
var gDocElem;
var gPrefToCheck;

#include ../shared.js

function debugDump(msg) {
  if (DEBUG) {
    dump("*** " + msg + "\n");
  }
}

__defineGetter__("gWW", function() {
  delete this.gWW;
  return this.gWW = AUS_Cc["@mozilla.org/embedcomp/window-watcher;1"].
                      getService(AUS_Ci.nsIWindowWatcher);
});

__defineGetter__("gApp", function() {
  delete this.gApp;
  return this.gApp = AUS_Cc["@mozilla.org/xre/app-info;1"].
                     getService(AUS_Ci.nsIXULAppInfo).
                     QueryInterface(AUS_Ci.nsIXULRuntime);
});

/**
 * The current test in TESTS array.
 */
__defineGetter__("gTest", function() {
  return TESTS[gTestCounter];
});

/**
 * The current test's callback. This will either return the callback defined in
 * the test's overrideCallback property or defaultCallback if the
 * overrideCallback property is undefined.
 */
__defineGetter__("gCallback", function() {
  return gTest.overrideCallback ? gTest.overrideCallback
                                : defaultCallback;
});

/**
 * The remotecontent element for the current page if one exists or null if a
 * remotecontent element doesn't exist.
 */
__defineGetter__("gRemoteContent", function() {
  switch (gTest.pageid) {
    case PAGEID_FOUND_BILLBOARD:
      return gWin.document.getElementById("updateMoreInfoContent");
    case PAGEID_LICENSE:
      return gWin.document.getElementById("licenseContent");
  }
  return null;
});

/**
 * The state for the remotecontent element if one exists or null if a
 * remotecontent element doesn't exist.
 */
__defineGetter__("gRemoteContentState", function() {
  if (gRemoteContent) {
    return gRemoteContent.getAttribute("state");
  }
  return null;
});

/**
 * The radiogroup for the license page.
 */
__defineGetter__("gAcceptDeclineLicense", function() {
  return gWin.document.getElementById("acceptDeclineLicense");
});

/**
 * The listbox for the incompatibleList page.
 */
__defineGetter__("gIncompatibleListbox", function() {
  return gWin.document.getElementById("incompatibleListbox");
});

/**
 * Default test run function that can be used by most tests.
 */
function runTestDefault() {
  debugDump("Entering runTestDefault");

  SimpleTest.waitForExplicitFinish();

  gWW.registerNotification(gWindowObserver);

  setupPrefs();
  removeUpdateDirsAndFiles();
  reloadUpdateManagerData();
  runTest();
}

/**
 * Default test finish function that can be used by most tests.
 */
function finishTestDefault() {
  debugDump("Entering finishTestDefault");

  gDocElem.removeEventListener("pageshow", onPageShowDefault, false);

  if (gTimeoutTimer) {
    gTimeoutTimer.cancel();
    gTimeoutTimer = null;
  }

  verifyTestsRan();

  gWW.unregisterNotification(gWindowObserver);

  resetPrefs();
  removeUpdateDirsAndFiles();
  reloadUpdateManagerData();
  SimpleTest.finish();
}

/**
 * nsITimerCallback for the timeout timer to cleanly finish a test if the Update
 * Window doesn't close for a test. This allows the next test to run properly if
 * a previous test fails.
 *
 * @param  aTimer
 *         The nsITimer that fired.
 */
function finishTestTimeout(aTimer) {
  gTimeoutTimer = null;
  ok(false, "Test timed out. Maximum time allowed is " + (TEST_TIMEOUT / 1000) +
     " seconds");
  gWin.close();
}

/**
 * Default callback for the wizard's documentElement pageshow listener. This
 * will return early for event's where the originalTarget's nodeName is not
 * wizardpage.
 */
function onPageShowDefault(aEvent) {
  // Return early if the event's original target isn't for a wizardpage element.
  // This check is necessary due to the remotecontent element firing pageshow.
  if (aEvent.originalTarget.nodeName != "wizardpage") {
    debugDump("onPageShowDefault - only handles events with an " +
              "originalTarget nodeName of |wizardpage|. " +
              "aEvent.originalTarget.nodeName = " +
              aEvent.originalTarget.nodeName + "... returning early");
    return;
  }

  gTestCounter++;
  gCallback(aEvent);
}

/**
 * Default callback that can be used by most tests.
 */
function defaultCallback(aEvent) {
  debugDump("Entering defaultCallback - TESTS[" + gTestCounter + "], " +
            "pageid: " + gTest.pageid + ", currentPage.pageid: " +
            gDocElem.currentPage.pageid + ", " +
            "aEvent.originalTarget.nodeName: " + aEvent.originalTarget.nodeName);

  if (gTest && gTest.extraStartFunction) {
    debugDump("defaultCallback - calling extraStartFunction " +
              gTest.extraStartFunction.name);
    if (gTest.extraStartFunction(aEvent)) {
      debugDump("defaultCallback - extraStartFunction early return");
      return;
    }
  }

  is(gDocElem.currentPage.pageid, gTest.pageid,
     "Checking currentPage.pageid equals " + gTest.pageid + " in pageshow");

  // Perform extra checks if specified by the test
  if (gTest.extraCheckFunction) {
    debugDump("delayedCallback - calling extraCheckFunction " +
              gTest.extraCheckFunction.name);
    gTest.extraCheckFunction();
  }

  // The wizard page buttons' disabled and hidden attributes are set after the
  // pageshow event so use executeSoon to allow them to be set so their disabled
  // and hidden attribute values can be checked.
  SimpleTest.executeSoon(delayedDefaultCallback);
}

/**
 * Delayed default callback called using executeSoon in defaultCallback which
 * allows the wizard page buttons' disabled and hidden attributes to be set
 * before checking their values.
 */
function delayedDefaultCallback() {
  debugDump("Entering delayedDefaultCallback - TESTS[" + gTestCounter + "], " +
            "pageid: " + gTest.pageid + ", currentPage.pageid: " +
            gDocElem.currentPage.pageid);

  // Verify the pageid hasn't changed after executeSoon was called.
  is(gDocElem.currentPage.pageid, gTest.pageid,
     "Checking currentPage.pageid equals " + gTest.pageid + " after " +
     "executeSoon");

  checkButtonStates();

  // Perform delayed extra checks if specified by the test
  if (gTest.extraDelayedCheckFunction) {
    debugDump("delayedDefaultCallback - calling extraDelayedCheckFunction " +
              gTest.extraDelayedCheckFunction.name);
    gTest.extraDelayedCheckFunction();
  }

  // Used to verify that this test has been performed
  gTest.ranTest = true;

  if (gTest.buttonClick) {
    debugDump("delayedDefaultCallback - clicking " + gTest.buttonClick +
              " button");
    if(gTest.extraDelayedFinishFunction) {
      throw("Tests cannot have a buttonClick and an extraDelayedFinishFunction property");
    }
    gDocElem.getButton(gTest.buttonClick).click();
  }
  else if (gTest.extraDelayedFinishFunction) {
    debugDump("delayedDefaultCallback - calling extraDelayedFinishFunction " +
              gTest.extraDelayedFinishFunction.name);
    gTest.extraDelayedFinishFunction();
  }
}

/**
 * Checks the wizard page buttons' disabled and hidden attributes values are
 * correct. If an expected button id is not specified then the expected disabled
 * and hidden attribute value is true.
 */
function checkButtonStates() {
  debugDump("Entering checkButtonStates - TESTS[" + gTestCounter + "], " +
            "pageid: " + gTest.pageid + ", currentPage.pageid: " +
            gDocElem.currentPage.pageid);

  const buttonNames = ["extra1", "extra2", "back", "next", "finish", "cancel"];
  let buttonStates = getExpectedButtonStates();
  buttonNames.forEach(function(aButtonName) {
    let button = gDocElem.getButton(aButtonName);
    let hasHidden = aButtonName in buttonStates &&
                    "hidden" in buttonStates[aButtonName];
    let hidden = hasHidden ? buttonStates[aButtonName].hidden : true;
    let hasDisabled = aButtonName in buttonStates &&
                      "disabled" in buttonStates[aButtonName];
    let disabled = hasDisabled ? buttonStates[aButtonName].disabled : true;
    is(button.hidden, hidden, "Checking " + aButtonName + " button " +
       "hidden attribute value equals " + (hidden ? "true" : "false"));
    if (button.hidden != hidden)
      debugDump("Checking " + aButtonName + " button hidden attribute " +
                "value equals " + (hidden ? "true" : "false"));
    is(button.disabled, disabled, "Checking " + aButtonName + " button " +
       "disabled attribute value equals " + (disabled ? "true" : "false"));
    if (button.disabled != disabled)
      debugDump("Checking " + aButtonName + " button disabled attribute " +
                "value equals " + (disabled ? "true" : "false"));
  });
}

/**
 * Returns the expected disabled and hidden attribute values for the buttons of
 * the current wizard page.
 */
function getExpectedButtonStates() {
  // Allow individual tests to override the expected button states.
  if (gTest.buttonStates) {
    return gTest.buttonStates;
  }

  switch (gTest.pageid) {
    case PAGEID_CHECKING:
    case PAGEID_INCOMPAT_CHECK:
      return { cancel: { disabled: false, hidden: false } };
    case PAGEID_FOUND_BASIC:
      return { extra1: { disabled: false, hidden: false },
               next  : { disabled: false, hidden: false } }
    case PAGEID_FOUND_BILLBOARD:
      return { extra1: { disabled: false, hidden: false },
               extra2: { disabled: false, hidden: false },
               next  : { disabled: false, hidden: false } }
    case PAGEID_LICENSE:
      if (gRemoteContentState != "loaded" ||
          gAcceptDeclineLicense.selectedIndex != 0) {
        return { extra1: { disabled: false, hidden: false },
                 next  : { disabled: true, hidden: false } };
      }
      return { extra1: { disabled: false, hidden: false },
               next  : { disabled: false, hidden: false } };
    case PAGEID_INCOMPAT_LIST:
      return { extra1: { disabled: false, hidden: false },
               next  : { disabled: false, hidden: false } };
    case PAGEID_DOWNLOADING:
      return { extra1: { disabled: false, hidden: false } };
    case PAGEID_NO_UPDATES_FOUND:
    case PAGEID_MANUAL_UPDATE:
    case PAGEID_ERRORS:
    case PAGEID_INSTALLED:
      return { finish: { disabled: false, hidden: false } };
    case PAGEID_ERROR_PATCHING:
      return { next  : { disabled: false, hidden: false } };
    case PAGEID_FINISHED:
    case PAGEID_FINISHED_BKGRD:
      return { extra1: { disabled: false, hidden: false },
               finish: { disabled: false, hidden: false } };
  }
  return null;
}

/**
 * Adds a load event listener to the current remotecontent element.
 */
function addRemoteContentLoadListener() {
  debugDump("Entering addRemoteContentLoadListener - TESTS[" + gTestCounter +
            "], pageid: " + gTest.pageid);

  gRemoteContent.addEventListener("load", remoteContentLoadListener, false);
}

/**
 * The nsIDOMEventListener for a remotecontent load event.
 */
function remoteContentLoadListener(aEvent) {
  // Return early if the event's original target's nodeName isn't remotecontent.
  if (aEvent.originalTarget.nodeName != "remotecontent") {
    debugDump("remoteContentLoadListener - only handles events with an " +
              "originalTarget nodeName of |remotecontent|. " +
              "aEvent.originalTarget.nodeName = " +
              aEvent.originalTarget.nodeName);
    return;
  }

  gTestCounter++;
  gCallback(aEvent);
}

/**
 * Waits until a remotecontent element to finish loading which is determined
 * by the current test's expectedRemoteContentState property and then removes
 * the event listener.
 *
 * Note: tests that use this function should not test the state of the
 *      remotecontent since this will check the expected state.
 *
 * @return false if the remotecontent has loaded and its state is the state
 *         specified in the current test's expectedRemoteContentState
 *         property... otherwise true.
 */
function waitForRemoteContentLoaded(aEvent) {
  // Return early until the remotecontent has loaded with the state that is
  // expected or isn't the event's originalTarget.
  if (gRemoteContentState != gTest.expectedRemoteContentState ||
      !aEvent.originalTarget.isSameNode(gRemoteContent)) {
    debugDump("waitForRemoteContentLoaded - returning early\n" +
              "gRemoteContentState: " + gRemoteContentState + "\n" +
              "expectedRemoteContentState: " +
              gTest.expectedRemoteContentState + "\n" +
              "aEvent.originalTarget.nodeName: " +
              aEvent.originalTarget.nodeName);
    return true;
  }

  gRemoteContent.removeEventListener("load", remoteContentLoadListener, false);
  return false;
}

/**
 * Compares the value of the remotecontent state attribute with the value
 * specified in the test's expectedRemoteContentState property.
 */
function checkRemoteContentState() {
  is(gRemoteContentState, gTest.expectedRemoteContentState, "Checking remote " +
     "content state equals " + gTest.expectedRemoteContentState + " - pageid " +
     gTest.pageid);
}

/**
 * Adds a select event listener to the license radiogroup element and clicks
 * the radio element specified in the current test's radioClick property.
 */
function addRadioGroupSelectListenerAndClick() {
  debugDump("Entering addRadioGroupSelectListenerAndClick - TESTS[" +
            gTestCounter + "], pageid: " + gTest.pageid);

  gAcceptDeclineLicense.addEventListener("select", radioGroupSelectListener,
                                         false);
  gWin.document.getElementById(gTest.radioClick).click();
}

/**
 * The nsIDOMEventListener for the license radiogroup select event.
 */
function radioGroupSelectListener(aEvent) {
  // Return early if the event's original target's nodeName isn't radiogroup.
  if (aEvent.originalTarget.nodeName != "radiogroup") {
    debugDump("remoteContentLoadListener - only handles events with an " +
              "originalTarget nodeName of |radiogroup|. " +
              "aEvent.originalTarget.nodeName = " +
              aEvent.originalTarget.nodeName);
    return;
  }

  gAcceptDeclineLicense.removeEventListener("select", radioGroupSelectListener,
                                            false);
  gTestCounter++;
  gCallback(aEvent);
}

/**
 * Compares the value of the License radiogroup's selectedIndex attribute with
 * the value specified in the test's expectedRadioGroupSelectedIndex property.
 */
function checkRadioGroupSelectedIndex() {
  is(gAcceptDeclineLicense.selectedIndex, gTest.expectedRadioGroupSelectedIndex,
     "Checking license radiogroup selectedIndex equals " +
     gTest.expectedRadioGroupSelectedIndex);
}

/**
 * Compares the return value of prefHasUserValue for the preference specified in
 * gPrefToCheck with the value passed in the aPrefHasValue parameter or the
 * value specified in the current test's prefHasUserValue property if
 * aPrefHasValue is undefined.
 *
 * @param  aPrefHasValue (optional)
 *         The expected value returned from prefHasUserValue for the preference
 *         specified in gPrefToCheck. If aPrefHasValue is undefined the value
 *         of the current test's prefHasUserValue property will be used.
 */
function checkPrefHasUserValue(aPrefHasValue) {
  let prefHasUserValue = aPrefHasValue === undefined ? gTest.prefHasUserValue
                                                     : aPrefHasValue;
  is(gPref.prefHasUserValue(gPrefToCheck), prefHasUserValue,
     "Checking prefHasUserValue for preference " + gPrefToCheck + " equals " +
     (prefHasUserValue ? "true" : "false"));
}

/**
 * Gets the update version info for the update url parameters to send to
 * update.sjs.
 *
 * @param  aExtensionVersion (optional)
 *         The application version for the update snippet. If not specified the
 *         current application version will be used.
 * @param  aPlatformVersion (optional)
 *         The platform version for the update snippet. If not specified the
 *         current platform version will be used.
 * @return The url parameters for the application and platform version to send
 *         to update.sjs.
 */
function getVersionParams(aExtensionVersion, aPlatformVersion) {
  return "&extensionVersion=" + (aExtensionVersion ? aExtensionVersion
                                                   : gApp.version) +
         "&platformVersion=" + (aPlatformVersion ? aPlatformVersion
                                                 : gApp.platformVersion);
}

/**
 * Verifies that all tests ran.
 */
function verifyTestsRan() {
  debugDump("Entering verifyTestsRan");

  // Return early if there are no tests defined.
  if (!TESTS) {
    return;
  }

  gTestCounter = -1;
  for (let i = 0; i < TESTS.length; ++i) {
    gTestCounter++;
    let test = TESTS[i];
    let msg = "Checking if TESTS[" + i + "] test was performed... " +
              "callback function name = " + gCallback.name + ", " +
              "pageid = " + test.pageid;
    ok(test.ranTest, msg);
  }
}

/**
 * Sets the most common preferences used by tests to values used by the tests
 * and saves some of the preference's original values if present so they can be
 * set back to the original values when each test has finished.
 */
function setupPrefs() {
  if (DEBUG) {
    gPref.setBoolPref(PREF_APP_UPDATE_LOG, true)
  }

  if (gPref.prefHasUserValue(PREF_APP_UPDATE_URL_OVERRIDE)) {
    gAppUpdateURL = gPref.setIntPref(PREF_APP_UPDATE_URL_OVERRIDE);
  }

  gAppUpdateChannel = gDefaultPrefBranch.getCharPref(PREF_APP_UPDATE_CHANNEL);
  setUpdateChannel();

  if (gPref.prefHasUserValue(PREF_APP_UPDATE_ENABLED)) {
    gAppUpdateEnabled = gPref.getBoolPref(PREF_APP_UPDATE_ENABLED);
  }

  gPref.setBoolPref(PREF_APP_UPDATE_AUTO, false);
  gPref.setIntPref(PREF_APP_UPDATE_IDLETIME, 0);
  gPref.setIntPref(PREF_APP_UPDATE_PROMPTWAITTIME, 0);
}

/**
 * Resets the most common preferences used by tests to their original values.
 */
function resetPrefs() {
  if (gAppUpdateURL) {
    gPref.setCharPref(PREF_APP_UPDATE_URL_OVERRIDE, gAppUpdateURL);
  }
  else if (gPref.prefHasUserValue(PREF_APP_UPDATE_URL_OVERRIDE)) {
    gPref.clearUserPref(PREF_APP_UPDATE_URL_OVERRIDE);
  }

  if (gAppUpdateChannel) {
    setUpdateChannel(gAppUpdateChannel);
  }

  if (gAppUpdateEnabled) {
    gPref.setBoolPref(PREF_APP_UPDATE_ENABLED, gAppUpdateEnabled);
  }
  else if (gPref.prefHasUserValue(PREF_APP_UPDATE_ENABLED)) {
    gPref.clearUserPref(PREF_APP_UPDATE_ENABLED);
  }

  if (gPref.prefHasUserValue(PREF_APP_UPDATE_IDLETIME)) {
    gPref.clearUserPref(PREF_APP_UPDATE_IDLETIME);
  }

  if (gPref.prefHasUserValue(PREF_APP_UPDATE_PROMPTWAITTIME)) {
    gPref.clearUserPref(PREF_APP_UPDATE_PROMPTWAITTIME);
  }

  if (gPref.prefHasUserValue(PREF_APP_UPDATE_URL_DETAILS)) {
    gPref.clearUserPref(PREF_APP_UPDATE_URL_DETAILS);
  }

  if (gPref.prefHasUserValue(PREF_APP_UPDATE_SHOW_INSTALLED_UI)) {
    gPref.clearUserPref(PREF_APP_UPDATE_SHOW_INSTALLED_UI);
  }

  if (gPref.prefHasUserValue(PREF_APP_UPDATE_LOG)) {
    gPref.clearUserPref(PREF_APP_UPDATE_LOG);
  }

  try {
    gPref.deleteBranch(PREF_APP_UPDATE_NEVER_BRANCH);
  }
  catch(e) {
  }
}

/**
 * Closes the update window if it is open.
 */
function closeUpdateWindow() {
  let updateWindow = getUpdateWindow();
  if (!updateWindow)
    return;

  ok(false, "Found an existing Update Window from a previous test... " +
            "attempting to close it.");
  updateWindow.close();
}

/**
 * Gets the update window.
 *
 * @return The nsIDOMWindowInternal for the Update Window if it is open and null
 *         if it isn't.
 */
function getUpdateWindow() {
  var wm = AUS_Cc["@mozilla.org/appshell/window-mediator;1"].
           getService(AUS_Ci.nsIWindowMediator);
  return wm.getMostRecentWindow(UPDATE_WINDOW_NAME);
}

/**
 * nsIObserver for receiving window open and close notifications.
 */
var gWindowObserver = {
  observe: function WO_observe(aSubject, aTopic, aData) {
    let win = aSubject.QueryInterface(AUS_Ci.nsIDOMEventTarget);

    if (aTopic == "domwindowclosed") {
      if (win.location != URI_UPDATE_PROMPT_DIALOG) {
        debugDump("gWindowObserver:observe - domwindowclosed event for " +
                  "window not being tested - location: " + win.location +
                  "... returning early");
        return;
      }
      // Allow tests the ability to provide their own function (it must be
      // named finishTest) for finishing the test.
      try {
        finishTest();
      }
      catch (e) {
        finishTestDefault();
      }
      return;
    }

    win.addEventListener("load", function onLoad() {
      win.removeEventListener("load", onLoad, false);
      // Ignore windows other than the update UI window.
      if (win.location != URI_UPDATE_PROMPT_DIALOG) {
        debugDump("gWindowObserver:observe:onLoad - load event for window " +
                  "not being tested - location: " + win.location +
                  "... returning early");
        return;
      }

      // The first wizard page should always be the dummy page.
      let pageid = win.document.documentElement.currentPage.pageid;
      if (pageid != PAGEID_DUMMY) {
        // This should never happen but if it does this will provide a clue
        // for diagnosing the cause.
        ok(false, "Unexpected load event - pageid got: " + pageid +
           ", expected: " + PAGEID_DUMMY + "... returning early");
        return;
      }

      gTimeoutTimer = AUS_Cc["@mozilla.org/timer;1"].
                      createInstance(AUS_Ci.nsITimer);
      gTimeoutTimer.initWithCallback(finishTestTimeout, TEST_TIMEOUT,
                                     AUS_Ci.nsITimer.TYPE_ONE_SHOT);

      gWin = win;
      gDocElem = gWin.document.documentElement;
      gDocElem.addEventListener("pageshow", onPageShowDefault, false);
    }, false);
  }
};
