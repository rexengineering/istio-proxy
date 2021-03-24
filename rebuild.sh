#!/bin/bash
set -e

#echo "Starting proxybuilder container. If this fails, try running buildtools/setup_build.sh"
#docker start proxybuilder

#echo "Successfully started the proxybuilder container. Commencing build."
docker exec -it proxybuilder /build.sh

echo "Done building. Copying Envoy Binary."
docker cp proxybuilder:/proxy/bazel-bin/src/envoy/envoy buildtools/

echo "Done copying Envoy Binary. Building final REX Istio-Proxy image."
docker build -t 'rex-proxy:1.8.2' -f Dockerfile.localproxy buildtools/
