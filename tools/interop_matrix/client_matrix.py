# Copyright 2017 gRPC authors.
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

# Defines languages, runtimes and releases for backward compatibility testing

from collections import OrderedDict


def get_github_repo(lang):
    return {
        "dart": "https://github.com/grpc/grpc-dart.git",
        "go": "https://github.com/grpc/grpc-go.git",
        "java": "https://github.com/grpc/grpc-java.git",
        "node": "https://github.com/grpc/grpc-node.git",
        # all other languages use the grpc.git repo.
    }.get(lang, "https://github.com/grpc/grpc.git")


def get_release_tags(lang):
    """Returns list of known releases for given language."""
    return list(LANG_RELEASE_MATRIX[lang].keys())


def get_runtimes_for_lang_release(lang, release):
    """Get list of valid runtimes for given release of lang."""
    runtimes = list(LANG_RUNTIME_MATRIX[lang])
    release_info = LANG_RELEASE_MATRIX[lang].get(release)
    if release_info and release_info.runtimes:
        runtimes = list(release_info.runtimes)
    return runtimes


def should_build_docker_interop_image_from_release_tag(lang):
    # All dockerfile definitions live in grpc/grpc repository.
    # For language that have a separate repo, we need to use
    # dockerfile definitions from head of grpc/grpc.
    if lang in ["go", "java", "node"]:
        return False
    return True


# Dictionary of default runtimes per language
LANG_RUNTIME_MATRIX = {
    "cxx": ["cxx"],  # This is actually debian8.
    "go": ["go1.8", "go1.11", "go1.16", "go1.19"],
    "java": ["java"],
    "python": ["python", "pythonasyncio"],
    "node": ["node"],
    "ruby": ["ruby"],
    "php": ["php7"],
    "csharp": ["csharp", "csharpcoreclr"],
}


class ReleaseInfo:
    """Info about a single release of a language"""

    def __init__(self, patch=[], runtimes=[], testcases_file=None):
        self.patch = patch
        self.runtimes = runtimes
        self.testcases_file = testcases_file


