#!/bin/bash

set -exo pipefail

VENV_NAME="venv-${RANDOM}"

python3 -m virtualenv "${VENV_NAME}"

"${VENV_NAME}"/bin/pip install -r requirements.txt
"${VENV_NAME}"/bin/pip freeze >requirements.lock

rm -rf "${VENV_NAME}"

