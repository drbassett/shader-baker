#include "StringSlice.h"

struct Version
{
	unsigned major, minor;
};

struct Parser
{
	char *cursor;
	char *end;

	unsigned lineNumber;
	char *lineBegin;

	Version version;

	char *nextElementBegin;
	char *elementsEnd;

	LoaderError *nextErrorSlot;
	LoaderError *errorsEnd;
};

static inline TextDocumentLocation getParserLocation(Parser const& parser)
{
	TextDocumentLocation result;
	result.charNumber = (unsigned) (parser.cursor - parser.lineBegin + 1);
	result.lineNumber = parser.lineNumber;
	return result;
}

static inline void incrementParserLineNumber(Parser& parser)
{
	++parser.lineNumber;
	parser.lineBegin = parser.cursor;
}

static void addLoaderError(Parser& parser, LoaderErrorType const& error)
{
	if (parser.nextErrorSlot != parser.errorsEnd)
	{
		parser.nextErrorSlot = {};
		parser.nextErrorSlot->type = error;
		parser.nextErrorSlot->location = getParserLocation(parser);
		++parser.nextErrorSlot;
	}
}

inline static void* reserveElementMemory(Parser& parser, size_t size)
{
	auto remainingSize = (size_t) parser.elementsEnd - (size_t) parser.nextElementBegin;
	if (remainingSize < size)
	{
//TODO use a dynamically-size structure for elements, and grow it here
		return nullptr;
	}

	auto pElement = parser.nextElementBegin;
	parser.nextElementBegin += size;
	return pElement;
}

inline static void* reserveElementSlot(Parser& parser, ElementType elementType, size_t elementSize)
{
	auto requiredSize = sizeof(elementType) + elementSize;
	auto memory = reserveElementMemory(parser, requiredSize);
	if (memory == nullptr)
	{
		return nullptr;
	}

	auto pElementType = (ElementType*) memory;
	*pElementType = elementType;
	auto pElement = pElementType + 1;
	return pElement;
}

static ShaderElement* addShaderElement(Parser& parser)
{
	auto elementSlot = (ShaderElement*) reserveElementSlot(
		parser, ElementType::Shader, sizeof(ShaderElement));
	if (elementSlot == nullptr)
	{
		return nullptr;
	}

	*elementSlot = {};
	return elementSlot;
}

static ProgramElement* addProgramElement(Parser& parser)
{
	auto elementSlot = (ProgramElement*) reserveElementSlot(
		parser, ElementType::Program, sizeof(ProgramElement));
	if (elementSlot == nullptr)
	{
		return nullptr;
	}

	*elementSlot = {};
	return elementSlot;
}

static StringToken* addAttachedShader(Parser& parser)
{
	auto elementSlot = (StringToken*) reserveElementMemory(parser, sizeof(StringToken));
	if (elementSlot == nullptr)
	{
		return nullptr;
	}

	*elementSlot = {};
	return elementSlot;
}

static bool addRenderConfigElement(Parser& parser, RenderConfigElement const& renderConfig)
{
	auto elementSlot = (RenderConfigElement*) reserveElementSlot(
		parser, ElementType::RenderConfig, sizeof(renderConfig));
	if (elementSlot == nullptr)
	{
		return false;
	}

	*elementSlot = renderConfig;
	return true;
}

static inline bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

static inline bool isLowercase(char c)
{
	return c >= 'a' && c <= 'z';
}

static inline bool isUppercase(char c)
{
	return c >= 'A' && c <= 'Z';
}

static inline void skipNextCharacter(Parser& parser, char c)
{
	if (parser.cursor != parser.end && *parser.cursor == c)
	{
		++parser.cursor;
	}
}

static void skipWhitespace(Parser& parser)
{
	while (parser.cursor != parser.end)
	{
		switch (*parser.cursor)
		{
		case '\n':
			++parser.cursor;
			skipNextCharacter(parser, '\r');
			incrementParserLineNumber(parser);
			break;
		case '\r':
			++parser.cursor;
			skipNextCharacter(parser, '\n');
			incrementParserLineNumber(parser);
			break;
		case ' ':
		case '\t':
			++parser.cursor;
			break;
		default:
			return;
		}
	}
}

