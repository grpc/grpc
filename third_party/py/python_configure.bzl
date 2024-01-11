# Adapted with modifications from tensorflow/third_party/py/
"""Repository rule for Python autoconfiguration.

`python_configure` depends on the following environment variables:

  * `PYTHON3_BIN_PATH`: location of python binary.
  * `PYTHON3_LIB_PATH`: Location of python libraries.
"""

_BAZEL_SH = "BAZEL_SH"
_PYTHON3_BIN_PATH = "PYTHON3_BIN_PATH"
_PYTHON3_LIB_PATH = "PYTHON3_LIB_PATH"

_HEADERS_HELP = (
    "Are Python headers installed? Try installing " +
    "python3-dev on Debian-based systems. Try python3-devel " +
    "on Redhat-based systems."
)

def _tpl(repository_ctx, tpl, substitutions = {}, out = None):
    if not out:
        out = tpl
    repository_ctx.template(
        out,
        Label("//third_party/py:%s.tpl" % tpl),
        substitutions,
    )

def _fail(msg):
    """Output failure message when auto configuration fails."""
    red = "\033[0;31m"
    no_color = "\033[0m"
    fail("%sPython Configuration Error:%s %s\n" % (red, no_color, msg))

def _is_windows(repository_ctx):
    """Returns true if the host operating system is windows."""
    os_name = repository_ctx.os.name.lower()
    return os_name.find("windows") != -1

def _execute(
        repository_ctx,
        cmdline,
        error_msg = None,
        error_details = None,
        empty_stdout_fine = False):
    """Executes an arbitrary shell command.

    Args:
        repository_ctx: the repository_ctx object
        cmdline: list of strings, the command to execute
        error_msg: string, a summary of the error if the command fails
        error_details: string, details about the error or steps to fix it
        empty_stdout_fine: bool, if True, an empty stdout result is fine, otherwise
        it's an error
    Return:
        the result of repository_ctx.execute(cmdline)
  """
    result = repository_ctx.execute(cmdline)
    if result.stderr or not (empty_stdout_fine or result.stdout):
        _fail("\n".join([
            error_msg.strip() if error_msg else "Repository command failed",
            result.stderr.strip(),
            error_details if error_details else "",
        ]))
    else:
        return result

def _read_dir(repository_ctx, src_dir):
    """Returns a string with all files in a directory.

  Finds all files inside a directory, traversing subfolders and following
  symlinks. The returned string contains the full path of all files
  separated by line breaks.
  """
    if _is_windows(repository_ctx):
        src_dir = src_dir.replace("/", "\\")
        find_result = _execute(
            repository_ctx,
            ["cmd.exe", "/c", "dir", src_dir, "/b", "/s", "/a-d"],
            empty_stdout_fine = True,
        )

        # src_files will be used in genrule.outs where the paths must
        # use forward slashes.
        return find_result.stdout.replace("\\", "/")
    else:
        find_result = _execute(
            repository_ctx,
            ["find", src_dir, "-follow", "-type", "f"],
            empty_stdout_fine = True,
        )
        return find_result.stdout

def _genrule(src_dir, genrule_name, command, outs):
    """Returns a string with a genrule.

  Genrule executes the given command and produces the given outputs.
  """
    return ("genrule(\n" + '    name = "' + genrule_name + '",\n' +
            "    outs = [\n" + outs + "\n    ],\n" + '    cmd = """\n' +
            command + '\n   """,\n' + ")\n")

def _normalize_path(path):
    """Returns a path with '/' and remove the trailing slash."""
    path = path.replace("\\", "/")
    if path[-1] == "/":
        path = path[:-1]
    return path

def _symlink_genrule_for_dir(
        repository_ctx,
        src_dir,
        dest_dir,
        genrule_name,
        src_files = [],
        dest_files = []):
    """Returns a genrule to symlink(or copy if on Windows) a set of files.

  If src_dir is passed, files will be read from the given directory; otherwise
  we assume files are in src_files and dest_files
  """
    if src_dir != None:
        src_dir = _normalize_path(src_dir)
        dest_dir = _normalize_path(dest_dir)
        files = "\n".join(
            sorted(_read_dir(repository_ctx, src_dir).splitlines()),
        )

        # Create a list with the src_dir stripped to use for outputs.
        dest_files = files.replace(src_dir, "").splitlines()
        src_files = files.splitlines()
    command = []
    outs = []
    for i in range(len(dest_files)):
        if dest_files[i] != "":
            # If we have only one file to link we do not want to use the dest_dir, as
            # $(@D) will include the full path to the file.
            dest = "$(@D)/" + dest_dir + dest_files[i] if len(
                dest_files,
            ) != 1 else "$(@D)/" + dest_files[i]

            # On Windows, symlink is not supported, so we just copy all the files.
            cmd = "cp -f" if _is_windows(repository_ctx) else "ln -s"
            command.append(cmd + ' "%s" "%s"' % (src_files[i], dest))
            outs.append('        "' + dest_dir + dest_files[i] + '",')
    return _genrule(
        src_dir,
        genrule_name,
        " && ".join(command),
        "\n".join(outs),
    )

