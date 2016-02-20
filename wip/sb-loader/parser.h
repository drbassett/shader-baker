struct Version
{
	unsigned major, minor;
};

enum struct ParseErrorType
{
	MissingVersionStatement,
	MissingMajorVersion,
	MissingMinorVersion,
	VersionMissingDot,
	UnsupportedVersion,
	MissingBlockBegin,
	UnclosedBlock,
	MissingPathBegin,
	UnclosedPath,
	MissingIdentifier,
	MissingBlockType,
	EmptyTupleWord,
	InvalidWordCharacter,
	UnexpectedBlockType,
	ExceededMaxShaderCount,
	ExceededMaxProgramCount,
	ExceededMaxAttachedShaderCount,
	ExceededMaxRenderConfigCount,
	RenderConfigMissingProgram,
	RenderConfigMultiplePrograms,
	RenderConfigMissingPrimitive,
	RenderConfigMultiplePrimitives,
	RenderConfigMissingCount,
	RenderConfigMultipleCounts,
	RenderConfigEmptyProgramName,
	RenderConfigEmptyOrInvalidCount,
};

struct ParseError
{
	ParseErrorType type;
	unsigned lineNumber;
	unsigned charNumber;
};

struct Parser
{
	char *cursor;
	char *end;

	unsigned lineNumber;
	char *lineBegin;

	Version version;

	ShaderDefinition *nextShaderSlot;
	ShaderDefinition *shadersEnd;

	ProgramDefinition *nextProgramSlot;
	ProgramDefinition *programsEnd;

	StringSlice *nextAttachedShaderSlot;
	StringSlice *attachedShadersEnd;

	RenderConfig *nextRenderConfigSlot;
	RenderConfig *renderConfigsEnd;

	ParseError *nextErrorSlot;
	ParseError *errorsEnd;
};

static inline unsigned getParserCharNumber(Parser const& parser)
{
	return (unsigned) (parser.cursor - parser.lineBegin + 1);
}

static inline void incrementParserLineNumber(Parser& parser)
{
	++parser.lineNumber;
	parser.lineBegin = parser.cursor;
}

static void addParseError(Parser& parser, ParseErrorType const& error)
{
	if (parser.nextErrorSlot != parser.errorsEnd)
	{
		auto slot = parser.nextErrorSlot;
		slot->type = error;
		slot->lineNumber = parser.lineNumber;
		slot->charNumber = getParserCharNumber(parser);
		++parser.nextErrorSlot;
	}
}

static bool addShaderDefinition(Parser& parser, ShaderDefinition const& shader)
{
	if (parser.nextShaderSlot != parser.shadersEnd)
	{
		*parser.nextShaderSlot = shader;
		++parser.nextShaderSlot;
		return true;
	} else
	{
		return false;
	}
}

static bool addProgramDefinition(Parser& parser, ProgramDefinition const& program)
{
	if (parser.nextProgramSlot != parser.programsEnd)
	{
		*parser.nextProgramSlot = program;
		++parser.nextProgramSlot;
		return true;
	} else
	{
		return false;
	}
}

static void initAttachedShaders(Parser& parser, ProgramDefinition& program)
{
	program.shadersBegin = parser.nextAttachedShaderSlot;
	program.shadersEnd = parser.nextAttachedShaderSlot;
}

static bool attachShaderToProgram(Parser& parser, ProgramDefinition& program, StringSlice shaderName)
{
	if (parser.nextAttachedShaderSlot != parser.attachedShadersEnd)
	{
		*parser.nextAttachedShaderSlot = shaderName;
		++parser.nextAttachedShaderSlot;
		++program.shadersEnd;
		return true;
	} else
	{
		return false;
	}
}

static bool addRenderConfig(Parser& parser, RenderConfig const& renderConfig)
{
	if (parser.nextRenderConfigSlot != parser.renderConfigsEnd)
	{
		*parser.nextRenderConfigSlot = renderConfig;
		++parser.nextRenderConfigSlot;
		return true;
	} else
	{
		return false;
	}
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
	assert(parser.cursor != parser.end);

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
		addParseError(parser, ParseErrorType::UnclosedBlock);
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
		addParseError(parser, ParseErrorType::MissingBlockType);
		return false;
	}
	skipWhitespace(parser);

	if (parser.cursor == parser.end)
	{
		addParseError(parser, ParseErrorType::MissingBlockBegin);
		return false;
	}

	if (*parser.cursor != '{')
	{
		addParseError(parser, ParseErrorType::InvalidWordCharacter);
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
		addParseError(parser, ParseErrorType::MissingPathBegin);
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
			addParseError(parser, ParseErrorType::UnclosedPath);
			return false;
		}

		char *current = parser.cursor;
		++parser.cursor;

		if (*current == '\'')
		{
			if (parser.cursor == parser.end)
			{
				addParseError(parser, ParseErrorType::UnclosedBlock);
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
		addParseError(parser, ParseErrorType::UnclosedBlock);
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
		addParseError(parser, ParseErrorType::UnclosedBlock);
		return false;
	}

	switch (*parser.cursor)
	{
	case ',':
		++parser.cursor;
		skipWhitespace(parser);
		if (result.begin == result.end)
		{
			addParseError(parser, ParseErrorType::EmptyTupleWord);
			return false;
		}
		return true;
	case '}':
		// consume this brace on the next call to this method
		return true;
	default:
		addParseError(parser, ParseErrorType::InvalidWordCharacter);
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
			addParseError(parser, ParseErrorType::UnclosedBlock);
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
		addParseError(parser, ParseErrorType::MissingVersionStatement);
		return false;
	}

	if (!readUint(parser, parser.version.major))
	{
		addParseError(parser, ParseErrorType::MissingMajorVersion);
		return false;
	}

	if (parser.cursor == parser.end || *parser.cursor != '.')
	{
		addParseError(parser, ParseErrorType::VersionMissingDot);
		return false;
	}
	++parser.cursor;

	if (!readUint(parser, parser.version.minor))
	{
		addParseError(parser, ParseErrorType::MissingMinorVersion);
		return false;
	}

	if (parser.version.major != 0 || parser.version.minor != 1)
	{
		addParseError(parser, ParseErrorType::UnsupportedVersion);
		return false;
	}
	
	if (!endBlock(parser))
	{
		return false;
	}
		
	return true;
}

