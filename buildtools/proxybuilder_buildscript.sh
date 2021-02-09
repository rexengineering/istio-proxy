#!/bin/bash
cd proxy
export PATH=/usr/lib/llvm-9/bin:/usr/lib/llvm/bin:/usr/local/go/bin:/gobin:/usr/local/google-cloud-sdk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin CC=clang CXX=clang++ && bazel  build --override_repository=envoy=/envoy  --config=libc++ --config=release //src/envoy:envoy
