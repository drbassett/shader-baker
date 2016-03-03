struct StringReference
{
	void *ptr;
};

inline StringSlice stringReferenceDeref(StringReference stringRef)
{
	auto pStringLength = (size_t*) stringRef.ptr;
	auto stringLength = *pStringLength;

	StringSlice result = {};
	result.begin = (char*) (pStringLength + 1);
	result.end = result.begin + stringLength;
	return result;
}

inline bool operator==(StringReference lhs, StringReference rhs)
{
	return stringReferenceDeref(lhs) == stringReferenceDeref(rhs);
}

inline bool operator!=(StringReference lhs, StringReference rhs)
{
	return !(lhs == rhs);
}

