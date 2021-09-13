FROM gcr.io/istio-testing/build-tools-proxy@sha256:f811d5d113e34f33ecec18b6b1e03dc0139d5cda15c0b1d70c8f0f1b6d016a0b AS builder

RUN mkdir /proxy
COPY . /proxy/
RUN mv /proxy/istio-envoy /envoy
RUN cd /proxy && export PATH=/usr/lib/llvm-9/bin:/usr/lib/llvm/bin:/usr/local/go/bin:/gobin:/usr/local/google-cloud-sdk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin CC=clang CXX=clang++ && bazel  build --override_repository=envoy=/envoy --config=libc++ --config=release //src/envoy:envoy

FROM builder AS test
RUN cd /proxy && export PATH=/usr/lib/llvm-9/bin:/usr/lib/llvm/bin:/usr/local/go/bin:/gobin:/usr/local/google-cloud-sdk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin CC=clang CXX=clang++ && bazel  test --override_repository=envoy=/envoy --config=libc++ //src/envoy/http/data_trace_logger:dtl_filter_test

FROM istio/proxyv2:1.8.2 AS container
COPY --from=builder /proxy/bazel-bin/src/envoy/envoy /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/pilot-agent"]
