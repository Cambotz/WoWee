#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_ROOT="${1:-$ROOT_DIR/extern/FidelityFX-SDK}"
KITS_DIR="$SDK_ROOT/Kits/FidelityFX"
FFX_SC="$KITS_DIR/tools/ffx_sc/ffx_sc.py"
OUT_DIR="$KITS_DIR/framegeneration/fsr3/internal/permutations/vk"
SHADER_DIR="$KITS_DIR/upscalers/fsr3/internal/shaders"

if [[ ! -f "$FFX_SC" ]]; then
  echo "Missing ffx_sc.py at $FFX_SC" >&2
  exit 1
fi

required_headers=(
  "$OUT_DIR/ffx_fsr2_accumulate_pass_wave64_16bit_permutations.h"
  "$OUT_DIR/ffx_fsr3upscaler_accumulate_pass_wave64_16bit_permutations.h"
  "$OUT_DIR/ffx_fsr3upscaler_autogen_reactive_pass_permutations.h"
)
if [[ "${WOWEE_FORCE_REGEN_PERMS:-0}" != "1" ]]; then
  missing=0
  for h in "${required_headers[@]}"; do
    [[ -f "$h" ]] || missing=1
  done
  if [[ $missing -eq 0 ]]; then
    echo "FidelityFX VK permutation headers already present."
    exit 0
  fi
fi

if [[ -z "${DXC:-}" ]]; then
  if [[ -x /tmp/dxc/bin/dxc ]]; then
    export DXC=/tmp/dxc/bin/dxc
  elif command -v dxc >/dev/null 2>&1; then
    export DXC="$(command -v dxc)"
  elif [[ "$(uname -s)" == "Linux" ]]; then
    _arch="$(uname -m)"
    if [[ "$_arch" == "aarch64" || "$_arch" == "arm64" ]]; then
      echo "Linux aarch64: no official arm64 DXC release available." >&2
      echo "Install 'directx-shader-compiler' via apt or set DXC=/path/to/dxc to regenerate." >&2
      echo "Skipping VK permutation codegen (permutations may be pre-built in the SDK checkout)."
      exit 0
    fi
    echo "DXC not found; downloading Linux DXC release to /tmp/dxc ..."
    tmp_json="$(mktemp)"
    curl -sS https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest > "$tmp_json"
    dxc_url="$(python3 - << 'PY' "$tmp_json"
import json, sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    data = json.load(f)
for a in data.get('assets', []):
    name = a.get('name', '')
    if name.startswith('linux_dxc_') and name.endswith('.x86_64.tar.gz'):
        print(a.get('browser_download_url', ''))
        break
PY
)"
    rm -f "$tmp_json"
    if [[ -z "$dxc_url" ]]; then
      echo "Failed to locate Linux DXC release asset URL." >&2
      exit 1
    fi
    rm -rf /tmp/dxc /tmp/linux_dxc.tar.gz
    curl -L --fail "$dxc_url" -o /tmp/linux_dxc.tar.gz
    mkdir -p /tmp/dxc
    tar -xzf /tmp/linux_dxc.tar.gz -C /tmp/dxc --strip-components=1
    export DXC=/tmp/dxc/bin/dxc
  elif [[ "$(uname -s)" =~ MINGW|MSYS|CYGWIN ]]; then
    echo "DXC not found; downloading Windows DXC release to /tmp/dxc ..."
    tmp_json="$(mktemp)"
    curl -sS https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest > "$tmp_json"
    dxc_url="$(python3 - << 'PY' "$tmp_json"
import json, sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    data = json.load(f)
for a in data.get('assets', []):
    name = a.get('name', '')
    if name.startswith('dxc_') and name.endswith('.zip'):
        print(a.get('browser_download_url', ''))
        break
PY
)"
    rm -f "$tmp_json"
    if [[ -z "$dxc_url" ]]; then
      echo "Failed to locate Windows DXC release asset URL." >&2
      exit 1
    fi
    rm -rf /tmp/dxc /tmp/dxc_win.zip
    curl -L --fail "$dxc_url" -o /tmp/dxc_win.zip
    mkdir -p /tmp/dxc
    unzip -q /tmp/dxc_win.zip -d /tmp/dxc
    if [[ -x /tmp/dxc/bin/x64/dxc.exe ]]; then
      export DXC=/tmp/dxc/bin/x64/dxc.exe
    elif [[ -x /tmp/dxc/bin/x86/dxc.exe ]]; then
      export DXC=/tmp/dxc/bin/x86/dxc.exe
    else
      echo "DXC download succeeded, but dxc.exe was not found." >&2
      exit 1
    fi
  else
    echo "DXC not found. Set DXC=/path/to/dxc or install to /tmp/dxc/bin/dxc" >&2
    exit 1
  fi
fi

mkdir -p "$OUT_DIR"

