FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
  cmake \
  g++ \
  gcc \
  git \
  make \
  python \
  && apt-get clean

RUN git clone -b release_36 http://llvm.org/git/llvm.git
RUN git clone -b release_36 http://llvm.org/git/clang.git
RUN git clone -b release_36 http://llvm.org/git/compiler-rt.git
RUN git clone -b release_36 http://llvm.org/git/clang-tools-extra.git
RUN git clone -b release_36 http://llvm.org/git/libcxx.git
RUN git clone -b release_36 http://llvm.org/git/libcxxabi.git

RUN mv clang llvm/tools
RUN mv compiler-rt llvm/projects
RUN mv clang-tools-extra llvm/tools/clang/tools
RUN mv libcxx llvm/projects
RUN mv libcxxabi llvm/projects

RUN mkdir llvm-build
RUN cd llvm-build && cmake ../llvm
RUN make -C llvm-build && make -C llvm-build install && rm -rf llvm-build

CMD ["bash"]
