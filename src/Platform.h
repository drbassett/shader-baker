#pragma once

#include "ShaderBaker.h"

enum class ReadFileError
{
	/// The file does not exist
	FileNotFound,

	/// The file is in use by another process
	FileInUse,

	/// The file cannot be accessed.
	///
	/// On Windows, this could be because it is pending deletion, or
	/// the current user has insufficient permissions to access it.
	AccessDenied,

	/// Some other error occured. File reading routines should be
	/// able to catch more specific errors, but we can't trust
	/// OS documentation to give us all the possible errors.
	Other,
};

void* PLATFORM_alloc(size_t size);
bool PLATFORM_free(void* memory);
void PLATFORM_readWholeFile(MemStack&, FilePath const, ReadFileError&, u8*& fileContents, size_t& fileSize);

