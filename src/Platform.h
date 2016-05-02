#pragma once

#include "ShaderBaker.h"

void* PLATFORM_alloc(size_t size);
bool PLATFORM_free(void* memory);
u8* PLATFORM_readWholeFile(MemoryStack& stack, StringSlice const fileName);

