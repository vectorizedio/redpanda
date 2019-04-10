import os
import sys
import logging
import requests
import io
import tarfile
logger = logging.getLogger('rp')
# v's
import shell
import log
import system

from constants import *


def _go_env():
    e = os.environ.copy()
    e["GOPATH"] = GOPATH
    e["GOBIN"] = GOLANG_BUILD_ROOT
    e["CGO_ENABLED"] = "1"
    e["GOOS"] = "linux"
    e["GOARCH"] = "amd64"
    return e


def go_get(package):
    shell.run_subprocess("%s get %s" % (_get_go_binary(), package), _go_env())


def go_build(path, file, output):
    cmd = "cd %s && %s build -a -tags netgo -ldflags '-w' -o %s %s"
    if log.is_debug():
        cmd = "cd %s && %s build -a -v -x -tags netgo -ldflags '-w' -o %s %s"
    shell.run_subprocess(cmd % (path, _get_go_binary(), output, file),
                         _go_env())


def go_test(path, packages):
    shell.run_subprocess(
        "cd %s && %s test %s" % (path, _get_go_binary(), packages), _go_env())


def get_crlfmt():
    crlfmt_path = "%s/%s" % (GOLANG_BUILD_ROOT, "crlfmt")
    if not os.path.exists(crlfmt_path):
        go_get("github.com/cockroachdb/crlfmt")
    return crlfmt_path


def _get_go_binary():
    target_path = _get_golang_path()
    if not os.path.exists(target_path):
        _download_go()
    return "%s/go/bin/go" % target_path


def _get_golang_path():
    return "%s/%s" % (GOLANG_COMPLILER_ROOT, GOLANG_VERSION)


def _download_go():
    system.execute_for_os(
        linux=_download_go_linux,
        darwin=_download_go_osx,
        freebsd=_download_go_freebsd)


def _download_go_linux():
    system.execute_for_arch(
        x64=lambda: _get_golang_tarball("linux", "amd64"),
        x86=lambda: _get_golang_tarball("linux", "386"),
        aarm32=lambda: _not_supported("linux", "arm32"),
        aarm64=lambda: _get_golang_tarball("linux", "arm64"))


def _download_go_osx():
    system.execute_for_arch(
        x64=lambda: _get_golang_tarball("darwin", "amd64"),
        x86=lambda: _not_supported("darwin", "386"))


def _download_go_freebsd():
    system.execute_for_arch(
        x64=lambda: _get_golang_tarball("freebsd", "amd64"),
        x86=lambda: _get_golang_tarball("freebsd", "386"),
        aarm32=lambda: _not_supported("freebsd", "arm32"),
        aarm64=lambda: _not_supported("freebsd", "arm64"))


def _get_golang_tarball(platform, arch):
    logger.info("Downloading GO version %s for %s - %s" % (GOLANG_VERSION,
                                                           platform, arch))
    url = "https://dl.google.com/go/go%s.%s-%s.tar.gz" % (GOLANG_VERSION,
                                                          platform, arch)
    resp = requests.get(url)
    io_bytes = io.BytesIO(resp.content)
    tar = tarfile.open(fileobj=io_bytes, mode='r')
    logger.info("Extracting golang tarball...")
    tar.extractall(path=_get_golang_path())


def _not_supported(platform, arch):
    msg = "%s - %s is not supported" % (platform, arch)
    logger.fatal(msg)
    raise RuntimeError(msg)
