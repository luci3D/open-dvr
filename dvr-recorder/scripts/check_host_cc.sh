#!/bin/bash
# Reports whether a native C compiler is available in WSL for host-side tests.
command -v gcc cc clang tcc 2>/dev/null || echo "no native compiler"
sudo -n true 2>/dev/null && echo "sudo: passwordless" || echo "sudo: needs a password"
