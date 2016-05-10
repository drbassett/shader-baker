#include "Common.h"

#include <cassert>

bool memStackInit(MemStack& stack, size_t capacity)
{
	assert(capacity > 0);
	
	auto memory = (u8*) PLATFORM_alloc(capacity);
	if (memory == nullptr)
	{
		return false;
	}

	stack.begin = memory;
	stack.top = memory;
	stack.end = memory + capacity;
	return true;
}

//TODO memory alignment
inline void* memStackPush(MemStack& mem, size_t size)
{
	size_t remainingSize = mem.end - mem.top;
//TODO figure out how to "gracefully crash" the application if memory runs out
	assert(size <= remainingSize);
	auto result = mem.top;
 	mem.top += size;
	return result;
}

inline MemStackMarker memStackMark(MemStack const& mem)
{
	return MemStackMarker{mem.top};
}

inline void memStackPop(MemStack& mem, MemStackMarker marker)
{
	assert(marker.p >= mem.begin && marker.p < mem.end);
	mem.top = marker.p;
}

inline void memStackClear(MemStack& mem)
{
	mem.top = mem.begin;
}

/// Finds the length of a C string, excluding the null terminator
/// Examples: "" -> 0, "abc123" -> 6
inline size_t cStringLength(char *c)
{
	auto end = c;
	while (*end != '\0')
	{
		++end;
	}
	return end - c;
}

inline StringSlice stringSliceFromCString(char *cstr)
{
	StringSlice result = {};
	result.begin = cstr;
	for (;;)
	{
		auto c = *cstr;
		if (c == 0)
		{
			break;
		}
		++cstr;
	}
	result.end = cstr;
	return result;
}

inline size_t stringSliceLength(StringSlice str)
{
	return str.end - str.begin;
}

bool operator==(StringSlice lhs, StringSlice rhs)
{
	if (stringSliceLength(lhs) != stringSliceLength(rhs))
	{
		return false;
	}
	while (lhs.begin != lhs.end)
	{
		if (*lhs.begin != *rhs.begin)
		{
			return false;
		}
		++lhs.begin;
		++rhs.begin;
	}
	return true;
}

inline bool operator!=(StringSlice lhs, StringSlice rhs)
{
	return !(lhs == rhs);
}

bool operator==(StringSlice lhs, const char* rhs)
{
	auto pLhs = lhs.begin;
	auto pRhs = rhs;
	for (;;)
	{
		auto lhsEnd = pLhs == lhs.end;
		auto rhsEnd = *pRhs == '\0';
		if (rhsEnd)
		{
			return lhsEnd;
		}

		if (lhsEnd)
		{
			return false;
		}

		if (*pLhs != *pRhs)
		{
			return false;
		}

		++pLhs;
		++pRhs;
	}
}

bool operator!=(StringSlice lhs, const char* rhs)
{
	return !(lhs == rhs);
}

inline PackedString packString(MemStack& mem, StringSlice str)
{
	size_t stringLength = stringSliceLength(str);
	auto ptr = memStackPush(mem, sizeof(stringLength) + stringLength);
	auto sizePtr = (size_t*) ptr;
	auto charPtr = (char*) (sizePtr + 1);
	*sizePtr = stringLength;
	memcpy(charPtr, str.begin, stringLength);
	return PackedString{ptr};
}

inline StringSlice unpackString(PackedString str)
{
	auto sizePtr = (size_t*) str.ptr;
	auto begin = (char*) (sizePtr + 1);
	auto end = begin + (*sizePtr);
	return StringSlice{begin, end};
}

void u32ToString(MemStack& mem, u32 value, char*& result, u32& length)
{
	// 10 characters is large enough to hold any 32 bit integer
	result = memStackPushArray(mem, char, 10);
	auto resultEnd = result;
	// This loop always has to execute at least once.
	// Otherwise, nothing gets printed for zero.
	do
	{
		*resultEnd = (value % 10) + '0';
		value /= 10;
		++resultEnd;
	} while (value > 0);
	length = (u32) (resultEnd - result);

	// reverse the string
	u32 lo = 0;
	u32 hi = length - 1;
	while (lo < hi)
	{
		char tmp = result[lo];
		result[lo] = result[hi];
		result[hi] = tmp;
		++lo;
		--hi;
	}

	// "deallocate" the extra space
	mem.top = (u8*) resultEnd;
}