static bool readRenderConfig(Parser& parser, StringSlice name)
{
	RenderConfig result = {};
	result.name = name;

	bool hasProgramBlock = false;
	bool hasPrimitiveBlock = false;
	bool hasCountBlock = false;
	for (;;)
	{
		if (parser.cursor == parser.end)
		{
			addParseError(parser, ParseErrorType::UnclosedBlock);
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
				addParseError(parser, ParseErrorType::RenderConfigMultiplePrograms);
			}
			hasProgramBlock = true;

			if (!readSingletonBlock(parser, result.programName))
			{
				return false;
			}
			if (isStringSliceEmpty(result.programName))
			{
				addParseError(parser, ParseErrorType::RenderConfigEmptyProgramName);
			}
		} else if (type == "Primitive")
		{
			if (hasPrimitiveBlock)
			{
				addParseError(parser, ParseErrorType::RenderConfigMultiplePrimitives);
			}
			hasPrimitiveBlock = true;

			if (!readSingletonBlock(parser, result.primitive))
			{
				return false;
			}
		} else if (type == "Count")
		{
			if (hasCountBlock)
			{
				addParseError(parser, ParseErrorType::RenderConfigMultipleCounts);
			}
			hasCountBlock = true;

			if (!readUint(parser, result.drawCount))
			{
				addParseError(parser, ParseErrorType::RenderConfigEmptyOrInvalidCount);
				return false;
			}
			if (!endBlock(parser))
			{
				return false;
			}
		} else
		{
			addParseError(parser, ParseErrorType::UnexpectedBlockType);
			return false;
		}
	}

exitLoop:
	if (!hasProgramBlock)
	{
		addParseError(parser, ParseErrorType::RenderConfigMissingProgram);
	}
	if (!hasPrimitiveBlock)
	{
		addParseError(parser, ParseErrorType::RenderConfigMissingPrimitive);
	}
	if (!hasCountBlock)
	{
		addParseError(parser, ParseErrorType::RenderConfigMissingCount);
	}

	if (!addRenderConfig(parser, result))
	{
		addParseError(parser, ParseErrorType::ExceededMaxRenderConfigCount);
		return false;
	} else
	{
		return true;
	}
}

static bool readShaderDefinition(Parser& parser, StringSlice name, ShaderType type)
{
	StringSlice path;
	if (!readPathBlock(parser, path))
	{
		return false;
	}

	ShaderDefinition shader = {};
	shader.name = name;
	shader.type = type;
	shader.path = path;
	if (!addShaderDefinition(parser, shader))
	{
		addParseError(parser, ParseErrorType::ExceededMaxShaderCount);
		return false;
	}

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
		auto identifier = readWord(parser);
		if (identifier.begin == identifier.end)
		{
			addParseError(parser, ParseErrorType::MissingIdentifier);
			return;
		}
		skipWhitespace(parser);

		if (parser.cursor == parser.end || !isWordCharacter(*parser.cursor))
		{
			addParseError(parser, ParseErrorType::InvalidWordCharacter);
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
			if (!readShaderDefinition(parser, identifier, ShaderType::Vertex))
			{
				return;
			}
		} else if (type == "TessControlShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::TessControl))
			{
				return;
			}
		} else if (type == "TessEvalShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::TessEval))
			{
				return;
			}
		} else if (type == "GeometryShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::Geometry))
			{
				return;
			}
		} else if (type == "FragmentShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::Fragment))
			{
				return;
			}
		} else if (type == "ComputeShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::Compute))
			{
				return;
			}
		} else if (type == "Program")
		{
			ProgramDefinition program = {};
			program.name = identifier;
			initAttachedShaders(parser, program);
			for (;;)
			{
				StringSlice shaderName;
				if (!readNextTupleWord(parser, shaderName))
				{
					return;
				}

				if (shaderName.begin == 0)
				{
					break;
				}
				
				if (!attachShaderToProgram(parser, program, shaderName))
				{
					addParseError(parser, ParseErrorType::ExceededMaxAttachedShaderCount);
					return;
				}
			}

			if (!addProgramDefinition(parser, program))
			{
				addParseError(parser, ParseErrorType::ExceededMaxProgramCount);
				return;
			}
		} else if (type == "RenderConfig")
		{
			if (!readRenderConfig(parser, identifier))
			{
				return;
			}
		} else
		{
			addParseError(parser, ParseErrorType::UnexpectedBlockType);
			if (!skipBlock(parser))
			{
				return;
			}
		}
		skipWhitespace(parser);
	}
}
