# Working with this repo

To set up your local checkout of this repo properly, follow these steps:

```
git clone git@bitbucket.org:rexdev/istio-proxy.git
cd istio-proxy
./repo_init.sh
```


When you are done, you will have a master branch that tracks istio/envoy on github, and a rex/master branch that tracks our local changes, each of which can be git pulled independently.

# NOTE. IMPORTANT!!!!!!!!!!!!!!!!!!!!

As of this commit, the `rex/master` branch is based off of the 1.7.1 tag of `sync`. *PLEASE* do not update `rex/master` to a more recent version of the upstream (`istio/envoy`) unless you BOTH know what you're doing AND have confirmed with the full infrastructure team that we are moving to a more recent version of istio.


# Building your Istio-Proxy Image

We build and develop using a Docker Container with volume mounts. To set up such a container, run `buildtools/setup_build.sh` (you may need to set `REX_ISTIO_ENVOY_PATH`).

That setup only needs to be run once. Then, to build your proxy image, just run `./rebuild.sh`.

NOTE: all commands must be run from the root of the repo.


# Using your Istio-Proxy Image (locally)

After you have built the Istio-proxy image locally, you can exercise that image using REXFlow. A good way to get a flavor is to follow the instructions at one of the rexflow [example readme's](https://bitbucket.org/rexdev/rexflow/src/f8c2a0a0ebd67227cfec802222ad0e1be22c5469/examples/istio/README.md?at=master)