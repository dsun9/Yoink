#ifndef POINTY_TEST_H
#define POINTY_TEST_H
#include <stdio.h>
#include <stdlib.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#endif
