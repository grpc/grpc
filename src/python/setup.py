# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""A setup module for the GRPC Python package."""

from distutils import core as _core

_EXTENSION_SOURCES = (
    'src/_adapter/_c.c',
    'src/_adapter/_call.c',
    'src/_adapter/_channel.c',
    'src/_adapter/_completion_queue.c',
    'src/_adapter/_error.c',
    'src/_adapter/_server.c',
)

_EXTENSION_INCLUDE_DIRECTORIES = (
    'src',
    # TODO(nathaniel): Can this path specification be made to work?
    #'../../include',
)

_EXTENSION_LIBRARIES = (
    'gpr',
    'grpc',
)

_EXTENSION_LIBRARY_DIRECTORIES = (
    # TODO(nathaniel): Can this path specification be made to work?
    #'../../libs/dbg',
)

_EXTENSION_MODULE = _core.Extension(
    '_adapter._c', sources=list(_EXTENSION_SOURCES),
    include_dirs=_EXTENSION_INCLUDE_DIRECTORIES,
    libraries=_EXTENSION_LIBRARIES,
    library_dirs=_EXTENSION_LIBRARY_DIRECTORIES)

_PACKAGES=(
    '_adapter',
    '_framework',
    '_framework.base',
    '_framework.base.packets',
    '_framework.common',
    '_framework.face',
    '_framework.face.testing',
    '_framework.foundation',
    '_junkdrawer',
)

_PACKAGE_DIRECTORIES = {
    '_adapter': 'src/_adapter',
    '_framework': 'src/_framework',
    '_junkdrawer': 'src/_junkdrawer',
}

_core.setup(
    name='grpc', version='0.0.1',
    ext_modules=[_EXTENSION_MODULE], packages=_PACKAGES,
    package_dir=_PACKAGE_DIRECTORIES)
