#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#
AUTOMAKE_OPTIONS = subdir-objects serial-tests

BUILT_SOURCES = gen-cpp/test_types.cpp

#noinst_LTLIBRARIES = libtutorialgencpp.la
noinst_LTLIBRARIES = libtestgencpp.la
nodist_libtestgencpp_la_SOURCES = \
	gen-cpp/Greeter.grpc.thrift.cpp \
	gen-cpp/Greeter.grpc.thrift.h \
	gen-cpp/test_constants.cpp \
	gen-cpp/test_constants.h \
	gen-cpp/test_types.cpp \
	gen-cpp/test_types.h



libtestgencpp_la_LIBADD = $(top_builddir)/lib/cpp/libthrift.la

noinst_PROGRAMS = \
	TestServer \
	TestClient

TestServer_SOURCES = \
	CppServer.cpp

TestServer_LDADD = \
	libtestgencpp.la \
	$(top_builddir)/lib/cpp/libthrift.la

TestClient_SOURCES = \
	CppClient.cpp

TestClient_LDADD = \
	libtestgencpp.la \
	$(top_builddir)/lib/cpp/libthrift.la

#
# Common thrift code generation rules
#
THRIFT = $(top_builddir)/compiler/cpp/thrift

gen-cpp/Greeter.grpc.thrift.cpp gen-cpp/test_constants.cpp gen-cpp/test_types.cpp: $(top_srcdir)/tutorial/cpp/test.thrift
	$(THRIFT) --gen cpp -r $<

AM_CPPFLAGS = $(BOOST_CPPFLAGS) $(LIBEVENT_CPPFLAGS) -I$(top_srcdir)/lib/cpp/src -Igen-cpp
AM_CXXFLAGS = -Wall -Wextra -pedantic
AM_LDFLAGS = $(BOOST_LDFLAGS) $(LIBEVENT_LDFLAGS) `pkg-config --libs grpc++ grpc` -lpthread -ldl -lgrpc

clean-local:
	$(RM) -r gen-cpp

testserver: all
	./TestServer

testclient: all
	./TestClient

style-local:
	$(CPPSTYLE_CMD)

EXTRA_DIST = \
	CMakeLists.txt \
	CppClient.cpp \
	CppServer.cpp
