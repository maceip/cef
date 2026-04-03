#!/bin/bash
# Source this file to configure sccache with S3 backend.
# Developers source it locally; CI runners have it pre-configured in the Docker image.
#
# Usage:
#   source ./infra/scripts/sccache-env.sh
#   # Then build as usual — sccache wraps the compiler transparently
#   ./tools/claude/build.sh build
#
# Requirements:
#   - sccache installed (brew install sccache / cargo install sccache)
#   - AWS credentials configured (aws configure / IAM role / env vars)

export SCCACHE_BUCKET="${SCCACHE_BUCKET:-cef-build-cache-$(aws sts get-caller-identity --query Account --output text 2>/dev/null || echo 'ACCOUNT')}"
export SCCACHE_REGION="${SCCACHE_REGION:-us-east-1}"
export SCCACHE_S3_KEY_PREFIX="sccache/"
export SCCACHE_S3_USE_SSL="true"
export SCCACHE_IDLE_TIMEOUT="0"

# Wrap compilers
export RUSTC_WRAPPER="sccache"

# For Chromium/CEF C++ builds, set CC/CXX wrappers in args.gn instead:
#   cc_wrapper = "sccache"
echo "sccache configured: bucket=$SCCACHE_BUCKET region=$SCCACHE_REGION"
echo "For C++ builds, add to args.gn:  cc_wrapper = \"sccache\""
echo "Run 'sccache --show-stats' to verify connectivity."
