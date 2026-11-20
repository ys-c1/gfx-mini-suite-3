#ifndef STATE_H
#define STATE_H

/* Legacy GSF1 record: magic + 16-byte tag + int frames + double metric. */
int state_save(const char* path, const char* tag, int frames, double metric);
int state_load(const char* path, char* tag, int* frames, double* metric);

/*
 * Extended GSF2 record: appends two float crosstalk metrics after the
 * GSF1 layout. crosstalk_dL is the mean |delta L| leak of the saturation
 * channel, crosstalk_dC the mean |delta C| leak of the lightness channel.
 */
int state_save_ext(const char* path, const char* tag, int frames, double metric,
                   float crosstalk_dL, float crosstalk_dC);
int state_load_ext(const char* path, char* tag, int* frames, double* metric,
                   float* crosstalk_dL, float* crosstalk_dC);
#endif
