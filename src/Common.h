#pragma once

#define arrayLength(array) sizeof(array) / sizeof(array[0])
#define memStackPushType(mem, type) (type*) memStackPush(mem, sizeof(type))
#define memStackPushArray(mem, type, size) (type*) memStackPush(mem, (size) * sizeof(type))
#define unreachable() assert(false)

struct MemStack
{
	u8 *begin, *top, *end;
};

struct MemStackMarker
{
	u8 *p;
};

struct StringSlice
{
	char *begin, *end;
};

/// Represents a string with a size and characters packed together in memory contiguously
struct PackedString
{
	void *ptr;
};

struct FilePath
{
	StringSlice path;
};

