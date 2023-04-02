# Copyright 2022 The gRPC Authors
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

# Regenerates all generated files in the Python examples directory.

ARTIFACTS=

ARTIFACTS += helloworld/helloworld_pb2.py
ARTIFACTS += helloworld/helloworld_pb2_grpc.py
ARTIFACTS += helloworld/helloworld_pb2.pyi

ARTIFACTS += timeout/helloworld_pb2.py
ARTIFACTS += timeout/helloworld_pb2_grpc.py
ARTIFACTS += timeout/helloworld_pb2.pyi

ARTIFACTS += keep_alive/helloworld_pb2.py
ARTIFACTS += keep_alive/helloworld_pb2_grpc.py
ARTIFACTS += keep_alive/helloworld_pb2.pyi

ARTIFACTS += health_checking/helloworld_pb2.py
ARTIFACTS += health_checking/helloworld_pb2_grpc.py
ARTIFACTS += health_checking/helloworld_pb2.pyi

ARTIFACTS += async_streaming/phone_pb2.py
ARTIFACTS += async_streaming/phone_pb2_grpc.py
ARTIFACTS += async_streaming/phone_pb2.pyi

ARTIFACTS += hellostreamingworld/hellostreamingworld_pb2.py
ARTIFACTS += hellostreamingworld/hellostreamingworld_pb2_grpc.py
ARTIFACTS += hellostreamingworld/hellostreamingworld_pb2.pyi

ARTIFACTS += uds/helloworld_pb2.py
ARTIFACTS += uds/helloworld_pb2_grpc.py
ARTIFACTS += uds/helloworld_pb2.pyi

ARTIFACTS += xds/helloworld_pb2.py
ARTIFACTS += xds/helloworld_pb2_grpc.py
ARTIFACTS += xds/helloworld_pb2.pyi

ARTIFACTS += multiplex/helloworld_pb2.py
ARTIFACTS += multiplex/helloworld_pb2_grpc.py
ARTIFACTS += multiplex/helloworld_pb2.pyi

ARTIFACTS += multiplex/route_guide_pb2.py
ARTIFACTS += multiplex/route_guide_pb2_grpc.py
ARTIFACTS += multiplex/route_guide_pb2.pyi

ARTIFACTS += route_guide/route_guide_pb2.py
ARTIFACTS += route_guide/route_guide_pb2_grpc.py
ARTIFACTS += route_guide/route_guide_pb2.pyi

ARTIFACTS += interceptors/default_value/helloworld_pb2.py
ARTIFACTS += interceptors/default_value/helloworld_pb2_grpc.py
ARTIFACTS += interceptors/default_value/helloworld_pb2.pyi

ARTIFACTS += interceptors/headers/helloworld_pb2.py
ARTIFACTS += interceptors/headers/helloworld_pb2_grpc.py
ARTIFACTS += interceptors/headers/helloworld_pb2.pyi

ARTIFACTS += metadata/helloworld_pb2.py
ARTIFACTS += metadata/helloworld_pb2_grpc.py
ARTIFACTS += metadata/helloworld_pb2.pyi

ARTIFACTS += data_transmission/demo_pb2.py
ARTIFACTS += data_transmission/demo_pb2_grpc.py
ARTIFACTS += data_transmission/demo_pb2.pyi

ARTIFACTS += lb_policies/helloworld_pb2.py
ARTIFACTS += lb_policies/helloworld_pb2_grpc.py
ARTIFACTS += lb_policies/helloworld_pb2.pyi

.PHONY: all
all: ${ARTIFACTS}

helloworld/helloworld_pb2.py helloworld/helloworld_pb2_grpc.py helloworld/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=helloworld --grpc_python_out=helloworld --pyi_out=helloworld -I ../protos ../protos/helloworld.proto

timeout/helloworld_pb2.py timeout/helloworld_pb2_grpc.py timeout/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=timeout --grpc_python_out=timeout --pyi_out=timeout -I ../protos ../protos/helloworld.proto