# Dictionary of known releases for given language.
LANG_RELEASE_MATRIX = {
    "cxx": OrderedDict(
        [
            ("v1.0.1", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.1.4", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.2.5", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.3.9", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.4.2", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.6.6", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.7.2", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.8.0", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.9.1", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.10.1", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.11.1", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.12.0", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.13.0", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.14.1", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.15.0", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.16.0", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.17.1", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.18.0", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.19.0", ReleaseInfo(testcases_file="cxx__v1.0.1")),
            ("v1.20.0", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.21.4", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.22.0", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.22.1", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.23.0", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.24.0", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.25.0", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.26.0", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.27.3", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.30.0", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.31.1", ReleaseInfo(testcases_file="cxx__v1.31.1")),
            ("v1.32.0", ReleaseInfo()),
            ("v1.33.2", ReleaseInfo()),
            ("v1.34.0", ReleaseInfo()),
            ("v1.35.0", ReleaseInfo()),
            ("v1.36.3", ReleaseInfo()),
            ("v1.37.0", ReleaseInfo()),
            ("v1.38.0", ReleaseInfo()),
            ("v1.39.0", ReleaseInfo()),
            ("v1.41.1", ReleaseInfo()),
            ("v1.42.0", ReleaseInfo()),
            ("v1.43.0", ReleaseInfo()),
            ("v1.44.0", ReleaseInfo()),
            ("v1.46.2", ReleaseInfo()),
            ("v1.47.1", ReleaseInfo()),
            ("v1.48.3", ReleaseInfo()),
            ("v1.49.1", ReleaseInfo()),
            ("v1.52.0", ReleaseInfo()),
            ("v1.53.0", ReleaseInfo()),
            ("v1.54.0", ReleaseInfo()),
            ("v1.55.0", ReleaseInfo()),
            ("v1.56.0", ReleaseInfo()),
            ("v1.57.0", ReleaseInfo()),
            ("v1.58.0", ReleaseInfo()),
        ]
    ),
    "go": OrderedDict(
        [
            (
                "v1.0.5",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.2.1",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.3.0",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.4.2",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.5.2",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.6.0",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.7.4",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.8.2",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.9.2",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.10.1",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.11.3",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.12.2",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.13.0",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.14.0",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.15.0",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.16.0",
                ReleaseInfo(runtimes=["go1.8"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.17.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.18.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.19.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.0.5"),
            ),
            (
                "v1.20.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.21.3",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.22.3",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.23.1",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.24.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.25.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.26.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.27.1",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.28.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.29.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.30.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.31.1",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.32.0",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            (
                "v1.33.1",
                ReleaseInfo(runtimes=["go1.11"], testcases_file="go__v1.20.0"),
            ),
            ("v1.34.0", ReleaseInfo(runtimes=["go1.11"])),
            ("v1.35.0", ReleaseInfo(runtimes=["go1.11"])),
            ("v1.36.0", ReleaseInfo(runtimes=["go1.11"])),
            ("v1.37.0", ReleaseInfo(runtimes=["go1.11"])),
            # NOTE: starting from release v1.38.0, use runtimes=['go1.16']
            ("v1.38.1", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.39.1", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.40.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.41.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.42.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.43.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.44.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.45.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.46.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.47.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.48.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.49.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.50.1", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.51.0", ReleaseInfo(runtimes=["go1.16"])),
            ("v1.52.3", ReleaseInfo(runtimes=["go1.19"])),
            ("v1.53.0", ReleaseInfo(runtimes=["go1.19"])),
            ("v1.54.1", ReleaseInfo(runtimes=["go1.19"])),
            ("v1.55.0", ReleaseInfo(runtimes=["go1.19"])),
            ("v1.56.2", ReleaseInfo(runtimes=["go1.19"])),
            ("v1.57.0", ReleaseInfo(runtimes=["go1.19"])),
            ("v1.58.0", ReleaseInfo(runtimes=["go1.19"])),
        ]
    ),
    "java": OrderedDict(
        [
            (
                "v1.0.3",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.1.2",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.2.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.3.1",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.4.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.5.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.6.1",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            ("v1.7.1", ReleaseInfo(testcases_file="java__v1.0.3")),
            (
                "v1.8.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.9.1",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.10.1",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.11.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            ("v1.12.1", ReleaseInfo(testcases_file="java__v1.0.3")),
            ("v1.13.2", ReleaseInfo(testcases_file="java__v1.0.3")),
            (
                "v1.14.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            ("v1.15.1", ReleaseInfo(testcases_file="java__v1.0.3")),
            (
                "v1.16.1",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            ("v1.17.2", ReleaseInfo(testcases_file="java__v1.0.3")),
            (
                "v1.18.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            (
                "v1.19.0",
                ReleaseInfo(
                    runtimes=["java_oracle8"], testcases_file="java__v1.0.3"
                ),
            ),
            ("v1.20.0", ReleaseInfo(runtimes=["java_oracle8"])),
            ("v1.21.1", ReleaseInfo()),
            ("v1.22.2", ReleaseInfo()),
            ("v1.23.0", ReleaseInfo()),
            ("v1.24.0", ReleaseInfo()),
            ("v1.25.0", ReleaseInfo()),
            ("v1.26.1", ReleaseInfo()),
            ("v1.27.2", ReleaseInfo()),
            ("v1.28.1", ReleaseInfo()),
            ("v1.29.0", ReleaseInfo()),
            ("v1.30.2", ReleaseInfo()),
            ("v1.31.2", ReleaseInfo()),
            ("v1.32.3", ReleaseInfo()),
            ("v1.33.1", ReleaseInfo()),
            ("v1.34.1", ReleaseInfo()),
            ("v1.35.1", ReleaseInfo()),
            ("v1.36.3", ReleaseInfo()),
            ("v1.37.1", ReleaseInfo()),
            ("v1.38.1", ReleaseInfo()),
            ("v1.39.0", ReleaseInfo()),
            ("v1.40.2", ReleaseInfo()),
            ("v1.41.3", ReleaseInfo()),
            ("v1.42.3", ReleaseInfo()),
            ("v1.43.3", ReleaseInfo()),
            ("v1.44.2", ReleaseInfo()),
            ("v1.45.4", ReleaseInfo()),
            ("v1.46.1", ReleaseInfo()),
            ("v1.47.1", ReleaseInfo()),
            ("v1.48.2", ReleaseInfo()),
            ("v1.49.2", ReleaseInfo()),
            ("v1.50.3", ReleaseInfo()),
            ("v1.51.3", ReleaseInfo()),
            ("v1.52.1", ReleaseInfo()),
            ("v1.53.0", ReleaseInfo()),
            ("v1.54.0", ReleaseInfo()),
            ("v1.55.1", ReleaseInfo()),
            ("v1.56.0", ReleaseInfo()),
            ("v1.57.2", ReleaseInfo()),
        ]
    ),
    "python": OrderedDict(
        [
            (
                "v1.0.x",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.1.4",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.2.5",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.3.9",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.4.2",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.6.6",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.7.2",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.8.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.9.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.10.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.0.x"
                ),
            ),
            (
                "v1.11.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.11.1"
                ),
            ),
            (
                "v1.12.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.11.1"
                ),
            ),
            (
                "v1.13.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.11.1"
                ),
            ),
            (
                "v1.14.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.11.1"
                ),
            ),
            (
                "v1.15.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.11.1"
                ),
            ),
            (
                "v1.16.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.11.1"
                ),
            ),
            (
                "v1.17.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.11.1"
                ),
            ),
            (
                "v1.18.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.19.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.20.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.21.4",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.22.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.22.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.23.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.24.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.25.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.26.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.27.3",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.30.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.31.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.32.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.33.2",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.34.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.35.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.36.3",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.37.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.38.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.39.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.18.0"
                ),
            ),
            (
                "v1.41.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.41.1"
                ),
            ),
            (
                "v1.42.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.41.1"
                ),
            ),
            (
                "v1.43.2",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__v1.41.1"
                ),
            ),
            (
                "v1.44.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.46.2",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.47.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.48.3",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.49.1",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.52.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.53.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.54.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.55.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.56.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.57.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
            (
                "v1.58.0",
                ReleaseInfo(
                    runtimes=["python"], testcases_file="python__master"
                ),
            ),
        ]
    ),
    "node": OrderedDict(
        [
            ("v1.0.1", ReleaseInfo(testcases_file="node__v1.0.1")),
            ("v1.1.4", ReleaseInfo(testcases_file="node__v1.1.4")),
            ("v1.2.5", ReleaseInfo(testcases_file="node__v1.1.4")),
            ("v1.3.9", ReleaseInfo(testcases_file="node__v1.1.4")),
            ("v1.4.2", ReleaseInfo(testcases_file="node__v1.1.4")),
            ("v1.6.6", ReleaseInfo(testcases_file="node__v1.1.4")),
            # TODO: https://github.com/grpc/grpc-node/issues/235.
            # ('v1.7.2', ReleaseInfo()),
            ("v1.8.4", ReleaseInfo()),
            ("v1.9.1", ReleaseInfo()),
            ("v1.10.0", ReleaseInfo()),
            ("v1.11.3", ReleaseInfo()),
            ("v1.12.4", ReleaseInfo()),
        ]
    ),
    "ruby": OrderedDict(
        [
            (
                "v1.0.1",
                ReleaseInfo(
                    patch=[
                        "tools/dockerfile/interoptest/grpc_interop_ruby/Dockerfile",
                        "tools/dockerfile/interoptest/grpc_interop_ruby/build_interop.sh",
                    ],
                    testcases_file="ruby__v1.0.1",
                ),
            ),
            ("v1.1.4", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.2.5", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.3.9", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.4.2", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.6.6", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.7.2", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.8.0", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.9.1", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.10.1", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.11.1", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.12.0", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.13.0", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.14.1", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.15.0", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.16.0", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            ("v1.17.1", ReleaseInfo(testcases_file="ruby__v1.1.4")),
            (
                "v1.18.0",
                ReleaseInfo(
                    patch=[
                        "tools/dockerfile/interoptest/grpc_interop_ruby/build_interop.sh",
                    ]
                ),
            ),
            ("v1.19.0", ReleaseInfo()),
            ("v1.20.0", ReleaseInfo()),
            ("v1.21.4", ReleaseInfo()),
            ("v1.22.0", ReleaseInfo()),
            ("v1.22.1", ReleaseInfo()),
            ("v1.23.0", ReleaseInfo()),
            ("v1.24.0", ReleaseInfo()),
            ("v1.25.0", ReleaseInfo()),
            # TODO: https://github.com/grpc/grpc/issues/18262.
            # If you are not encountering the error in above issue
            # go ahead and upload the docker image for new releases.
            ("v1.26.0", ReleaseInfo()),
            ("v1.27.3", ReleaseInfo()),
            ("v1.30.0", ReleaseInfo()),
            ("v1.31.1", ReleaseInfo()),
            ("v1.32.0", ReleaseInfo()),
            ("v1.33.2", ReleaseInfo()),
            ("v1.34.0", ReleaseInfo()),
            ("v1.35.0", ReleaseInfo()),
            ("v1.36.3", ReleaseInfo()),
            ("v1.37.0", ReleaseInfo()),
            ("v1.38.0", ReleaseInfo()),
            ("v1.39.0", ReleaseInfo()),
            ("v1.41.1", ReleaseInfo()),
            ("v1.42.0", ReleaseInfo()),
            ("v1.43.0", ReleaseInfo()),
            ("v1.44.0", ReleaseInfo()),
            ("v1.46.2", ReleaseInfo()),
            ("v1.47.1", ReleaseInfo()),
            ("v1.48.3", ReleaseInfo()),
            ("v1.49.1", ReleaseInfo()),
            ("v1.52.0", ReleaseInfo()),
            ("v1.53.0", ReleaseInfo()),
            ("v1.54.0", ReleaseInfo()),
            ("v1.55.0", ReleaseInfo()),
            ("v1.56.0", ReleaseInfo()),
            ("v1.57.0", ReleaseInfo()),
            ("v1.58.0", ReleaseInfo()),
        ]
    ),
    "php": OrderedDict(
        [
            ("v1.0.1", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.1.4", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.2.5", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.3.9", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.4.2", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.6.6", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.7.2", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.8.0", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.9.1", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.10.1", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.11.1", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.12.0", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.13.0", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.14.1", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.15.0", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.16.0", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.17.1", ReleaseInfo(testcases_file="php__v1.0.1")),
            ("v1.18.0", ReleaseInfo()),
            # v1.19 and v1.20 were deliberately omitted here because of an issue.
            # See https://github.com/grpc/grpc/issues/18264
            ("v1.21.4", ReleaseInfo()),
            ("v1.22.0", ReleaseInfo()),
            ("v1.22.1", ReleaseInfo()),
            ("v1.23.0", ReleaseInfo()),
            ("v1.24.0", ReleaseInfo()),
            ("v1.25.0", ReleaseInfo()),
            ("v1.26.0", ReleaseInfo()),
            ("v1.27.3", ReleaseInfo()),
            ("v1.30.0", ReleaseInfo()),
            ("v1.31.1", ReleaseInfo()),
            ("v1.32.0", ReleaseInfo()),
            ("v1.33.2", ReleaseInfo()),
            ("v1.34.0", ReleaseInfo()),
            ("v1.35.0", ReleaseInfo()),
            ("v1.36.3", ReleaseInfo()),
            ("v1.37.0", ReleaseInfo()),
            ("v1.38.0", ReleaseInfo()),
            ("v1.39.0", ReleaseInfo()),
            ("v1.41.1", ReleaseInfo()),
            ("v1.42.0", ReleaseInfo()),
            ("v1.43.0", ReleaseInfo()),
            ("v1.44.0", ReleaseInfo()),
            ("v1.46.2", ReleaseInfo()),
            ("v1.47.1", ReleaseInfo()),
            ("v1.48.3", ReleaseInfo()),
            ("v1.49.1", ReleaseInfo()),
            ("v1.52.0", ReleaseInfo()),
            ("v1.53.0", ReleaseInfo()),
            ("v1.54.0", ReleaseInfo()),
            ("v1.55.0", ReleaseInfo()),
            ("v1.56.0", ReleaseInfo()),
            ("v1.57.0", ReleaseInfo()),
            ("v1.58.0", ReleaseInfo()),
        ]
    ),
    "csharp": OrderedDict(
        [
            (
                "v1.0.1",
                ReleaseInfo(
                    patch=[
                        "tools/dockerfile/interoptest/grpc_interop_csharp/Dockerfile",
                        "tools/dockerfile/interoptest/grpc_interop_csharpcoreclr/Dockerfile",
                    ],
                    testcases_file="csharp__v1.1.4",
                ),
            ),
            ("v1.1.4", ReleaseInfo(testcases_file="csharp__v1.1.4")),
            ("v1.2.5", ReleaseInfo(testcases_file="csharp__v1.1.4")),
            ("v1.3.9", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.4.2", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.6.6", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.7.2", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.8.0", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.9.1", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.10.1", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.11.1", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.12.0", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.13.0", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.14.1", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.15.0", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.16.0", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.17.1", ReleaseInfo(testcases_file="csharp__v1.3.9")),
            ("v1.18.0", ReleaseInfo(testcases_file="csharp__v1.18.0")),
            ("v1.19.0", ReleaseInfo(testcases_file="csharp__v1.18.0")),
            ("v1.20.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.20.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.21.4", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.22.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.22.1", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.23.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.24.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.25.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.26.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.27.3", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.30.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.31.1", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.32.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.33.2", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.34.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.35.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.36.3", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.37.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.38.1", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.39.1", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.41.1", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.42.0", ReleaseInfo(testcases_file="csharp__v1.20.0")),
            ("v1.43.0", ReleaseInfo()),
            ("v1.44.0", ReleaseInfo()),
            ("v1.46.2", ReleaseInfo()),
        ]
    ),
}
