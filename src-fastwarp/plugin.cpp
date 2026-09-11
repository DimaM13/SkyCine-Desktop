#include <VapourSynth4.h>
#include <VSHelper4.h>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>

struct FastWarpData {
    VSNode* node0;
    VSNode* node1;
    VSNode* nodeFlow;
    const VSVideoInfo* vi;
};

static inline void warp_plane_uint8(
    const uint8_t* src0, const uint8_t* src1, uint8_t* dst,
    int pw, int ph, ptrdiff_t s_stride, ptrdiff_t d_stride,
    const float* flow_p0, const float* flow_p1, const float* flow_p2,
    int flow_w, int flow_h, ptrdiff_t flow_stride
) {
    const float scale_x = (float)pw / (float)flow_w;
    const float scale_y = (float)ph / (float)flow_h;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < ph; y++) {
        float norm_y = ((float)y + 0.5f) / (float)ph;
        if (norm_y < 0.0f) norm_y = 0.0f;
        if (norm_y > 1.0f) norm_y = 1.0f;
        float fy_f = norm_y * (float)(flow_h - 1);
        int fy0 = (int)fy_f;
        if (fy0 > flow_h - 2) fy0 = flow_h - 2;
        int fy1 = fy0 + 1;
        float wy1 = fy_f - (float)fy0;
        float wy0 = 1.0f - wy1;

        uint8_t* out_row = dst + y * d_stride;

        for (int x = 0; x < pw; x++) {
            float norm_x = ((float)x + 0.5f) / (float)pw;
            if (norm_x < 0.0f) norm_x = 0.0f;
            if (norm_x > 1.0f) norm_x = 1.0f;
            float fx_f = norm_x * (float)(flow_w - 1);
            int fx0 = (int)fx_f;
            if (fx0 > flow_w - 2) fx0 = flow_w - 2;
            int fx1 = fx0 + 1;
            float wx1 = fx_f - (float)fx0;
            float wx0 = 1.0f - wx1;

            ptrdiff_t idx00 = (ptrdiff_t)fy0 * flow_stride + fx0;
            ptrdiff_t idx01 = (ptrdiff_t)fy0 * flow_stride + fx1;
            ptrdiff_t idx10 = (ptrdiff_t)fy1 * flow_stride + fx0;
            ptrdiff_t idx11 = (ptrdiff_t)fy1 * flow_stride + fx1;

            float dx = (flow_p0[idx00] * wx0 + flow_p0[idx01] * wx1) * wy0 +
                       (flow_p0[idx10] * wx0 + flow_p0[idx11] * wx1) * wy1;
            float dy = (flow_p1[idx00] * wx0 + flow_p1[idx01] * wx1) * wy0 +
                       (flow_p1[idx10] * wx0 + flow_p1[idx11] * wx1) * wy1;
            float mask = flow_p2 ? ((flow_p2[idx00] * wx0 + flow_p2[idx01] * wx1) * wy0 +
                                   (flow_p2[idx10] * wx0 + flow_p2[idx11] * wx1) * wy1) : 0.5f;

            float sx0 = (float)x + dx * scale_x;
            float sy0 = (float)y + dy * scale_y;
            float sx1 = (float)x - dx * scale_x;
            float sy1 = (float)y - dy * scale_y;

            if (sx0 < 0.0f) sx0 = 0.0f;
            if (sx0 > (float)(pw - 1)) sx0 = (float)(pw - 1);
            if (sy0 < 0.0f) sy0 = 0.0f;
            if (sy0 > (float)(ph - 1)) sy0 = (float)(ph - 1);

            int ix0 = (int)sx0;
            if (ix0 > pw - 2) ix0 = pw - 2;
            int iy0 = (int)sy0;
            if (iy0 > ph - 2) iy0 = ph - 2;
            float qx1 = sx0 - (float)ix0; float qx0 = 1.0f - qx1;
            float qy1 = sy0 - (float)iy0; float qy0 = 1.0f - qy1;

            const uint8_t* p0 = src0 + (ptrdiff_t)iy0 * s_stride + ix0;
            float c0 = ((float)p0[0] * qx0 + (float)p0[1] * qx1) * qy0 +
                       ((float)p0[s_stride] * qx0 + (float)p0[s_stride + 1] * qx1) * qy1;

            if (sx1 < 0.0f) sx1 = 0.0f;
            if (sx1 > (float)(pw - 1)) sx1 = (float)(pw - 1);
            if (sy1 < 0.0f) sy1 = 0.0f;
            if (sy1 > (float)(ph - 1)) sy1 = (float)(ph - 1);

            int ix1 = (int)sx1;
            if (ix1 > pw - 2) ix1 = pw - 2;
            int iy1 = (int)sy1;
            if (iy1 > ph - 2) iy1 = ph - 2;
            float rx1 = sx1 - (float)ix1; float rx0 = 1.0f - rx1;
            float ry1 = sy1 - (float)iy1; float ry0 = 1.0f - ry1;

            const uint8_t* p1 = src1 + (ptrdiff_t)iy1 * s_stride + ix1;
            float c1 = ((float)p1[0] * rx0 + (float)p1[1] * rx1) * ry0 +
                       ((float)p1[s_stride] * rx0 + (float)p1[s_stride + 1] * rx1) * ry1;

            if (mask < 0.0f) mask = 0.0f;
            if (mask > 1.0f) mask = 1.0f;
            float val = c0 * mask + c1 * (1.0f - mask);
            int ival = (int)(val + 0.5f);
            if (ival < 0) ival = 0;
            if (ival > 255) ival = 255;
            out_row[x] = (uint8_t)ival;
        }
    }
}

