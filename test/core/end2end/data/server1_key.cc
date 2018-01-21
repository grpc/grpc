/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

extern const char test_server1_key[] = {
  0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x42, 0x45, 0x47, 0x49, 0x4e, 0x20, 0x52,
  0x53, 0x41, 0x20, 0x50, 0x52, 0x49, 0x56, 0x41, 0x54, 0x45, 0x20, 0x4b,
  0x45, 0x59, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x0a, 0x4d, 0x49, 0x49, 0x43,
  0x57, 0x77, 0x49, 0x42, 0x41, 0x41, 0x4b, 0x42, 0x67, 0x51, 0x44, 0x68,
  0x77, 0x78, 0x55, 0x6e, 0x4b, 0x43, 0x77, 0x6c, 0x53, 0x61, 0x57, 0x41,
  0x77, 0x7a, 0x4f, 0x42, 0x32, 0x4c, 0x53, 0x48, 0x56, 0x65, 0x67, 0x4a,
  0x48, 0x76, 0x37, 0x44, 0x44, 0x57, 0x6d, 0x69, 0x6e, 0x54, 0x67, 0x34,
  0x77, 0x7a, 0x4c, 0x4c, 0x73, 0x66, 0x2b, 0x4c, 0x51, 0x38, 0x6e, 0x5a,
  0x0a, 0x62, 0x70, 0x6a, 0x66, 0x6e, 0x35, 0x76, 0x67, 0x49, 0x7a, 0x78,
  0x43, 0x75, 0x52, 0x68, 0x34, 0x52, 0x70, 0x39, 0x51, 0x59, 0x4d, 0x35,
  0x46, 0x68, 0x66, 0x72, 0x4a, 0x58, 0x39, 0x77, 0x63, 0x59, 0x61, 0x77,
  0x50, 0x2f, 0x48, 0x54, 0x62, 0x4a, 0x37, 0x70, 0x37, 0x4c, 0x56, 0x51,
  0x4f, 0x32, 0x51, 0x59, 0x41, 0x50, 0x2b, 0x61, 0x6b, 0x4d, 0x54, 0x48,
  0x78, 0x67, 0x4b, 0x75, 0x4d, 0x0a, 0x42, 0x7a, 0x56, 0x56, 0x2b, 0x2b,
  0x33, 0x77, 0x57, 0x54, 0x6f, 0x4b, 0x66, 0x56, 0x5a, 0x55, 0x6a, 0x46,
  0x58, 0x38, 0x6e, 0x66, 0x54, 0x66, 0x47, 0x4d, 0x47, 0x77, 0x57, 0x41,
  0x48, 0x4a, 0x44, 0x6e, 0x6c, 0x45, 0x47, 0x6e, 0x55, 0x34, 0x74, 0x6c,
  0x39, 0x55, 0x75, 0x6a, 0x6f, 0x43, 0x56, 0x34, 0x45, 0x4e, 0x4a, 0x74,
  0x7a, 0x46, 0x6f, 0x51, 0x49, 0x44, 0x41, 0x51, 0x41, 0x42, 0x0a, 0x41,
  0x6f, 0x47, 0x41, 0x4a, 0x2b, 0x36, 0x68, 0x70, 0x7a, 0x4e, 0x72, 0x32,
  0x34, 0x79, 0x54, 0x51, 0x5a, 0x74, 0x46, 0x57, 0x51, 0x70, 0x44, 0x70,
  0x45, 0x79, 0x46, 0x70, 0x6c, 0x64, 0x64, 0x4b, 0x4a, 0x4d, 0x4f, 0x78,
  0x44, 0x79, 0x61, 0x33, 0x53, 0x39, 0x70, 0x70, 0x4b, 0x33, 0x76, 0x54,
  0x57, 0x72, 0x49, 0x49, 0x54, 0x56, 0x32, 0x78, 0x4e, 0x63, 0x75, 0x63,
  0x77, 0x37, 0x49, 0x0a, 0x63, 0x65, 0x54, 0x62, 0x64, 0x79, 0x72, 0x47,
  0x73, 0x79, 0x6a, 0x73, 0x55, 0x30, 0x2f, 0x48, 0x64, 0x43, 0x63, 0x49,
  0x66, 0x39, 0x79, 0x6d, 0x32, 0x6a, 0x66, 0x6d, 0x47, 0x4c, 0x55, 0x77,
  0x6d, 0x79, 0x68, 0x6c, 0x74, 0x4b, 0x56, 0x77, 0x30, 0x51, 0x59, 0x63,
  0x46, 0x42, 0x30, 0x58, 0x4c, 0x6b, 0x63, 0x30, 0x6e, 0x49, 0x35, 0x59,
  0x76, 0x45, 0x59, 0x6f, 0x65, 0x56, 0x44, 0x67, 0x0a, 0x6f, 0x6d, 0x5a,
  0x49, 0x58, 0x6e, 0x31, 0x45, 0x33, 0x45, 0x57, 0x2b, 0x73, 0x53, 0x49,
  0x57, 0x53, 0x62, 0x6b, 0x4d, 0x75, 0x39, 0x62, 0x59, 0x32, 0x6b, 0x73,
  0x74, 0x4b, 0x58, 0x52, 0x32, 0x55, 0x5a, 0x6d, 0x4d, 0x67, 0x57, 0x44,
  0x74, 0x6d, 0x42, 0x45, 0x50, 0x4d, 0x61, 0x45, 0x43, 0x51, 0x51, 0x44,
  0x36, 0x79, 0x54, 0x34, 0x54, 0x41, 0x5a, 0x4d, 0x35, 0x68, 0x47, 0x42,
  0x62, 0x0a, 0x63, 0x69, 0x42, 0x4b, 0x67, 0x4d, 0x55, 0x50, 0x36, 0x50,
  0x77, 0x4f, 0x68, 0x50, 0x68, 0x4f, 0x4d, 0x50, 0x49, 0x76, 0x69, 0x6a,
  0x4f, 0x35, 0x30, 0x41, 0x69, 0x75, 0x36, 0x69, 0x75, 0x43, 0x56, 0x38,
  0x38, 0x6c, 0x31, 0x51, 0x49, 0x79, 0x33, 0x38, 0x67, 0x57, 0x56, 0x68,
  0x78, 0x6a, 0x4e, 0x72, 0x71, 0x36, 0x50, 0x33, 0x34, 0x36, 0x6a, 0x34,
  0x49, 0x42, 0x67, 0x2b, 0x6b, 0x42, 0x0a, 0x39, 0x61, 0x6c, 0x77, 0x70,
  0x43, 0x4f, 0x44, 0x41, 0x6b, 0x45, 0x41, 0x35, 0x6e, 0x53, 0x6e, 0x6d,
  0x39, 0x6b, 0x36, 0x79, 0x6b, 0x59, 0x65, 0x51, 0x57, 0x4e, 0x53, 0x30,
  0x66, 0x4e, 0x57, 0x69, 0x52, 0x69, 0x6e, 0x43, 0x64, 0x6c, 0x32, 0x33,
  0x41, 0x37, 0x75, 0x73, 0x44, 0x47, 0x53, 0x75, 0x4b, 0x4b, 0x6c, 0x6d,
  0x30, 0x31, 0x39, 0x69, 0x6f, 0x6d, 0x4a, 0x2f, 0x52, 0x67, 0x64, 0x0a,
  0x4d, 0x4b, 0x44, 0x4f, 0x70, 0x30, 0x71, 0x2f, 0x32, 0x4f, 0x6f, 0x73,
  0x74, 0x62, 0x74, 0x65, 0x4f, 0x57, 0x4d, 0x32, 0x4d, 0x52, 0x46, 0x66,
  0x34, 0x6a, 0x4d, 0x48, 0x33, 0x77, 0x79, 0x56, 0x43, 0x77, 0x4a, 0x41,
  0x66, 0x41, 0x64, 0x6a, 0x4a, 0x38, 0x73, 0x7a, 0x6f, 0x4e, 0x4b, 0x54,
  0x52, 0x53, 0x61, 0x67, 0x53, 0x62, 0x68, 0x39, 0x76, 0x57, 0x79, 0x67,
  0x6e, 0x42, 0x32, 0x76, 0x0a, 0x49, 0x42, 0x79, 0x63, 0x36, 0x6c, 0x34,
  0x54, 0x54, 0x75, 0x5a, 0x51, 0x4a, 0x52, 0x47, 0x7a, 0x43, 0x76, 0x65,
  0x61, 0x66, 0x7a, 0x39, 0x6c, 0x6f, 0x76, 0x75, 0x42, 0x33, 0x57, 0x6f,
  0x68, 0x43, 0x41, 0x42, 0x64, 0x51, 0x52, 0x64, 0x39, 0x75, 0x6b, 0x43,
  0x58, 0x4c, 0x32, 0x43, 0x70, 0x73, 0x45, 0x70, 0x71, 0x7a, 0x6b, 0x61,
  0x66, 0x4f, 0x51, 0x4a, 0x41, 0x4a, 0x55, 0x6a, 0x63, 0x0a, 0x55, 0x53,
  0x65, 0x64, 0x44, 0x6c, 0x71, 0x33, 0x7a, 0x47, 0x5a, 0x77, 0x59, 0x4d,
  0x31, 0x59, 0x77, 0x38, 0x64, 0x38, 0x52, 0x75, 0x69, 0x72, 0x42, 0x55,
  0x46, 0x5a, 0x4e, 0x71, 0x4a, 0x65, 0x6c, 0x59, 0x61, 0x69, 0x2b, 0x6e,
  0x52, 0x59, 0x43, 0x6c, 0x44, 0x6b, 0x52, 0x56, 0x46, 0x67, 0x62, 0x35,
  0x79, 0x6b, 0x73, 0x6f, 0x59, 0x79, 0x63, 0x62, 0x71, 0x35, 0x54, 0x78,
  0x47, 0x6f, 0x0a, 0x56, 0x65, 0x71, 0x4b, 0x4f, 0x76, 0x67, 0x50, 0x70,
  0x6a, 0x34, 0x52, 0x57, 0x50, 0x48, 0x6c, 0x4c, 0x77, 0x4a, 0x41, 0x47,
  0x55, 0x4d, 0x6b, 0x33, 0x62, 0x71, 0x54, 0x39, 0x31, 0x78, 0x42, 0x55,
  0x43, 0x6e, 0x4c, 0x52, 0x73, 0x2f, 0x76, 0x66, 0x6f, 0x43, 0x70, 0x48,
  0x70, 0x67, 0x36, 0x65, 0x79, 0x77, 0x51, 0x54, 0x42, 0x44, 0x41, 0x56,
  0x36, 0x78, 0x6b, 0x79, 0x7a, 0x34, 0x61, 0x0a, 0x52, 0x48, 0x33, 0x49,
  0x37, 0x2f, 0x2b, 0x79, 0x6a, 0x33, 0x5a, 0x78, 0x52, 0x32, 0x4a, 0x6f,
  0x57, 0x48, 0x67, 0x55, 0x77, 0x5a, 0x37, 0x6c, 0x5a, 0x6b, 0x31, 0x56,
  0x6e, 0x68, 0x66, 0x66, 0x46, 0x79, 0x65, 0x37, 0x53, 0x42, 0x58, 0x79,
  0x61, 0x67, 0x3d, 0x3d, 0x0a, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x45, 0x4e,
  0x44, 0x20, 0x52, 0x53, 0x41, 0x20, 0x50, 0x52, 0x49, 0x56, 0x41, 0x54,
  0x45, 0x20, 0x4b, 0x45, 0x59, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x0a, 0x00
};
