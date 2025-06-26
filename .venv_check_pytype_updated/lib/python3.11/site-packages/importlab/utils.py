"""Utility functions."""

from contextlib import contextmanager
import errno
import logging
import os
import shutil
import subprocess
import tempfile
import textwrap


def setup_logging(name, log_file, level=logging.INFO):
    formatter = logging.Formatter(
        fmt='%(asctime)s %(levelname)s %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S')
    handler = logging.FileHandler(log_file)
    handler.setFormatter(formatter)
    logger = logging.getLogger(name)
    logger.setLevel(level)
    logger.addHandler(handler)
    return logger


@contextmanager
def cd(path):
    old = os.getcwd()
    os.chdir(os.path.expanduser(path))
    try:
        yield
    finally:
        os.chdir(old)


def expand_path(path, cwd=None):
    def expand(p):
        return os.path.realpath(os.path.expanduser(p))

    if cwd:
        with cd(cwd):
            return expand(path)
    else:
        return expand(path)


def expand_paths(paths, cwd=None):
    return [expand_path(x, cwd) for x in paths]


def collect_files(path, extension):
    """Collect all the files with extension in a directory tree."""

    # We should only call this on an actual directory; callers should do the
    # validation.
    assert os.path.isdir(path)
    out = []
    # glob would be faster (see PEP471) but python glob doesn't do **/*
    for root, _, files in os.walk(path):
        out += [os.path.join(root, f) for f in files if f.endswith(extension)]
    return out


def expand_source_files(filenames, cwd=None):
    """Expand a list of filenames passed in as sources.

    This is a helper function for handling command line arguments that specify a
    list of source files and directories.

    Any directories in filenames will be scanned recursively for .py files.
    Any files that do not end with ".py" will be dropped.

    Args:
        filenames: A list of filenames to process.
        cwd: An optional working directory to expand relative paths
    Returns:
        A list of sorted full paths to .py files
    """
    out = []
    for f in expand_paths(filenames, cwd):
        if os.path.isdir(f):
            # If we have a directory, collect all the .py files within it.
            out += collect_files(f, ".py")
        else:
            if f.endswith(".py"):
                out.append(f)
    return sorted(set(out))


def split_version(version):
    return tuple([int(v) for v in version.split('.')])


def makedirs(path):
    """Create a nested directory; don't fail if any of it already exists."""
    try:
        os.makedirs(path)
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise


class Tempdir(object):
    """Context handler for creating temporary directories."""

    def __enter__(self):
        self.setup()
        return self

    def setup(self):
        self.path = tempfile.mkdtemp()

    def create_directory(self, filename):
        """Create a subdirectory in the temporary directory."""
        path = os.path.join(self.path, filename)
        makedirs(path)
        return path

    def create_file(self, filename, indented_data=None):
        """Create a file in the temporary directory.

        Dedents the contents.
        """
        filedir, filename = os.path.split(filename)
        if filedir:
            self.create_directory(filedir)
        path = os.path.join(self.path, filedir, filename)
        data = indented_data
        if isinstance(data, bytes) and not isinstance(data, str):
            # This is binary data rather than text.
            mode = 'wb'
        else:
            mode = 'w'
            if data:
                data = textwrap.dedent(data)
        with open(path, mode) as fi:
            if data:
                fi.write(data)
        return path

    def delete_file(self, filename):
        os.unlink(os.path.join(self.path, filename))

    def teardown(self):
        shutil.rmtree(path=self.path)

    def __exit__(self, error_type, value, tb):
        self.teardown()
        return False  # reraise any exceptions

    def __getitem__(self, filename):
        """Get the full path for an entry in this directory."""
        return os.path.join(self.path, filename)


def strip_suffix(string, suffix):
    """Remove a suffix from a string if it exists."""
    if string.endswith(suffix):
        return string[:-(len(suffix))]
    return string


def run_py_file(version, path, *args):
    exe = 'python%d.%d' % version
    args = [exe, path] + list(args)
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()
    return p.returncode, stdout, stderr
