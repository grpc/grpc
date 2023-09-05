# Copyright 2023 The gRPC Authors
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
"""Patches the compile() to allow enable parallel compilation of C/C++.

build_ext has lots of C/C++ files and normally them one by one.
Enabling parallel build helps a lot.
"""

import distutils.ccompiler
from distutils.errors import DistutilsExecError, CompileError
import distutils._msvccompiler
import os

try:
    BUILD_EXT_COMPILER_JOBS = int(
        os.environ["GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS"]
    )
except KeyError:
    import multiprocessing

    BUILD_EXT_COMPILER_JOBS = multiprocessing.cpu_count()


# monkey-patch for parallel compilation
def _parallel_compile(
    self,
    sources,
    output_dir=None,
    macros=None,
    include_dirs=None,
    debug=0,
    extra_preargs=None,
    extra_postargs=None,
    depends=None,
):
    # setup the same way as distutils.ccompiler.CCompiler
    # https://github.com/python/cpython/blob/31368a4f0e531c19affe2a1becd25fc316bc7501/Lib/distutils/ccompiler.py#L564
    macros, objects, extra_postargs, pp_opts, build = self._setup_compile(
        output_dir, macros, include_dirs, sources, depends, extra_postargs
    )
    cc_args = self._get_cc_args(pp_opts, debug, extra_preargs)

    def _compile_single_file(obj):
        try:
            src, ext = build[obj]
        except KeyError:
            return
        self._compile(obj, src, ext, cc_args, extra_postargs, pp_opts)

    # run compilation of individual files in parallel
    import multiprocessing.pool

    multiprocessing.pool.ThreadPool(BUILD_EXT_COMPILER_JOBS).map(
        _compile_single_file, objects
    )
    return objects

def _parallel_msvc_compile(
    self,
    sources,
    output_dir=None,
    macros=None,
    include_dirs=None,
    debug=0,
    extra_preargs=None,
    extra_postargs=None,
    depends=None,
):
    if not self.initialized:
        self.initialize()
    compile_info = self._setup_compile(output_dir, macros, include_dirs,
                                        sources, depends, extra_postargs)
    macros, objects, extra_postargs, pp_opts, build = compile_info
    compile_opts = extra_preargs or []
    compile_opts.append('/c')
    compile_opts.append('/O2')
    print("compile_options: " + self.compile_options)
    if debug:
        compile_opts.extend(self.compile_options_debug)
    else:
        compile_opts.extend(self.compile_options)

    add_cpp_opts = False

    for obj in objects:
        try:
            src, ext = build[obj]
        except KeyError:
            continue
        if debug:
            # pass the full pathname to MSVC in debug mode,
            # this allows the debugger to find the source file
            # without asking the user to browse for it
            src = os.path.abspath(src)

        if ext in self._c_extensions:
            input_opt = "/Tc" + src
        elif ext in self._cpp_extensions:
            input_opt = "/Tp" + src
            add_cpp_opts = True
        elif ext in self._rc_extensions:
            # compile .RC to .RES file
            input_opt = src
            output_opt = "/fo" + obj
            try:
                self.spawn([self.rc] + pp_opts + [output_opt, input_opt])
            except DistutilsExecError as msg:
                raise CompileError(msg)
            continue
        elif ext in self._mc_extensions:
            # Compile .MC to .RC file to .RES file.
            #   * '-h dir' specifies the directory for the
            #     generated include file
            #   * '-r dir' specifies the target directory of the
            #     generated RC file and the binary message resource
            #     it includes
            #
            # For now (since there are no options to change this),
            # we use the source-directory for the include file and
            # the build directory for the RC file and message
            # resources. This works at least for win32all.
            h_dir = os.path.dirname(src)
            rc_dir = os.path.dirname(obj)
            try:
                # first compile .MC to .RC and .H file
                self.spawn([self.mc, '-h', h_dir, '-r', rc_dir, src])
                base, _ = os.path.splitext(os.path.basename (src))
                rc_file = os.path.join(rc_dir, base + '.rc')
                # then compile .RC to .RES file
                self.spawn([self.rc, "/fo" + obj, rc_file])

            except DistutilsExecError as msg:
                raise CompileError(msg)
            continue
        else:
            # how to handle this file?
            raise CompileError("Don't know how to compile {} to {}"
                                .format(src, obj))

        args = [self.cc] + compile_opts + pp_opts
        if add_cpp_opts:
            args.append('/EHsc')
        args.append(input_opt)
        args.append("/Fo" + obj)
        args.extend(extra_postargs)

        try:
            self.spawn(args)
        except DistutilsExecError as msg:
            raise CompileError(msg)

    return objects

def monkeypatch_compile_maybe():
    """Monkeypatching is dumb, but the build speed gain is worth it."""
    if BUILD_EXT_COMPILER_JOBS > 1:
        distutils.ccompiler.CCompiler.compile = _parallel_compile
    distutils._msvccompiler.MSVCCompiler.compile = _parallel_msvc_compile