static bool readUint(Parser& parser, unsigned& result)
{
	char *begin = parser.cursor;
	result = 0;
	while (parser.cursor != parser.end && isDigit(*parser.cursor))
	{
//TODO check for overflow
		result = result * 10 + (*parser.cursor - '0');
		++parser.cursor;
	}
	return parser.cursor != begin;
}

static inline bool isWordCharacter(char c)
{
	return isLowercase(c) || isUppercase(c) || isDigit(c) || c == '_';
}

static StringSlice readWord(Parser& parser)
{
	StringSlice result = {};
	result.begin = parser.cursor;
	while (parser.cursor != parser.end
		&& isWordCharacter(*parser.cursor))
	{
		++parser.cursor;
	}
	result.end = parser.cursor;
	return result;
}

static bool endBlock(Parser& parser)
{
	skipWhitespace(parser);
	if (parser.cursor == parser.end || *parser.cursor != '}')
	{
		addLoaderError(parser, LoaderErrorType::UnclosedBlock);
		return false;
	}
	++parser.cursor;
	skipWhitespace(parser);
	return true;
}

// Reads a type, then begins a block immediately following it. Result is
// the type that's read.
static bool beginNamedBlock(Parser& parser, StringSlice& result)
{
	result = readWord(parser);
	if (result.begin == result.end)
	{
		addLoaderError(parser, LoaderErrorType::MissingBlockType);
		return false;
	}
	skipWhitespace(parser);

	if (parser.cursor == parser.end)
	{
		addLoaderError(parser, LoaderErrorType::MissingBlockBegin);
		return false;
	}

	if (*parser.cursor != '{')
	{
		addLoaderError(parser, LoaderErrorType::InvalidWordCharacter);
		return false;
	}

	++parser.cursor;
	skipWhitespace(parser);
	return true;
}

// Reads a block containing a single word. This word may be empty.
static bool readSingletonBlock(Parser& parser, StringSlice& result)
{
	result = readWord(parser);
	return endBlock(parser);
}

// Reads a block containing a single path token
static bool readPathBlock(Parser& parser, StringSlice& result)
{
	if (parser.cursor == parser.end || *parser.cursor != '\'')
	{
		addLoaderError(parser, LoaderErrorType::MissingPathBegin);
		return false;
	}
	++parser.cursor;

	result.begin = parser.cursor;

	for (;;)
	{
		// call skipWhitespace() to keep the line number up-to-date
		skipWhitespace(parser);

		if (parser.cursor == parser.end)
		{
			addLoaderError(parser, LoaderErrorType::UnclosedPath);
			return false;
		}

		char *current = parser.cursor;
		++parser.cursor;

		if (*current == '\'')
		{
			if (parser.cursor == parser.end)
			{
				addLoaderError(parser, LoaderErrorType::UnclosedBlock);
				return false;
			}

			if (*parser.cursor == '\'')
			{
				// two single-quotes in a row - this is an escape sequence
				++parser.cursor;
			} else
			{
				result.end = current;
				return endBlock(parser);
			}
		}
	}
}

// Reads the next word from a tuple block. Returns false if there was an
// error in the input, true if all is well. In the case that there is no
// error, the result will  contain the next word, or a null string slice
// if at the end of the tuple.
static bool readNextTupleWord(Parser& parser, StringSlice& result)
{
	result = {};

	if (parser.cursor == parser.end)
	{
		addLoaderError(parser, LoaderErrorType::UnclosedBlock);
		return false;
	}
	if (*parser.cursor == '}')
	{
		++parser.cursor;
		return true;
	}

	result = readWord(parser);
	skipWhitespace(parser);
	
	if (parser.cursor == parser.end)
	{
		addLoaderError(parser, LoaderErrorType::UnclosedBlock);
		return false;
	}

	switch (*parser.cursor)
	{
	case ',':
		++parser.cursor;
		skipWhitespace(parser);
		if (result.begin == result.end)
		{
			addLoaderError(parser, LoaderErrorType::EmptyTupleWord);
			return false;
		}
		return true;
	case '}':
		// consume this brace on the next call to this method
		return true;
	default:
		addLoaderError(parser, LoaderErrorType::InvalidWordCharacter);
		return false;
	}
}

