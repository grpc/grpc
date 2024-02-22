resolved = [
     {
          "original_rule_class": "local_repository",
          "original_attributes": {
               "name": "bazel_tools",
               "path": "/home/vscode/.cache/bazel/_bazel_vscode/install/117cee491f5c7d83be6e3c6d6b5b8ca4/embedded_tools"
          },
          "native": "local_repository(name = \"bazel_tools\", path = __embedded_dir__ + \"/\" + \"embedded_tools\")"
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository com_envoyproxy_protoc_gen_validate instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:478:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "com_envoyproxy_protoc_gen_validate",
               "generator_name": "com_envoyproxy_protoc_gen_validate",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://github.com/envoyproxy/protoc-gen-validate/archive/4694024279bdac52b77e22dc87808bd0fd732b69.tar.gz"
               ],
               "sha256": "1e490b98005664d149b379a9529a6aa05932b8a11b76b4cd86f3d22d76346f47",
               "strip_prefix": "protoc-gen-validate-4694024279bdac52b77e22dc87808bd0fd732b69",
               "patches": [
                    "//third_party:protoc-gen-validate.patch"
               ],
               "patch_args": [
                    "-p1"
               ]
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://github.com/envoyproxy/protoc-gen-validate/archive/4694024279bdac52b77e22dc87808bd0fd732b69.tar.gz"
                         ],
                         "sha256": "1e490b98005664d149b379a9529a6aa05932b8a11b76b4cd86f3d22d76346f47",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "protoc-gen-validate-4694024279bdac52b77e22dc87808bd0fd732b69",
                         "add_prefix": "",
                         "type": "",
                         "patches": [
                              "//third_party:protoc-gen-validate.patch"
                         ],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p1"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "com_envoyproxy_protoc_gen_validate"
                    },
                    "output_tree_hash": "27f2caba94dbbf61195a6055d0605010c84c44bdb4f80e979e035ddc79ae85a1"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository io_bazel_rules_go instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:415:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "io_bazel_rules_go",
               "generator_name": "io_bazel_rules_go",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.27.0/rules_go-v0.27.0.tar.gz",
                    "https://github.com/bazelbuild/rules_go/releases/download/v0.27.0/rules_go-v0.27.0.tar.gz"
               ],
               "sha256": "69de5c704a05ff37862f7e0f5534d4f479418afc21806c887db544a316f3cb6b"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://mirror.bazel.build/github.com/bazelbuild/rules_go/releases/download/v0.27.0/rules_go-v0.27.0.tar.gz",
                              "https://github.com/bazelbuild/rules_go/releases/download/v0.27.0/rules_go-v0.27.0.tar.gz"
                         ],
                         "sha256": "69de5c704a05ff37862f7e0f5534d4f479418afc21806c887db544a316f3cb6b",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "io_bazel_rules_go"
                    },
                    "output_tree_hash": "33c1a29030a4e525c5b5f53d45f463a770b07673119a5455dceefe9fa7d785f1"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository build_bazel_apple_support instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:435:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "build_bazel_apple_support",
               "generator_name": "build_bazel_apple_support",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/apple_support/releases/download/1.11.1/apple_support.1.11.1.tar.gz",
                    "https://github.com/bazelbuild/apple_support/releases/download/1.11.1/apple_support.1.11.1.tar.gz"
               ],
               "sha256": "cf4d63f39c7ba9059f70e995bf5fe1019267d3f77379c2028561a5d7645ef67c"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/apple_support/releases/download/1.11.1/apple_support.1.11.1.tar.gz",
                              "https://github.com/bazelbuild/apple_support/releases/download/1.11.1/apple_support.1.11.1.tar.gz"
                         ],
                         "sha256": "cf4d63f39c7ba9059f70e995bf5fe1019267d3f77379c2028561a5d7645ef67c",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "build_bazel_apple_support"
                    },
                    "output_tree_hash": "ebcc105aae2ceefe3bc84019a2ce9f88def6a7d4bc7806f43e4ddf5de5641922"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository rules_python instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:540:21: in grpc_deps\n  /workspaces/grpc/bazel/grpc_python_deps.bzl:23:21: in grpc_python_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "rules_python",
               "generator_name": "rules_python",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "url": "https://github.com/bazelbuild/rules_python/releases/download/0.26.0/rules_python-0.26.0.tar.gz",
               "sha256": "9d04041ac92a0985e344235f5d946f71ac543f1b1565f2cdbc9a2aaee8adf55b",
               "strip_prefix": "rules_python-0.26.0"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "https://github.com/bazelbuild/rules_python/releases/download/0.26.0/rules_python-0.26.0.tar.gz",
                         "urls": [],
                         "sha256": "9d04041ac92a0985e344235f5d946f71ac543f1b1565f2cdbc9a2aaee8adf55b",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "rules_python-0.26.0",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "rules_python"
                    },
                    "output_tree_hash": "1cd0bdb7a0b481ee2eff2ee20651c34414b9bf8b29378b194128390fd6bbdcd4"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository envoy_api instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:404:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "envoy_api",
               "generator_name": "envoy_api",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/data-plane-api/archive/78f198cf96ecdc7120ef640406770aa01af775c4.tar.gz",
                    "https://github.com/envoyproxy/data-plane-api/archive/78f198cf96ecdc7120ef640406770aa01af775c4.tar.gz"
               ],
               "sha256": "ddd3beedda1178a79e0d988f76f362002aced09749452515853f106e22bd2249",
               "strip_prefix": "data-plane-api-78f198cf96ecdc7120ef640406770aa01af775c4"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/envoyproxy/data-plane-api/archive/78f198cf96ecdc7120ef640406770aa01af775c4.tar.gz",
                              "https://github.com/envoyproxy/data-plane-api/archive/78f198cf96ecdc7120ef640406770aa01af775c4.tar.gz"
                         ],
                         "sha256": "ddd3beedda1178a79e0d988f76f362002aced09749452515853f106e22bd2249",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "data-plane-api-78f198cf96ecdc7120ef640406770aa01af775c4",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "envoy_api"
                    },
                    "output_tree_hash": "2c76a80da257ef5e97ba7f61bc464b39500dfa7bedf94c0616d2d77c8c2adc7c"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository bazel_gazelle instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:457:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "bazel_gazelle",
               "generator_name": "bazel_gazelle",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://mirror.bazel.build/github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz",
                    "https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz"
               ],
               "sha256": "de69a09dc70417580aabf20a28619bb3ef60d038470c7cf8442fafcf627c21cb"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://mirror.bazel.build/github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz",
                              "https://github.com/bazelbuild/bazel-gazelle/releases/download/v0.24.0/bazel-gazelle-v0.24.0.tar.gz"
                         ],
                         "sha256": "de69a09dc70417580aabf20a28619bb3ef60d038470c7cf8442fafcf627c21cb",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "bazel_gazelle"
                    },
                    "output_tree_hash": "afdfb2e9705b7c2951d051165c513fa4a8165e1daf99cebeb10fecae0c01fb26"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository build_bazel_rules_apple instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:425:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "build_bazel_rules_apple",
               "generator_name": "build_bazel_rules_apple",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_apple/releases/download/3.1.1/rules_apple.3.1.1.tar.gz",
                    "https://github.com/bazelbuild/rules_apple/releases/download/3.1.1/rules_apple.3.1.1.tar.gz"
               ],
               "sha256": "34c41bfb59cdaea29ac2df5a2fa79e5add609c71bb303b2ebb10985f93fa20e7"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/bazelbuild/rules_apple/releases/download/3.1.1/rules_apple.3.1.1.tar.gz",
                              "https://github.com/bazelbuild/rules_apple/releases/download/3.1.1/rules_apple.3.1.1.tar.gz"
                         ],
                         "sha256": "34c41bfb59cdaea29ac2df5a2fa79e5add609c71bb303b2ebb10985f93fa20e7",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "build_bazel_rules_apple"
                    },
                    "output_tree_hash": "7e73ab2fff840dbd672268ff2099d326a19cac817f6fe2e810b6717b538c7035"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository com_google_protobuf instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:264:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "com_google_protobuf",
               "generator_name": "com_google_protobuf",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/protobuf/archive/7f94235e552599141950d7a4a3eaf93bc87d1b22.tar.gz",
                    "https://github.com/protocolbuffers/protobuf/archive/7f94235e552599141950d7a4a3eaf93bc87d1b22.tar.gz"
               ],
               "sha256": "70f480fe9cb0c6829dbf6be3c388103313aacb65de667b86d981bbc9eaedb905",
               "strip_prefix": "protobuf-7f94235e552599141950d7a4a3eaf93bc87d1b22",
               "patches": [
                    "//third_party:protobuf.patch"
               ],
               "patch_args": [
                    "-p1"
               ]
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/protocolbuffers/protobuf/archive/7f94235e552599141950d7a4a3eaf93bc87d1b22.tar.gz",
                              "https://github.com/protocolbuffers/protobuf/archive/7f94235e552599141950d7a4a3eaf93bc87d1b22.tar.gz"
                         ],
                         "sha256": "70f480fe9cb0c6829dbf6be3c388103313aacb65de667b86d981bbc9eaedb905",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "protobuf-7f94235e552599141950d7a4a3eaf93bc87d1b22",
                         "add_prefix": "",
                         "type": "",
                         "patches": [
                              "//third_party:protobuf.patch"
                         ],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p1"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "com_google_protobuf"
                    },
                    "output_tree_hash": "c25b61ff6868cc802aaa583d2f269aaf398c3aa8dd263995b79b290a43f58856"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository com_google_googleapis instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:445:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "com_google_googleapis",
               "generator_name": "com_google_googleapis",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz",
                    "https://github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz"
               ],
               "sha256": "5bb6b0253ccf64b53d6c7249625a7e3f6c3bc6402abd52d3778bfa48258703a0",
               "strip_prefix": "googleapis-2f9af297c84c55c8b871ba4495e01ade42476c92",
               "build_file": "//bazel:googleapis.BUILD"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz",
                              "https://github.com/googleapis/googleapis/archive/2f9af297c84c55c8b871ba4495e01ade42476c92.tar.gz"
                         ],
                         "sha256": "5bb6b0253ccf64b53d6c7249625a7e3f6c3bc6402abd52d3778bfa48258703a0",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "googleapis-2f9af297c84c55c8b871ba4495e01ade42476c92",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file": "//bazel:googleapis.BUILD",
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "com_google_googleapis"
                    },
                    "output_tree_hash": "0a8df3f8b0c71738a4a7afe96ebefa968555670b6a632a7322bd6fbbebe80d36"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository google_cloud_cpp instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:530:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "google_cloud_cpp",
               "generator_name": "google_cloud_cpp",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.16.0.tar.gz",
                    "https://github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.16.0.tar.gz"
               ],
               "sha256": "7ca7f583b60d2aa1274411fed3b9fb3887119b2e84244bb3fc69ea1db819e4e5",
               "strip_prefix": "google-cloud-cpp-2.16.0"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.16.0.tar.gz",
                              "https://github.com/googleapis/google-cloud-cpp/archive/refs/tags/v2.16.0.tar.gz"
                         ],
                         "sha256": "7ca7f583b60d2aa1274411fed3b9fb3887119b2e84244bb3fc69ea1db819e4e5",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "google-cloud-cpp-2.16.0",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "google_cloud_cpp"
                    },
                    "output_tree_hash": "4090fea3325cd80d963517d499c5f3456dd1b5a8c11f5a48dcd855ea98137ea6"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository bazel_toolchains instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:361:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "bazel_toolchains",
               "generator_name": "bazel_toolchains",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/releases/download/4.1.0/bazel-toolchains-4.1.0.tar.gz",
                    "https://github.com/bazelbuild/bazel-toolchains/releases/download/4.1.0/bazel-toolchains-4.1.0.tar.gz"
               ],
               "sha256": "179ec02f809e86abf56356d8898c8bd74069f1bd7c56044050c2cd3d79d0e024",
               "strip_prefix": "bazel-toolchains-4.1.0"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://mirror.bazel.build/github.com/bazelbuild/bazel-toolchains/releases/download/4.1.0/bazel-toolchains-4.1.0.tar.gz",
                              "https://github.com/bazelbuild/bazel-toolchains/releases/download/4.1.0/bazel-toolchains-4.1.0.tar.gz"
                         ],
                         "sha256": "179ec02f809e86abf56356d8898c8bd74069f1bd7c56044050c2cd3d79d0e024",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "bazel-toolchains-4.1.0",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "bazel_toolchains"
                    },
                    "output_tree_hash": "7ef5573ceabea4840ef82399f66d18e61ba51b94612a0079113b674448c2209a"
               }
          ]
     },
     {
          "original_rule_class": "//third_party/android:android_configure.bzl%android_configure",
          "definition_information": "Repository local_config_android instantiated at:\n  /workspaces/grpc/WORKSPACE:38:18: in <toplevel>\nRepository rule android_configure defined at:\n  /workspaces/grpc/third_party/android/android_configure.bzl:56:36: in <toplevel>\n",
          "original_attributes": {
               "name": "local_config_android"
          },
          "repositories": [
               {
                    "rule_class": "//third_party/android:android_configure.bzl%android_configure",
                    "attributes": {
                         "name": "local_config_android"
                    },
                    "output_tree_hash": "2f7463bdf982eae75c4553c60fa89ce3b0779681a03f6a353ad8393be0a497e0"
               }
          ]
     },
     {
          "original_rule_class": "@rules_python//python/private:internal_config_repo.bzl%internal_config_repo",
          "definition_information": "Repository rules_python_internal instantiated at:\n  /workspaces/grpc/WORKSPACE:11:16: in <toplevel>\n  /workspaces/grpc/bazel/grpc_extra_deps.bzl:76:20: in grpc_extra_deps\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/rules_python/python/repositories.bzl:50:10: in py_repositories\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/utils.bzl:233:18: in maybe\nRepository rule internal_config_repo defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/rules_python/python/private/internal_config_repo.bzl:93:39: in <toplevel>\n",
          "original_attributes": {
               "name": "rules_python_internal",
               "generator_name": "rules_python_internal",
               "generator_function": "grpc_extra_deps",
               "generator_location": None
          },
          "repositories": [
               {
                    "rule_class": "@rules_python//python/private:internal_config_repo.bzl%internal_config_repo",
                    "attributes": {
                         "name": "rules_python_internal",
                         "generator_name": "rules_python_internal",
                         "generator_function": "grpc_extra_deps",
                         "generator_location": None
                    },
                    "output_tree_hash": "e3cdc1dc7ccd575c9f96009b8b405a42d410026808c2f3cf4bd64bd0cf62fe2c"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository bazel_skylib instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:372:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "bazel_skylib",
               "generator_name": "bazel_skylib",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
                    "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz"
               ],
               "sha256": "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz",
                              "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.3/bazel-skylib-1.0.3.tar.gz"
                         ],
                         "sha256": "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "bazel_skylib"
                    },
                    "output_tree_hash": "ec0173581163d32cb764072fa396fc158fb56ac7660d93f944ae7723401a67e2"
               }
          ]
     },
     {
          "original_rule_class": "@rules_python//python/pip_install:pip_repository.bzl%pip_repository",
          "definition_information": "Repository grpc_python_dependencies instantiated at:\n  /workspaces/grpc/WORKSPACE:64:10: in <toplevel>\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/rules_python/python/pip.bzl:157:19: in pip_parse\nRepository rule pip_repository defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/rules_python/python/pip_install/pip_repository.bzl:530:33: in <toplevel>\n",
          "original_attributes": {
               "name": "grpc_python_dependencies",
               "generator_name": "grpc_python_dependencies",
               "generator_function": "pip_parse",
               "generator_location": None,
               "requirements_lock": "//:requirements.bazel.txt"
          },
          "repositories": [
               {
                    "rule_class": "@rules_python//python/pip_install:pip_repository.bzl%pip_repository",
                    "attributes": {
                         "name": "grpc_python_dependencies",
                         "generator_name": "grpc_python_dependencies",
                         "generator_function": "pip_parse",
                         "generator_location": None,
                         "requirements_lock": "//:requirements.bazel.txt"
                    },
                    "output_tree_hash": "1d4d13a96fbea1305f9ac20371f4f0558da62ff0433a6ddda88250d4082b458e"
               }
          ]
     },
     {
          "original_rule_class": "@com_google_protobuf//bazel:system_python.bzl%system_python",
          "definition_information": "Repository system_python instantiated at:\n  /workspaces/grpc/WORKSPACE:75:14: in <toplevel>\nRepository rule system_python defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/com_google_protobuf/bazel/system_python.bzl:269:32: in <toplevel>\n",
          "original_attributes": {
               "name": "system_python",
               "minimum_python_version": "3.7"
          },
          "repositories": [
               {
                    "rule_class": "@com_google_protobuf//bazel:system_python.bzl%system_python",
                    "attributes": {
                         "name": "system_python",
                         "minimum_python_version": "3.7"
                    },
                    "output_tree_hash": "abec8d69115603fc646909f4a2e7e2cce3ec77ad232ba2617737dc2aea0c3eba"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository build_bazel_rules_swift instantiated at:\n  /workspaces/grpc/WORKSPACE:11:16: in <toplevel>\n  /workspaces/grpc/bazel/grpc_extra_deps.bzl:62:29: in grpc_extra_deps\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/build_bazel_rules_apple/apple/repositories.bzl:122:15: in apple_rules_dependencies\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/build_bazel_rules_apple/apple/repositories.bzl:86:14: in _maybe\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "build_bazel_rules_swift",
               "generator_name": "build_bazel_rules_swift",
               "generator_function": "grpc_extra_deps",
               "generator_location": None,
               "url": "https://github.com/bazelbuild/rules_swift/releases/download/1.13.0/rules_swift.1.13.0.tar.gz",
               "sha256": "28a66ff5d97500f0304f4e8945d936fe0584e0d5b7a6f83258298007a93190ba"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "https://github.com/bazelbuild/rules_swift/releases/download/1.13.0/rules_swift.1.13.0.tar.gz",
                         "urls": [],
                         "sha256": "28a66ff5d97500f0304f4e8945d936fe0584e0d5b7a6f83258298007a93190ba",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "build_bazel_rules_swift"
                    },
                    "output_tree_hash": "9e6eb42278684bd709cd716863b02fab099ba6230c568e60e4cc0444457b9e8a"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository com_github_google_benchmark instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:315:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "com_github_google_benchmark",
               "generator_name": "com_github_google_benchmark",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/benchmark/archive/344117638c8ff7e239044fd0fa7085839fc03021.tar.gz",
                    "https://github.com/google/benchmark/archive/344117638c8ff7e239044fd0fa7085839fc03021.tar.gz"
               ],
               "sha256": "8e7b955f04bc6984e4f14074d0d191474f76a6c8e849e04a9dced49bc975f2d4",
               "strip_prefix": "benchmark-344117638c8ff7e239044fd0fa7085839fc03021"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/google/benchmark/archive/344117638c8ff7e239044fd0fa7085839fc03021.tar.gz",
                              "https://github.com/google/benchmark/archive/344117638c8ff7e239044fd0fa7085839fc03021.tar.gz"
                         ],
                         "sha256": "8e7b955f04bc6984e4f14074d0d191474f76a6c8e849e04a9dced49bc975f2d4",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "benchmark-344117638c8ff7e239044fd0fa7085839fc03021",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "com_github_google_benchmark"
                    },
                    "output_tree_hash": "0d4f4c60f23c6bacb9af971da9b0ec4462607d6d43e073197983f25e46e811a4"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository io_opentelemetry_cpp instantiated at:\n  /workspaces/grpc/WORKSPACE:5:10: in <toplevel>\n  /workspaces/grpc/bazel/grpc_deps.bzl:519:21: in grpc_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "io_opentelemetry_cpp",
               "generator_name": "io_opentelemetry_cpp",
               "generator_function": "grpc_deps",
               "generator_location": None,
               "urls": [
                    "https://storage.googleapis.com/grpc-bazel-mirror/github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.13.0.tar.gz",
                    "https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.13.0.tar.gz"
               ],
               "sha256": "7735cc56507149686e6019e06f588317099d4522480be5f38a2a09ec69af1706",
               "strip_prefix": "opentelemetry-cpp-1.13.0"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "",
                         "urls": [
                              "https://storage.googleapis.com/grpc-bazel-mirror/github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.13.0.tar.gz",
                              "https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.13.0.tar.gz"
                         ],
                         "sha256": "7735cc56507149686e6019e06f588317099d4522480be5f38a2a09ec69af1706",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "opentelemetry-cpp-1.13.0",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "io_opentelemetry_cpp"
                    },
                    "output_tree_hash": "503b76dcd873f09647e023322d3b0c27e17acdc24a9a91801bfe7eef4ca3d9ad"
               }
          ]
     },
     {
          "original_rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
          "definition_information": "Repository rules_foreign_cc instantiated at:\n  /workspaces/grpc/WORKSPACE:114:15: in <toplevel>\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/com_github_google_benchmark/bazel/benchmark_deps.bzl:18:21: in benchmark_deps\nRepository rule http_archive defined at:\n  /home/vscode/.cache/bazel/_bazel_vscode/36ada809a32f5e26aa6c88ec5de66938/external/bazel_tools/tools/build_defs/repo/http.bzl:372:31: in <toplevel>\n",
          "original_attributes": {
               "name": "rules_foreign_cc",
               "generator_name": "rules_foreign_cc",
               "generator_function": "benchmark_deps",
               "generator_location": None,
               "url": "https://github.com/bazelbuild/rules_foreign_cc/archive/0.7.1.tar.gz",
               "sha256": "bcd0c5f46a49b85b384906daae41d277b3dc0ff27c7c752cc51e43048a58ec83",
               "strip_prefix": "rules_foreign_cc-0.7.1"
          },
          "repositories": [
               {
                    "rule_class": "@bazel_tools//tools/build_defs/repo:http.bzl%http_archive",
                    "attributes": {
                         "url": "https://github.com/bazelbuild/rules_foreign_cc/archive/0.7.1.tar.gz",
                         "urls": [],
                         "sha256": "bcd0c5f46a49b85b384906daae41d277b3dc0ff27c7c752cc51e43048a58ec83",
                         "integrity": "",
                         "netrc": "",
                         "auth_patterns": {},
                         "canonical_id": "",
                         "strip_prefix": "rules_foreign_cc-0.7.1",
                         "add_prefix": "",
                         "type": "",
                         "patches": [],
                         "remote_patches": {},
                         "remote_patch_strip": 0,
                         "patch_tool": "",
                         "patch_args": [
                              "-p0"
                         ],
                         "patch_cmds": [],
                         "patch_cmds_win": [],
                         "build_file_content": "",
                         "workspace_file_content": "",
                         "name": "rules_foreign_cc"
                    },
                    "output_tree_hash": "a71cca7857fe664e92d6090f9c0da81918d84dcb410dd945f49e3583db1687ad"
               }
          ]
     }
]