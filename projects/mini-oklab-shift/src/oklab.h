#ifndef OKLAB_H
#define OKLAB_H

/* INT_MAX: referenced by the oklab_shift() contract below. */
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Legacy interface.  FROZEN: do not change semantics or code paths.   */
/* This path multiplies linearized channels by a scalar and writes the */
/* result back through the sRGB transfer function.  Its byte-exact     */
/* output is pinned by tests/test_oklab_path.c.                        */
/* ------------------------------------------------------------------ */
float srgb_to_linear(float v);
float linear_to_srgb(float v);
int brightness_shift(const unsigned char* rgb,int n,float factor,unsigned char* out);

/* ------------------------------------------------------------------ */
/* Perceptually uniform (Oklab) path.                                  */
/*                                                                     */
/* Colour pipeline: sRGB 8-bit -> linear sRGB -> Oklab -> adjust L ->  */
/* inverse Oklab -> gamut-map chroma at constant hue -> sRGB 8-bit.    */
/* Oklab L is roughly perceptually uniform: adding a constant dL        */
/* produces the same apparent brightness change at every tone.         */
/* ------------------------------------------------------------------ */

/* Decode one 8-bit sRGB pixel (channels 0..255) to Oklab.
   lab = {L, a, b}; L is 0..1 for in-gamut input. */
void rgb8_to_oklab(int r,int g,int b,double lab[3]);

/* Oklab -> linear sRGB, WITHOUT gamut mapping.  Output may fall
   outside [0,1] for saturated colours.  L is not clamped here. */
void oklab_to_linear(const double lab[3],double lin[3]);

/* Oklab -> 8-bit sRGB.  L is clamped to [0,1].  If the colour leaves
   the sRGB gamut, chroma (a,b) is scaled toward the neutral axis while
   keeping the hue angle atan2(b,a) fixed, then the result is quantised
   with round-half-up.  Non-finite inputs are treated as neutral.
   Gamut clipping is not reversible: a colour lifted out of the sRGB
   gamut loses chroma permanently (see the swatch table in the tests). */
void oklab_to_rgb8(const double lab[3],unsigned char out[3]);

/* Shift perceived lightness: L' = clamp(L + delta_l, 0, 1), a and b
   untouched, then oklab_to_rgb8().  delta_l == 0 (including -0.0) is a
   value-exact identity (byte copy, no float round trip).  For delta_l
   != 0 the round trip is limited only by 8-bit quantisation; the
   unquantised double pipeline stays within 2.7e-7 in linear sRGB over
   all 2^24 colours.  Negative delta_l darkens and is valid.
   npix is the number of pixels; rgb/out hold 3*npix bytes.
   out may alias rgb.  Returns 0 on success.  Returns -1 and leaves out
   untouched if rgb/out is NULL, npix <= 0, npix > INT_MAX/3, or
   delta_l is NaN/Inf. */
int oklab_shift(const unsigned char* rgb,int npix,double delta_l,unsigned char* out);

#ifdef __cplusplus
}
#endif

#endif /* OKLAB_H */
