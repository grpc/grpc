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


# This function will ascii encode unicode string inputs if necessary.
# In Python3, unicode strings are the default str type.
cdef bytes str_to_bytes(object s):
  if s is None or isinstance(s, bytes):
    return s
  elif isinstance(s, unicode):
    return s.encode('ascii')
  else:
    raise TypeError('Expected bytes, str, or unicode, not {}'.format(type(s)))


# TODO(https://github.com/grpc/grpc/issues/13782): It would be nice for us if
# the type of metadata that we accept were exactly the same as the type of
# metadata that we deliver to our users (so "str" for this function's
# parameter rather than "object"), but would it be nice for our users? Right
# now we haven't yet heard from enough users to know one way or another.
cdef bytes _encode(object string_or_none):
  if string_or_none is None:
    return b''
  elif isinstance(string_or_none, (bytes,)):
    return <bytes>string_or_none
  elif isinstance(string_or_none, (unicode,)):
    return string_or_none.encode('utf8')
  else:
    raise TypeError('Expected str, not {}'.format(type(string_or_none)))


cdef str _decode(bytes bytestring):
    if isinstance(bytestring, (str,)):
        return <str>bytestring
    else:
        try:
            return bytestring.decode('utf8')
        except UnicodeDecodeError:
            _LOGGER.exception('Invalid encoding on %s', bytestring)
            return bytestring.decode('latin1')