static inline void warp_plane_float(
    const float* src0, const float* src1, float* dst,
    int pw, int ph, ptrdiff_t s_stride, ptrdiff_t d_stride,
    const float* flow_p0, const float* flow_p1, const float* flow_p2,
    int flow_w, int flow_h, ptrdiff_t flow_stride
) {
    const float scale_x = (float)pw / (float)flow_w;
    const float scale_y = (float)ph / (float)flow_h;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < ph; y++) {
        float norm_y = ((float)y + 0.5f) / (float)ph;
        if (norm_y < 0.0f) norm_y = 0.0f;
        if (norm_y > 1.0f) norm_y = 1.0f;
        float fy_f = norm_y * (float)(flow_h - 1);
        int fy0 = (int)fy_f;
        if (fy0 > flow_h - 2) fy0 = flow_h - 2;
        int fy1 = fy0 + 1;
        float wy1 = fy_f - (float)fy0;
        float wy0 = 1.0f - wy1;

        float* out_row = dst + y * d_stride;

        for (int x = 0; x < pw; x++) {
            float norm_x = ((float)x + 0.5f) / (float)pw;
            if (norm_x < 0.0f) norm_x = 0.0f;
            if (norm_x > 1.0f) norm_x = 1.0f;
            float fx_f = norm_x * (float)(flow_w - 1);
            int fx0 = (int)fx_f;
            if (fx0 > flow_w - 2) fx0 = flow_w - 2;
            int fx1 = fx0 + 1;
            float wx1 = fx_f - (float)fx0;
            float wx0 = 1.0f - wx1;

            ptrdiff_t idx00 = (ptrdiff_t)fy0 * flow_stride + fx0;
            ptrdiff_t idx01 = (ptrdiff_t)fy0 * flow_stride + fx1;
            ptrdiff_t idx10 = (ptrdiff_t)fy1 * flow_stride + fx0;
            ptrdiff_t idx11 = (ptrdiff_t)fy1 * flow_stride + fx1;

            float dx = (flow_p0[idx00] * wx0 + flow_p0[idx01] * wx1) * wy0 +
                       (flow_p0[idx10] * wx0 + flow_p0[idx11] * wx1) * wy1;
            float dy = (flow_p1[idx00] * wx0 + flow_p1[idx01] * wx1) * wy0 +
                       (flow_p1[idx10] * wx0 + flow_p1[idx11] * wx1) * wy1;
            float mask = flow_p2 ? ((flow_p2[idx00] * wx0 + flow_p2[idx01] * wx1) * wy0 +
                                   (flow_p2[idx10] * wx0 + flow_p2[idx11] * wx1) * wy0) : 0.5f;

            float sx0 = (float)x + dx * scale_x;
            float sy0 = (float)y + dy * scale_y;
            float sx1 = (float)x - dx * scale_x;
            float sy1 = (float)y - dy * scale_y;

            if (sx0 < 0.0f) sx0 = 0.0f;
            if (sx0 > (float)(pw - 1)) sx0 = (float)(pw - 1);
            if (sy0 < 0.0f) sy0 = 0.0f;
            if (sy0 > (float)(ph - 1)) sy0 = (float)(ph - 1);

            int ix0 = (int)sx0;
            if (ix0 > pw - 2) ix0 = pw - 2;
            int iy0 = (int)sy0;
            if (iy0 > ph - 2) iy0 = ph - 2;
            float qx1 = sx0 - (float)ix0; float qx0 = 1.0f - qx1;
            float qy1 = sy0 - (float)iy0; float qy0 = 1.0f - qy1;

            const float* p0 = src0 + (ptrdiff_t)iy0 * s_stride + ix0;
            float c0 = (p0[0] * qx0 + p0[1] * qx1) * qy0 +
                       (p0[s_stride] * qx0 + p0[s_stride + 1] * qx1) * qy1;

            if (sx1 < 0.0f) sx1 = 0.0f;
            if (sx1 > (float)(pw - 1)) sx1 = (float)(pw - 1);
            if (sy1 < 0.0f) sy1 = 0.0f;
            if (sy1 > (float)(ph - 1)) sy1 = (float)(ph - 1);

            int ix1 = (int)sx1;
            if (ix1 > pw - 2) ix1 = pw - 2;
            int iy1 = (int)sy1;
            if (iy1 > ph - 2) iy1 = ph - 2;
            float rx1 = sx1 - (float)ix1; float rx0 = 1.0f - rx1;
            float ry1 = sy1 - (float)iy1; float ry0 = 1.0f - ry1;

            const float* p1 = src1 + (ptrdiff_t)iy1 * s_stride + ix1;
            float c1 = (p1[0] * rx0 + p1[1] * rx1) * ry0 +
                       (p1[s_stride] * rx0 + p1[s_stride + 1] * rx1) * ry1;

            if (mask < 0.0f) mask = 0.0f;
            if (mask > 1.0f) mask = 1.0f;
            out_row[x] = c0 * mask + c1 * (1.0f - mask);
        }
    }
}

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

            if (d->vi->format.sampleType == stInteger && d->vi->format.bytesPerSample == 1) {
                warp_plane_uint8(
                    s0, s1, d_ptr,
                    pw, ph, s_stride, d_stride,
                    flow_p0, flow_p1, flow_p2,
                    flow_w, flow_h, flow_stride
                );
            } else if (d->vi->format.sampleType == stFloat && d->vi->format.bytesPerSample == 4) {
                warp_plane_float(
                    reinterpret_cast<const float*>(s0),
                    reinterpret_cast<const float*>(s1),
                    reinterpret_cast<float*>(d_ptr),
                    pw, ph,
                    s_stride / sizeof(float),
                    d_stride / sizeof(float),
                    flow_p0, flow_p1, flow_p2,
                    flow_w, flow_h, flow_stride
                );
            }
        }

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
    FILE* logf = fopen("C:\\Users\\dimam\\fastwarp.log", "w");
    if (logf) { fprintf(logf, "fastwarpCreate entered\n"); fflush(logf); }

    int err = 0;
    VSNode* node0 = vsapi->mapGetNode(in, "clip0", 0, &err);
    if (logf) { fprintf(logf, "node0: %p, err: %d\n", node0, err); fflush(logf); }
    if (err || !node0) {
        vsapi->mapSetError(out, "FastWarp: Failed to get clip0");
        if (logf) fclose(logf);
        return;
    }

    VSNode* node1 = vsapi->mapGetNode(in, "clip1", 0, &err);
    if (logf) { fprintf(logf, "node1: %p, err: %d\n", node1, err); fflush(logf); }
    if (err || !node1) {
        vsapi->freeNode(node0);
        vsapi->mapSetError(out, "FastWarp: Failed to get clip1");
        if (logf) fclose(logf);
        return;
    }

    VSNode* nodeFlow = vsapi->mapGetNode(in, "flow", 0, &err);
    if (logf) { fprintf(logf, "nodeFlow: %p, err: %d\n", nodeFlow, err); fflush(logf); }
    if (err || !nodeFlow) {
        vsapi->freeNode(node0);
        vsapi->freeNode(node1);
        vsapi->mapSetError(out, "FastWarp: Failed to get flow");
        if (logf) fclose(logf);
        return;
    }

    const VSVideoInfo* vi_src = vsapi->getVideoInfo(node0);
    if (logf) { fprintf(logf, "vi_src: %p, width=%d, height=%d\n", vi_src, vi_src ? vi_src->width : 0, vi_src ? vi_src->height : 0); fflush(logf); }

    auto d = std::make_unique<FastWarpData>();
    d->node0 = node0;
    d->node1 = node1;
    d->nodeFlow = nodeFlow;
    d->vi = vi_src;

    if (logf) { fprintf(logf, "Calling createVideoFilter with vi_src and nullptr deps...\n"); fflush(logf); }
    vsapi->createVideoFilter(out, "Warp", vi_src, fastwarpGetFrame, fastwarpFree, fmParallel, nullptr, 0, d.release(), core);
    if (logf) { fprintf(logf, "createVideoFilter returned successfully!\n"); fclose(logf); }
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
    vspapi->configPlugin("com.skycine.fastwarp", "fastwarp", "SkyCine Bilinear Flow Warper", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Warp", "clip0:vnode;clip1:vnode;flow:vnode;", "clip:vnode;", fastwarpCreate, nullptr, plugin);
}
