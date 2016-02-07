//TODO Need to distinguish between soft and hard parsing errors. For example,
//     a type missing the start of block will likely make the rest of the file
//     impossible to parse, so we should stop parsing immediately (a hard error).
//     However, a type that is unrecognized by the parser can be easily ignored
//     by skipping its block. Currently, the parser returns simple boolean values
//     for failure conditions, which do not have information to distinguish
//     between these cases.

//TODO Clean up skipWhitespace() calls.

//TODO Don't forget, the parser leaves extra single quotes inside paths, so
//     the path parser needs to advance 2 characters when it encounters a
//     single quote

#include <cassert>
#include <cstdio>
#include <cstdlib>

#define ArrayLength(a) sizeof(a) / sizeof(a[0])
#define ArrayEnd(a) (a) + sizeof(a) / sizeof(a[0])

struct StringSlice
{
	char *begin, *end;
};

struct Version
{
	unsigned major, minor;
};

enum struct ShaderType
{
	Vertex,
	TessControl,
	TessEval,
	Geometry,
	Fragment,
	Compute,
};

struct ShaderDefinition
{
	StringSlice name;
	ShaderType type;
	StringSlice path;
};

struct ProgramDefinition
{
	StringSlice name;
	StringSlice *shadersBegin;
	StringSlice *shadersEnd;
};

struct RenderConfig
{
	StringSlice name;
	StringSlice programName;
	StringSlice primitive;
	unsigned drawCount;
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

struct ParseResult
{
	Version version;
	
	ShaderDefinition *shadersBegin;
	ShaderDefinition *shadersEnd;

	ProgramDefinition *programsBegin;
	ProgramDefinition *programsEnd;

	RenderConfig *renderConfigsBegin;
	RenderConfig *renderConfigsEnd;

	ParseError *errorsBegin;
	ParseError *errorsEnd;
};

struct ApplicationState
{
	char rawContents[1 << 16];
	size_t rawContentsLength;

	ShaderDefinition shaders[1024];
	ProgramDefinition programs[1024];

	// storage for the names of shaders attached to a program
	StringSlice attachedShaderNames[4096];

	RenderConfig renderConfigs[1024];

	// 64 errors should be plenty for a person to deal with at once
	ParseError parseErrors[64];
};

inline void unreachable()
{
	assert(false);
}

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

inline unsigned getParserCharNumber(Parser const& parser)
{
	return (unsigned) (parser.cursor - parser.lineBegin + 1);
}

inline void incrementParserLineNumber(Parser& parser)
{
	++parser.lineNumber;
	parser.lineBegin = parser.cursor;
}

void addParseError(Parser& parser, ParseErrorType const& error)
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

bool addShaderDefinition(Parser& parser, ShaderDefinition const& shader)
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

bool addProgramDefinition(Parser& parser, ProgramDefinition const& program)
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

void initAttachedShaders(Parser& parser, ProgramDefinition& program)
{
	program.shadersBegin = parser.nextAttachedShaderSlot;
	program.shadersEnd = parser.nextAttachedShaderSlot;
}

bool attachShaderToProgram(Parser& parser, ProgramDefinition& program, StringSlice shaderName)
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

bool addRenderConfig(Parser& parser, RenderConfig const& renderConfig)
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

inline bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

inline bool isLowercase(char c)
{
	return c >= 'a' && c <= 'z';
}

inline bool isUppercase(char c)
{
	return c >= 'A' && c <= 'Z';
}

inline void skipNextCharacter(Parser& parser, char c)
{
	assert(parser.cursor != parser.end);

	if (parser.cursor != parser.end && *parser.cursor == c)
	{
		++parser.cursor;
	}
}

void skipWhitespace(Parser& parser)
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

bool readUint(Parser& parser, unsigned& result)
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

inline bool isWordCharacter(char c)
{
	return isLowercase(c) || isUppercase(c) || isDigit(c) || c == '_';
}

StringSlice readWord(Parser& parser)
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

bool beginBlock(Parser& parser)
{
	if (parser.cursor == parser.end || *parser.cursor != '{')
	{
		addParseError(parser, ParseErrorType::MissingBlockBegin);
		return false;
	}
	++parser.cursor;
	skipWhitespace(parser);
	return true;
}

bool endBlock(Parser& parser)
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
bool beginNamedBlock(Parser& parser, StringSlice& result)
{
	result = readWord(parser);
	if (result.begin == result.end)
	{
		addParseError(parser, ParseErrorType::MissingBlockType);
		return false;
	}
	skipWhitespace(parser);

	if (parser.cursor == parser.end || *parser.cursor != '{')
	{
		addParseError(parser, ParseErrorType::InvalidWordCharacter);
		return false;
	}

	++parser.cursor;
	skipWhitespace(parser);
	return true;
}

// Reads a block containing a single word. This word may be empty.
bool readSingletonBlock(Parser& parser, StringSlice& result)
{
	result = readWord(parser);
	return endBlock(parser);
}

// Reads a block containing a single path token
bool readPathBlock(Parser& parser, StringSlice& result)
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
bool readNextTupleWord(Parser& parser, StringSlice& result)
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
bool skipBlock(Parser& parser)
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

bool readVersionStatement(Parser& parser, Version& result)
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