// Advance the parser past the end of the current block
static bool skipBlock(Parser& parser)
{
	unsigned numBracesToClose = 1;

	for (;;)
	{
		if (parser.cursor == parser.end)
		{
			addLoaderError(parser, LoaderErrorType::UnclosedBlock);
			return false;
		}

		char *c = parser.cursor;
		++parser.cursor;

		if (*c == '{')
		{
			++numBracesToClose;
		} else if (*c == '}')
		{
			--numBracesToClose;
			if (numBracesToClose == 0)
			{
				return true;
			}
		}

		// call skipWhitespace() to keep the line number up-to-date
		skipWhitespace(parser);
	}
}

static bool readVersionStatement(Parser& parser)
{
	StringSlice type;
	if (!beginNamedBlock(parser, type))
	{
		return false;
	}

	if (type != "Version")
	{
		addLoaderError(parser, LoaderErrorType::MissingVersionStatement);
		return false;
	}

	if (!readUint(parser, parser.version.major))
	{
		addLoaderError(parser, LoaderErrorType::MissingMajorVersion);
		return false;
	}

	if (parser.cursor == parser.end || *parser.cursor != '.')
	{
		addLoaderError(parser, LoaderErrorType::VersionMissingDot);
		return false;
	}
	++parser.cursor;

	if (!readUint(parser, parser.version.minor))
	{
		addLoaderError(parser, LoaderErrorType::MissingMinorVersion);
		return false;
	}

	if (parser.version.major != 0 || parser.version.minor != 1)
	{
		addLoaderError(parser, LoaderErrorType::UnsupportedVersion);
		return false;
	}
	
	if (!endBlock(parser))
	{
		return false;
	}
		
	return true;
}

bool stringToDrawPrimitive(StringSlice string, DrawPrimitive& result)
{
	if (string == "Points")
	{
		result = DrawPrimitive::Points;
	} else if (string == "Lines")
	{
		result = DrawPrimitive::Lines;
	} else if (string == "LineStrip")
	{
		result = DrawPrimitive::LineStrip;
	} else if (string == "LineLoop")
	{
		result = DrawPrimitive::LineLoop;
	} else if (string == "Triangles")
	{
		result = DrawPrimitive::Triangles;
	} else if (string == "TriangleStrip")
	{
		result = DrawPrimitive::TriangleStrip;
	} else if (string == "TriangleFan")
	{
		result = DrawPrimitive::TriangleFan;
	} else
	{
		return false;
	}

	return true;
}

static bool readRenderConfigElement(Parser& parser, StringToken nameToken)
{
	RenderConfigElement result = {};
	result.nameToken = nameToken;

	bool hasProgramBlock = false;
	bool hasPrimitiveBlock = false;
	bool hasCountBlock = false;
	for (;;)
	{
		if (parser.cursor == parser.end)
		{
			addLoaderError(parser, LoaderErrorType::UnclosedBlock);
			return false;
		}

		if (*parser.cursor == '}')
		{
			++parser.cursor;
			skipWhitespace(parser);
			goto exitLoop;
		}

		StringSlice type;
		if (!beginNamedBlock(parser, type))
		{
			return false;
		}

		if (type == "Program")
		{
			if (hasProgramBlock)
			{
				addLoaderError(parser, LoaderErrorType::RenderConfigMultiplePrograms);
			}
			hasProgramBlock = true;

			result.programNameToken.location = getParserLocation(parser);
			if (!readSingletonBlock(parser, result.programNameToken.value))
			{
				return false;
			}
			if (isStringSliceEmpty(result.programNameToken.value))
			{
				addLoaderError(parser, LoaderErrorType::RenderConfigEmptyProgramName);
			}
		} else if (type == "Primitive")
		{
			if (hasPrimitiveBlock)
			{
				addLoaderError(parser, LoaderErrorType::RenderConfigMultiplePrimitives);
			}
			hasPrimitiveBlock = true;

			StringSlice primitiveName;
			if (!readSingletonBlock(parser, primitiveName))
			{
				return false;
			}

			if (!stringToDrawPrimitive(primitiveName, result.primitive))
			{
				addLoaderError(parser, LoaderErrorType::UnknownDrawPrimitive);
			}
		} else if (type == "Count")
		{
			if (hasCountBlock)
			{
				addLoaderError(parser, LoaderErrorType::RenderConfigMultipleCounts);
			}
			hasCountBlock = true;

			if (!readUint(parser, result.drawCount))
			{
				addLoaderError(parser, LoaderErrorType::RenderConfigEmptyOrInvalidCount);
				return false;
			}

			if (!endBlock(parser))
			{
				return false;
			}
		} else
		{
			addLoaderError(parser, LoaderErrorType::UnexpectedBlockType);
			return false;
		}
	}

exitLoop:
	if (!hasProgramBlock)
	{
		addLoaderError(parser, LoaderErrorType::RenderConfigMissingProgram);
	}
	if (!hasPrimitiveBlock)
	{
		addLoaderError(parser, LoaderErrorType::RenderConfigMissingPrimitive);
	}
	if (!hasCountBlock)
	{
		addLoaderError(parser, LoaderErrorType::RenderConfigMissingCount);
	}

	if (!addRenderConfigElement(parser, result))
	{
		addLoaderError(parser, LoaderErrorType::ExceededMaxRenderConfigCount);
		return false;
	}

	return true;
}

