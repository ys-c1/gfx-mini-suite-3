#ifndef STATE_H
#define STATE_H
int state_save(const char* path, const char* tag, int frames, double metric);
int state_load(const char* path, char* tag, int* frames, double* metric);
#endif
