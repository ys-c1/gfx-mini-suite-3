#ifndef OKLAB_H
#define OKLAB_H

/* ---- sRGB component helpers (legacy pipeline, kept unchanged) ---- */
float srgb_to_linear(float v);
float linear_to_srgb(float v);

/*
 * Legacy single-channel entry: multiplicative linear-light gain.
 * rgb/out hold n component values (any length). Kept verbatim as the
 * original overall adjustment entry. Note that a linear RGB gain moves
 * lightness and chroma together, which is the coupling being replaced.
 */
int brightness_shift(const unsigned char* rgb, int n, float factor, unsigned char* out);

/* ---- Oklab / Oklch poles of the decoupled pipeline ---- */
typedef struct { float L, a, b; } Oklab;
typedef struct { float L, C, h; } Oklch;

Oklab srgb_to_oklab(unsigned char r, unsigned char g, unsigned char b);
void oklab_to_srgb(Oklab c, unsigned char rgb[3]);
Oklch oklab_to_lch(Oklab c);
Oklab lch_to_oklab(Oklch c);

/*
 * Two-channel decoupled adjustment over n RGB triplets (3*n bytes each).
 *
 * dL  : additive Oklch lightness delta. Only the L coordinate is touched.
 *       0.0 means no lightness change.
 * sat : relative Oklch chroma change. Only the C coordinate is touched,
 *       using scale = 1 + sat. 0.0 means no saturation change; negative
 *       values desaturate; sat <= -1 maps to a scale of 0 (full gray).
 *
 * When dL == 0 and sat == 0 the input is copied to the output byte for
 * byte, so zero parameters restore every value exactly.
 *
 * Optional residuals (NULL allowed), measured in Oklch on the quantized
 * 8-bit output and averaged over pixels:
 *   resid_dL = mean |L_out - L_in|, resid_dC = mean |C_out - C_in|.
 * Crosstalk of the lightness channel is resid_dC measured with sat = 0;
 * crosstalk of the saturation channel is resid_dL measured with dL = 0.
 *
 * Returns 0 on success, -1 on bad arguments.
 */
int tone_chroma_adjust(const unsigned char* rgb, int n, float dL, float sat,
                       unsigned char* out, float* resid_dL, float* resid_dC);

#endif
