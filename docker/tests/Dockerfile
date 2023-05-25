FROM ubuntu:22.04 as builder

ARG COMPILER=gcc
ARG NANO_SANITIZER
ARG NANO_COVERAGE

# Install build dependencies
COPY ./ci/prepare/linux /tmp/prepare
RUN /tmp/prepare/prepare.sh

COPY ./ /root/src
WORKDIR /root/src

# Build tests
RUN ci/build-tests.sh

COPY docker/tests/all_tests.sh /usr/bin/all_tests.sh

RUN ldconfig

WORKDIR /root/src/build
USER root

ENV PATH="${PATH}:/usr/bin"
ENV PATH="${PATH}:/root/src/build"
CMD ["all_tests.sh"]

ARG REPOSITORY=nanocurrency/nano-node
LABEL org.opencontainers.image.source https://github.com/$REPOSITORY