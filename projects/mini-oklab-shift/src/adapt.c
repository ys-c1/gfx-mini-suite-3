#include "adapt.h"
#include "oklab.h"
#include <math.h>
#include <string.h>

/*
 * 基于参考白的 von Kries 色适应。
 *
 * 适应空间 = 线性光 sRGB（gamma 解码后的线性光域）。
 * 场景参考白在该空间的三通道为 (Wr,Wg,Wb)，目标白为等比 D65 白，
 * 于是通道增益为 1/W 的对角缩放：
 *
 *   lin'_c = gain_c * lin_c,  gain_c = blend(strength, 1, 1/W_c)
 *
 * 性质：
 *   - 任意等比中性灰 (g,g,g) 经全强度适应后仍为等比值，中性严格保持；
 *   - 参考白本身映射到 (1,1,1)；
 *   - strength=0 时增益全为 1，apply 直接 memcpy，逐值原样还原；
 *   - 纯对角缩放不引入跨通道混合，色相扰动小，色相偏移有上界保证。
 */

/* 由线性光 sRGB 参考白构建已混合强度的通道增益 */
static int build_gains_lin(adapt_ctx_t* ctx, const float wlin[3], float strength) {
    if (!ctx || !wlin) return ADAPT_E_ARG;
    if (!isfinite(strength) || strength < 0.0f || strength > 1.0f) return ADAPT_E_STRENGTH;

    int nonzero = 0;
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(wlin[i]) || wlin[i] < 0.0f) return ADAPT_E_WHITE;
        if (wlin[i] > 0.0f) nonzero = 1;
    }
    if (!nonzero) return ADAPT_E_WHITE; /* 全零参考白无法做除数 */

    memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < 3; ++i) {
        float gain = 1.0f;
        if (wlin[i] > 1e-8f) gain = 1.0f / wlin[i];
        /* 强度混合：0 -> 单位增益（恒等），1 -> 全适应增益 */
        ctx->m[i][i] = (1.0f - strength) * 1.0f + strength * gain;
    }
    ctx->strength = strength;
    return ADAPT_OK;
}

/* 由 sRGB 0..1 编码参考白构建已混合强度的通道增益 */
static int build_gains(adapt_ctx_t* ctx, const float ref_srgb[3], float strength) {
    if (!ctx || !ref_srgb) return ADAPT_E_ARG;
    float wlin[3];
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(ref_srgb[i]) || ref_srgb[i] < 0.0f || ref_srgb[i] > 1.0f)
            return ADAPT_E_WHITE;
        wlin[i] = srgb_to_linear(ref_srgb[i]);
    }
    int rc = build_gains_lin(ctx, wlin, strength);
    if (rc != ADAPT_OK) return rc;
    for (int i = 0; i < 3; ++i) {
        float v = ref_srgb[i] * 255.0f + 0.5f;
        ctx->ref[i] = (unsigned char)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    }
    return ADAPT_OK;
}

int adapt_init_linear(adapt_ctx_t* ctx, const float ref_linear[3], float strength) {
    int rc = build_gains_lin(ctx, ref_linear, strength);
    if (rc != ADAPT_OK) return rc;
    for (int i = 0; i < 3; ++i) {
        float v = linear_to_srgb(ref_linear[i]) * 255.0f + 0.5f;
        ctx->ref[i] = (unsigned char)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    }
    return ADAPT_OK;
}

int adapt_init_f(adapt_ctx_t* ctx, const float ref_srgb[3], float strength) {
    int rc = build_gains(ctx, ref_srgb, strength);
    if (rc != ADAPT_OK) return rc;
    for (int i = 0; i < 3; ++i) {
        float v = ref_srgb[i] * 255.0f + 0.5f;
        ctx->ref[i] = (unsigned char)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    }
    return ADAPT_OK;
}

int adapt_init8(adapt_ctx_t* ctx, const unsigned char ref8[3], float strength) {
    if (!ctx || !ref8) return ADAPT_E_ARG;
    if (ref8[0] == 0 && ref8[1] == 0 && ref8[2] == 0) return ADAPT_E_WHITE;
    float ref_srgb[3];
    for (int i = 0; i < 3; ++i) ref_srgb[i] = ref8[i] / 255.0f;
    int rc = build_gains(ctx, ref_srgb, strength);
    if (rc != ADAPT_OK) return rc;
    memcpy(ctx->ref, ref8, 3);
    return ADAPT_OK;
}

int adapt_apply_linear(const adapt_ctx_t* ctx, const float lin_in[3], float lin_out[3]) {
    if (!ctx || !lin_in || !lin_out) return ADAPT_E_ARG;
    if (ctx->strength == 0.0f) {
        lin_out[0] = lin_in[0];
        lin_out[1] = lin_in[1];
        lin_out[2] = lin_in[2];
        return ADAPT_OK;
    }
    lin_out[0] = ctx->m[0][0] * lin_in[0];
    lin_out[1] = ctx->m[1][1] * lin_in[1];
    lin_out[2] = ctx->m[2][2] * lin_in[2];
    return ADAPT_OK;
}

int adapt_apply8(const adapt_ctx_t* ctx, const unsigned char* rgb,
                 int npix, unsigned char* out) {
    if (!ctx || !rgb || !out || npix <= 0) return ADAPT_E_ARG;
    size_t bytes = (size_t)npix * 3u;
    /* 强度为零：逐值原样还原，不做任何算术 */
    if (ctx->strength == 0.0f) {
        memcpy(out, rgb, bytes);
        return ADAPT_OK;
    }
    float g0 = ctx->m[0][0], g1 = ctx->m[1][1], g2 = ctx->m[2][2];
    for (int p = 0; p < npix; ++p) {
        const unsigned char* in_px = rgb + 3 * p;
        unsigned char* out_px = out + 3 * p;
        float lin0 = srgb_to_linear(in_px[0] / 255.0f) * g0;
        float lin1 = srgb_to_linear(in_px[1] / 255.0f) * g1;
        float lin2 = srgb_to_linear(in_px[2] / 255.0f) * g2;
        float v0 = linear_to_srgb(lin0) * 255.0f + 0.5f;
        float v1 = linear_to_srgb(lin1) * 255.0f + 0.5f;
        float v2 = linear_to_srgb(lin2) * 255.0f + 0.5f;
        out_px[0] = (unsigned char)(v0 < 0.0f ? 0.0f : (v0 > 255.0f ? 255.0f : v0));
        out_px[1] = (unsigned char)(v1 < 0.0f ? 0.0f : (v1 > 255.0f ? 255.0f : v1));
        out_px[2] = (unsigned char)(v2 < 0.0f ? 0.0f : (v2 > 255.0f ? 255.0f : v2));
    }
    return ADAPT_OK;
}
