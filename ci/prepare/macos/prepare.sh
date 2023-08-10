#!/bin/bash
set -euox pipefail

brew update
brew install coreutils

# Qt
brew install qt@5
brew link qt@5

# Workaround: https://github.com/Homebrew/homebrew-core/issues/8392
echo "$(brew --prefix qt@5)/bin" >> $GITHUB_PATH

# Clang/LLVM
brew install llvm
brew link llvm

# Set Homebrew's clang as the default C/C++ compiler
echo "$(brew --prefix llvm)/bin" >> $GITHUB_PATH
echo "CC=$(brew --prefix llvm)/bin/clang" >> $GITHUB_ENV
echo "CXX=$(brew --prefix llvm)/bin/clang++" >> $GITHUB_ENV