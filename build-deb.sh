#!/bin/bash

# Terminate execution if any subsystem command fails
set -e

PACKAGE_NAME="traverso"
BUILD_DIR="obj-x86_64-linux-gnu"

echo "=== 1. Purging previous compilation state ==="
# Reset the debhelper infrastructure state
dh_clean
# Delete internal building and caching trees
rm -rf "$BUILD_DIR"

echo "=== 2. Compiling production Debian binaries ==="
# - parallel limits compilation threads to prevent system resource exhaustion
# - terse suppresses detailed compiler flags to enforce clean progress logging
DEB_BUILD_OPTIONS="parallel=$(($(nproc)-1)) terse" dpkg-buildpackage -us -uc -b

echo "=== 3. Cleaning up build environment workspace ==="
# Final sanitization to remove duplicate binary objects and save space
dh_clean
rm -rf "$BUILD_DIR"

echo "================================================================="
echo " SUCCESS! The production-ready .deb package is generated."
echo " Location: ../ - Parent directory"
echo " Install string: sudo apt install ../${PACKAGE_NAME}_*_amd64.deb"
echo "================================================================="
