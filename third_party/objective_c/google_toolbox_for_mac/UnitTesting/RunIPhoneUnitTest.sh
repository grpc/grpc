#!/bin/bash
#  RunIPhoneUnitTest.sh
#  Copyright 2008 Google Inc.
#
#  Licensed under the Apache License, Version 2.0 (the "License"); you may not
#  use this file except in compliance with the License.  You may obtain a copy
#  of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
#  License for the specific language governing permissions and limitations under
#  the License.
#
#  Runs all unittests through the iPhone simulator. We don't handle running them
#  on the device. To run on the device just choose "run".

set -o errexit
set -o nounset
# Uncomment the next line to trace execution.
#set -o verbose

#  Controlling environment variables:
# GTM_DISABLE_ZOMBIES -
#   Set to a non-zero value to turn on zombie checks. You will probably
#   want to turn this off if you enable leaks.
GTM_DISABLE_ZOMBIES=${GTM_DISABLE_ZOMBIES:=1}

# GTM_DISABLE_TERMINATION
#   Set to a non-zero value so that the app doesn't terminate when it's finished
#   running tests. This is useful when using it with external tools such
#   as Instruments.

# GTM_REMOVE_GCOV_DATA
#   Before starting the test, remove any *.gcda files for the current run so
#   you won't get errors when the source file has changed and the data can't
#   be merged.
#
GTM_REMOVE_GCOV_DATA=${GTM_REMOVE_GCOV_DATA:=0}

# GTM_DISABLE_USERDIR_SETUP
#   Controls whether or not CFFIXED_USER_HOME is erased and set up from scratch
#   for you each time the script is run. In some cases you may have a wrapper
#   script calling this one that takes care of that for us so you can set up
#   a certain user configuration.
GTM_DISABLE_USERDIR_SETUP=${GTM_DISABLE_USERDIR_SETUP:=0}

# GTM_DISABLE_IPHONE_LAUNCH_DAEMONS
#   Controls whether or not we launch up the iPhone Launch Daemons before
#   we start testing. You need Launch Daemons to test anything that interacts
#   with security. Note that it is OFF by default. Set
#   GTM_DISABLE_IPHONE_LAUNCH_DAEMONS=0 before calling this script
#   to turn it on.
GTM_DISABLE_IPHONE_LAUNCH_DAEMONS=${GTM_DISABLE_IPHONE_LAUNCH_DAEMONS:=1}

# GTM_TEST_AFTER_BUILD
#   When set to 1, tests are run only when TEST_AFTER_BUILD is set to "YES".
#   This can be used to have tests run as an after build step when running
#   from the command line, but not when running from within XCode.
GTM_USE_TEST_AFTER_BUILD=${GTM_USE_TEST_AFTER_BUILD:=0}

ScriptDir=$(dirname "$(echo $0 | sed -e "s,^\([^/]\),$(pwd)/\1,")")
ScriptName=$(basename "$0")
ThisScript="${ScriptDir}/${ScriptName}"
XCODE_VERSION_MINOR=${XCODE_VERSION_MINOR:=0000}

GTMXcodeNote() {
    echo "${ThisScript}:${1}: note: GTM ${2}"
}

GTMXcodeError() {
    echo "${ThisScript}:${1}: error: GTM ${2}"
}

# Creates a file containing the plist for the securityd daemon and prints the
# filename to stdout.
GTMCreateLaunchDaemonPlist() {
  local plist_file
  plist_file="$TMPDIR/securityd.$$.plist"
  echo $plist_file

  # Create the plist file with PlistBuddy.
  /usr/libexec/PlistBuddy \
    -c "Add :Label string RunIPhoneLaunchDaemons" \
    -c "Add :ProgramArguments array" \
        -c "Add :ProgramArguments: string \"$IPHONE_SIMULATOR_ROOT/usr/libexec/securityd\"" \
    -c "Add :EnvironmentVariables dict" \
        -c "Add :EnvironmentVariables:DYLD_ROOT_PATH string \"$IPHONE_SIMULATOR_ROOT\"" \
        -c "Add :EnvironmentVariables:IPHONE_SIMULATOR_ROOT string \"$IPHONE_SIMULATOR_ROOT\"" \
        -c "Add :EnvironmentVariables:CFFIXED_USER_HOME string \"$CFFIXED_USER_HOME\"" \
    -c "Add :MachServices dict" \
        -c "Add :MachServices:com.apple.securityd bool YES" "$plist_file" > /dev/null
}

if [[ "$GTM_USE_TEST_AFTER_BUILD" == 1 && "$TEST_AFTER_BUILD" == "NO" ]]; then
  GTMXcodeNote ${LINENO} "Skipping running of unittests since TEST_AFTER_BUILD=NO."
