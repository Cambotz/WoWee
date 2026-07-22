## AMD FSR2 Vulkan Permutation Snapshot

This directory contains pre-generated Vulkan permutation headers used by the AMD FSR2 SDK Vulkan backend.

Why this exists:
- Recent upstream `GPUOpen-Effects/FidelityFX-FSR2` checkouts may not include generated `*_permutations.h` files.
- WoWee uses these vendored headers to bootstrap AMD FSR2 builds consistently on Linux, macOS, and Windows without requiring Wine.

Source SDK commit used for this snapshot:
- `1680d1e` (from local `extern/FidelityFX-FSR2` checkout when snapshot was generated)

Refresh process:
1. Regenerate Vulkan permutation headers in `extern/FidelityFX-FSR2/src/ffx-fsr2-api/vk/shaders`.
2. Copy `ffx_fsr2_*pass*.h` files into this directory.
3. Commit the updated snapshot together with any AMD FSR2 integration changes.
