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
"""The Python implementation of the GRPC interoperability test client."""

import argparse
import os

from google import auth as google_auth
from google.auth import jwt as google_auth_jwt
import grpc

from src.proto.grpc.testing import test_pb2_grpc
from tests.interop import methods
from tests.interop import resources


def parse_interop_client_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--server_host",
        default="localhost",
        type=str,
        help="the host to which to connect",
    )
    parser.add_argument(
        "--server_port",
        type=int,
        required=True,
        help="the port to which to connect",
    )
    parser.add_argument(
        "--test_case",
        default="large_unary",
        type=str,
        help="the test case to execute",
    )
    parser.add_argument(
        "--use_tls",
        default=False,
        type=resources.parse_bool,
        help="require a secure connection",
    )
    parser.add_argument(
        "--use_alts",
        default=False,
        type=resources.parse_bool,
        help="require an ALTS secure connection",
    )
    parser.add_argument(
        "--use_test_ca",
        default=False,
        type=resources.parse_bool,
        help="replace platform root CAs with ca.pem",
    )
    parser.add_argument(
        "--custom_credentials_type",
        choices=["compute_engine_channel_creds"],
        default=None,
        help="use google default credentials",
    )
    parser.add_argument(
        "--server_host_override",
        type=str,
        help="the server host to which to claim to connect",
    )
    parser.add_argument(
        "--oauth_scope", type=str, help="scope for OAuth tokens"
    )
    parser.add_argument(
        "--default_service_account",
        type=str,
        help="email address of the default service account",
    )
    parser.add_argument(
        "--grpc_test_use_grpclb_with_child_policy",
        type=str,
        help=(
            "If non-empty, set a static service config on channels created by "
            + "grpc::CreateTestChannel, that configures the grpclb LB policy "
            + "with a child policy being the value of this flag (e.g."
            " round_robin " + "or pick_first)."
        ),
    )
    return parser.parse_args()


def _create_call_credentials(args):
    if args.test_case == "oauth2_auth_token":
        google_credentials, unused_project_id = google_auth.default(
            scopes=[args.oauth_scope]
        )
        google_credentials.refresh(google_auth.transport.requests.Request())
        return grpc.access_token_call_credentials(google_credentials.token)
    elif args.test_case == "compute_engine_creds":
        google_credentials, unused_project_id = google_auth.default(
            scopes=[args.oauth_scope]
        )
        return grpc.metadata_call_credentials(
            google_auth.transport.grpc.AuthMetadataPlugin(
                credentials=google_credentials,
                request=google_auth.transport.requests.Request(),
            )
        )
    elif args.test_case == "jwt_token_creds":
        google_credentials = (
            google_auth_jwt.OnDemandCredentials.from_service_account_file(
                os.environ[google_auth.environment_vars.CREDENTIALS]
            )
        )
        return grpc.metadata_call_credentials(
            google_auth.transport.grpc.AuthMetadataPlugin(
                credentials=google_credentials, request=None
            )
        )
    else:
        return None


def get_secure_channel_parameters(args):
    call_credentials = _create_call_credentials(args)

    channel_opts = ()
    if args.grpc_test_use_grpclb_with_child_policy:
        channel_opts += (
            (
                "grpc.service_config",
                '{"loadBalancingConfig": [{"grpclb": {"childPolicy": [{"%s":'
                " {}}]}}]}" % args.grpc_test_use_grpclb_with_child_policy,
            ),
        )
    if args.custom_credentials_type is not None:
        if args.custom_credentials_type == "compute_engine_channel_creds":
            assert call_credentials is None
            google_credentials, unused_project_id = google_auth.default(
                scopes=[args.oauth_scope]
            )
            call_creds = grpc.metadata_call_credentials(
                google_auth.transport.grpc.AuthMetadataPlugin(
                    credentials=google_credentials,
                    request=google_auth.transport.requests.Request(),
                )
            )
            channel_credentials = grpc.compute_engine_channel_credentials(
                call_creds
            )
        else:
            raise ValueError(
                f"Unknown credentials type '{args.custom_credentials_type}'"
            )
    elif args.use_tls:
        if args.use_test_ca:
            root_certificates = resources.test_root_certificates()
        else:
            root_certificates = None  # will load default roots.

        channel_credentials = grpc.ssl_channel_credentials(root_certificates)
        if call_credentials is not None:
            channel_credentials = grpc.composite_channel_credentials(
                channel_credentials, call_credentials
            )

        if args.server_host_override:
            channel_opts += (
                (
                    "grpc.ssl_target_name_override",
                    args.server_host_override,
                ),
            )
    elif args.use_alts:
        channel_credentials = grpc.alts_channel_credentials()

    return channel_credentials, channel_opts


def _create_channel(args):
    target = f"{args.server_host}:{args.server_port}"

    if (
        args.use_tls
        or args.use_alts
        or args.custom_credentials_type is not None
    ):
        channel_credentials, options = get_secure_channel_parameters(args)
        return grpc.secure_channel(target, channel_credentials, options)
    else:
        return grpc.insecure_channel(target)


def create_stub(channel, args):
    if args.test_case == "unimplemented_service":
        return test_pb2_grpc.UnimplementedServiceStub(channel)
    else:
        return test_pb2_grpc.TestServiceStub(channel)


def _test_case_from_arg(test_case_arg):
    for test_case in methods.TestCase:
        if test_case_arg == test_case.value:
            return test_case
    else:
        raise ValueError(f'No test case "{test_case_arg}"!')


def test_interoperability():
    args = parse_interop_client_args()
    channel = _create_channel(args)
    stub = create_stub(channel, args)
    test_case = _test_case_from_arg(args.test_case)
    test_case.test_interoperability(stub, args)


if __name__ == "__main__":
    test_interoperability()
