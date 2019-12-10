#! /bin/bash -ex

JOBS=`nproc` || JOBS=4
python3 -m pip install pytype==2019.11.27

python3 -m pytype --keep-going -j "$JOBS" --strict-import --config "setup.cfg"
