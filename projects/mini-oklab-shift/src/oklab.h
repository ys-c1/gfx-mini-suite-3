#ifndef OKLAB_H
#define OKLAB_H

#ifdef __cplusplus
extern "C" {
#endif

/* sRGB transfer functions (existing, unchanged). */
float srgb_to_linear(float v);
float linear_to_srgb(float v);

/*
 * Legacy per-channel clipping path (existing behaviour, kept bit-for-bit:
 * same expression order, same truncation, same clamp).
 */
int brightness_shift(const unsigned char* rgb, int n, float factor,
                     unsigned char* out);

/*
 * Per-call report for brightness_shift_gamut().
 *
 * All "delta" figures are per-channel 8-bit code units, new output minus
 * legacy output.  "out_of_gamut" counts pixels whose linear target had any
 * channel above 1.0.  "changed" counts pixels that differ from the legacy
 * path (a subset of out_of_gamut: in-gamut pixels are copied verbatim).
 *
 * "max_delta"      largest |delta| actually produced in this call (8-bit
 *                  code units, per channel).
 * "max_delta_bound" upper bound for THIS image, derived from the values
 *                  the call actually produced: max over out-of-gamut
 *                  channels of 255*(linear_to_srgb(leg) -
 *                  linear_to_srgb(mixed)), +1 for truncation, capped by
 *                  the global span 255*(1-linear_to_srgb(1-strength))+1
 *                  and by 255.  The sRGB encoding is monotone, so
 *                  max_delta <= max_delta_bound holds by construction.
 * "max_de"         largest Oklab dE (x100) between new and legacy output,
 *                  over changed pixels.
 * Hue figures compare output hue with the hue of the ideal (unclipped)
 * target.  Samples need chroma >= OKLAB_HUE_MIN_CHROMA in both, so pure
 * greys and neutrals do not contribute noise.
 * "legacy_hue_*" are the same measurements for the legacy path, for
 * side-by-side comparison.
 * "oog_hue_max_deg" is the headline number: worst hue drift among pixels
 * that actually left the gamut (oog_hue_samples counts those samples).
 */
typedef struct gamut_stats {
    int    components;   /* n, as passed in */
    int    pixels;       /* n/3 */
    int    out_of_gamut;
    int    changed;
    int    max_delta;
    double max_delta_bound;
    double max_de;
    int    hue_samples;
    double hue_mean_deg;
    double hue_max_deg;
    double legacy_hue_mean_deg;
    double legacy_hue_max_deg;
    int    oog_hue_samples;
    double oog_hue_max_deg;
} gamut_stats;

/*
 * Hue-preserving gamut pullback.
 *
 * n is a component count, exactly as in brightness_shift(): rgb and out
 * each hold n bytes, interpreted as n/3 RGB pixels, so n must be a
 * multiple of 3 (this is validated, unlike in brightness_shift()).
 *
 * The linear-RGB target is computed exactly as brightness_shift() computes
 * it: srgb_to_linear(input/255)*factor per channel.  Pixels whose target
 * stays inside [0,1]^3 are copied through untouched, so their bytes are
 * identical to the legacy path (this is checked byte-for-byte in tests).
 *
 * For a pixel that leaves the cube, the whole triple is scaled by
 * 1/max(r,g,b) so its dominant channel lands exactly on 1.0.  That is a
 * positive scalar times the target, i.e. the colour slides along the black-
 * to-target ray.  Oklab is homogeneous in linear RGB (LMS is linear in
 * RGB, cbrt scales by k^(1/3), and L, a, b are linear in those cube
 * roots), so the whole (L,a,b) triple scales by k^(1/3): hue atan2(b,a)
 * is exactly unchanged and the chroma/lightness ratio is unchanged.  Only
 * lightness and chroma scale, and the result is in the cube by
 * construction.  The invariance is exact in continuous colour; the only
 * residual drift is the 8-bit quantization of the output (which the
 * hue_* statistics report, and which is a few tenths of a degree).
 *
 * strength in [0,1] blends that radial pullback with the legacy per-channel
 * clamp: 0 reproduces the legacy output exactly, 1 is the full hue-preserving
 * pullback, intermediate values trade hue accuracy for smaller deviations.
 * Both endpoints are inside the cube, so any blend is representable.
 *
 * out and rgb may be the same buffer (in-place use is supported);
 * stats may be NULL.
 *
 * Deterministic edge cases:
 *   all black      -> tmax=0, in gamut, copied verbatim ([0,0,0])
 *   all white      -> each channel = factor; scalar multiple of (1,1,1).
 *                     Radial pullback returns (1,1,1) at any factor; legacy
 *                     also clamps to (1,1,1).  No visible change.
 *   one channel at 1.0 but another over -> if tmax > 1 the whole triple is
 *                     scaled, so the saturated primary (255,0,0) is already
 *                     on the cube edge and stays (255,0,0); a colour with
 *                     two saturated channels such as (0,255,255) is pulled
 *                     back along its ray.  No division by zero is possible:
 *                     the out-of-gamut branch implies tmax > 1.
 *
 * Returns 0 on success; -1 for NULL rgb/out, n<=0, n not a multiple of 3,
 * factor <= 0 or non-finite, or strength outside [0,1] or NaN.
 */
int brightness_shift_gamut(const unsigned char* rgb, int n, float factor,
                           float strength, unsigned char* out,
                           gamut_stats* stats);

/* --- Oklab helpers, used by callers/tests to measure hue and colour. --- */

/* Linear sRGB -> Oklab. */
void linear_rgb_to_oklab(const float lin[3], float lab[3]);

/* 8-bit sRGB -> Oklab. */
void srgb8_to_oklab(const unsigned char rgb[3], float lab[3]);

/* Oklab hue in degrees [0,360); NAN when chroma < OKLAB_HUE_MIN_CHROMA. */
float oklab_hue_deg(const float lab[3]);

/* Shortest angular difference in [0,180]; NAN if either argument is NAN. */
float oklab_hue_diff_deg(float h1, float h2);

/* sqrt(dL^2+da^2+db^2) in Oklab units (multiply by 100 for dE*100). */
float oklab_delta_e(const float lab1[3], const float lab2[3]);

#define OKLAB_HUE_MIN_CHROMA 1.0e-4f

#ifdef __cplusplus
}
#endif

#endif /* OKLAB_H */
