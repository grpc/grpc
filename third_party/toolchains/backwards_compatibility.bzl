# Copyright 2021 The gRPC authors.
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

load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_exec_properties_dict", "create_rbe_exec_properties_dict","merge_dicts")

def bc_exec_properties(exec_properties):
    if True:
        d = dict(labels = exec_properties[1], **exec_properties[0])
        return create_rbe_exec_properties_dict(
            **d
        )
    else:
        labels_dict = {"label:" + key : exec_properties[1][key] for key in exec_properties[1]}
        return merge_dicts(
            create_exec_properties_dict(),
            labels_dict,
        )


def bc_platform(**kwargs):
    exec_properties = kwargs.pop("exec_properties", None)
    if len(exec_properties) != 2:
        fail("exec_properties must be a list of size 2.")

    d = dict(labels = exec_properties[1], **exec_properties[0])
    print(d)
    output_exec_properties = create_rbe_exec_properties_dict(
        **d
    )

    native.platform(
        exec_properties = output_exec_properties,
        **kwargs,
    )