	if (!readUint(parser, result.major))
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

	if (!readUint(parser, result.minor))
	{
		addParseError(parser, ParseErrorType::MissingMinorVersion);
		return false;
	}

	if (result.major != 0 || result.minor != 1)
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

bool readRenderConfig(Parser& parser, StringSlice name)
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

bool readShaderDefinition(Parser& parser, StringSlice name, ShaderType type)
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

ParseResult parse(ApplicationState& appState)
{
	Parser parser = {};
	parser.cursor = appState.rawContents;
	parser.end = appState.rawContents + appState.rawContentsLength;
	parser.nextShaderSlot = appState.shaders;
	parser.shadersEnd = ArrayEnd(appState.shaders);
	parser.nextProgramSlot = appState.programs;
	parser.programsEnd = ArrayEnd(appState.programs);
	parser.nextAttachedShaderSlot = appState.attachedShaderNames;
	parser.attachedShadersEnd = ArrayEnd(appState.attachedShaderNames);
	parser.nextRenderConfigSlot = appState.renderConfigs;
	parser.renderConfigsEnd = ArrayEnd(appState.renderConfigs);
	parser.nextErrorSlot = appState.parseErrors;
	parser.errorsEnd = ArrayEnd(appState.parseErrors);
	parser.lineNumber = 1;
	parser.lineBegin = parser.cursor;

	ParseResult result = {};

	skipWhitespace(parser);
	if (!readVersionStatement(parser, result.version))
	{
		goto returnResult;
	}

	while (parser.cursor != parser.end)
	{
		auto identifier = readWord(parser);
		if (identifier.begin == identifier.end)
		{
			addParseError(parser, ParseErrorType::MissingIdentifier);
			goto returnResult;
		}
		skipWhitespace(parser);

		if (parser.cursor == parser.end || !isWordCharacter(*parser.cursor))
		{
			addParseError(parser, ParseErrorType::InvalidWordCharacter);
			goto returnResult;
		}

		StringSlice type;
		if (!beginNamedBlock(parser, type))
		{
			goto returnResult;
		}
		
//TODO maybe use a hash map rather than an 'if' cascade
		if (type == "VertexShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::Vertex))
			{
				goto returnResult;
			}
		} else if (type == "TessControlShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::TessControl))
			{
				goto returnResult;
			}
		} else if (type == "TessEvalShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::TessEval))
			{
				goto returnResult;
			}
		} else if (type == "GeometryShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::Geometry))
			{
				goto returnResult;
			}
		} else if (type == "FragmentShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::Fragment))
			{
				goto returnResult;
			}
		} else if (type == "ComputeShader")
		{
			if (!readShaderDefinition(parser, identifier, ShaderType::Compute))
			{
				goto returnResult;
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
					goto returnResult;
				}

				if (shaderName.begin == 0)
				{
					break;
				}
				
				if (!attachShaderToProgram(parser, program, shaderName))
				{
					addParseError(parser, ParseErrorType::ExceededMaxAttachedShaderCount);
					goto returnResult;
				}
			}

			if (!addProgramDefinition(parser, program))
			{
				addParseError(parser, ParseErrorType::ExceededMaxProgramCount);
				goto returnResult;
			}
		} else if (type == "RenderConfig")
		{
			if (!readRenderConfig(parser, identifier))
			{
				goto returnResult;
			}
		} else
		{
			addParseError(parser, ParseErrorType::UnexpectedBlockType);
			if (!skipBlock(parser))
			{
				goto returnResult;
			}
		}
		skipWhitespace(parser);
	}

returnResult:
	result.shadersBegin = appState.shaders;
	result.shadersEnd = parser.nextShaderSlot;
	result.programsBegin = appState.programs;
	result.programsEnd = parser.nextProgramSlot;
	result.renderConfigsBegin = appState.renderConfigs;
	result.renderConfigsEnd = parser.nextRenderConfigSlot;
	result.errorsBegin = appState.parseErrors;
	result.errorsEnd = parser.nextErrorSlot;
	return result;
}

