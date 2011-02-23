/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const Cc = Components.classes;
const Ci = Components.interfaces;

const URI_BLOCKLIST_DIALOG = "chrome://mozapps/content/extensions/blocklist.xul";

let ww, strings;

// This tests that the blocklist dialog still affects soft-blocked add-ons
// if the user clicks the "Restart Later" button. It also ensures that the
// "Cancel" button is correctly renamed (to "Restart Later").
let args = {
  restart: false,
  list: [{
    name: "Bug 523784 softblocked addon",
    version: "1",
    icon: "chrome://mozapps/skin/plugins/pluginGeneric.png",
    disable: false,
    blocked: false,
  }],
};

function test() {
  waitForExplicitFinish();
  ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].
       getService(Ci.nsIWindowWatcher);
  strings = Cc["@mozilla.org/intl/stringbundle;1"].
            getService(Ci.nsIStringBundleService);

  let windowObserver = {
    observe: function(aSubject, aTopic, aData) {
      if (aTopic != "domwindowopened")
        return;

      ww.unregisterNotification(windowObserver);

      let win = aSubject.QueryInterface(Ci.nsIDOMWindow);
      win.addEventListener("load", function() {
        win.removeEventListener("load", arguments.callee, false);

        executeSoon(function() bug523784_test1(win));
      }, false);
    }
  };
  ww.registerNotification(windowObserver);

  args.wrappedJSObject = args;
  ww.openWindow(null, URI_BLOCKLIST_DIALOG, "",
                         "chrome,centerscreen,dialog,titlebar", args);
}

function bug523784_test1(win) {
  let bundle = strings.createBundle("chrome://mozapps/locale/update/updates.properties");
  let cancelButton = win.document.documentElement.getButton("cancel");

  is(cancelButton.getAttribute("label"),
     bundle.GetStringFromName("restartLaterButton"),
     "Text should be changed on Cancel button");
  is(cancelButton.getAttribute("accesskey"),
     bundle.GetStringFromName("restartLaterButton.accesskey"),
     "Accesskey should also be changed on Cancel button");

  let windowObserver = {
    observe: function(aSubject, aTopic, aData) {
        if (aTopic != "domwindowclosed")
          return;

        ww.unregisterNotification(windowObserver);

        ok(args.list[0].disable, "Should be blocking add-on");
        ok(!args.restart, "Should not restart browser immediately");

        executeSoon(finish);
    }
  };
  ww.registerNotification(windowObserver);

  cancelButton.doCommand();
}