static bool readShaderElement(Parser& parser, StringToken nameToken, ShaderType type)
{
	StringToken pathToken;
	pathToken.location = getParserLocation(parser);
	if (!readPathBlock(parser, pathToken.value))
	{
		return false;
	}

	ShaderElement* shader = addShaderElement(parser);
	if (shader == nullptr)
	{
		addLoaderError(parser, LoaderErrorType::ExceededMaxShaderCount);
		return false;
	}

	shader->nameToken = nameToken;
	shader->type = type;
	shader->pathToken = pathToken;

	return true;
}

void parse(Parser& parser)
{
	skipWhitespace(parser);
	if (!readVersionStatement(parser))
	{
		return;
	}

	while (parser.cursor != parser.end)
	{
		StringToken identifierToken;
		identifierToken.location = getParserLocation(parser);
		identifierToken.value = readWord(parser);
		if (stringSliceLength(identifierToken.value) == 0)
		{
			addLoaderError(parser, LoaderErrorType::MissingIdentifier);
			return;
		}
		skipWhitespace(parser);

		if (parser.cursor == parser.end || !isWordCharacter(*parser.cursor))
		{
			addLoaderError(parser, LoaderErrorType::InvalidWordCharacter);
			return;
		}

		StringSlice type;
		if (!beginNamedBlock(parser, type))
		{
			return;
		}
		
//TODO maybe use a hash map rather than an 'if' cascade
		if (type == "VertexShader")
		{
			if (!readShaderElement(parser, identifierToken, ShaderType::Vertex))
			{
				return;
			}
		} else if (type == "TessControlShader")
		{
			if (!readShaderElement(parser, identifierToken, ShaderType::TessControl))
			{
				return;
			}
		} else if (type == "TessEvalShader")
		{
			if (!readShaderElement(parser, identifierToken, ShaderType::TessEval))
			{
				return;
			}
		} else if (type == "GeometryShader")
		{
			if (!readShaderElement(parser, identifierToken, ShaderType::Geometry))
			{
				return;
			}
		} else if (type == "FragmentShader")
		{
			if (!readShaderElement(parser, identifierToken, ShaderType::Fragment))
			{
				return;
			}
		} else if (type == "ComputeShader")
		{
			if (!readShaderElement(parser, identifierToken, ShaderType::Compute))
			{
				return;
			}
		} else if (type == "Program")
		{
			ProgramElement* program = addProgramElement(parser);
			if (program == nullptr)
			{
				addLoaderError(parser, LoaderErrorType::ExceededMaxProgramCount);
				return;
			}

			program->nameToken = identifierToken;

			for (;;)
			{
				StringToken shaderNameToken;
				shaderNameToken.location = getParserLocation(parser);
				if (!readNextTupleWord(parser, shaderNameToken.value))
				{
					return;
				}

				if (shaderNameToken.value.begin == 0)
				{
					break;
				}

				auto shaderNameTokenSlot = addAttachedShader(parser);
				if (shaderNameTokenSlot == nullptr)
				{
					addLoaderError(parser, LoaderErrorType::ExceededMaxAttachedShaderCount);
					return;
				}

				*shaderNameTokenSlot = shaderNameToken;
				++program->attachedShaderCount;
			}
		} else if (type == "RenderConfig")
		{
			if (!readRenderConfigElement(parser, identifierToken))
			{
				return;
			}
		} else
		{
			addLoaderError(parser, LoaderErrorType::UnexpectedBlockType);
			if (!skipBlock(parser))
			{
				return;
			}
		}
		skipWhitespace(parser);
	}
}

