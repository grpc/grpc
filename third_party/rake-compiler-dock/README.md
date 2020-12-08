rake-compiler-dock for gRPC
===================================

This has customized docker images for Ruby based on
[rake-compiler-dock](https://github.com/rake-compiler/rake-compiler-dock).
gRPC needs four docker images to build Ruby artifacts;

- rake_x64-linux: Linux / 32bit
- rake_x86_64-linux: Linux / 64bit
- rake_x86-mingw32: Windows / 32bit
- rake_x64-mingw32: Windows / 64bit

### Customization

#### Linux

The linux docker images of `rake-compiler-dock` are based on Ubuntu, which is enough for
most cases but it becomes hard to keep it compataible for some conservative Linux distrubitions
such as CentOS 6 because Ubuntu uses more modern libraries than them.
As a result, generated artifacts sometimes cannot run on CentOS 6 due to missing dependencies. 
This can be easily addressed by using CentOS 6 based docker images such as 
[dockcross manylinux2010](https://github.com/dockcross/dockcross), which was invented
to handle the very same problem of Python. By using the same solution, 
Ruby can have the simple way of building more portable artifacts.
This idea is summarized in
[rake-compiler-dock#33](https://github.com/rake-compiler/rake-compiler-dock/issues/33).

These two new docker images; `rake_x64-linux` and `rake_x86_64-linux` are based on 
[Dockerfile.mri.erb](https://github.com/rake-compiler/rake-compiler-dock/blob/master/Dockerfile.mri.erb)
with following customizations;

- Changing the base image from `ubuntu:16.04` to `dockcross/manylinux2010`
- Removing rvm account due to the complexity of having the same thing on `manylinux2010`
  (mainly due to the limit of `gosu` handling groups)
- Removing cross compiling setup for x86 because `manylinux2010-x86` already did it. 
  (like cross compilers for x86 and mk_i686)
- Removing glibc hack because `manylinux2010` doesn't needit.
- Adding `patchelf_gem.sh` to trim the unnecessary dependency of `libcrypt.so.2`. 
  Without this, artifacts for Ruby 2.3 to 2.5 happens to have a `libcrypt.so.2` link although
  it doesn't have any external symbols from it.

#### Windows

Windows docker images are almost identical to `rake-compiler-dock` but with some exception;

- Renaming `gettimeofday` to `rb_gettimeofday` in `win32.h`
