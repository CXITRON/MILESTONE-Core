#pragma once
#include <stdlib.h>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
inline void *heap_caps_calloc(size_t n,size_t size,int){return BusMock::noMemory?nullptr:calloc(n,size);}
