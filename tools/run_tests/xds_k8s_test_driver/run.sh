#!/usr/bin/env bash
set -eo pipefail

readonly XDS_K8S_DRIVER_DIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
readonly XDS_K8S_DRIVER_VENV_DIR="${XDS_K8S_DRIVER_VENV_DIR:-$XDS_K8S_DRIVER_DIR/venv}"
readonly XDS_K8S_CONFIG="${XDS_K8S_CONFIG:-$XDS_K8S_DRIVER_DIR/config/local-dev.cfg}"

display_usage() {
  cat <<EOF >/dev/stderr
Convenience script to execute tests/ and helper bin/ scripts.

USAGE: $0 script_path [arguments]
   script_path: path to python script to execute, relative to driver root folder
   arguments ...: arguments passed to program in sys.argv

ENVIRONMENT:
   XDS_K8S_CONFIG: file path to the config flagfile, relative to
                   driver root folder. Default: config/local-dev.cfg
                   Will be appended as --flagfile="config_absolute_path" argument
   XDS_K8S_DRIVER_VENV_DIR: the path to python virtual environment directory
                            Default: $XDS_K8S_DRIVER_DIR/venv
DESCRIPTION:
This tool performs the following:
1) Ensures python virtual env installed and activated
2) Exports test driver root in PYTHONPATH
3) Automatically appends --flagfile="\$XDS_K8S_CONFIG" argument

EXAMPLES:
$0 bin/run_td_setup.py --help      # list script-specific options
$0 bin/run_td_setup.py --helpfull  # list all available options
XDS_K8S_CONFIG=./path-to-flagfile.cfg $0 bin/run_td_setup.py --namespace=override-namespace
$0 tests/baseline_test.py
$0 tests/security_test.py --verbosity=1 --logger_levels=__main__:DEBUG,framework:DEBUG
$0 tests/security_test.py SecurityTest.test_mtls --nocheck_local_certs
EOF
  exit 1
}

if [[ "$#" -eq 0 || "$1" = "-h" || "$1" = "--help" ]]; then
  display_usage
fi

if [[ -z "${VIRTUAL_ENV}" ]]; then
  if [[ -d "${XDS_K8S_DRIVER_VENV_DIR}" ]]; then
    # Intentional: No need to check python venv activate script.
    # shellcheck source=/dev/null
    source "${XDS_K8S_DRIVER_VENV_DIR}/bin/activate"
  else
    echo "Missing python virtual environment directory: ${XDS_K8S_DRIVER_VENV_DIR}" >&2
    echo "Follow README.md installation steps first." >&2
    exit 1
  fi
fi

cd "${XDS_K8S_DRIVER_DIR}"
export PYTHONPATH="${XDS_K8S_DRIVER_DIR}"
exec python "$@" --flagfile="${XDS_K8S_CONFIG}"
