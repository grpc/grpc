# Copyright 2020 The gRPC authors.
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

def _generate_copied_files_impl(ctx):
    srcs = ctx.attr.srcs[0]
    strip_prefix = ctx.attr.strip_prefix
    dest = ctx.attr.dest

    outs = []
    for f in srcs.files.to_list():
        destination_path = f.path
        if f.path.startswith("external"):
            external_separator = f.path.find("/")
            repository_separator = f.path.find("/", external_separator + 1)
            destination_path = f.path[repository_separator + 1:]
        if not destination_path.startswith(strip_prefix):
            fail("File '{}' did not start with '{}'.".format(
                destination_path,
                strip_prefix,
            ))
        destination_path = dest + destination_path[len(strip_prefix):]
        destination_dir = destination_path.rfind("/")
        out_file = ctx.actions.declare_file(destination_path)
        outs.append(out_file)
        ctx.actions.run_shell(
            inputs = [f],
            outputs = [out_file],
            command = "mkdir -p {0} && cp {1} {2}".format(
                out_file.dirname,
                f.path,
                out_file.path,
            ),
        )

    return [DefaultInfo(files = depset(direct = outs))]

_generate_copied_files = rule(
    attrs = {
        "srcs": attr.label_list(
            mandatory = True,
            allow_empty = False,
        ),
        "strip_prefix": attr.string(
            default = "",
        ),
        "dest": attr.string(
            mandatory = True,
        ),
    },
    implementation = _generate_copied_files_impl,
)

def internal_copied_filegroup(name, srcs, strip_prefix, dest):
    """Copies a file group to the current package.

    Useful for using an existing filegroup as a data dependency.

    Args:
      name: The name of the rule.
      srcs: A single filegroup.
      strip_prefix: An optional string to strip from the beginning
        of the path of each file in the filegroup. Must end in a slash.
      dest: The directory in which to put the files, relative to the
        current package. Must end in a slash.
    """
    if len(srcs) != 1:
        fail("srcs must be a single filegroup.")

    if not dest.endswith("/"):
        fail("dest must end with a '/' character.")

    _symlink_target = name + "_symlink"
    _generate_copied_files(
        name = _symlink_target,
        srcs = srcs,
        strip_prefix = strip_prefix,
        dest = dest,
    )

    native.filegroup(
        name = name,
        srcs = [":" + _symlink_target],
    )