def _get_python_bin(repository_ctx, bin_path_key, default_bin_path, allow_absent):
    """Gets the python bin path."""
    python_bin = repository_ctx.os.environ.get(bin_path_key, default_bin_path)
    if not repository_ctx.path(python_bin).exists:
        # It's a command, use 'which' to find its path.
        python_bin_path = repository_ctx.which(python_bin)
    else:
        # It's a path, use it as it is.
        python_bin_path = python_bin
    if python_bin_path != None:
        return str(python_bin_path)
    if not allow_absent:
        _fail("Cannot find python in PATH, please make sure " +
              "python is installed and add its directory in PATH, or --define " +
              "%s='/something/else'.\nPATH=%s" %
              (bin_path_key, repository_ctx.os.environ.get("PATH", "")))
    else:
        return None

def _get_bash_bin(repository_ctx):
    """Gets the bash bin path."""
    bash_bin = repository_ctx.os.environ.get(_BAZEL_SH)
    if bash_bin != None:
        return bash_bin
    else:
        bash_bin_path = repository_ctx.which("bash")
        if bash_bin_path != None:
            return str(bash_bin_path)
        else:
            _fail(
                "Cannot find bash in PATH, please make sure " +
                "bash is installed and add its directory in PATH, or --define " +
                "%s='/path/to/bash'.\nPATH=%s" %
                (_BAZEL_SH, repository_ctx.os.environ.get("PATH", "")),
            )

def _get_python_lib(repository_ctx, python_bin, lib_path_key):
    """Gets the python lib path."""
    python_lib = repository_ctx.os.environ.get(lib_path_key)
    if python_lib != None:
        return python_lib
    print_lib = (
        "<<END\n" + "from __future__ import print_function\n" +
        "import site\n" + "import os\n" + "\n" + "try:\n" +
        "  input = raw_input\n" + "except NameError:\n" + "  pass\n" + "\n" +
        "python_paths = []\n" + "if os.getenv('PYTHONPATH') is not None:\n" +
        "  python_paths = os.getenv('PYTHONPATH').split(':')\n" + "try:\n" +
        "  library_paths = site.getsitepackages()\n" +
        "except AttributeError:\n" +
        " import sysconfig\n" +
        " library_paths = [sysconfig.get_path('purelib')]\n" +
        "all_paths = set(python_paths + library_paths)\n" + "paths = []\n" +
        "for path in all_paths:\n" + "  if os.path.isdir(path):\n" +
        "    paths.append(path)\n" + "if len(paths) >=1:\n" +
        "  print(paths[0])\n" + "END"
    )
    cmd = '"%s" - %s' % (python_bin, print_lib)
    result = repository_ctx.execute([_get_bash_bin(repository_ctx), "-c", cmd])
    return result.stdout.strip("\n")

def _check_python_lib(repository_ctx, python_lib):
    """Checks the python lib path."""
    cmd = 'test -d "%s" -a -x "%s"' % (python_lib, python_lib)
    result = repository_ctx.execute([_get_bash_bin(repository_ctx), "-c", cmd])
    if result.return_code == 1:
        _fail("Invalid python library path: %s" % python_lib)

def _check_python_bin(repository_ctx, python_bin, bin_path_key, allow_absent):
    """Checks the python bin path."""
    cmd = '[[ -x "%s" ]] && [[ ! -d "%s" ]]' % (python_bin, python_bin)
    result = repository_ctx.execute([_get_bash_bin(repository_ctx), "-c", cmd])
    if result.return_code == 1:
        if not allow_absent:
            _fail("--define %s='%s' is not executable. Is it the python binary?" %
                  (bin_path_key, python_bin))
        else:
            return None
    return True

