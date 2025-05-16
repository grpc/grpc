
set +e

# install pre-requisites for gRPC C core build
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python3 python3-pip clang

fail () {
    echo "$(tput setaf 1) $1 $(tput sgr 0)"
    if [[ -n $SERVER_PID ]] ; then
      kill -9 $SERVER_PID || true
    fi
    SERVER_PID=
    exit 1
}

pass () {
    echo "$(tput setaf 2) $1 $(tput sgr 0)"
}

# For linux we use secure_getenv to read env variable
# this return null if linux capabilites are added on the executable
# For customer using linux capability , they need to define "--define GRPC_FORCE_UNSECURE_GETENV=1"
# to read env variables.
# enable log

tools/bazel version

# Build without the define , this will use secure_getenv
tools/bazel build  //examples/cpp/helloworld:greeter_callback_client
# Add linux capability
sudo setcap "cap_net_raw=ep" ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
getcap ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
export GRPC_TRACE=http
./bazel-bin/examples/cpp/helloworld/greeter_callback_client &> wow.log
cat wow.log
# We should not see log get enabled as secure_getenv will return null in this case
grep "gRPC Tracers:" wow.log
return_code=$?
if [[ $return_code -eq 0 ]]; then
    fail "Able to read env variable with linux capability set"
fi

# Build using the define to force "getenv" instead of "secure_getenv"
tools/bazel build  --define GRPC_FORCE_UNSECURE_GETENV=1 //examples/cpp/helloworld:greeter_callback_client
# Add linux capability
sudo setcap "cap_net_raw=ep" ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
ls -lrt ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
./bazel-bin/examples/cpp/helloworld/greeter_callback_client &> wow.log
grep "gRPC Tracers:" wow.log
return_code=$?
# check if logs got enabled
if [[ ! $return_code -eq 0 ]]; then
    fail "Fail to read env variable with linux capability"
fi