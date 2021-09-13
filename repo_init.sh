#!/bin/bash
set -ex
git remote add sync https://github.com/istio/proxy.git
git pull --all
git checkout --track sync/master
git checkout rex/master