def _get_python_include(repository_ctx, python_bin):
    """Gets the python include path."""
    result = _execute(
        repository_ctx,
        [
            python_bin,
            "-c",
            "from __future__ import print_function;" +
            "import sysconfig;" +
            "print(sysconfig.get_path('include'))",
        ],
        error_msg = "Problem getting python include path for {}.".format(python_bin),
        error_details = (
            "Is the Python binary path set up right? " + "(See ./configure or " +
            python_bin + ".) " + _HEADERS_HELP
        ),
    )
    include_path = result.stdout.splitlines()[0]
    _execute(
        repository_ctx,
        [
            python_bin,
            "-c",
            "import os;" +
            "main_header = os.path.join(r'{}', 'Python.h');".format(include_path) +
            "assert os.path.exists(main_header), main_header + ' does not exist.'",
        ],
        error_msg = "Unable to find Python headers for {}".format(python_bin),
        error_details = _HEADERS_HELP,
        empty_stdout_fine = True,
    )
    return include_path

def _get_python_import_lib_name(repository_ctx, python_bin, bin_path_key):
    """Get Python import library name (pythonXY.lib) on Windows."""
    result = _execute(
        repository_ctx,
        [
            python_bin,
            "-c",
            "import sys;" + 'print("python" + str(sys.version_info[0]) + ' +
            '      str(sys.version_info[1]) + ".lib")',
        ],
        error_msg = "Problem getting python import library.",
        error_details = ("Is the Python binary path set up right? " +
                         "(See ./configure or " + bin_path_key + ".) "),
    )
    return result.stdout.splitlines()[0]

def _create_single_version_package(
        repository_ctx,
        variety_name,
        bin_path_key,
        default_bin_path,
        lib_path_key,
        allow_absent):
    """Creates the repository containing files set up to build with Python."""
    empty_include_rule = "filegroup(\n  name=\"{}_include\",\n  srcs=[],\n)".format(variety_name)

    python_bin = _get_python_bin(repository_ctx, bin_path_key, default_bin_path, allow_absent)
    if (python_bin == None or
        _check_python_bin(repository_ctx,
                          python_bin,
                          bin_path_key,
                          allow_absent) == None) and allow_absent:
            python_include_rule = empty_include_rule
    else:
        python_lib = _get_python_lib(repository_ctx, python_bin, lib_path_key)
        _check_python_lib(repository_ctx, python_lib)
        python_include = _get_python_include(repository_ctx, python_bin)
        python_include_rule = _symlink_genrule_for_dir(
            repository_ctx,
            python_include,
            "{}_include".format(variety_name),
            "{}_include".format(variety_name),
        )
    python_import_lib_genrule = ""

    # To build Python C/C++ extension on Windows, we need to link to python import library pythonXY.lib
    # See https://docs.python.org/3/extending/windows.html
    if _is_windows(repository_ctx):
        python_include = _normalize_path(python_include)
        python_import_lib_name = _get_python_import_lib_name(
            repository_ctx,
            python_bin,
            bin_path_key,
        )
        python_import_lib_src = python_include.rsplit(
            "/",
            1,
        )[0] + "/libs/" + python_import_lib_name
        python_import_lib_genrule = _symlink_genrule_for_dir(
            repository_ctx,
            None,
            "",
            "{}_import_lib".format(variety_name),
            [python_import_lib_src],
            [python_import_lib_name],
        )
    _tpl(
        repository_ctx,
        "variety",
        {
            "%{PYTHON_INCLUDE_GENRULE}": python_include_rule,
            "%{PYTHON_IMPORT_LIB_GENRULE}": python_import_lib_genrule,
            "%{VARIETY_NAME}": variety_name,
        },
        out = "{}/BUILD".format(variety_name),
    )

def _python_autoconf_impl(repository_ctx):
    """Implementation of the python_autoconf repository rule."""
    _create_single_version_package(
        repository_ctx,
        "_python3",
        _PYTHON3_BIN_PATH,
        "python3" if not _is_windows(repository_ctx) else "python.exe",
        _PYTHON3_LIB_PATH,
        False
    )
    _tpl(repository_ctx, "BUILD")

python_configure = repository_rule(
    implementation = _python_autoconf_impl,
    environ = [
        _BAZEL_SH,
        _PYTHON3_BIN_PATH,
        _PYTHON3_LIB_PATH,
    ],
    attrs = {
        "_build_tpl": attr.label(
            default = Label("//third_party/py:BUILD.tpl"),
            allow_single_file = True,
        ),
        "_variety_tpl": attr.label(
            default = Label("//third_party/py:variety.tpl"),
            allow_single_file = True,
        ),
    },
)
"""Detects and configures the local Python.

It expects the system have a working Python 3 installation.

Add the following to your WORKSPACE FILE:

```python
python_configure(name = "local_config_python")
```

Args:
  name: A unique name for this workspace rule.
"""