char* parseErrorTypeToStr(ParseErrorType errorType)
{
	switch (errorType)
	{
	case ParseErrorType::MissingVersionStatement:
		return "The Version statement must be the first one";
	case ParseErrorType::MissingMajorVersion:
		return "Missing major version";
	case ParseErrorType::MissingMinorVersion:
		return "Missing minor version";
	case ParseErrorType::VersionMissingDot:
		return "Expected '.' character after major version";
	case ParseErrorType::UnsupportedVersion:
		return "Version is unsupported by this version of Shader Baker";
	case ParseErrorType::MissingBlockBegin:
		return "Missing start of block";
	case ParseErrorType::UnclosedBlock:
		return "Unclosed block";
	case ParseErrorType::MissingPathBegin:
		return "Missing start of path";
	case ParseErrorType::UnclosedPath:
		return "Unclosed path";
	case ParseErrorType::MissingIdentifier:
		return "Statement must begin with an identifier";
	case ParseErrorType::MissingBlockType:
		return "Statement type must follow identifier";
	case ParseErrorType::EmptyTupleWord:
		return "Tuple contains an empty word";
	case ParseErrorType::InvalidWordCharacter:
		return "Invalid character in word";
	case ParseErrorType::RenderConfigMissingProgram:
		return "Missing Program block in a RenderConfig block";
	case ParseErrorType::RenderConfigMultiplePrograms:
		return "Multiple Program blocks declared in a RenderConfig block";
	case ParseErrorType::RenderConfigMissingPrimitive:
		return "Missing Primitive block in a RenderConfig block";
	case ParseErrorType::RenderConfigMultiplePrimitives:
		return "Multiple Primitive blocks declared in a RenderConfig block";
	case ParseErrorType::RenderConfigMissingCount:
		return "Missing Count block in a RenderConfig block";
	case ParseErrorType::RenderConfigMultipleCounts:
		return "Multiple Count blocks declared in a RenderConfig block";
	case ParseErrorType::RenderConfigEmptyProgramName:
		return "Program name is empty";
	case ParseErrorType::RenderConfigEmptyOrInvalidCount:
		return "Count value is empty or invalid";
	case ParseErrorType::UnexpectedBlockType:
		return "Unexpected block type";
	case ParseErrorType::ExceededMaxShaderCount:
		return "Too many shaders!";
	case ParseErrorType::ExceededMaxProgramCount:
		return "Too many programs!";
	case ParseErrorType::ExceededMaxAttachedShaderCount:
		return "Too many attached shaders!";
	case ParseErrorType::ExceededMaxRenderConfigCount:
		return "Too many rendering configurations!";
	default:
		unreachable();
		return "";
	}
}

