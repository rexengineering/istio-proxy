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