elif [ "$PLATFORM_NAME" == "iphonesimulator" ]; then
  # Xcode 4.5 changed how simulator app can be run. The way this script has
  # been working results in them now just logging a message and calling exit(0)
  # from Apple code. Report the error that the tests aren't going to work.
  if [[ "${XCODE_VERSION_MINOR}" -ge "0450" ]]; then
    GTMXcodeError ${LINENO} \
      "Unit testing process not supported on Xcode >= 4.5 (${XCODE_VERSION_MINOR}). Use RuniOSUnitTestsUnderSimulator.sh."
    exit 1
  fi

  # We kill the iPhone simulator because otherwise we run into issues where
  # the unittests fail becuase the simulator is currently running, and
  # at this time the iPhone SDK won't allow two simulators running at the same
  # time.
  set +e
  /usr/bin/killall "iPhone Simulator"
  set -e

  if [ $GTM_REMOVE_GCOV_DATA -ne 0 ]; then
    if [ "${OBJECT_FILE_DIR}-${CURRENT_VARIANT}" != "-" ]; then
      if [ -d "${OBJECT_FILE_DIR}-${CURRENT_VARIANT}" ]; then
        GTMXcodeNote ${LINENO} "Removing any .gcda files"
        (cd "${OBJECT_FILE_DIR}-${CURRENT_VARIANT}" && \
            find . -type f -name "*.gcda" -print0 | xargs -0 rm -f )
      fi
    fi
  fi

  export DYLD_ROOT_PATH="$SDKROOT"
  export DYLD_FRAMEWORK_PATH="$CONFIGURATION_BUILD_DIR"
  export IPHONE_SIMULATOR_ROOT="$SDKROOT"
  export CFFIXED_USER_HOME="$TEMP_FILES_DIR/iPhone Simulator User Dir"

  # See http://developer.apple.com/technotes/tn2004/tn2124.html for an
  # explanation of these environment variables.

  # NOTE: any setup work is done before turning on the environment variables
  # to avoid having the setup work also get checked by what the variables
  # enabled.

  if [ $GTM_DISABLE_USERDIR_SETUP -eq 0 ]; then
    # Cleanup user home directory
    if [ -d "$CFFIXED_USER_HOME" ]; then
      rm -rf "$CFFIXED_USER_HOME"
    fi
    mkdir "$CFFIXED_USER_HOME"
    mkdir "$CFFIXED_USER_HOME/Documents"
    mkdir -p "$CFFIXED_USER_HOME/Library/Caches"
  fi

  if [ $GTM_DISABLE_IPHONE_LAUNCH_DAEMONS -eq 0 ]; then
    # Remove any instance of RunIPhoneLaunchDaemons left running in the case the
    # 'trap' below fails. We first must check for RunIPhoneLaunchDaemons'
    # presence as 'launchctl remove' will kill this script if run from within an
    # Xcode build.
    launchctl list | grep RunIPhoneLaunchDaemons && launchctl remove RunIPhoneLaunchDaemons

    # If we want to test anything that interacts with the keychain, we need
    # securityd up and running.
    LAUNCH_DAEMON_PLIST="$(GTMCreateLaunchDaemonPlist)"
    launchctl load $LAUNCH_DAEMON_PLIST
    rm $LAUNCH_DAEMON_PLIST

    # No matter how we exit, we want to shut down our launchctl job.
    trap "launchctl remove RunIPhoneLaunchDaemons" INT TERM EXIT
  fi

  if [ $GTM_DISABLE_ZOMBIES -eq 0 ]; then
    GTMXcodeNote ${LINENO} "Enabling zombies"
    export CFZombieLevel=3
    export NSZombieEnabled=YES
  fi

  export MallocScribble=YES
  export MallocPreScribble=YES
  export MallocGuardEdges=YES
  export MallocStackLogging=YES
  export NSAutoreleaseFreedObjectCheckEnabled=YES

  # Turn on the mostly undocumented OBJC_DEBUG stuff.
  export OBJC_DEBUG_FRAGILE_SUPERCLASSES=YES
  export OBJC_DEBUG_UNLOAD=YES
  # Turned off due to the amount of false positives from NS classes.
  # export OBJC_DEBUG_FINALIZERS=YES
  export OBJC_DEBUG_NIL_SYNC=YES
  export OBJC_PRINT_REPLACED_METHODS=YES

  # Start our app.
  "$TARGET_BUILD_DIR/$EXECUTABLE_PATH" -RegisterForSystemEvents

else
  GTMXcodeNote ${LINENO} "Skipping running of unittests for device build."
fi
exit 0
