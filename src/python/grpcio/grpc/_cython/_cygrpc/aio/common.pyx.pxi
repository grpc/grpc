# Copyright 2019 The gRPC Authors
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


cdef object deserialize(object deserializer, bytes raw_message):
    """Perform deserialization on raw bytes.

    Failure to deserialize is a fatal error.
    """
    if deserializer:
        return deserializer(raw_message)
    else:
        return raw_message


cdef bytes serialize(object serializer, object message):
    """Perform serialization on a message.

    Failure to serialize is a fatal error.
    """
    if serializer:
        return serializer(message)
    else:
        return message
