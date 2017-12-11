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
"""Implementation of gRPC Python interceptors."""


class _ServicePipeline(object):

    def __init__(self, interceptors):
        self.interceptors = tuple(interceptors)

    def _continuation(self, thunk, index):
        return lambda context: self._intercept_at(thunk, index, context)

    def _intercept_at(self, thunk, index, context):
        if index < len(self.interceptors):
            interceptor = self.interceptors[index]
            thunk = self._continuation(thunk, index + 1)
            return interceptor.intercept_service(thunk, context)
        else:
            return thunk(context)

    def execute(self, thunk, context):
        return self._intercept_at(thunk, 0, context)


def service_pipeline(interceptors):
    return _ServicePipeline(interceptors) if interceptors else None
