# set up a working RCD build environment
export RAKE_EXTENSION_TASK_NO_NATIVE=true
if ! test -e "$HOME"/.rake-compiler ; then
  ln -s /usr/local/rake-compiler "$HOME"/.rake-compiler
fi
mkdir -p "$HOME"/.gem
