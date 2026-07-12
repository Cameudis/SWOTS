// SPDX-License-Identifier: GPL-2.0-or-later

#include "overlay.hpp"

#include "draw.hpp"
#include "motion.hpp"
#include "control.hpp"

#include <cstdio>

extern "C" u64 __nx_vi_layer_id;

namespace overlay {

static Result addToLayerStack(ViLayer *layer, ViLayerStack stack) {
    const struct {
        u32 stack;
        u64 layerId;
    } in = {static_cast<u32>(stack), layer->layer_id};
    return serviceDispatchIn(viGetSession_IManagerDisplayService(), 6000, in);
}

Result init(Context *context) {
    if (!context || context->initialized) return 0;

    Result rc = viInitialize(ViServiceType_Manager);
    s32 maxZ = 0;
    char stage[48]{};
    if (R_FAILED(rc)) {
        control::writeStatus("viInitialize", rc);
        return rc;
    }

    rc = viOpenDefaultDisplay(&context->display);
    if (R_FAILED(rc)) {
        control::writeStatus("viOpenDefaultDisplay", rc);
        goto fail_vi;
    }

    // viCreateLayer consumes libnx's weak __nx_vi_layer_id. The old scaffold
    // stored the managed ID in an unrelated local variable, so it then opened
    // a stray layer instead of the layer it had just created.
    rc = viCreateManagedLayer(&context->display, static_cast<ViLayerFlags>(0), 0,
                              &__nx_vi_layer_id);
    if (R_FAILED(rc)) {
        control::writeStatus("viCreateManagedLayer", rc);
        goto fail_display;
    }

    rc = viCreateLayer(&context->display, &context->layer);
    if (R_FAILED(rc)) {
        control::writeStatus("viCreateLayer", rc);
        goto fail_display;
    }

    rc = viSetLayerScalingMode(&context->layer, ViScalingMode_FitToLayer);
    if (R_FAILED(rc)) {
        control::writeStatus("viSetLayerScalingMode", rc);
        goto fail_layer;
    }

    rc = viGetZOrderCountMax(&context->display, &maxZ);
    if (R_SUCCEEDED(rc) && maxZ > 0) {
        // Reserve the absolute top Z slot for Tesla and native system UI.
        // Using maxZ here prevents Tesla from becoming visible while cues run.
        const s32 cueZ = maxZ > 2 ? maxZ - 2 : 0;
        rc = viSetLayerZ(&context->layer, cueZ);
        if (R_FAILED(rc)) {
            control::writeStatus("viSetLayerZ", rc);
            goto fail_layer;
        }
    }

    // Default is sufficient for gameplay. Do not join the dedicated LCD
    // stack used by Tesla; older VI implementations cannot reliably keep two
    // third-party managed overlays active in that stack at once.
    rc = addToLayerStack(&context->layer, ViLayerStack_Default);
    if (R_FAILED(rc)) {
        control::writeStatus("addToLayerStack(Default)", rc);
        goto fail_layer;
    }
    rc = viSetLayerSize(&context->layer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (R_FAILED(rc)) {
        control::writeStatus("viSetLayerSize", rc);
        goto fail_layer;
    }
    rc = viSetLayerPosition(&context->layer, 0.0f, 0.0f);
    if (R_FAILED(rc)) {
        control::writeStatus("viSetLayerPosition", rc);
        goto fail_layer;
    }

    rc = nwindowCreateFromLayer(&context->window, &context->layer);
    if (R_FAILED(rc)) {
        control::writeStatus("nwindowCreateFromLayer", rc);
        goto fail_layer;
    }

    rc = framebufferCreate(&context->framebuffer, &context->window,
                           FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT,
                           PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        control::writeStatus("framebufferCreate", rc);
        goto fail_window;
    }

    rc = framebufferMakeLinear(&context->framebuffer);
    if (R_FAILED(rc)) {
        control::writeStatus("framebufferMakeLinear", rc);
        goto fail_framebuffer;
    }

    context->initialized = true;
    std::snprintf(stage, sizeof(stage), "rendering(maxZ=%d,z=%d)", maxZ,
                  maxZ > 2 ? maxZ - 2 : 0);
    control::writeStatus(stage, 0);
    return 0;

fail_framebuffer:
    framebufferClose(&context->framebuffer);
fail_window:
    nwindowClose(&context->window);
fail_layer:
    viDestroyManagedLayer(&context->layer);
fail_display:
    viCloseDisplay(&context->display);
fail_vi:
    viExit();
    *context = {};
    return rc;
}

void render(Context *context, Motion &motion, const Config &config, float dt) {
    if (!context || !context->initialized) return;
    motion.update(motion.sample(), config, dt);

    u32 stride = 0;
    auto *pixels = static_cast<u32 *>(framebufferBegin(&context->framebuffer, &stride));
    draw::frame(pixels, stride, FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT, motion, config);
    framebufferEnd(&context->framebuffer);
}

void fini(Context *context) {
    if (!context || !context->initialized) return;
    framebufferClose(&context->framebuffer);
    nwindowClose(&context->window);
    viDestroyManagedLayer(&context->layer);
    viCloseDisplay(&context->display);
    viExit();
    *context = {};
}

} // namespace overlay
