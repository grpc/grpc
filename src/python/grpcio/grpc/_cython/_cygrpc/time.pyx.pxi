# Copyright 2018 gRPC authors.
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


cdef gpr_timespec _timespec_from_time(object time):
  cdef gpr_timespec timespec
  cdef double c_seconds = time
  if time is None:
    return gpr_inf_future(GPR_CLOCK_REALTIME)
  else:
    return gpr_time_from_nanos(<int64_t>(c_seconds * GPR_NS_PER_SEC), GPR_CLOCK_REALTIME)


cdef double _time_from_timespec(gpr_timespec timespec) except *:
  cdef gpr_timespec real_timespec = gpr_convert_clock_type(
      timespec, GPR_CLOCK_REALTIME)
  return <double>real_timespec.seconds + <double>real_timespec.nanoseconds / GPR_NS_PER_SEC
