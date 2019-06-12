#!/bin/bash
#
#  RunMacOSUnitTests.sh
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
#  Run the unit tests in this test bundle.
#  Set up some env variables to make things as likely to crash as possible.
#  See http://developer.apple.com/technotes/tn2004/tn2124.html for details.
#

set -o errexit
set -o nounset
# Uncomment the next line to trace execution.
#set -o verbose

# Required to make Xcode 6 actually run tests.
export TEST_AFTER_BUILD=YES

# Controlling environment variables:
#
# GTM_DISABLE_ZOMBIES -
#   Set to a non-zero value to turn on zombie checks. You will probably
#   want to turn this off if you enable leaks.
GTM_DISABLE_ZOMBIES=${GTM_DISABLE_ZOMBIES:=0}

# GTM_DO_NOT_REMOVE_GCOV_DATA
#   By default before starting the test, we remove any *.gcda files for the
#   current project build configuration so you won't get errors when a source
#   file has changed and the gcov data can't be merged.
#   We remove all the gcda files for the  current configuration for the entire
#   project so that if you are building a test bundle to test another separate
#   bundle we make sure to clean up the files for the test bundle and the bundle
#   that you are testing.
#   If you DO NOT want this to occur, set GTM_DO_NOT_REMOVE_GCOV_DATA to a
#   non-zero value.
GTM_DO_NOT_REMOVE_GCOV_DATA=${GTM_DO_NOT_REMOVE_GCOV_DATA:=0}

# GTM_REMOVE_TARGET_GCOV_ONLY
#   By default all *.gcda files are removed form the project.  Setting this to
#   1 causes only the *.gcda files for the target to be removed.
#   If GTM_DO_NOT_REMOVE_GCOV_DATA is set, this has no effect.
GTM_REMOVE_TARGET_GCOV_ONLY=${GTM_REMOVE_TARGET_GCOV_ONLY:=0}

# GTM_ONE_TEST_AT_A_TIME
#   By default your tests run how ever parallel your projects/targets are
#   setup.  Setting this to 1 will cause only one to run at a time, this is
#   useful if you are doing UI tests with the helper that controls the
#   colorsync profile, or any other machine wide state.
GTM_ONE_TEST_AT_A_TIME=${GTM_ONE_TEST_AT_A_TIME:=0}

ScriptDir=$(dirname "$(echo $0 | sed -e "s,^\([^/]\),$(pwd)/\1,")")
ScriptName=$(basename "$0")
ThisScript="${ScriptDir}/${ScriptName}"

GTMXcodeNote() {
  echo ${ThisScript}:${1}: note: GTM ${2}
}

# Helper that works like the linux flock util, so you can run something, but
# have only one run at a time.
MaybeFlock() {
  if [ $GTM_ONE_TEST_AT_A_TIME -ne 0 ]; then
    GTMXcodeNote ${LINENO} "Serializing test execution."
    python -c "import fcntl, subprocess, sys
file = open('$TMPDIR/GTM_one_test_at_a_time', 'a')
fcntl.flock(file.fileno(), fcntl.LOCK_EX)
sys.exit(subprocess.call(sys.argv[1:]))" "${@}"
  else
    GTMXcodeNote ${LINENO} "Allowing parallel test execution."
    "${@}"
  fi
}

SetMemoryVariables() {
  # Jack up some memory stress so we can catch more bugs.

  # This is done via a helper so it can be invoked in two places at the
  # last possible moment to avoid the variables causing tracing from other
  # processes that are invoked along the way.

  if [ $GTM_DISABLE_ZOMBIES -eq 0 ]; then
    GTMXcodeNote ${LINENO} "Enabling zombies"
    # CFZombieLevel disabled because it doesn't play well with the
    # security framework
    # export CFZombieLevel=3
    export NSZombieEnabled=YES
  fi

  export MallocScribble=YES
  export MallocPreScribble=YES
  export MallocGuardEdges=YES
  export NSAutoreleaseFreedObjectCheckEnabled=YES

  # Turn on the mostly undocumented OBJC_DEBUG stuff.
  export OBJC_DEBUG_FRAGILE_SUPERCLASSES=YES
  export OBJC_DEBUG_UNLOAD=YES
  # Turned off due to the amount of false positives from NS classes.
  # export OBJC_DEBUG_FINALIZERS=YES
  export OBJC_DEBUG_NIL_SYNC=YES
}

if [ ! $GTM_DO_NOT_REMOVE_GCOV_DATA ]; then
  GTM_GCOV_CLEANUP_DIR="${CONFIGURATION_TEMP_DIR}"
  if [ $GTM_REMOVE_TARGET_GCOV_ONLY ]; then
    GTM_GCOV_CLEANUP_DIR="${OBJECT_FILE_DIR}-${CURRENT_VARIANT}"
  fi
  if [ "${GTM_GCOV_CLEANUP_DIR}" != "-" ]; then
    if [ -d "${GTM_GCOV_CLEANUP_DIR}" ]; then
      GTMXcodeNote ${LINENO} "Removing gcov data files from ${GTM_GCOV_CLEANUP_DIR}"
      (cd "${GTM_GCOV_CLEANUP_DIR}" && \
        find . -type f -name "*.gcda" -print0 | xargs -0 rm -f )
    fi
  fi
fi

SetMemoryVariables
MaybeFlock "${SYSTEM_DEVELOPER_DIR}/Tools/RunTargetUnitTests"
