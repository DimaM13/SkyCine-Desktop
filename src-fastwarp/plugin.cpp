#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <VapourSynth4.h>
#include <VSHelper4.h>
#include "vulkan_warper.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

struct FastWarpData {
    VSNode* node0;
    VSNode* node1;
    VSNode* nodeFlow;
    const VSVideoInfo* vi;
    std::unique_ptr<VulkanWarper> warper;
    float time_step;
};

static const VSFrame* VS_CC fastwarpGetFrame(int n, int activationReason, void* instanceData, void** frameData, VSFrameContext* frameCtx, VSCore* core, const VSAPI* vsapi) {
    FastWarpData* d = static_cast<FastWarpData*>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node0, frameCtx);
        vsapi->requestFrameFilter(n, d->node1, frameCtx);
        vsapi->requestFrameFilter(n, d->nodeFlow, frameCtx);
        return nullptr;
    } else if (activationReason == arAllFramesReady) {
        const VSFrame* src0 = vsapi->getFrameFilter(n, d->node0, frameCtx);
        const VSFrame* src1 = vsapi->getFrameFilter(n, d->node1, frameCtx);
        const VSFrame* flow = vsapi->getFrameFilter(n, d->nodeFlow, frameCtx);

        if (!src0 || !src1 || !flow) return nullptr;

        VSFrame* dst = vsapi->newVideoFrame(&d->vi->format, d->vi->width, d->vi->height, src0, core);

        int flow_w = vsapi->getFrameWidth(flow, 0);
        int flow_h = vsapi->getFrameHeight(flow, 0);
        ptrdiff_t flow_stride = vsapi->getStride(flow, 0) / sizeof(float);

        const float* flow_p0 = reinterpret_cast<const float*>(vsapi->getReadPtr(flow, 0));
        const float* flow_p1 = reinterpret_cast<const float*>(vsapi->getReadPtr(flow, 1));
        const VSVideoFormat* flow_fmt = vsapi->getVideoFrameFormat(flow);
        const float* flow_p2 = (flow_fmt->numPlanes > 2) ? reinterpret_cast<const float*>(vsapi->getReadPtr(flow, 2)) : nullptr;

        for (int p = 0; p < d->vi->format.numPlanes; p++) {
            int pw = vsapi->getFrameWidth(src0, p);
            int ph = vsapi->getFrameHeight(src0, p);
            ptrdiff_t s_stride = vsapi->getStride(src0, p);
            ptrdiff_t d_stride = vsapi->getStride(dst, p);

            const uint8_t* s0 = vsapi->getReadPtr(src0, p);
            const uint8_t* s1 = vsapi->getReadPtr(src1, p);
            uint8_t* d_ptr = vsapi->getWritePtr(dst, p);

            bool ok = d->warper->warp_plane(
                s0, s1, d_ptr,
                pw, ph, s_stride, d_stride,
                flow_p0, flow_p1, flow_p2,
                flow_w, flow_h, flow_stride,
                d->time_step
            );

            if (!ok) {
                vsapi->setFilterError("FastWarp: Vulkan GPU execution failed during frame dispatch", frameCtx);
                vsapi->freeFrame(dst);
                vsapi->freeFrame(src0);
                vsapi->freeFrame(src1);
                vsapi->freeFrame(flow);
                return nullptr;
            }
        }

        vsapi->freeFrame(src0);
        vsapi->freeFrame(src1);
        vsapi->freeFrame(flow);

        return dst;
    }
    return nullptr;
}

static void VS_CC fastwarpFree(void* instanceData, VSCore* core, const VSAPI* vsapi) {
    FastWarpData* d = static_cast<FastWarpData*>(instanceData);
    if (d) {
        if (d->node0) vsapi->freeNode(d->node0);
        if (d->node1) vsapi->freeNode(d->node1);
        if (d->nodeFlow) vsapi->freeNode(d->nodeFlow);
        delete d;
    }
}

static void VS_CC fastwarpCreate(const VSMap* in, VSMap* out, void* userData, VSCore* core, const VSAPI* vsapi) {
    int err = 0;
    VSNode* node0 = vsapi->mapGetNode(in, "clip0", 0, &err);
    if (err || !node0) {
        vsapi->mapSetError(out, "FastWarp: Failed to get clip0");
        return;
    }

    VSNode* node1 = vsapi->mapGetNode(in, "clip1", 0, &err);
    if (err || !node1) {
        vsapi->freeNode(node0);
        vsapi->mapSetError(out, "FastWarp: Failed to get clip1");
        return;
    }

    VSNode* nodeFlow = vsapi->mapGetNode(in, "flow", 0, &err);
    if (err || !nodeFlow) {
        vsapi->freeNode(node0);
        vsapi->freeNode(node1);
        vsapi->mapSetError(out, "FastWarp: Failed to get flow");
        return;
    }

    float time_step = (float)vsapi->mapGetFloat(in, "time_step", 0, &err);
    if (err) time_step = 0.5f;

    int gpu_id = static_cast<int>(vsapi->mapGetInt(in, "gpu_id", 0, &err));
    if (err) gpu_id = 0;

    const VSVideoInfo* vi_src = vsapi->getVideoInfo(node0);
    const VSVideoInfo* vi_flow = vsapi->getVideoInfo(nodeFlow);

    // Strictly enforce Vulkan GPU Compute Shader (No CPU fallback)
    auto warper = std::make_unique<VulkanWarper>();
    if (!warper->init(gpu_id, vi_src->width, vi_src->height, vi_flow->width, vi_flow->height)) {
        vsapi->freeNode(node0);
        vsapi->freeNode(node1);
        vsapi->freeNode(nodeFlow);
        vsapi->mapSetError(out, "FastWarp: Failed to initialize Vulkan GPU Compute device! Vulkan-capable GPU is strictly required.");
        return;
    }

    auto d = std::make_unique<FastWarpData>();
    d->node0 = node0;
    d->node1 = node1;
    d->nodeFlow = nodeFlow;
    d->vi = vi_src;
    d->warper = std::move(warper);
    d->time_step = time_step;

    vsapi->createVideoFilter(out, "Warp", vi_src, fastwarpGetFrame, fastwarpFree, fmParallel, nullptr, 0, d.release(), core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
#ifdef _WIN32
    HMODULE hMod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)VapourSynthPluginInit2, &hMod);
#endif
    vspapi->configPlugin("com.skycine.fastwarp", "fastwarp", "SkyCine GPU Vulkan Flow Warper", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Warp", "clip0:vnode;clip1:vnode;flow:vnode;time_step:float:opt;gpu_id:int:opt;", "clip:vnode;", fastwarpCreate, nullptr, plugin);
}
