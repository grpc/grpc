# Patches to grpc repo tags for the backwards compatibility interop tests

This directory has patch files that can be applied to different tags
of the grpc git repo in order to run the interop tests for a specific
language based on that tag.

For example, because the ruby interop tests do not run on the v1.0.1 tag out
of the box, but we still want to test compatibility of the 1.0.1 ruby release
with other versions, we can apply a patch to the v1.0.1 tag from this directory
that makes the necessary changes that are needed to run the ruby interop tests
from that tag. We can then use that patch to build the docker image for the
ruby v1.0.1 interop tests.

## How to add a new patch to this directory

Patch files in this directory are meant to be applied to a git tag
with a `git apply` command.

1. Under the `patches` directory, create a new subdirectory
titled `<language>_<git_tag>` for the git tag being modified.

2. `git checkout <git_tag>`

3. Make necessary modifications to the git repo at that tag.

4. 

```
git diff > ~/git_repo.patch
git checkout <current working branch>
cp ~/git_repo.patch tools/interop_matrix/patches/<language>_<git_tag>/
```

5. Edit the `LANGUAGE_RELEASE_MATRIX` in `client_matrix.py` for your language/tag
and add a `'patch': [<files>,....]` entry to it's `dictionary`.

After doing this, the interop image creation script can apply that patch to the
tag with `git apply` before uploading to the test image repo.
