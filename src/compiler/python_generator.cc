/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <cassert>
#include <cctype>
#include <map>
#include <ostream>
#include <sstream>

#include "src/compiler/python_generator.h"
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor.h>

using google::protobuf::FileDescriptor;
using google::protobuf::ServiceDescriptor;
using google::protobuf::MethodDescriptor;
using google::protobuf::io::Printer;
using google::protobuf::io::StringOutputStream;
using std::initializer_list;
using std::map;
using std::string;

namespace grpc_python_generator {
namespace {
//////////////////////////////////
// BEGIN FORMATTING BOILERPLATE //
//////////////////////////////////

// Converts an initializer list of the form { key0, value0, key1, value1, ... }
// into a map of key* to value*. Is merely a readability helper for later code.
map<string, string> ListToDict(const initializer_list<string>& values) {
  assert(values.size() % 2 == 0);
  map<string, string> value_map;
  auto value_iter = values.begin();
  for (unsigned i = 0; i < values.size()/2; ++i) {
    string key = *value_iter;
    ++value_iter;
    string value = *value_iter;
    value_map[key] = value;
    ++value_iter;
  }
  return value_map;
}

// Provides RAII indentation handling. Use as:
// {
//   IndentScope raii_my_indent_var_name_here(my_py_printer);
//   // constructor indented my_py_printer
//   ...
//   // destructor called at end of scope, un-indenting my_py_printer
// }
class IndentScope {
 public:
  explicit IndentScope(Printer* printer) : printer_(printer) {
    printer_->Indent();
  }

  ~IndentScope() {
    printer_->Outdent();
  }

 private:
  Printer* printer_;
};

////////////////////////////////
// END FORMATTING BOILERPLATE //
////////////////////////////////

void PrintService(const ServiceDescriptor* service,
                  Printer* out) {
  string doc = "<fill me in later!>";
  map<string, string> dict = ListToDict({
        "Service", service->name(),
        "Documentation", doc,
      });
  out->Print(dict, "class $Service$Service(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(dict, "\"\"\"$Documentation$\"\"\"\n");
    out->Print("def __init__(self):\n");
    {
      IndentScope raii_method_indent(out);
      out->Print("pass\n");
    }
  }
}

void PrintServicer(const ServiceDescriptor* service,
                   Printer* out) {
  string doc = "<fill me in later!>";
  map<string, string> dict = ListToDict({
        "Service", service->name(),
        "Documentation", doc,
      });
  out->Print(dict, "class $Service$Servicer(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(dict, "\"\"\"$Documentation$\"\"\"\n");
    for (int i = 0; i < service->method_count(); ++i) {
      auto meth = service->method(i);
      out->Print("def $Method$(self, arg):\n", "Method", meth->name());
      {
        IndentScope raii_method_indent(out);
        out->Print("raise NotImplementedError()\n");
      }
    }
  }
}

void PrintStub(const ServiceDescriptor* service,
               Printer* out) {
  string doc = "<fill me in later!>";
  map<string, string> dict = ListToDict({
        "Service", service->name(),
        "Documentation", doc,
      });
  out->Print(dict, "class $Service$Stub(object):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print(dict, "\"\"\"$Documentation$\"\"\"\n");
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* meth = service->method(i);
      auto methdict = ListToDict({"Method", meth->name()});
      out->Print(methdict, "def $Method$(self, arg):\n");
      {
        IndentScope raii_method_indent(out);
        out->Print("raise NotImplementedError()\n");
      }
      out->Print(methdict, "$Method$.async = None\n");
    }
  }
}

