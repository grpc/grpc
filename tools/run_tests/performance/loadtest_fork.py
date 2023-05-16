#!/usr/bin/env python3
# Copyright 2023 The gRPC Authors
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

# Script to generate a shell script containing ENVs necessary to run
# adhoc/ephemeral jobs with forked test-infra repo other than the
# grpc/test-infra for the OSS benchmarks
# tests. The generated script will have name as grpc_e2e_performance_gke_env.sh,
# and will be sourced in
# tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh
#
# See documentation below:

import argparse

def main() -> None:
    argp = argparse.ArgumentParser(
        description='Creates a grpc_e2e_performance_gke_env.sh script providing the necessary ENVs to run OSS performance benchmark tests from some forked repos.')
    argp.add_argument(
        '--grpc_test_infra_repo',
        default='grpc/test-infra',
        type=str,
        help='The flag will supply the value for GRPC_TEST_INFRA_REPO environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the source code of test-infra repo to clone from, default to grpc/test-infra.',
    )
    argp.add_argument(
        '--grpc_test_infra_branch',
        default='master',
        type=str,
        help='The flag will supply the value for GRPC_TEST_INFRA_BRANCH environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the branch of grpc test-infra repo to check out, default to master.',
    )
    argp.add_argument(
        '--grpc_dotnet_repo',
        default='grpc/test-infra',
        type=str,
        help='The flag will supply the value for GRPC_DOTNET_REPO environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the source code of grpc dotnet repo to clone from, default to grpc/grpc-dotnet.',
    )
    argp.add_argument(
        '--grpc_dotnet_branch',
        default='grpc/test-infra',
        type=str,
        help='The flag will supply the value for GRPC_DOTNET_BRANCH environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the branch of grpc dotnet repo to check out, default to master.',
    )
    argp.add_argument(
        '--grpc_java_repo',
        default='grpc/test-infra',
        type=str,
        help='The flag will supply the value for GRPC_JAVA_REPO environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the source code of grpc java repo to clone from, default to grpc/grpc-java.',
    )
    argp.add_argument(
        '--grpc_java_branch',
        default='grpc/test-infra',
        type=str,
        help='The flag will supply the value for GRPC_JAVA_BRANCH environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the branch of grpc java repo to check out, default to master.',
    )
    argp.add_argument(
        '--grpc_go_repo',
        default='grpc/test-infra',
        type=str,
        help='The flag will supply the value for GRPC_GO_REPO environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the source code of grpc java repo to clone from, default to grpc/grpc-go.',
    )
    argp.add_argument(
        '--grpc_go_branch',
        default='grpc/test-infra',
        type=str,
        help='The flag will supply the value for GRPC_GO_BRANCH environment variables which would be included in tools/internal_ci/linux/grpc_e2e_performance_gke_experiment.sh, specify the branch of grpc go repo to check out, default to master.',
    )

    args = argp.parse_args()

    env_dict = {}
    env_dict ["GRPC_TEST_INFRA_REPO"] = args.grpc_test_infra_repo
    env_dict ["GRPC_TEST_INFRA_BRANCH"] = args.grpc_test_infra_branch
    env_dict ["GRPC_DOTNET_REPO"] = args.grpc_dotnet_repo
    env_dict ["GRPC_DOTNET_INFRA_BRANCH"] = args.grpc_dotnet_branch
    env_dict ["GRPC_JAVA_REPO"] = args.grpc_java_repo
    env_dict ["GRPC_JAVA_BRANCH"] = args.grpc_java_branch
    env_dict ["GRPC_GO_REPO"] = args.grpc_go_repo
    env_dict ["GRPC_GO_BRANCH"] = args.grpc_go_branch


    with open("grpc_e2e_performance_gke_env.sh", 'w') as export_file:
        export_file.write("#!/bin/bash\n")
        for k, v in env_dict.items():
            export_file.write("{}={}\n".format(k, v))

if __name__ == '__main__':
    main()
