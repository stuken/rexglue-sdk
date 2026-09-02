# GPU render target cvar tuning guide

ReXGlue's graphics core is a direct port of Xenia Canary. Most render-target cvars kept Xenia's
help text verbatim, but a handful were stripped down to one-liners during the port and lost the
tuning guidance that explains *why* you'd ever change them. This doc restores that context,
sourced from Xenia Canary's original cvar definitions. It's a reference, not something read at
runtime - the in-code help strings (`rex::cvar::ListFlags()`, `--help`, the settings UI) stay
short by design.

Only cvars whose in-code description is meaningfully shorter than this doc's explanation are
listed. Cvars whose in-code help is already the full text (`draw_resolution_scale_threshold`,
`debug_msaa_2x_as_4x`, `no_discard_stencil_in_transfer_pipelines`) aren't repeated here - read
them directly at their definition site.

## `render_target_path_d3d12`

Defined in `src/graphics/d3d12/render_target_cache.cpp`, category `GPU/D3D12`, init-only.

Render target emulation path to use on Direct3D 12. Values: `rtv`, `rov`, or anything else for
automatic selection.

- **`rtv`** - Host render targets and fixed-function blending and depth/stencil testing, copying
  between render targets when needed. Lower accuracy (limited pixel format support).
  Performance is limited primarily by render target layout changes requiring copying, but is
  generally higher than `rov`.
- **`rov`** - Manual pixel packing, blending, and depth/stencil testing via pixel shader
  interlock, with free render target layout changes. Requires a GPU supporting rasterizer-ordered
  views. Highest accuracy (all pixel formats handled in software). Performance is limited
  primarily by overdraw. On AMD drivers, this has historically caused shader compiler crashes in
  many cases.
- **Any other value (automatic)** - Chooses RTV by default (the ROV path is currently much slower
  in general), except on Intel GPUs, which default to ROV because of an Intel stencil-testing bug
  that breaks Xbox 360 Direct3D 9-style clears - Intel Arc GPUs are exempted from this since they
  don't have the bug (see `native_stencil_value_output_d3d12_intel` below, and
  `RTV_D3D12_AUDIT.md` items A1/A2 for how this default was restored after the port temporarily
  dropped the Arc exemption).

## `native_stencil_value_output_d3d12_intel`

Defined alongside `render_target_path_d3d12`, category `GPU/D3D12`.

Allow pixel-shader-specified stencil reference output usage on Direct3D 12 on (non-Arc) Intel
GPUs. Xenia's original note: this wasn't working correctly on Intel UHD Graphics 630 as of driver
27.20.0100.8336 (March 2021) - that's why it defaults to `false` and needs to be explicitly
enabled for testing on that class of hardware. Intel Arc GPUs don't need this cvar at all; they're
exempted directly in the `use_stencil_reference_output_` condition.

## `native_stencil_value_output`

Defined in `src/graphics/d3d12/render_target_cache.cpp`, category `GPU`.

Use pixel shader stencil reference output where available, for purposes like copying between
render targets. Can be disabled for scalability testing - with it off, a much more expensive
8-quad drawing fallback is used instead of a single shader-specified-stencil draw.

D3D12-only in practice: PS-specified stencil reference output has no Vulkan equivalent in this
architecture (Vulkan uses dynamic stencil reference state via `vkCmdSetStencilReference` instead),
so this cvar is only ever consumed by the D3D12 render target cache in both Xenia and ReXGlue.

## `gamma_render_target_as_unorm16`

Defined in `src/graphics/flags.cpp`, category `GPU`, init-only (see below for why).

When the host can't write 8-bit-per-component pixels with piecewise linear gamma encoding
directly with correct blending, use the 16-bit unsigned normalized format instead, if supported,
for conceptually correct `8_8_8_8_GAMMA` render target format blending in linear color space.
Greatly increases accuracy for this format, but may result in render target copying costs if the
game switches between `8_8_8_8_GAMMA` and `8_8_8_8` views for the same EDRAM render target.

