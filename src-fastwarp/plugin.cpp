#include <VapourSynth4.h>
#include <VSHelper4.h>
#include "vulkan_warper.h"
#include <memory>
#include <string>

struct FastWarpData {
    VSNode* node0;
    VSNode* node1;
    VSNode* nodeFlow;
    VSNode* nodeMask;
    const VSVideoInfo* vi;
    std::unique_ptr<VulkanWarper> warper;
};

static const VSFrame* VS_CC fastwarpGetFrame(int n, int activationReason, void* instanceData, void** frameData, VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi) {
    FastWarpData* d = static_cast<FastWarpData*>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node0, frameCtx);
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->nodeFlow, frameCtx);
        vsapi->requestFrameFilter(n, d->nodeMask, frameCtx);
        return nullptr;
    } else if (activationReason == arAllFramesReady) {
        const VSFrame* src0 = vsapi->getFrameFilter(n, d->node0, frameCtx);
        const VSFrame* src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame* flow = vsapi->getFrameFilter(n, d->nodeFlow, frameCtx);
        const VSFrame* mask = vsapi->getFrameFilter(n, d->nodeMask, frameCtx);

        VSFrame* dst = vsapi->newVideoFrame(d->vi->format, d->vi->width, d->vi->height, src0, core);

        int width = d->vi->width;
        int height = d->vi->height;
        int flow_w = vsapi->getFrameWidth(flow, 0);
        int flow_h = vsapi->getFrameHeight(flow, 0);

        const float* r0 = reinterpret_cast<const float*>(vsapi->getReadPtr(src0, 0));
        const float* g0 = reinterpret_cast<const float*>(vsapi->getReadPtr(src0, 1));
        const float* b0 = reinterpret_cast<const float*>(vsapi->getReadPtr(src0, 2));

        const float* r1 = reinterpret_cast<const float*>(vsapi->getReadPtr(src1, 0));
        const float* g1 = reinterpret_cast<const float*>(vsapi->getReadPtr(src1, 1));
        const float* b1 = reinterpret_cast<const float*>(vsapi->getReadPtr(src1, 2));

        const float* flow_ptr = reinterpret_cast<const float*>(vsapi->getReadPtr(flow, 0));
        const float* mask_ptr = reinterpret_cast<const float*>(vsapi->getReadPtr(mask, 0));

        float* dst_r = reinterpret_cast<float*>(vsapi->getWritePtr(dst, 0));
        float* dst_g = reinterpret_cast<float*>(vsapi->getWritePtr(dst, 1));
        float* dst_b = reinterpret_cast<float*>(vsapi->getWritePtr(dst, 2));

        // Call hardware Vulkan bilinear compute shader warp
        bool ok = d->warper->warp(
            r0, g0, b0,
            r1, g1, b1,
            width, height,
            flow_ptr, flow_w, flow_h,
            mask_ptr,
            dst_r, dst_g, dst_b
        );

        vsapi->freeFrame(src0);
        vsapi->freeFrame(src1);
        vsapi->freeFrame(flow);
        vsapi->freeFrame(mask);

        return dst;
    }
    return nullptr;
}

static void VS_CC fastwarpFree(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    FastWarpData* d = static_cast<FastWarpData*>(instanceData);
    vsapi->freeNode(d->node0);
    vsapi->freeNode(d->node1);
    vsapi->freeNode(d->nodeFlow);
    vsapi->freeNode(d->nodeMask);
    delete d;
}

static void VS_CC fastwarpCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi) {
    auto d = std::make_unique<FastWarpData>();
    d->node0 = vsapi->mapGetNode(in, "clip0", 0, nullptr);
    d->node1 = vsapi->mapGetNode(in, "clip1", 0, nullptr);
    d->nodeFlow = vsapi->mapGetNode(in, "flow", 0, nullptr);
    d->nodeMask = vsapi->mapGetNode(in, "mask", 0, nullptr);
    d->vi = vsapi->getVideoInfo(d->node0);

    d->warper = std::make_unique<VulkanWarper>();
    if (!d->warper->init(0)) {
        vsapi->mapSetError(out, "FastWarp: Failed to initialize Vulkan compute device.");
        return;
    }

    VSFilterDependency deps[] = {
        { d->node0, rpStrictSpatial },
        { d->node1, rpStrictSpatial },
        { d->nodeFlow, rpStrictSpatial },
        { d->nodeMask, rpStrictSpatial }
    };

    vsapi->createVideoFilter(out, "Warp", d->vi, fastwarpGetFrame, fastwarpFree, fmParallel, deps, 4, d.release(), core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
    vspapi->configPlugin("com.skycine.fastwarp", "fastwarp", "SkyCine Hardware Vulkan Warper", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Warp", "clip0:vnode;clip1:vnode;flow:vnode;mask:vnode;", "clip:vnode;", fastwarpCreate, nullptr, plugin);
}
