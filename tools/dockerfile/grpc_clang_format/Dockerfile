FROM ubuntu:vivid
RUN apt-get update
RUN apt-get -y install clang-format-3.6
ADD clang_format_all_the_things.sh /
CMD ["echo 'Run with tools/distrib/clang_format_code.sh'"]

