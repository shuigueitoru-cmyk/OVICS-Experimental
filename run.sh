#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
bash build-linux.sh --run
