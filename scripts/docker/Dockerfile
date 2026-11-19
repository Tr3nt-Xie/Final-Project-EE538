# Development / CI image: everything needed to build and test AegisProxy on Linux.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates \
      clang-18 clang-format-18 clang-tidy-18 \
      libgtest-dev python3 curl \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/clang-format-18 /usr/local/bin/clang-format \
    && ln -s /usr/bin/clang-tidy-18 /usr/local/bin/clang-tidy
WORKDIR /work