**Why init-only:** this is read once in `RenderTargetCache::Initialize()` (both D3D12 and Vulkan)
and cached into a member that drives render target format selection and shader/pipeline
modification keys, with no live-invalidation path for either. It used to be marked hot-reload,
which was never actually honored - toggling it at runtime did nothing, or desynced newly-created
pipelines (which read the cvar live) from already-created render targets (which didn't). See
`RTV_D3D12_AUDIT.md` item A3.

## `depth_float24_round`

Defined in `src/graphics/flags.cpp`, category `GPU`, init-only (same reason as above - cached at
`Initialize()`, no live invalidation, see `RTV_D3D12_AUDIT.md` item A3).

Whether to round to the nearest even, rather than truncate (round towards zero), the depth when
converting it to 24-bit floating-point (20e4) from host precision (32-bit float) when using a
host depth buffer. Ignored (always rounds to nearest even) when the depth buffer is emulated in
software via ROV/pixel-shader-interlock - the float24 rounding mode only matters on the RTV path.

**`false` (recommended, default):**
- The conversion may move depth values farther from the camera.
- Without `depth_float24_convert_in_pixel_shader`: the "greater or equal" depth test function
  keeps working fine even if full host-precision depth data is lost - it's still possible to draw
  another pass of the same geometry with it.
- With `depth_float24_convert_in_pixel_shader`: faster - the pixel shader for hidden surfaces may
  still be skipped (conservative depth output).

**`true` (special cases only):** for issues caused by minor 32-bit floating-point rounding errors
- for instance, when a game tries to draw something at the camera plane by setting Z of the vertex
position to W. Uses the same rounding mode as the Direct3D 9 reference rasterizer. The conversion
may move depth values closer *or* farther.
- Without `depth_float24_convert_in_pixel_shader`: **not** possible to recover from a full
  host-precision depth data loss - in subsequent passes of the same geometry, half the samples
  will fail the "greater or equal" depth test.
- With `depth_float24_convert_in_pixel_shader`: slower - depth rejection before the pixel shader
  isn't possible.

Known round-trip test cases from Xenia's original comments (title IDs, kept for reference):
lossless round trip `545407F2`; lossy round trip with a "greater or equal" test afterwards
`4D530919`; lossy round trip with an "equal" test afterwards `535107F5`, `565507EF`.

## `depth_float24_convert_in_pixel_shader`

Defined alongside `depth_float24_round`, category `GPU`, init-only (same reason).

Whether to convert depth values to 24-bit floating-point (20e4) from host precision directly in
the pixel shaders of guest draws, when using a host depth buffer. Ignored when the depth buffer is
emulated in software via ROV, since 24-bit depth is always used directly there.

Prevents a specific visual artifact - interleaved stripes of rendered/not-rendered surface parts,
either equal-width with the "greater or equal" test function or much thinner with "equal" - that
happens if full host-precision depth data is lost. This loss can happen if the game reloads depth
data previously evicted from EDRAM back into EDRAM, but the EDRAM region that held it was
overwritten by a different depth buffer in the meantime, or the game reloads it to a different
EDRAM location than before - the renderer then can't restore full-precision depth and falls back
to the lower-precision converted value, so later passes over the same geometry no longer match
what's stored in the depth buffer.

Costly: disables depth buffer compression, and with MSAA forces the pixel shader to run per-sample
rather than per-pixel (2x or 4x heavier depending on sample count). If sample-rate shading isn't
supported by the host GPU, the conversion only happens when MSAA is off. The rounding direction
is controlled by `depth_float24_round` - with that set to `true`, this becomes even *more* costly
because pixel shaders must run regardless of whether the surface is occluded; with it `false`,
conservative depth output means depth rejection before the pixel shader may still work.

## `depth_transfer_not_equal_test`

Defined in `src/graphics/flags.cpp`, category `GPU`.

When transferring data between depth render targets, use a "not equal" test to avoid rewriting
depth via shader depth output when it's already the same as what's in the depth buffer, in round
trips of the same data.

- `true`: transfer round trips are more friendly to depth compression (arbitrary depth output can
  disable compression entirely on some GPUs), which benefits subsequent rendering.
- `false`: may reduce bandwidth during transfers, since the previous depth doesn't need to be read
  first - but loses the compression-friendliness benefit above.

## `mrt_edram_used_range_clamp_to_min`

Defined in `src/graphics/pipeline/render_target/cache.cpp` (base cache, shared D3D12/Vulkan),
category `GPU`.

With host render targets, if multiple render targets are bound, estimate the EDRAM range modified
by any of them as no bigger than the distance between any two of the bound render targets in
EDRAM, rather than letting the last one claim the rest of EDRAM. Mostly affects draws without
viewport clipping. Setting this `false` gives higher accuracy in rare cases, but can increase how
much copying needs to happen.

## `execute_unclipped_draw_vs_on_cpu_for_psi_render_backend`

Defined alongside `mrt_edram_used_range_clamp_to_min`, category `GPU`.

If `execute_unclipped_draw_vs_on_cpu` is enabled, also execute the vertex shader for unclipped
draws on the CPU when using the pixel-shader-interlock (ROV) render backend - which, unlike RTV,
never needs expensive copying between host render targets when EDRAM range ownership changes. When
this is on, excessive barriers when switching between render targets in separate EDRAM locations
can be eliminated. Defaults on: the GPU is usually the bottleneck with ROV, so anything that can
improve GPU-side performance is favorable.

## `snorm16_render_target_full_range`

Defined alongside the two cvars above, category `GPU.Debug`.

When the host can only support `16_16`/`16_16_16_16` render targets as `-1...1`, remap
`-32...32` to `-1...1` to use the full possible value range, at the expense of multiplicative
blending correctness.

## `draw_resolution_scaled_texture_offsets`

Defined in `src/graphics/pipeline/shader/dxbc_translator_fetch.cpp`, category `GPU/Shader`.

Apply offsets from texture fetch instructions taking draw resolution scale into account for
render-to-texture, for more correct shadow filtering, bloom, etc., in some cases.
