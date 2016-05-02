#pragma once

#include "ShaderBaker.h"

void* PLATFORM_alloc(size_t size);
bool PLATFORM_free(void* memory);
u8* PLATFORM_readWholeFile(MemoryStack&, FilePath const fileName, size_t& fileSize);

