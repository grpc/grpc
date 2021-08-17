# Copyright (c) 2009-2021, Google LLC
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of Google LLC nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Internal rules for building upb."""

load(":upb_proto_library.bzl", "GeneratedSrcsInfo")

UPB_DEFAULT_CPPOPTS = select({
    "//:windows": [],
    "//conditions:default": [
        # copybara:strip_for_google3_begin
        "-Wextra",
        # "-Wshorten-64-to-32",  # not in GCC (and my Kokoro images doesn't have Clang)
        "-Werror",
        "-Wno-long-long",
        # copybara:strip_end
    ],
})

UPB_DEFAULT_COPTS = select({
    "//:windows": [],
    "//:fasttable_enabled_setting": ["-std=gnu99", "-DUPB_ENABLE_FASTTABLE"],
    "//conditions:default": [
        # copybara:strip_for_google3_begin
        "-std=c99",
        "-pedantic",
        "-Werror=pedantic",
        "-Wall",
        "-Wstrict-prototypes",
        # GCC (at least) emits spurious warnings for this that cannot be fixed
        # without introducing redundant initialization (with runtime cost):
        #   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80635
        #"-Wno-maybe-uninitialized",
        # copybara:strip_end
    ],
})

def _librule(name):
    return name + "_lib"

runfiles_init = """\
# --- begin runfiles.bash initialization v2 ---
# Copy-pasted from the Bazel Bash runfiles library v2.
set -uo pipefail; f=bazel_tools/tools/bash/runfiles/runfiles.bash
source "${RUNFILES_DIR:-/dev/null}/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "${RUNFILES_MANIFEST_FILE:-/dev/null}" | cut -f2- -d' ')" 2>/dev/null || \
  source "$0.runfiles/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.exe.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  { echo>&2 "ERROR: cannot find $f"; exit 1; }; f=; set -e
# --- end runfiles.bash initialization v2 ---
"""

def _get_real_short_path(file):
    # For some reason, files from other archives have short paths that look like:
    #   ../com_google_protobuf/google/protobuf/descriptor.proto
    short_path = file.short_path
    if short_path.startswith("../"):
        second_slash = short_path.index("/", 3)
        short_path = short_path[second_slash + 1:]
    return short_path

def _get_real_root(file):
    real_short_path = _get_real_short_path(file)
    return file.path[:-len(real_short_path) - 1]

def _get_real_roots(files):
    roots = {}
    for file in files:
        real_root = _get_real_root(file)
        if real_root:
            roots[real_root] = True
    return roots.keys()

def _remove_prefix(str, prefix):
    if not str.startswith(prefix):
        fail("%s doesn't start with %s" % (str, prefix))
    return str[len(prefix):]

def _remove_suffix(str, suffix):
    if not str.endswith(suffix):
        fail("%s doesn't end with %s" % (str, suffix))
    return str[:-len(suffix)]

def make_shell_script(name, contents, out):
    contents = runfiles_init + contents  # copybara:strip_for_google3
    contents = contents.replace("$", "$$")
    native.genrule(
        name = "gen_" + name,
        outs = [out],
        cmd = "(cat <<'HEREDOC'\n%s\nHEREDOC\n) > $@" % contents,
    )

# upb_amalgamation() rule, with file_list aspect.

SrcList = provider(
    fields = {
        "srcs": "list of srcs",
    },
)

def _file_list_aspect_impl(target, ctx):
    if GeneratedSrcsInfo in target:
        srcs = target[GeneratedSrcsInfo]
        return [SrcList(srcs = srcs.srcs + srcs.hdrs)]

    srcs = []
    for src in ctx.rule.attr.srcs:
        srcs += src.files.to_list()
    for hdr in ctx.rule.attr.hdrs:
        srcs += hdr.files.to_list()
    for hdr in ctx.rule.attr.textual_hdrs:
        srcs += hdr.files.to_list()
    return [SrcList(srcs = srcs)]

_file_list_aspect = aspect(
    implementation = _file_list_aspect_impl,
)

def _upb_amalgamation(ctx):
    inputs = []
    for lib in ctx.attr.libs:
        inputs += lib[SrcList].srcs
    srcs = [src for src in inputs if src.path.endswith("c")]
    ctx.actions.run(
        inputs = inputs,
        outputs = ctx.outputs.outs,
        arguments = [ctx.bin_dir.path + "/", ctx.attr.prefix] + [f.path for f in srcs] + ["-I" + root for root in _get_real_roots(inputs)],
        progress_message = "Making amalgamation",
        executable = ctx.executable.amalgamator,
    )
    return []

upb_amalgamation = rule(
    attrs = {
        "amalgamator": attr.label(
            executable = True,
            cfg = "host",
        ),
        "prefix": attr.string(
            default = "",
        ),
        "libs": attr.label_list(aspects = [_file_list_aspect]),
        "outs": attr.output_list(),
    },
    implementation = _upb_amalgamation,
)
