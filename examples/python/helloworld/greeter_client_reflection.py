# Copyright 2023 gRPC authors.
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
"""The Python implementation of the GRPC helloworld.Greeter client with reflection."""

import logging

from google.protobuf.descriptor_pool import DescriptorPool
import grpc
from grpc_reflection.v1alpha.proto_reflection_descriptor_database import (
    ProtoReflectionDescriptorDatabase,
)


def run():
    print("Will try to greet world ...")
    with grpc.insecure_channel("localhost:50051") as channel:
        reflection_db = ProtoReflectionDescriptorDatabase(channel)
        services = reflection_db.get_services()
        print(f"found services: {services}")

        desc_pool = DescriptorPool(reflection_db)
        service_desc = desc_pool.FindServiceByName("helloworld.Greeter")
        print(f"found Greeter service with name: {service_desc.full_name}")
        for methods in service_desc.methods:
            print(f"found method name: {methods.full_name}")
            input_type = methods.input_type
            print(f"input type for this method: {input_type.full_name}")

        request_desc = desc_pool.FindMessageTypeByName(
            "helloworld.HelloRequest"
        )
        print(f"found request name: {request_desc.full_name}")


if __name__ == "__main__":
    logging.basicConfig()
    run()
