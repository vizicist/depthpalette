#!/bin/bash
cd "$(dirname "$0")"
cmake -B build -G Ninja
cmake --build build -j$(nproc)
