#!/bin/bash

set -ex

if [[ -z "${REX_ISTIO_ENVOY_PATH}" ]]; then
  echo "You must set REX_ISTIO_ENVOY_PATH to the path to your istio-envoy repo."
  exit
else
    REX_ISTIO_ENVOY_PATH=$(echo $REX_ISTIO_ENVOY_PATH | sed 's:/*$::')
    echo $REX_ISTIO_ENVOY_PATH
fi

# Build the image for the "proxybuilder" container.
docker build -t rex-proxybuilder:1.8.2 -f buildtools/Dockerfile.proxybuilder .

# Run a container from the above image. This container will have two volume mounts,
# one at /envoy and one at /proxy. Each mount corresponds to the local repository
# on your laptop; therefore, when you edit code on your laptop, it edits code on
# the builder container.

echo "Please check in this shell that the /proxy and /envoy directories have the content you need. Then type ^D"

docker run -it --mount type=bind,source=${REX_ISTIO_ENVOY_PATH},target=/envoy --mount type=bind,source=${PWD},target=/proxy --name proxybuilder rex-proxybuilder:1.8.2 /bin/bash

echo "If your /envoy and /proxy folders on the container look right, then you are ready to start building!"