# First generate frame interpolation + optical flow permutations via SDK script.
(
  cd "$SDK_ROOT"
  ./generate_vk_permutations.sh
)

BASE_ARGS=(-reflection -embed-arguments -E CS -Wno-for-redefinition -Wno-ambig-lit-shift -DFFX_GPU=1 -DFFX_HLSL=1 -DFFX_IMPLICIT_SHADER_REGISTER_BINDING_HLSL=0)
WAVE32=(-DFFX_HLSL_SM=62 -T cs_6_2)
WAVE64=("-DFFX_PREFER_WAVE64=[WaveSize(64)]" -DFFX_HLSL_SM=66 -T cs_6_6)
BIT16=(-DFFX_HALF=1 -enable-16bit-types)

compile_shader() {
  local file="$1"; shift
  local name="$1"; shift
  python3 "$FFX_SC" "${BASE_ARGS[@]}" "$@" -name="$name" -output="$OUT_DIR" "$file"
}

# FSR2 (for upscalers/fsr3/internal/ffx_fsr2_shaderblobs.cpp)
FSR2_COMMON=(
  -DFFX_FSR2_EMBED_ROOTSIG=0
  -DFFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
  -DFFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
  "-DFFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}"
  "-DFFX_FSR2_OPTION_HDR_COLOR_INPUT={0,1}"
  "-DFFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}"
  "-DFFX_FSR2_OPTION_JITTERED_MOTION_VECTORS={0,1}"
  "-DFFX_FSR2_OPTION_INVERTED_DEPTH={0,1}"
  "-DFFX_FSR2_OPTION_APPLY_SHARPENING={0,1}"
  -I "$KITS_DIR/api/internal/include/gpu"
  -I "$KITS_DIR/upscalers/fsr3/include/gpu"
)
FSR2_SHADERS=(
  ffx_fsr2_autogen_reactive_pass
  ffx_fsr2_accumulate_pass
  ffx_fsr2_compute_luminance_pyramid_pass
  ffx_fsr2_depth_clip_pass
  ffx_fsr2_lock_pass
  ffx_fsr2_reconstruct_previous_depth_pass
  ffx_fsr2_rcas_pass
  ffx_fsr2_tcr_autogen_pass
)

for shader in "${FSR2_SHADERS[@]}"; do
  file="$SHADER_DIR/$shader.hlsl"
  [[ -f "$file" ]] || continue
  compile_shader "$file" "$shader" -DFFX_HALF=0 "${WAVE32[@]}" "${FSR2_COMMON[@]}"
  compile_shader "$file" "${shader}_wave64" -DFFX_HALF=0 "${WAVE64[@]}" "${FSR2_COMMON[@]}"
  compile_shader "$file" "${shader}_16bit" "${BIT16[@]}" "${WAVE32[@]}" "${FSR2_COMMON[@]}"
  compile_shader "$file" "${shader}_wave64_16bit" "${BIT16[@]}" "${WAVE64[@]}" "${FSR2_COMMON[@]}"
done

# FSR3 upscaler (for upscalers/fsr3/internal/ffx_fsr3upscaler_shaderblobs.cpp)
FSR3_COMMON=(
  -DFFX_FSR3UPSCALER_EMBED_ROOTSIG=0
  -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
  -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
  "-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}"
  -I "$KITS_DIR/api/internal/gpu"
  -I "$KITS_DIR/upscalers/fsr3/include/gpu"
)
FSR3_SHADERS=(
  ffx_fsr3upscaler_autogen_reactive_pass
  ffx_fsr3upscaler_accumulate_pass
  ffx_fsr3upscaler_luma_pyramid_pass
  ffx_fsr3upscaler_prepare_reactivity_pass
  ffx_fsr3upscaler_prepare_inputs_pass
  ffx_fsr3upscaler_shading_change_pass
  ffx_fsr3upscaler_rcas_pass
  ffx_fsr3upscaler_shading_change_pyramid_pass
  ffx_fsr3upscaler_luma_instability_pass
  ffx_fsr3upscaler_debug_view_pass
)

for shader in "${FSR3_SHADERS[@]}"; do
  file="$SHADER_DIR/$shader.hlsl"
  [[ -f "$file" ]] || continue
  compile_shader "$file" "$shader" -DFFX_HALF=0 "${WAVE32[@]}" "${FSR3_COMMON[@]}"
  compile_shader "$file" "${shader}_wave64" -DFFX_HALF=0 "${WAVE64[@]}" "${FSR3_COMMON[@]}"
  compile_shader "$file" "${shader}_16bit" "${BIT16[@]}" "${WAVE32[@]}" "${FSR3_COMMON[@]}"
  compile_shader "$file" "${shader}_wave64_16bit" "${BIT16[@]}" "${WAVE64[@]}" "${FSR3_COMMON[@]}"
done

echo "Generated VK permutation headers in $OUT_DIR"
