/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _THRIFT_WINDOWS_CONFIG_H_
#define _THRIFT_WINDOWS_CONFIG_H_ 1

#if defined(_MSC_VER) && (_MSC_VER > 1200)
#pragma once
#endif // _MSC_VER

#ifndef _WIN32
#error "This is a Windows header only"
#endif

#include <io.h>
#include <stdlib.h>
#include <direct.h>

#define strtoll(begin_ptr, end_ptr, length) _strtoi64(begin_ptr, end_ptr, length)

#define PRIu64 "I64d"
#define PRIi64 "I64d"

// squelch deprecation warnings
#pragma warning(disable : 4996)
// squelch bool conversion performance warning
#pragma warning(disable : 4800)

#endif // _THRIFT_WINDOWS_CONFIG_H_
