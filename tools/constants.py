#!/usr/bin/env python3
import sys
import os
import logging
sys.path.append(os.path.dirname(__file__))

# rp
import git

RP_ROOT = git.get_git_root(relative=os.path.dirname(__file__))
RP_BUILD_ROOT = "%s/build" % RP_ROOT
GOPATH = "%s/gopath" % RP_BUILD_ROOT
GOLANG_ROOT = "%s/src/go/pkg" % RP_ROOT
GOLANG_CMDS_ROOT = "%s/cmd" % GOLANG_ROOT
GOLANG_BUILD_ROOT = "%s/go/bin" % RP_BUILD_ROOT
GOLANG_COMPLILER_ROOT = "%s/go" % RP_BUILD_ROOT
GOLANG_VERSION = "1.12.3"
GOLANG_COMPILER = "%s/%s/go/bin/go" % (GOLANG_COMPLILER_ROOT, GOLANG_VERSION)
CPPLINT_URL = "https://raw.githubusercontent.com/google/styleguide/gh-pages/cpplint/cpplint.py"
CLANG_SOURCE_VERSION = "8.0.0"

LLVM_REF='llvmorg-9.0.0'
LLVM_MD5='c5fbb6a232c907e912883fac8b221e2d'
