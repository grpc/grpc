# Copyright 2016 gRPC authors.
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


# This function will ascii encode unicode string inputs if neccesary.
# In Python3, unicode strings are the default str type.
cdef bytes str_to_bytes(object s):
  if s is None or isinstance(s, bytes):
    return s
  elif isinstance(s, unicode):
    return s.encode('ascii')
  else:
    raise TypeError('Expected bytes, str, or unicode, not {}'.format(type(s)))
