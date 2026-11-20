#ifndef ADAPT_H
#define ADAPT_H

/*
 * 基于参考白的色适应（von Kries 通道缩放）。
 *
 * 适应空间 = sRGB 解码后的线性光 RGB。按参考白做三通道对角增益
 * （参考白映射为等比 D65 白），再编码回 sRGB。
 * 强度 strength 在线性光域与单位增益混合：strength=0 时恒等，逐值原样还原。
 * 对角缩放严格保持中性：等比灰输入，全强度适应后仍是等比灰。
 */

/* 色卡整体色相偏移上界（度），测试与基准程序强制校验 */
#define ADAPT_HUE_BOUND_DEG 10.0f

#define ADAPT_OK          0
#define ADAPT_E_ARG      -1  /* 空指针 / 像素数非法 */
#define ADAPT_E_WHITE    -2  /* 参考白非法：全零、通道越界或非有限值 */
#define ADAPT_E_STRENGTH -3  /* 强度不在 [0,1] */

typedef struct {
    float m[3][3];              /* 已混合强度的线性 RGB 域变换矩阵 */
    float strength;             /* 0 时 apply 走直通拷贝 */
    unsigned char ref[3];       /* 配置的参考白（sRGB 0..255） */
} adapt_ctx_t;

/* 用 sRGB 0..255 的参考白初始化。返回 ADAPT_OK 或负错误码。 */
int adapt_init8(adapt_ctx_t* ctx, const unsigned char ref8[3], float strength);

/* 用 sRGB 0..1 编码值的参考白初始化（测试用）。 */
int adapt_init_f(adapt_ctx_t* ctx, const float ref_srgb[3], float strength);

/* 用线性光 RGB（0..1）参考白初始化（测试用）。 */
int adapt_init_linear(adapt_ctx_t* ctx, const float ref_linear[3], float strength);

/*
 * 对打包 RGB（npix 个像素，rgb/out 各 3*npix 字节）做色适应。
 * strength==0 时 out 与 rgb 逐字节相同。
 */
int adapt_apply8(const adapt_ctx_t* ctx, const unsigned char* rgb,
                 int npix, unsigned char* out);

/* 线性光 RGB 单像素变换（测试用），不做裁剪。 */
int adapt_apply_linear(const adapt_ctx_t* ctx, const float lin_in[3], float lin_out[3]);

#endif
