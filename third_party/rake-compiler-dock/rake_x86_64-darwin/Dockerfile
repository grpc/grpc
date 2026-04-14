FROM ghcr.io/rake-compiler/rake-compiler-dock-image:1.11.1-mri-x86_64-darwin

#=================
# Install ccache
# This should put ccache under /usr/local/bin
RUN (curl -L -f https://github.com/ccache/ccache/releases/download/v4.13.3/ccache-4.13.3-linux-x86_64-glibc.tar.xz | tar -xJ -C /root) \
    && cd /root/ccache-4.13.3-linux-x86_64-glibc \
    && ./install.sh
