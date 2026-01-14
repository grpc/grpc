# Copyright 2025 the gRPC authors.
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

"""
A module extension that provides wrappers for macros from bazel_toolchains
so they can be used in MODULE.bazel.
"""

load(
    "@bazel_toolchains//rules/exec_properties:exec_properties.bzl",
    "create_rbe_exec_properties_dict",
    "custom_exec_properties",
)

def _exec_properties_impl(ctx):
    for module in ctx.modules:
        repositories = {}
        for cep in module.tags.custom_exec_properties:
            name = cep.name
            constant = cep.constant
            rbe_exec_properties_dict = cep.rbe_exec_properties_dict
            if not name in repositories:
                repositories[name] = {}
            repositories[name][constant] = create_rbe_exec_properties_dict(
                labels = rbe_exec_properties_dict,
            )
        for name, constants in repositories.items():
            custom_exec_properties(name, constants)

_custom_exec_properties = tag_class(attrs = {
    "name": attr.string(),
    "constant": attr.string(),
    "rbe_exec_properties_dict": attr.string_dict(),
})

exec_properties = module_extension(
    implementation = _exec_properties_impl,
    tag_classes = {"custom_exec_properties": _custom_exec_properties},
)
