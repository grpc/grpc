
#if 0
int utf8_range2(const unsigned char* data, int len);
#else
int utf8_naive(const unsigned char* data, int len);
static inline int utf8_range2(const unsigned char* data, int len) {
  return utf8_naive(data, len);
}
#endif