void printParseErrors(ParseError* begin, ParseError* end)
{
	while (begin != end)
	{
		printf(
			"ERROR %d (line %d, character %d): %s\n",
			(unsigned) begin->type,
			begin->lineNumber,
			begin->charNumber,
			parseErrorTypeToStr(begin->type));
		++begin;
	}
}

char* shaderTypeToStr(ShaderType type)
{
	switch (type)
	{
	case ShaderType::Vertex:
		return "Vertex";
	case ShaderType::TessControl:
		return "Tessalation Control";
	case ShaderType::TessEval:
		return "Tesselation Evaluation";
	case ShaderType::Geometry:
		return "Geometry";
	case ShaderType::Fragment:
		return "Fragment";
	case ShaderType::Compute:
		return "Compute";
	default:
		unreachable();
		return "";
	}
}

void printStringSlice(StringSlice str)
{
	char *c = str.begin;
	while (c != str.end)
	{
		putchar(*c);
		++c;
	}
}

void printParseResult(ParseResult& result)
{
	bool hasErrors = result.errorsBegin != result.errorsEnd;
	if (hasErrors)
	{
		printParseErrors(result.errorsBegin, result.errorsEnd);
	} else
	{
		printf("Parse Successful\n\n");
		auto version = result.version;
		printf("Version %d.%d\n", version.major, version.minor);

		printf("\n\nSHADERS:\n");
		{
			auto shader = result.shadersBegin;
			while (shader != result.shadersEnd)
			{
				printStringSlice(shader->name);
				printf(": %s shader at path \"", shaderTypeToStr(shader->type));
				printStringSlice(shader->path);
				fputs("\"\n", stdout);
				++shader;
			}
		}

		printf("\n\nPROGRAMS:\n");
		{
			auto program = result.programsBegin;
			while (program != result.programsEnd)
			{
				printStringSlice(program->name);
				fputs(": ", stdout);
				auto attachedShader = program->shadersBegin;
				while (attachedShader != program->shadersEnd)
				{
					printStringSlice(*attachedShader);
					fputs(", ", stdout);
					++attachedShader;
				}
				putchar('\n');
				++program;
			}
		}

		printf("\n\nRENDERING CONFIGURATIONS:\n");
		{
			auto config = result.renderConfigsBegin;
			while (config != result.renderConfigsEnd)
			{
				printStringSlice(config->name);
				printf(": renders count=%d ", config->drawCount);
				printStringSlice(config->primitive);
				fputs(" with program '", stdout);
				printStringSlice(config->programName);
				fputs("'\n", stdout);
				++config;
			}
		}
	}
}

static bool readProjectFile(ApplicationState& appState, char *fileName)
{
	auto file = fopen(fileName, "rb");
	if (!file)
	{
		perror("Failed to open project file\n");
		return false;
	}
	auto fileSize = fread(
		appState.rawContents,
		sizeof(appState.rawContents[0]),
		sizeof(appState.rawContents),
		file);
	appState.rawContentsLength = fileSize;
	auto readFileError = ferror(file);
	if (readFileError)
	{
		perror("Unable to read project file\n");
	} else 
	{
		readFileError = !feof(file);
		if (readFileError)
		{
			perror("Not enough memory to read input file\n");
		}
	}
	if (fclose(file) != 0)
	{
		perror("WARNING: failed to close project file\n");
	}
	if (readFileError)
	{
		return false;
	}

	return true;
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("Usage: sbc projectFile\n");
		return 0;
	}
	auto projectFileName = argv[1];

	auto appState = (ApplicationState*) malloc(sizeof(ApplicationState));

	auto result = 0;
	if (!readProjectFile(*appState, projectFileName))
	{
		result = 1;
		goto exit;
	}

	auto parseResult = parse(*appState);
	printParseResult(parseResult);

exit:
	free(appState);
	return result;
}
