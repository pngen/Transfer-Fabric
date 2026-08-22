# Contributing

1. Build in Release and Debug with zero warnings (MSVC /W4 /WX; GCC/Clang `-Wall -Wextra -Wpedantic -Werror`).
2. Run the full test suite; a transfer must never report success after a failed verification.
3. Add a test for any behavior change; property tests and adversarial tests are expected.
4. Keep the domain boundary: Transfer Fabric moves bytes; it does not allocate buffers or set cache/residency policy.
5. Keep the license Apache-2.0 and the repository free of project-specific prose in the license body.
