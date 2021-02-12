$ErrorActionPreference = "Stop";
trap { $host.SetShouldExit(1) }

#bazel-harness
# runs the test harness via bazel
bazel "--output_base=c:\_pgv" "--bazelrc=windows\bazel.rc" run //tests/harness/executor:executor
exit $LASTEXITCODE
