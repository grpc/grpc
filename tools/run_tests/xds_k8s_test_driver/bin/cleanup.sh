#!/usr/bin/env bash
set -eo pipefail

display_usage() {
  cat <<EOF >/dev/stderr
Performs full TD and K8S resource cleanup

USAGE: $0 [--secure] [arguments]
   --secure: Perform TD resource cleanup specific for PSM Security setup
   arguments ...: additional arguments passed to ./run.sh

ENVIRONMENT:
   XDS_K8S_CONFIG: file path to the config flagfile, relative to
                   driver root folder. Default: config/local-dev.cfg
                   Will be appended as --flagfile="config_absolute_path" argument
   XDS_K8S_DRIVER_VENV_DIR: the path to python virtual environment directory
                            Default: $XDS_K8S_DRIVER_DIR/venv
EXAMPLES:
$0
$0 --secure
XDS_K8S_CONFIG=./path-to-flagfile.cfg $0 --namespace=override-namespace
EOF
  exit 1
}

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
  display_usage
fi

readonly SCRIPT_DIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
readonly XDS_K8S_DRIVER_DIR="${SCRIPT_DIR}/.."

cd "${XDS_K8S_DRIVER_DIR}"

if [[ "$1" == "--secure" ]]; then
  shift
  ./run.sh bin/run_td_setup.py --cmd=cleanup --security=mtls "$@" && \
  ./run.sh bin/run_test_client.py --cmd=cleanup --secure "$@" && \
  ./run.sh bin/run_test_server.py --cmd=cleanup --cleanup_namespace --secure "$@"
else
  ./run.sh bin/run_td_setup.py --cmd=cleanup "$@" && \
  ./run.sh bin/run_test_client.py --cmd=cleanup "$@" && \
  ./run.sh bin/run_test_server.py --cmd=cleanup --cleanup_namespace "$@"
fi
