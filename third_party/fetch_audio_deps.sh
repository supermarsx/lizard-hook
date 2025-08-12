#!/usr/bin/env bash
set -e
DIR="$(dirname "$0")"
cd "$DIR"

if [ ! -d "miniaudio" ]; then
  git clone --depth 1 https://github.com/mackron/miniaudio.git
fi

if [ ! -d "dr_libs" ]; then
  git clone --depth 1 https://github.com/mackron/dr_libs.git
fi
