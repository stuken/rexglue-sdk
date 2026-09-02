/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <rex/ui/renderdoc_api.h>

REXCVAR_DEFINE_BOOL(gpu_allow_invalid_fetch_constants, false, "GPU",
                    "Allow invalid fetch constants");
REXCVAR_DEFINE_BOOL(
    debug_msaa_2x_as_4x, false, "GPU.Debug",
    "Use 4x MSAA with 2 samples instead of native 2x MSAA when available. For "
    "scalability testing on host GPU APIs where 2x is not mandatory. MSAA will "
    "be of a similar or worse quality and use more memory.");
REXCVAR_DEFINE_BOOL(depth_float24_round, false, "GPU", "Round float24 depth values")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(depth_float24_convert_in_pixel_shader, false, "GPU",
                    "Convert float24 depth in pixel shader")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(depth_transfer_not_equal_test, true, "GPU",
                    "Use not-equal test for depth transfer");
REXCVAR_DEFINE_BOOL(gamma_render_target_as_unorm16, true, "GPU",
                    "Use R16G16B16A16_UNORM for gamma render targets (more accurate than sRGB)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(
    ac6_ground_fix, false, "HACKS",
    "This fixes(hide) issues with black ground in AC6. Use only in AC6. "
    "Might cause issues in other titles.");
REXCVAR_DEFINE_BOOL(
    no_discard_stencil_in_transfer_pipelines, false, "GPU.Debug",
    "Skip stencil bit discard in render target transfer pipelines. "
    "May improve performance on some GPUs.");
REXCVAR_DEFINE_BOOL(
    force_depth_clamp, false, "GPU",
    "Use host depth clamping instead of near and far plane clipping when "
    "guest clipping is enabled. X/Y/W clipping is unaffected. On Vulkan, "
    "this requires depthClamp support.");
REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU.Debug", "Path to dump shaders to");
REXCVAR_DEFINE_BOOL(use_fuzzy_alpha_epsilon, false, "GPU",
                    "Use approximate compare for alpha test values to prevent "
                    "flickering on NVIDIA graphics cards")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(gpu_debug_markers, false, "GPU",
                    "Insert debug markers into GPU command streams for tools "
                    "like PIX and RenderDoc. Automatically enabled when "
                    "RenderDoc is detected.");

bool IsGpuDebugMarkersEnabled() {
  static bool cached = false;
  static bool result = false;
  if (!cached) {
    cached = true;
    if (REXCVAR_GET(gpu_debug_markers)) {
      result = true;
      REXLOG_INFO("GPU debug markers enabled via CVar");
    } else {
      auto renderdoc_api = rex::ui::RenderDocAPI::CreateIfConnected();
      if (renderdoc_api) {
        result = true;
        REXLOG_INFO("GPU debug markers auto-enabled (RenderDoc detected)");
      }
    }
  }
  return result;
}
