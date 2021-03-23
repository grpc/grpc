FROM larskanis/rake-compiler-dock-mri-x86-mingw32:1.1.0

RUN find / -name win32.h | while read f ; do sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done
