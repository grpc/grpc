FROM larskanis/rake-compiler-dock-mri:0.7.2

RUN find / -name rbconfig.rb | while read f ; do sed -i 's/0x0501/0x0600/' $f ; done
RUN find / -name win32.h | while read f ; do sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done
RUN find / -name libwinpthread.dll.a | xargs rm
RUN find / -name libwinpthread-1.dll | xargs rm
RUN find / -name *msvcrt-ruby*.dll.a | while read f ; do n=`echo $f | sed s/.dll//` ; mv $f $n ; done
RUN apt-get install -y g++-multilib

# Make the system to have GLIBC 2.12 instead of 2.23 so that
# generated ruby package can run on CentOS 6 with GLIBC 2.12
RUN sed -i 's/__GLIBC_MINOR__\t23/__GLIBC_MINOR__\t12/' /usr/include/features.h

CMD bash
