#
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
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Mozilla Foundation.
# Portions created by the Initial Developer are Copyright (C) 2009
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Ted Mielczarek <ted.mielczarek@gmail.com>
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

"""
Runs the reftest test harness.
"""

import logging
import sys, shutil, os, os.path
SCRIPT_DIRECTORY = os.path.abspath(os.path.realpath(os.path.dirname(sys.argv[0])))
sys.path.append(SCRIPT_DIRECTORY)
import automation
from optparse import OptionParser
from tempfile import mkdtemp

oldcwd = os.getcwd()
os.chdir(SCRIPT_DIRECTORY)

def getFullPath(path):
  "Get an absolute path relative to oldcwd."
  return os.path.normpath(os.path.join(oldcwd, os.path.expanduser(path)))

def createReftestProfile(profileDir):
  "Sets up a clean profile for reftest."

  # Start with a clean slate.
  shutil.rmtree(profileDir, True)
  os.mkdir(profileDir)
  # reftest should only need the dump pref set
  prefsFile = open(os.path.join(profileDir, "user.js"), "w")
  prefsFile.write("""user_pref("browser.dom.window.dump.enabled", true);
""")
  prefsFile.close()
  # install the reftest extension bits into the profile
  profileExtensionsPath = os.path.join(profileDir, "extensions")
  os.mkdir(profileExtensionsPath)
  reftestExtensionPath = os.path.join(SCRIPT_DIRECTORY, "reftest")
  extFile = open(os.path.join(profileExtensionsPath, "reftest@mozilla.org"), "w")
  extFile.write(reftestExtensionPath)
  extFile.close()

def main():
  parser = OptionParser()
  parser.add_option("--appname",
                    action = "store", type = "string", dest = "app",
                    default = os.path.join(SCRIPT_DIRECTORY, automation.DEFAULT_APP),
                    help = "absolute path to application, overriding default")
  parser.add_option("--extra-profile-file",
                    action = "append", dest = "extraProfileFiles",
                    default = [],
                    help = "copy specified files/dirs to testing profile")
  options, args = parser.parse_args()

  if len(args) != 1:
    print >>sys.stderr, "No reftest.list specified."
    sys.exit(1)

  options.app = getFullPath(options.app)
  if not os.path.exists(options.app):
    print """Error: Path %(app)s doesn't exist.
Are you executing $objdir/_tests/reftest/runreftest.py?""" \
        % {"app": options.app}
    sys.exit(1)

  profileDir = None
  try:
    profileDir = mkdtemp()
    createReftestProfile(profileDir)
    copyExtraFilesToProfile(options, profileDir)

    # browser environment
    browserEnv = dict(os.environ)

    # These variables are necessary for correct application startup; change
    # via the commandline at your own risk.
    browserEnv["NO_EM_RESTART"] = "1"
    browserEnv["XPCOM_DEBUG_BREAK"] = "warn"
    appDir = os.path.dirname(options.app)
    if automation.UNIXISH:
      browserEnv["LD_LIBRARY_PATH"] = appDir
      browserEnv["MOZILLA_FIVE_HOME"] = appDir
      browserEnv["GNOME_DISABLE_CRASH_DIALOG"] = "1"

    # Retrieve the logger where to report the leaks to.
    log = logging.getLogger()

    # Enable leaks (only) detection to its own log file.
    leakLogFile = os.path.join(profileDir, "runreftest_leaks.log")
    browserEnv["XPCOM_MEM_LEAK_LOG"] = leakLogFile

    def processLeakLog():
      "Process the leak log."
      # For the time being, don't warn (nor "info") if the log file is not there. (Bug 469518)
      if os.path.exists(leakLogFile):
        leaks = open(leakLogFile, "r")
        # For the time being, simply copy the log. (Bug 469518)
        log.info(leaks.read().rstrip())
        leaks.close()

    # run once with -silent to let the extension manager do its thing
    # and then exit the app
    log.info("REFTEST INFO | runreftest.py | Performing extension manager registration: start.\n")
    status = automation.runApp(None, browserEnv, options.app, profileDir,
                               extraArgs = ["-silent"])
    # We don't care to call |processLeakLog()| for this step.
    log.info("\nREFTEST INFO | runreftest.py | Performing extension manager registration: end.")

    # then again to actually run reftest
    log.info("REFTEST INFO | runreftest.py | Running tests: start.\n")
    reftestlist = getFullPath(args[0])
    status = automation.runApp(None, browserEnv, options.app, profileDir,
                               extraArgs = ["-reftest", reftestlist])
    processLeakLog()
    log.info("\nREFTEST INFO | runreftest.py | Running tests: end.")
  finally:
    if profileDir is not None:
      shutil.rmtree(profileDir)
  sys.exit(status)

def copyExtraFilesToProfile(options, profileDir):
  "Copy extra files or dirs specified on the command line to the testing profile."
  for f in options.extraProfileFiles:
    abspath = getFullPath(f)
    dest = os.path.join(profileDir, os.path.basename(abspath))
    if os.path.isdir(abspath):
      shutil.copytree(abspath, dest)
    else:
      shutil.copy(abspath, dest)

if __name__ == "__main__":
  main()
