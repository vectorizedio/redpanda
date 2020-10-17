ARG BASE_SHA=latest
ARG GOLANG_SHA=latest
ARG CLANG_SHA=latest
ARG NODE_SHA=latest
FROM gcr.io/redpandaci/golang:${GOLANG_SHA} as godeps
FROM gcr.io/redpandaci/node:${NODE_SHA} as vnode
FROM gcr.io/redpandaci/clang:${CLANG_SHA} as clang

FROM gcr.io/redpandaci/base:${BASE_SHA}

ARG COMPILER=gcc
ARG BUILD_TYPE=release

COPY 3rdparty.cmake.in CMakeLists.txt /v/
COPY tools /v/tools/

COPY --from=godeps /vectorized/go /vectorized/go
COPY --from=vnode /vectorized/node /vectorized/node
COPY --from=clang /vectorized/llvm /vectorized/llvm

RUN pip install /v/tools && \
    cp tools/ci/vtools-${COMPILER}-${BUILD_TYPE}.yml /v/.vtools.yml && \
    vtools install cpp-deps && \
    pip uninstall -y vtools && \
    rm -r /v
