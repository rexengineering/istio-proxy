FROM gcr.io/istio-testing/build-tools-proxy:release-1.11-latest AS builder

RUN mkdir /proxy
COPY . /proxy/
RUN mv /proxy/istio-envoy /envoy
RUN cd /proxy && export PATH=/usr/lib/llvm-9/bin:/usr/lib/llvm/bin:/usr/local/go/bin:/gobin:/usr/local/google-cloud-sdk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin CC=clang CXX=clang++ && bazel build --override_repository=envoy=/envoy --config=libc++ --config=release //src/envoy:envoy

FROM builder AS test
RUN cd /proxy && export PATH=/usr/lib/llvm-9/bin:/usr/lib/llvm/bin:/usr/local/go/bin:/gobin:/usr/local/google-cloud-sdk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin CC=clang CXX=clang++ && bazel test --override_repository=envoy=/envoy --config=libc++ //src/envoy/http/data_trace_logger:dtl_filter_test

FROM istio/proxyv2:1.11.2 AS container
COPY --from=builder /proxy/bazel-bin/src/envoy/envoy /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/pilot-agent"]
