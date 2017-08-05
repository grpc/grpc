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

#ifndef NET_GRPC_HHVM_GRPC_COMMON_H_
#define NET_GRPC_HHVM_GRPC_COMMON_H_

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>

#ifdef HAVE_CONFIG_H
    #include "config.h"
#endif

#include "hphp/runtime/ext/extension.h"

class TraceScope
{
public:
    TraceScope(const std::string& message, const std::string& function, const std::string& file) :
        m_Message{ message}, m_Function{ function }, m_File{ file }
    {
        std::cout << __TIME__ << " - " << m_Message << " - Entry " << m_Function << ' '
                  << m_File << std::endl;
    }
    ~TraceScope(void)
    {
        std::cout << __TIME__ << " - " << m_Message << " - Exit  " << m_Function << ' '
                  << m_File << std::endl;
    }
    std::string m_Message;
    std::string m_Function;
    std::string m_File;
};

#define HHVM_TRACE_DEBUG
#define HHVM_TRACE_DEBUG_DETAILED

#ifdef HHVM_TRACE_DEBUG
    #define HHVM_TRACE_SCOPE(x) TraceScope traceScope{ x, __func__, __FILE__ };
#else
    #define HHVM_TRACE_SCOPE(x)
#endif // HHVM_TRACE_DEBUG

namespace HPHP {

#define IMPLEMENT_GET_CLASS(cls) \
  Class* cls::getClass() { \
    if (s_class == nullptr) { \
        s_class = Unit::lookupClass(s_className.get()); \
        assert(s_class); \
    } \
    return s_class; \
  }

}

#endif /* NET_GRPC_HHVM_GRPC_COMMON_H_ */
