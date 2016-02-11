FROM ubuntu:14.04

RUN apt-get -y update && \
    apt-get install -y curl git-core mingw-w64 xz-utils build-essential gcc-multilib wget unzip

RUN mkdir -p /opt/mingw && \
    curl -SL http://downloads.sourceforge.net/mingw-w64/i686-w64-mingw32-gcc-4.7.2-release-linux64_rubenvb.tar.xz | \
    tar -xJC /opt/mingw && \
    echo "export PATH=\$PATH:/opt/mingw/mingw32/bin" >> /etc/rubybashrc

RUN mkdir -p /opt/mingw && \
    curl -SL http://downloads.sourceforge.net/mingw-w64/x86_64-w64-mingw32-gcc-4.7.2-release-linux64_rubenvb.tar.xz | \
    tar -xJC /opt/mingw && \
    echo "export PATH=\$PATH:/opt/mingw/mingw64/bin" >> /etc/rubybashrc

# Add "rvm" as system group, to avoid conflicts with host GIDs typically starting with 1000
RUN groupadd -r rvm && useradd -r -g rvm -G sudo -p "" --create-home rvm && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/rubybashrc
USER rvm

# install rvm, RVM 1.26.0+ has signed releases, source rvm for usage outside of package scripts
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys D39DC0E3 && \
    (curl -L http://get.rvm.io | sudo bash -s stable) && \
    bash -c " \
        source /etc/rubybashrc && \
        rvmsudo rvm cleanup all "

# Regenerate the following using build.sh if the build folder changes.
RUN echo \
H4sIAJSZulYAA+07a1fbSLL5evUragxnbAdLtvwEApnJAMmwN68DJHPvGbLattSyNZYlbXfLhpuw\
v/1WtSS/gBB2EnZn1n3AbvWjqrqqurqq1O6nQejVH33T0sDS63Xo2+51GovfRXlkN7uddrPd67a7\
jxq23Wy2HkHn25KVlVQqJgAeJcEFDz8z7q7+P2jpa/nLYOBPLfcb4SABd7vt2+Rvd5ur8m92Gp1H\
0PhG9CyV/3D51x8b8BjOhoGERMQDwcYwZJEXcgmnxy+OX58BPoEfiykTnoRAgYqxKVZDLmiGy6W0\
CMSxAoQRRIpHHvdoVJ+DSCNgEt4eH4JNo2jgYeyOcC6xXckCBKKbBmoIJS/rxYmlpZnwJoKXQZRe\
1ABRg8d9loYKUHEjFuYkC6IzmwLYHguEyqLLfFBG5hnSzXEYhwMlQvMA2IgVKyX69TQPEgSW46Bp\
dcPYCCI3TD0Oexk4a/h0se1S1tVlwuX15ikLFLUayBtIAg/2ofHEMCZx4BkZ3Q5CpM4KfRD0dFw1\
PhoAgV/BCVWsAYyCMKSnWk5t9YlxlcEcsyDSU5kYuDVwh6jPj7E++fVDlaBIJVJXA2auCuIIIj51\
suoTQkJIFVOpRMpMG0kDqD+GU64gTTSzMwAp8gylKhPuBv6l7kBAkAGyiEewANmSzCmksg9L6ySk\
WOPjRF1KxFL5fnnamMlR9ck1aH7IBjJnHiysppLxowYLYGpQIf4+rjaqenTGdhT7qKIh54z9qDlL\
Aqp8n7FA9wIIjsuN4Jej/zk+Oz17dvbutLLQf8VDybO5M8bxC+5Okopmu/0BiclrOUDUJxGLSomG\
lZaRZCA0XJToQ+//zP4nTLlDLr+RH3D/8x8fmuvz/yHKsvxF2r80bWvb6plJq9f+Svpwf/m3e73e\
Wv4PUT4v/yhOLN33u3Dc4f817Ia9Iv9ut9Fe+38PUbzA98E0B+jYsbobR34woIO+P6+jj+HxC+h0\
7E6P+ZbV7bYavM/Axk3d6Tz4gbUuX7XcsP+bVstqfE1X4J+w/82evbb/D1FulX8UOxJDOT8I+e89\
Au6y/+1ua0X+vW6jubb/D1G+2P7zPm95265lsVar2++1CvtvmubiTGNra2tp9o8/YlC5vdNu1Xqw\
lVW6gI1eDLsUBjHX8SPHdVD/3JHjp5ELpc2Xx6+PXr8pQalQQaziQHeiB8wUs2RgJAeKSwWli82b\
BmBkdnHJ5RMKV6MMocsUPH1KBGKAj/E67O05zw6O3jw3zA1sCSIOPz97f+ScHr0+fH788ghsA/IB\
YIAf/LlOvBv9vx2r9S+2/63G2v4/SLlV/g9n/+2m3b5u/9f53wcpX2z/t3e2e60G2n/X591m5372\
v93d6Wj7n1ewkVK1ZPvJ4AcR+OPY0znOZKATcm39SXk7ytqBTNg0moC8lC4LQ3CcouYORRwrGHAD\
/gt0kVxJnKZ4GHqBwEc+om/fHRIKN5ZDkEE0BMXwI4wHTRBxGnnniwBSBIBfA/zyGB/HET6F3FVO\
yMSAO75HeUv850hTGmWVcwOWy3ikgjEH+hiMYTCmiiPADWM86AZc6d78O/Y9dglJjCtK6PPcMJeB\
JYIzoinbkSCHqfLiaaTzoKFCHXZH4IVOoLhgijvJ0BPG1o0gvmTm/HSWDgppwsT+3zaxzt1hvHQS\
b+YiLMEnoAFKOHL4N+OOU30+q7Q5w1D6cx2sf5DyGfv/NVI/uvwT+R+70V3b/4co6/zPf3ZZ2f9s\
xE03Hid4xgizgYag8xUCgS/1/7sd9A8aPdr/jfY6//8g5W7503v5OFUmv1DSInNxbxx32f9es7ls\
/5t2q7fO/zxIWbL/ismRrPeDqO6KWEqT3AGLdALPg1v78vOhy1x32+7i+cBYk7V36Hzottt5fHD7\
7CxeuL1fxw+tTm0bo4dWETuQC1za+Pju9OjE+fnNq6OrutZjWd/4+Or49YtfsPH07AqfTt799L/O\
wYHz/ujk9PjN66v6K4SZZ4aewq85jMxzL5mmhrKfT/vp3fHLw6tSLestmyaPWD/kphwywb3yvN0L\
pO4IItSlMDS92MVeM+8t9o8a3dTohuXMT88baZvtlwn0B7q4ABvwPBYuh18QOMYNTR03nSB3wLa2\
a/ixU9xTkXQ7I1D3PJDv3v/0yFRGXD/VNzruaQZu2P9YaT0qvL9Oq7ts/5vNnt37N9v/2cWkh6Do\
QctzEY+hbfvbrXa7veO77Vaz1W/6Ozsdvt2zO4x1tttNb8f3dlqtNrzCSPiUJ2D3oNHY1X/QRBEa\
BGYXXjIh4b9ZFEjYw0hZ/jgQPPg/Ez8i6cXCtzz+1DjEKHMXTpmqQasBf2ERQrC70Njepb8ebKFG\
NIzTtP8bBty78OvbZ2cHP3+AgyGLBlxfO/LJIo0xFiVM4/yqWaamAe7D7CZZoaqGcYZTMPadBHEq\
V+dOYzHiHuB2GsbxKIgGEEcID7EYJcH/ngboC5dFP3OGyyWgnINl6Oty2Y05xDdGtC6S4LJU8gKv\
vgknIhx+CTkgCQWgGo5SGGNzGZWVQTQg+UJwhYODCJim0gMeTQIRR2MeKcs4VlkO22dBqLd6GGM0\
P+BjfBgyuvzFgYUyhoQJBbGf39xikceEB2HQF0xc1rCCNvU3iXKke30CtTpfDtKnjQjiD6RMkVoa\
CkOlErlbr+MBMUz7FnJ52UismAzaJfVsfn07Y/0KxxFVFE/plhqKU98TJM7P2A5zto9Hop+zvGZM\
h4E7pMnqMglczdbFhaLVJNbqk6Gmlxbo+4hTuu43UxoPkpAptKBjY8JCWqPmmqtSDTDi3OMe8QP5\
OWWXOQxiNE5lPgq0EG+dkkRJDWSccZ8w5DxGutRQQzYoS4THYiw80qx8gS/4WCdVKQ1UKxoj1KEJ\
z2445oACLlFP9F1FY6b1CypBpNFttwlHHX8bcobKF8WK1+YUaeMOHnLTVTFShsxiMMAZESmOXq8s\
tg8CwFOthqc5IL24Umwf6DuAHm5YL0NPhEsLTmNKEDGUFe6+cQLJaACm8EsWnfYG0a/Voo5S4ZEM\
ULZ4vFuiD5+g1YGtpWLeVAyws0M+VxM86NqomZILut0nK1vVGjRtPPlCnjWYVcNYcmVup6F/e1/u\
yjRaDebvoCvj79jtHXvFlbl9dubK3N6vXZnujnZl9Bc2/IVNGMxGgkSvAEXW1xbL52gTUB9nGbwN\
vXWY5y3pvHRFkKj5KHfswd4esL508iGW4KFWMId00/HRWFeoG2WnW6oGzPONGzMzRdyaBmGYkyO1\
0vkw5XrbaEcNsp2P6j0HQK/kLByvN7IMFGpMfiH2hwo6XAXSq/oCnlJVW7d5ynJDK9z9KNi6NwU5\
jgL7QrI0TrRiWUkqh5XHGpOTUevkXRVtS6rV+SweechIknKr26m1dmBLfzfuKecNyF9DZitj+par\
dpC185etujBk3xWzsjlObokdPb6CI5xiZA3IqXYmXCyIe4N2GKXfM0bUlmRPZnQ8GvtUR2tQuM5o\
FiLKFSNZBctXwHzJzJzswpWfy+Sah35dXLXV7Pht5VZ1q60m678IQr6k0oclBsZJdhtaL4X2ln6a\
xzF4gLjZms3b1rxIml520eDQ2A9kbT+pTwvcLnAW4z6Ll8Qxn0sywJOTz4RFSkW2E7cLbi89E5WE\
1FQWs65TXIjiM9TOePicjrs44VFFWREb4wlVnparepj/aVGUvjUVuFsXfIAcTfk8Oo9Kiy80iqE0\
wBH92zX9+hRNDr0LqawaCT8QUq1u6IXqfIdqAZA6ZIy/SdgF1musK7RIs44eltk244ebXLNimkA8\
4DUblyxusWLYgxIGhSX4/vu8srcPOpzNQ+BFft8gGLGlbhQNkhPTbzsU3aPX3JtL4/Py3boJxJ0y\
uD7Lkmn/u0r9r5WZclQLvauUq5t1xHp+bpM+nJ83y0sQ7sWfxTVfw5219pHyEVTPH1eK2HuGH3Fn\
SyhX74aU/wLg9wLS/uS5VU0EBh0VKDL3Wg3PK1K4dMYEF+fVGfAkVTLj0pKISbBT9IFWW7N9kyNe\
7VQijVDleQWrPAyXqOWLsBYedHVuQu9lkm41ojOTtLqR7m1/7jIqN9ihgjlLS1+sar+gvaMTWe3t\
bs2+r1tAjCq5LEIfP4v9ZIoRkWYD+qUotlazji5SHE5KFPZEefSS7xYMARnFL3G0BPCUZ1Hj7k2B\
nvpNusMUA7qVGI9PzH58UYR5nTm8ecSs6SjPGXU9lp5bU1TpkKFL80ZH/IvMFXwcT8iniaSC3ZP+\
gZ66qGArI2gfv3357Oz5m5NXdwy7YbvfMOrw6PTg5PjtGY4kK5I5Wd4PldVO3EgmGE2rYzUs2kHx\
VFq28Qd4OZb//jP1Ytxf3wjHXfn/ZqN77fc/rca/Wf7vT5r/PyySxzNbNXFGnCewtQ8lsglOIuKL\
y8w85HV/1npycKhT7c7Szltpzneabj1+9ezFEaxk5dc3P/51Jd//SgSJMxUsSb5BjvuO97+2bfdW\
7382Ouv3fw9SNr6rp1LoN290mY18HMNAv4AUInMu0C2hmCZEpyz0dGJbKrpIppOJ7wNByVN0ByB7\
LZaPQhAY9+vMNtPX6zA2RWeknw7mnsZ0OrUms/lWLAZ1Fbgjrurb7W7LMGY+g+LjhEgpz5voMVVB\
KMuGoZUXYwp0BrWn12eSk5tX2WxUryxcWckwdOISxzw7efEenVxyND6i8wf+/j/qfzXrcGWNWaI9\
QiYGFLpSWnMfznLMFv3SuawRlbNOyw1jyY3MuXxHlFgYr+Hkmu4ljxQ7f8V6jVo/GOQEGnnqqCAk\
u1S4TAhs5VlWJOjjJ1WLP1EUiODgyjDSCHskXYNEnlRAE1SDxwVY8r35Bfp9drOnEbqxxxHZ5g8W\
NWe/NM6ZYXGG7qN2lYnGWAR63fPliLFuXF1isbpsSrYsjZNwrQ35H61k9h9DOPatvL+77/+026v3\
/5qtxtr/e5CC9p9sP9rMoWEM0FQn9HrBHEBp88XxIV3QfXHy5t3bkjHWt6jNBOpoAerDeMyNVHKx\
PNpMsfZO116AmIxrFFjQpBK29OdTwRzTwNOjE7TNdIFjf7NCJMDe3h6U9C3jf2TdVSOMwJSgTyl9\
Di1HowiG5pfq1lIzngoac1qgwRhZcmVq5NlN7Z+enf7sHL1+v1/nytU3X4kC4ebdGM+VNn9cu6br\
si7rsi7r8qcs/w8AInVFAFAAAA==\
| base64 -d | tar xzC /tmp

# Import patch files for ruby and gems
RUN cp -r /tmp/build/patches /home/rvm/patches/
ENV BASH_ENV /etc/rubybashrc

# install rubies and fix permissions on
RUN bash -c " \
    export CFLAGS='-s -O3 -fno-fast-math -fPIC' && \
    for v in 2.3.0 ; do \
        rvm install \$v --patch \$(echo ~/patches/ruby-\$v/* | tr ' ' ','); \
    done && \
    rvm cleanup all && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Install rake-compiler and typical gems in all Rubies
# do not generate documentation for gems
RUN echo "gem: --no-ri --no-rdoc" >> ~/.gemrc && \
    bash -c " \
        rvm all do gem install bundler rake-compiler hoe mini_portile rubygems-tasks && \
        rvm 2.3.0 do gem install mini_portile2 && \
        find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

# Install rake-compiler's cross rubies in global dir instead of /root
RUN sudo mkdir -p /usr/local/rake-compiler && \
    sudo chown rvm.rvm /usr/local/rake-compiler && \
    ln -s /usr/local/rake-compiler ~/.rake-compiler

# Patch rake-compiler to avoid build of ruby extensions
RUN cd /usr/local/rvm/gems/ruby-2.3.0/gems/rake-compiler-0.9.5 && git apply /home/rvm/patches/rake-compiler-0.9.5/*.diff ; \
    true

RUN bash -c "rvm use 2.3.0 --default && \
    export MAKE=\"make -j`nproc`\" CFLAGS='-s -O1 -fno-omit-frame-pointer -fno-fast-math' && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=x86_64-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=i686-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=x86_64-w64-mingw32 && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=x86_64-linux-gnu && \
    rm -rf ~/.rake-compiler/tmp/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

RUN bash -c "rvm use 2.3.0 --default && \
    export MAKE=\"make -j`nproc`\" CFLAGS='-m32 -s -O1 -fno-omit-frame-pointer -fno-fast-math' LDFLAGS='-m32' && \
    rake-compiler cross-ruby VERSION=2.3.0 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.2.2 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.1.5 HOST=i686-linux-gnu && \
    rake-compiler cross-ruby VERSION=2.0.0-p645 HOST=i686-linux-gnu && \
    rm -rf ~/.rake-compiler/tmp/builds ~/.rake-compiler/sources && \
    find /usr/local/rvm -type d -print0 | sudo xargs -0 chmod g+sw "

RUN bash -c " \
    rvm alias create 2.3 2.3.0 "

USER root

# Fix paths in rake-compiler/config.yml and add rvm and mingw-tools to the global bashrc
RUN sed -i -- "s:/root/.rake-compiler:/usr/local/rake-compiler:g" /usr/local/rake-compiler/config.yml && \
    echo "source /etc/profile.d/rvm.sh" >> /etc/bash.bashrc && \
    echo "export PATH=\$PATH:/opt/mingw/mingw32/bin" >> /etc/bash.bashrc && \
    echo "export PATH=\$PATH:/opt/mingw/mingw64/bin" >> /etc/bash.bashrc

# Install wrappers for strip commands as a workaround for "Protocol error" in boot2docker.
RUN cp /tmp/build/strip_wrapper /root/
RUN mv /opt/mingw/mingw32/bin/i686-w64-mingw32-strip /opt/mingw/mingw32/bin/i686-w64-mingw32-strip.bin && \
    mv /opt/mingw/mingw64/bin/x86_64-w64-mingw32-strip /opt/mingw/mingw64/bin/x86_64-w64-mingw32-strip.bin && \
    mv /usr/bin/i686-w64-mingw32-strip /usr/bin/i686-w64-mingw32-strip.bin && \
    mv /usr/bin/x86_64-w64-mingw32-strip /usr/bin/x86_64-w64-mingw32-strip.bin && \
    ln /root/strip_wrapper /opt/mingw/mingw32/bin/i686-w64-mingw32-strip && \
    ln /root/strip_wrapper /opt/mingw/mingw64/bin/x86_64-w64-mingw32-strip && \
    ln /root/strip_wrapper /usr/bin/i686-w64-mingw32-strip && \
    ln /root/strip_wrapper /usr/bin/x86_64-w64-mingw32-strip

RUN find / -name rbconfig.rb | while read f ; do sed -i 's/0x0501/0x0600/' $f ; done
RUN find / -name win32.h | while read f ; do sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done
RUN sed -i 's/defined.__MINGW64__.$/1/' /usr/local/rake-compiler/ruby/i686-w64-mingw32/ruby-2.0.0-p645/include/ruby-2.0.0/ruby/win32.h
RUN find / -name libwinpthread.dll.a | xargs rm
RUN find / -name libwinpthread-1.dll | xargs rm
RUN find / -name *msvcrt-ruby*.dll.a | while read f ; do n=`echo $f | sed s/.dll//` ; mv $f $n ; done
RUN find /usr/local/rake-compiler/ruby -name libruby.so | xargs rm
RUN find /usr/local/rake-compiler/ruby -name libruby-static.a | while read f ; do ar t $f | xargs ar d $f ; done
RUN find /usr/local/rake-compiler/ruby -name libruby-static.a | while read f ; do mv $f `echo $f | sed s/-static//` ; done

# Install SIGINT forwarder
RUN cp /tmp/build/sigfw.c /root/
RUN gcc $HOME/sigfw.c -o /usr/local/bin/sigfw

# Install user mapper
RUN cp /tmp/build/runas /usr/local/bin/

# Install sudoers configuration
RUN cp /tmp/build/sudoers /etc/sudoers.d/rake-compiler-dock

ENV RUBY_CC_VERSION 2.3.0:2.2.2:2.1.5:2.0.0

CMD bash
