FROM gcr.io/istio-testing/build-tools-proxy@sha256:ef91d7f32369c5663b650cc645adf6da06bb94fcb3a0bfdd6cef43e5cfc669e0 AS releasebuilder

RUN apt-get update -y && apt-get install -y emacs less
RUN mkdir /proxy
COPY . /proxy/
RUN cd /proxy && export PATH=/usr/lib/llvm-9/bin:/usr/lib/llvm/bin:/usr/local/go/bin:/gobin:/usr/local/google-cloud-sdk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin CC=clang CXX=clang++ && bazel  build  --config=libc++ //src/envoy:envoy

FROM builder AS test
RUN cd /proxy && export PATH=/usr/lib/llvm-9/bin:/usr/lib/llvm/bin:/usr/local/go/bin:/gobin:/usr/local/google-cloud-sdk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin CC=clang CXX=clang++ && bazel  test  --config=libc++ //src/envoy/http/data_trace_logger:dtl_filter_test

FROM istio/proxyv2:1.7.0 AS container
COPY --from=builder /proxy/bazel-bin/src/envoy/envoy /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/pilot-agent"]
