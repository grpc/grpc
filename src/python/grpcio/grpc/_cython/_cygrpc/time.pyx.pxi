# Copyright 2018 The gRPC Authors
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
  return (gpr_inf_future(GPR_CLOCK_REALTIME)
          if time is None else
          gpr_time_from_nanos(time * 1e9, GPR_CLOCK_REALTIME))

cdef double _time_from_timespec(gpr_timespec timespec) except *:
  return gpr_timespec_to_micros(
    gpr_convert_clock_type(timespec, GPR_CLOCK_REALTIME)) / 1e6
