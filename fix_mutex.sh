#!/bin/bash

EXCLUDE_FILE_ARGS=(
    -g '!src/core/util/sync.h' # gotta patch.
    -g '!src/core/lib/promise/promise_mutex.h' 
    -g '!test/core/promise/promise_mutex_test.cc' 
    -g '!src/core/lib/promise/inter_activity_mutex.h' 
    -g '!test/core/promise/inter_activity_mutex*' 
  )

test() {
  rg '\bUnlock\(\)' -t cpp ${EXCLUDE_FILE_ARGS[@]}
}

if [[ -z "$1" ]]; then
  test
else
  "$@"
fi
