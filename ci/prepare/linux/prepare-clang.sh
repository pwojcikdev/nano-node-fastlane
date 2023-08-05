#!/bin/bash
set -euo pipefail

apt-get update -qq && apt-get install -yqq \
clang-15 \
lldb

update-alternatives --install /usr/bin/cc cc /usr/bin/clang-15 100
update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-15 100

# Workaround to get a path that can be easily passed into cmake for BOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE
# See https://www.boost.org/doc/libs/1_70_0/doc/html/stacktrace/configuration_and_build.html#stacktrace.configuration_and_build.f3
backtrace_file=$(find /usr/lib/gcc/ -name 'backtrace.h' | head -n 1) && test -f $backtrace_file && ln -s $backtrace_file /tmp/backtrace.h