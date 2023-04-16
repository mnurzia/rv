typedef int size_t;

size_t strlen(const char* s) {
  size_t sz = 0;
  while (*s) {
    sz++;
  }
  return sz;
}

int rv_main(void) {
  return strlen("abcdef");
}
