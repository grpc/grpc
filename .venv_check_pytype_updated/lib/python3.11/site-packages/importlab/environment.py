import os

from . import utils
from . import fs


class Environment(object):
    def __init__(self, path, python_version):
        self.path = path.paths
        self.python_version = python_version


def path_from_pythonpath(pythonpath):
    """Create an fs.Path object from a pythonpath string."""
    path = fs.Path()
    for p in pythonpath.split(os.pathsep):
        path.add_path(utils.expand_path(p), 'os')
    return path


def create_from_args(args):
    python_version_string = args.python_version
    python_version = utils.split_version(python_version_string)
    path = path_from_pythonpath(args.pythonpath)
    return Environment(path, python_version)
