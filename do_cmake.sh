#!/bin/bash
if test -e build; then
    echo 'build dir already exists; rm -rf build and re-run'
    exit 1
fi

git submodule update --init --recursive
mkdir build
cd build
NPROC=${NPROC:-$(nproc)}
# ARGS="$ARGS --trace"
# mkdir -p boost/src/
# cp ../../boost_1_67_0.tar.bz2 /root/container/seastar_test/build/boost/src/

cmake -DBOOST_J=$NPROC $ARGS "$@" ..
echo done.