void PrintStubImpl(const ServiceDescriptor* service,
                   Printer* out) {
  map<string, string> dict = ListToDict({
        "Service", service->name(),
      });
  out->Print(dict, "class _$Service$Stub($Service$Stub):\n");
  {
    IndentScope raii_class_indent(out);
    out->Print("def __init__(self, face_stub, default_timeout):\n");
    {
      IndentScope raii_method_indent(out);
      out->Print("self._face_stub = face_stub\n"
                 "self._default_timeout = default_timeout\n"
                 "stub_self = self\n");

      for (int i = 0; i < service->method_count(); ++i) {
        const MethodDescriptor* meth = service->method(i);
        bool server_streaming = meth->server_streaming();
        bool client_streaming = meth->client_streaming();
        std::string blocking_call, future_call;
        if (server_streaming) {
          if (client_streaming) {
            blocking_call = "stub_self._face_stub.inline_stream_in_stream_out";
            future_call = blocking_call;
          } else {
            blocking_call = "stub_self._face_stub.inline_value_in_stream_out";
            future_call = blocking_call;
          }
        } else {
          if (client_streaming) {
            blocking_call = "stub_self._face_stub.blocking_stream_in_value_out";
            future_call = "stub_self._face_stub.future_stream_in_value_out";
          } else {
            blocking_call = "stub_self._face_stub.blocking_value_in_value_out";
            future_call = "stub_self._face_stub.future_value_in_value_out";
          }
        }
        // TODO(atash): use the solution described at
        // http://stackoverflow.com/a/2982 to bind 'async' attribute
        // functions to def'd functions instead of using callable attributes.
        auto methdict = ListToDict({
          "Method", meth->name(),
          "BlockingCall", blocking_call,
          "FutureCall", future_call
        });
        out->Print(methdict, "class $Method$(object):\n");
        {
          IndentScope raii_callable_indent(out);
          out->Print("def __call__(self, arg):\n");
          {
            IndentScope raii_callable_call_indent(out);
            out->Print(methdict,
                       "return $BlockingCall$(\"$Method$\", arg, "
                       "stub_self._default_timeout)\n");
          }
          out->Print("def async(self, arg):\n");
          {
            IndentScope raii_callable_async_indent(out);
            out->Print(methdict,
                       "return $FutureCall$(\"$Method$\", arg, "
                       "stub_self._default_timeout)\n");
          }
        }
        out->Print(methdict, "self.$Method$ = $Method$()\n");
      }
    }
  }
}

void PrintStubGenerators(const ServiceDescriptor* service, Printer* out) {
  map<string, string> dict = ListToDict({
        "Service", service->name(),
      });
  // Write out a generator of linked pairs of Server/Stub
  out->Print(dict, "def mock_$Service$(servicer, default_timeout):\n");
  {
    IndentScope raii_mock_indent(out);
    out->Print("value_in_value_out = {}\n"
               "value_in_stream_out = {}\n"
               "stream_in_value_out = {}\n"
               "stream_in_stream_out = {}\n");
    for (int i = 0; i < service->method_count(); ++i) {
      const MethodDescriptor* meth = service->method(i);
      std::string super_interface, meth_dict;
      bool server_streaming = meth->server_streaming();
      bool client_streaming = meth->client_streaming();
      if (server_streaming) {
        if (client_streaming) {
          super_interface = "InlineStreamInStreamOutMethod";
          meth_dict = "stream_in_stream_out";
        } else {
          super_interface = "InlineValueInStreamOutMethod";
          meth_dict = "value_in_stream_out";
        }
      } else {
        if (client_streaming) {
          super_interface = "InlineStreamInValueOutMethod";
          meth_dict = "stream_in_value_out";
        } else {
          super_interface = "InlineValueInValueOutMethod";
          meth_dict = "value_in_value_out";
        }
      }
      map<string, string> methdict = ListToDict({
            "Method", meth->name(),
            "SuperInterface", super_interface,
            "MethodDict", meth_dict
          });
      out->Print(
          methdict, "class $Method$(_face_interfaces.$SuperInterface$):\n");
      {
        IndentScope raii_inline_class_indent(out);
        out->Print("def service(self, request, context):\n");
        {
          IndentScope raii_inline_class_fn_indent(out);
          out->Print(methdict, "return servicer.$Method$(request)\n");
        }
      }
      out->Print(methdict, "$MethodDict$['$Method$'] = $Method$()\n");
    }
    out->Print(
         "face_linked_pair = _face_testing.server_and_stub(default_timeout,"
         "inline_value_in_value_out_methods=value_in_value_out,"
         "inline_value_in_stream_out_methods=value_in_stream_out,"
         "inline_stream_in_value_out_methods=stream_in_value_out,"
         "inline_stream_in_stream_out_methods=stream_in_stream_out)\n");
    out->Print("class LinkedPair(object):\n");
    {
      IndentScope raii_linked_pair(out);
      out->Print("def __init__(self, server, stub):\n");
      {
        IndentScope raii_linked_pair_init(out);
        out->Print("self.server = server\n"
                   "self.stub = stub\n");
      }
    }
    out->Print(
        dict,
        "stub = _$Service$Stub(face_linked_pair.stub, default_timeout)\n");
    out->Print("return LinkedPair(None, stub)\n");
  }
}

}  // namespace

string GetServices(const FileDescriptor* file) {
  string output;
  StringOutputStream output_stream(&output);
  Printer out(&output_stream, '$');
  out.Print("from grpc.framework.face import demonstration as _face_testing\n");
  out.Print("from grpc.framework.face import interfaces as _face_interfaces\n");

  for (int i = 0; i < file->service_count(); ++i) {
    auto service = file->service(i);
    PrintService(service, &out);
    PrintServicer(service, &out);
    PrintStub(service, &out);
    PrintStubImpl(service, &out);
    PrintStubGenerators(service, &out);
  }
  return output;
}

}  // namespace grpc_python_generator
