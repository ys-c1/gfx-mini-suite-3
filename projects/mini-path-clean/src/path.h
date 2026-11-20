#ifndef PATH_H
#define PATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Existing polyline helpers -- behaviour is unchanged. */
int path_remove_collinear(const float* pts, size_t n, float* out, size_t cap,
                          size_t* outn, float eps);
float path_length(const float* pts, size_t n);

/*
 * Curvature-adaptive resampling.
 *
 * `target` is a hard upper bound on the number of output points.  The
 * resampled polyline is a subsequence of the input vertices selected by
 * Douglas-Peucker-style recursive splitting with tolerance `max_dev`.
 * Because the chord approximation error of a bend grows with curvature
 * (error ~ kappa * chord^2 / 8), the splitter keeps many short chords in
 * sharp bends and long chords across straight runs; no separate density
 * knob is needed.  The result therefore satisfies
 *
 *     every point of the input polyline is within max_dev of the result
 *
 * (and `path_max_deviation` measures that bound).
 *
 * The result never introduces a self-intersection: if removing vertices
 * would make two non-adjacent output chords cross, input vertices are put
 * back until the output is simple again.
 *
 * Degenerate results:
 *   n == 0               -> success, *outn = 0, nothing written
 *   n == 1               -> success, *outn = 1 (the vertex is copied)
 *   fully straight input -> success, *outn = 2 (the two endpoints);
 *                           deviation is zero
 *   target == 0          -> -3, *outn = points required (1 if n == 1,
 *                           else 2); the caller allowed no points, so the
 *                           max_dev guarantee cannot be met
 *
 * Return codes:
 *    0  success (max_dev and the target cap are both satisfied)
 *   -1  invalid argument (null pointer, n > 0 with pts == NULL,
 *       negative/NaN max_dev)
 *   -2  output capacity or source-index capacity too small
 *   -3  `target` is below the point count needed for `max_dev`;
 *       *outn receives that required count and no points are written
 *   -5  a self-intersection-free result satisfying max_dev cannot be
 *       produced within `target` (for example the input itself crosses)
 *
 * `path_resample_map` additionally writes the input index of every output
 * point to `src_index` (which must hold at least `cap` entries) if it is
 * non-NULL.  `path_resample` is the same call without the index map.
 */
int path_resample(const float* pts, size_t n, float* out, size_t cap,
                  size_t target, float max_dev, size_t* outn);
int path_resample_map(const float* pts, size_t n, float* out, size_t cap,
                      size_t target, float max_dev, size_t* outn,
                      size_t* src_index);

/*
 * Maximum distance from the sampled points of the input polyline (every
 * vertex plus every edge midpoint) to the closest segment of `simp`.
 *
 * For a fixed output segment the distance is convex in the query point, so
 * the continuous maximum over each input edge is attained at one of its two
 * endpoints; sampling the midpoint can only lower the reported value.  A
 * resample with tolerance T matches every input vertex to an output segment
 * within T, hence reports a value <= T.  Returns HUGE_VALF when m == 0 and
 * n > 0, 0 when n == 0, and -1 for NULL pointers.
 */
float path_max_deviation(const float* orig, size_t n,
                         const float* simp, size_t m);

/*
 * 1 if two non-adjacent segments cross, 0 if the polyline is simple,
 * -1 for invalid arguments.  Segments that merely share an endpoint (for
 * example the first and last segment of a closed path) are not counted.
 */
int path_has_self_intersection(const float* pts, size_t n);

#ifdef __cplusplus
}
#endif

#endif
