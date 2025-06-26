import abc
import glob
import os
import tarfile
import tempfile


class FileSystemError(Exception):
    pass


class FileSystem(abc.ABC):
    """Interface for file systems."""

    @abc.abstractmethod
    def isfile(self, path):
        """Is this a file?"""
        pass

    @abc.abstractmethod
    def isdir(self, path):
        """Is this a directory?"""
        pass

    @abc.abstractmethod
    def read(self, path):
        """Read a file."""
        pass

    @abc.abstractmethod
    def refer_to(self, path):
        """Get a fully qualified path for the given path."""
        pass

    def relative_path(self, path):
        """Return the relative path to `path`.

        If this filesystem has a root directory, and path is within that
        directory tree, return the relative path; otherwise return None.
        """
        return None


class StoredFileSystem(FileSystem):
    """File system based on a file list."""

    def __init__(self, files):
        self.files = files
        self.dirs = {os.path.dirname(f) for f in files}

    def isfile(self, path):
        return path in self.files

    def isdir(self, path):
        return path in self.dirs

    def read(self, path):
        return self.files[path]

    def refer_to(self, path):
        return path


class OSFileSystem(FileSystem):
    """File system that uses an OS file system underneath."""

    def __init__(self, root):
        assert root is not None
        self.root = root
        _, tmp_path = tempfile.mkstemp()
        self._is_case_insensitive = os.path.exists(tmp_path.upper())

    def _join(self, path):
        return os.path.join(self.root, path)

    def _matches_path(self, path):
        if self._is_case_insensitive:
            return path in glob.glob(path+'*')
        return True

    def isfile(self, path):
        assert path is not None
        fullpath = self._join(path)
        return os.path.isfile(fullpath) and self._matches_path(fullpath)

    def isdir(self, path):
        assert path is not None
        fullpath = self._join(path)
        return os.path.isdir(fullpath) and self._matches_path(fullpath)

    def read(self, path):
        with open(self._join(path), 'r') as fi:
            return fi.read()

    def refer_to(self, path):
        return self._join(path)

    def relative_path(self, path):
        if path.startswith(self.root):
            return path[len(self.root) + 1:]
        return None


class RemappingFileSystem(FileSystem, abc.ABC):
    """File system wrapper that transforms a path before looking it up."""

    def __init__(self, underlying):
        self.underlying = underlying

    @abc.abstractmethod
    def map_path(self, path):
        pass

    def isfile(self, path):
        return self.underlying.isfile(self.map_path(path))

    def isdir(self, path):
        return self.underlying.isdir(self.map_path(path))

    def read(self, path):
        return self.underlying.read(self.map_path(path))

    def refer_to(self, path):
        return self.underlying.refer_to(self.map_path(path))


class ExtensionRemappingFileSystem(RemappingFileSystem):
    """File system that remaps .py file extensions."""

    def __init__(self, underlying, extension):
        super(ExtensionRemappingFileSystem, self).__init__(underlying)
        self.extension = extension

    def map_path(self, path):
        p, ext = os.path.splitext(path)
        if ext == '.py':
            return p + '.' + self.extension
        return path


class PYIFileSystem(ExtensionRemappingFileSystem):
    """File system that remaps .py file extensions to pyi."""

    def __init__(self, underlying):
        super(PYIFileSystem, self).__init__(underlying, 'pyi')


class TarFileSystem(object):
    """Filesystem that serves files out of a .tar."""

    def __init__(self, tar):
        self.tar = tar
        self.files = list(t.name for t in tar.getmembers() if t.isfile())
        self.directories = list(t.name for t in tar.getmembers() if t.isdir())
        self.top_level = {f.split(os.path.sep)[0] for f in self.files}

    def isfile(self, path):
        return any(os.path.join(top, path) in self.files
                   for top in self.top_level)

    def isdir(self, path):
        return any(os.path.join(top, path) in self.files
                   for top in self.top_level)

    def read(self, path):
        return self.tar.extractfile(path).read()

    def refer_to(self, path):
        return 'tar:' + path

    @staticmethod
    def read_tarfile(archive_filename):
        tar = tarfile.open(archive_filename)
        return TarFileSystem(tar)


class Path(object):
    def __init__(self, paths=None):
        self.paths = paths if paths else []

    def add_path(self, path, kind='os'):
        if kind == 'os':
            path = OSFileSystem(path)
        elif kind == 'pyi':
            path = PYIFileSystem(OSFileSystem(path))
        else:
            raise FileSystemError('Unrecognized filesystem type: ', kind)
        self.paths.append(path)

    def add_fs(self, fs):
        assert isinstance(fs, FileSystem), 'Unrecognised filesystem: %r' % fs
        self.paths.append(fs)
