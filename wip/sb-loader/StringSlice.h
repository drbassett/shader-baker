#pragma once

struct StringSlice
{
	char *begin, *end;
};

StringSlice stringSliceFromCStr(char *cStr)
{
	StringSlice str;
	str.begin = cStr;
	while (*cStr != 0)
	{
		++cStr;
	}
	str.end = cStr;
	return str;
}

inline size_t stringSliceLength(StringSlice str)
{
	return str.end - str.begin;
}

inline bool isStringSliceEmpty(StringSlice str)
{
	return str.end == str.begin;
}

bool operator==(StringSlice lhs, StringSlice rhs)
{
	if (stringSliceLength(lhs) != stringSliceLength(rhs))
	{
		return false;
	}

	for (;;)
	{
		if (lhs.begin == lhs.end)
		{
			return true;
		}

		if (*lhs.begin != *rhs.begin)
		{
			return false;
		}

		++lhs.begin;
		++rhs.begin;
	}
}

inline bool operator!=(StringSlice lhs, StringSlice rhs)
{
	return !(lhs == rhs);
}

bool operator==(StringSlice lhs, const char* rhs)
{
	for (;;)
	{
		bool rhsNull = *rhs == 0;
		if (lhs.begin == lhs.end)
		{
			return rhsNull;
		}

		if (rhsNull || *lhs.begin != *rhs)
		{
			return false;
		}

		++lhs.begin;
		++rhs;
	}
}

inline bool operator!=(StringSlice lhs, const char* rhs)
{
	return !(lhs == rhs);
}

