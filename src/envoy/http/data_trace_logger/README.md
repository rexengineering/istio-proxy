# Data Trace Logger Filter

This is a rex-implemented filter that logs request and response data and headers to jaeger.

## Building

To build the Envoy static binary for mac, see [`this confluence`](https://rexproduct.atlassian.net/wiki/spaces/EN/pages/1015644223/Building+Istio+and+Envoy)

## Running

* Start up some containers in Docker:
    - `docker run -d -p 8088:80 nginx:latest`
    - `docker run -d -e COLLECTOR_ZIPKIN_HTTP_PORT=9411 -p 9411:9411 -p 16686:16686 jaegertracing/all-in-one`
* Build and run custom Envoy:
    - Build as per the Confluence.
    - `bazel-bin/src/envoy/envoy -c data-trace-logger/data_trace_logger.yaml`
* Send traffic to Envoy:
    - `curl -v http://127.0.0.1:8080/`
* Check the traces in Jaeger.
    - `open http://127.0.0.1:16686`

