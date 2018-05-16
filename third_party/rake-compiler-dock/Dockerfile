FROM larskanis/rake-compiler-dock:0.6.2

RUN find / -name rbconfig.rb | while read f ; do sed -i 's/0x0501/0x0600/' $f ; done
RUN find / -name win32.h | while read f ; do sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done
RUN sed -i 's/defined.__MINGW64__.$/1/' /usr/local/rake-compiler/ruby/i686-w64-mingw32/ruby-2.0.0-p645/include/ruby-2.0.0/ruby/win32.h
RUN find / -name libwinpthread.dll.a | xargs rm
RUN find / -name libwinpthread-1.dll | xargs rm
RUN find / -name *msvcrt-ruby*.dll.a | while read f ; do n=`echo $f | sed s/.dll//` ; mv $f $n ; done
RUN apt-get install -y g++-multilib

CMD bash
