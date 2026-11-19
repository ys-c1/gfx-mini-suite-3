float srgb_to_linear(float v);
float linear_to_srgb(float v);
int brightness_shift(const unsigned char* rgb,int n,float factor,unsigned char* out);
