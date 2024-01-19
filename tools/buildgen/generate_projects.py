# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import glob
import multiprocessing
import os
import pickle
import shutil
import sys
import tempfile
from typing import Dict, List, Union

import _utils
import yaml

PROJECT_ROOT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", ".."
)
os.chdir(PROJECT_ROOT)
# TODO(lidiz) find a better way for plugins to reference each other
sys.path.append(os.path.join(PROJECT_ROOT, "tools", "buildgen", "plugins"))

# from tools.run_tests.python_utils import jobset
jobset = _utils.import_python_module(
    os.path.join(
        PROJECT_ROOT, "tools", "run_tests", "python_utils", "jobset.py"
    )
)

PREPROCESSED_BUILD = ".preprocessed_build"
test = {} if os.environ.get("TEST", "false") == "true" else None

assert sys.argv[1:], "run generate_projects.sh instead of this directly"
parser = argparse.ArgumentParser()
parser.add_argument(
    "build_files",
    nargs="+",
    default=[],
    help="build files describing build specs",
)
parser.add_argument(
    "--templates", nargs="+", default=[], help="mako template files to render"
)
parser.add_argument(
    "--output_merged",
    "-m",
    default="",
    type=str,
    help="merge intermediate results to a file",
)
parser.add_argument(
    "--jobs",
    "-j",
    default=multiprocessing.cpu_count(),
    type=int,
    help="maximum parallel jobs",
)
parser.add_argument(
    "--base", default=".", type=str, help="base path for generated files"
)
args = parser.parse_args()


def preprocess_build_files() -> _utils.Bunch:
    """Merges build yaml into a one dictionary then pass it to plugins."""
    build_spec = dict()
    for build_file in args.build_files:
        with open(build_file, "r") as f:
            _utils.merge_json(build_spec, yaml.safe_load(f.read()))
    # Executes plugins. Plugins update the build spec in-place.
    for py_file in sorted(glob.glob("tools/buildgen/plugins/*.py")):
        plugin = _utils.import_python_module(py_file)
        plugin.mako_plugin(build_spec)
    if args.output_merged:
        with open(args.output_merged, "w") as f:
            f.write(yaml.dump(build_spec))
    # Makes build_spec sort of immutable and dot-accessible
    return _utils.to_bunch(build_spec)


def generate_template_render_jobs(templates: List[str]) -> List[jobset.JobSpec]:
    """Generate JobSpecs for each one of the template rendering work."""
    jobs = []
    base_cmd = [sys.executable, "tools/buildgen/_mako_renderer.py"]
    for template in sorted(templates, reverse=True):
        root, f = os.path.split(template)
        if os.path.splitext(f)[1] == ".template":
            out_dir = args.base + root[len("templates") :]
            out = os.path.join(out_dir, os.path.splitext(f)[0])
            if not os.path.exists(out_dir):
                os.makedirs(out_dir)
            cmd = base_cmd[:]
            cmd.append("-P")
            cmd.append(PREPROCESSED_BUILD)
            cmd.append("-o")
            if test is None:
                cmd.append(out)
            else:
                tf = tempfile.mkstemp()
                test[out] = tf[1]
                os.close(tf[0])
                cmd.append(test[out])
            cmd.append(args.base + "/" + root + "/" + f)
            jobs.append(
                jobset.JobSpec(cmd, shortname=out, timeout_seconds=None)
            )
    return jobs


def main() -> None:
    templates = args.templates
    if not templates:
        for root, _, files in os.walk("templates"):
            for f in files:
                templates.append(os.path.join(root, f))

    build_spec = preprocess_build_files()
    with open(PREPROCESSED_BUILD, "wb") as f:
        pickle.dump(build_spec, f)

    err_cnt, _ = jobset.run(
        generate_template_render_jobs(templates), maxjobs=args.jobs
    )
    if err_cnt != 0:
        print(
            "ERROR: %s error(s) found while generating projects." % err_cnt,
            file=sys.stderr,
        )
        sys.exit(1)

    if test is not None:
        for s, g in test.items():
            if os.path.isfile(g):
                assert 0 == os.system("diff %s %s" % (s, g)), s
                os.unlink(g)
            else:
                assert 0 == os.system("diff -r %s %s" % (s, g)), s
                shutil.rmtree(g, ignore_errors=True)


if __name__ == "__main__":
    main()