keep_alive/helloworld_pb2.py keep_alive/helloworld_pb2_grpc.py keep_alive/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=keep_alive --grpc_python_out=keep_alive --pyi_out=keep_alive -I ../protos ../protos/helloworld.proto

health_checking/helloworld_pb2.py health_checking/helloworld_pb2_grpc.py health_checking/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=health_checking --grpc_python_out=health_checking --pyi_out=health_checking -I ../protos ../protos/helloworld.proto

async_streaming/phone_pb2.py async_streaming/phone_pb2_grpc.py async_streaming/phone_pb2.pyi: async_streaming/phone.proto
	python3 -m grpc_tools.protoc --python_out=async_streaming --grpc_python_out=async_streaming --pyi_out=async_streaming -I async_streaming async_streaming/phone.proto

hellostreamingworld/hellostreamingworld_pb2.py hellostreamingworld/hellostreamingworld_pb2_grpc.py hellostreamingworld/hellostreamingworld_pb2.pyi: ../protos/hellostreamingworld.proto
	python3 -m grpc_tools.protoc --python_out=hellostreamingworld --grpc_python_out=hellostreamingworld --pyi_out=hellostreamingworld -I ../protos ../protos/hellostreamingworld.proto

uds/helloworld_pb2.py uds/helloworld_pb2_grpc.py uds/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=uds --grpc_python_out=uds --pyi_out=uds -I ../protos ../protos/helloworld.proto

xds/helloworld_pb2.py xds/helloworld_pb2_grpc.py xds/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=xds --grpc_python_out=xds --pyi_out=xds -I ../protos ../protos/helloworld.proto

multiplex/helloworld_pb2.py multiplex/helloworld_pb2_grpc.py multiplex/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=multiplex --grpc_python_out=multiplex --pyi_out=multiplex -I ../protos ../protos/helloworld.proto

multiplex/route_guide_pb2.py multiplex/route_guide_pb2_grpc.py multiplex/route_guide_pb2.pyi: ../protos/route_guide.proto
	python3 -m grpc_tools.protoc --python_out=multiplex --grpc_python_out=multiplex --pyi_out=multiplex -I ../protos ../protos/route_guide.proto

route_guide/route_guide_pb2.py route_guide/route_guide_pb2_grpc.py route_guide/route_guide_pb2.pyi: ../protos/route_guide.proto
	python3 -m grpc_tools.protoc --python_out=route_guide --grpc_python_out=route_guide --pyi_out=route_guide -I ../protos ../protos/route_guide.proto


interceptors/default_value/helloworld_pb2.py interceptors/default_value/helloworld_pb2_grpc.py interceptors/default_value/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=interceptors/default_value --grpc_python_out=interceptors/default_value --pyi_out=interceptors/default_value -I ../protos ../protos/helloworld.proto

interceptors/headers/helloworld_pb2.py interceptors/headers/helloworld_pb2_grpc.py interceptors/headers/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=interceptors/headers --grpc_python_out=interceptors/headers --pyi_out=interceptors/headers -I ../protos ../protos/helloworld.proto

metadata/helloworld_pb2.py metadata/helloworld_pb2_grpc.py metadata/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=metadata --grpc_python_out=metadata --pyi_out=metadata -I ../protos ../protos/helloworld.proto

data_transmission/demo_pb2.py data_transmission/demo_pb2_grpc.py data_transmission/demo_pb2.pyi: data_transmission/demo.proto
	python3 -m grpc_tools.protoc --python_out=data_transmission --grpc_python_out=data_transmission --pyi_out=data_transmission -I data_transmission data_transmission/demo.proto

lb_policies/helloworld_pb2.py lb_policies/helloworld_pb2_grpc.py lb_policies/helloworld_pb2.pyi: ../protos/helloworld.proto
	python3 -m grpc_tools.protoc --python_out=lb_policies --grpc_python_out=lb_policies --pyi_out=lb_policies -I ../protos ../protos/helloworld.proto

.PHONY: clean
clean:
	rm -f ${ARTIFACTS}
