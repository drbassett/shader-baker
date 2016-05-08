#include "ProjectLoader.h"

//TODO consider restricting the available characters for identifiers

inline static TextLocation parserTextLocation(ProjectParser& parser, char *pChar)
{
	auto charNumber = (u32) (pChar - parser.lineBegin + 1);
	return TextLocation{pChar, parser.lineNumber, charNumber};
}

inline static TextLocation parserTextLocation(ProjectParser& parser)
{
	return parserTextLocation(parser, parser.cursor);
}

inline static void addError(
	MemStack& mem,
	ProjectParser& parser,
	TextLocation location,
	ParseProjectErrorType errorType)
{
	auto error = memStackPushType(mem, ParseProjectError);
	error->type = errorType;
	error->location = location;
	error->next = parser.errors;
	parser.errors = error;
}

inline static void addError(
	MemStack& mem,
	ProjectParser& parser,
	char *pChar,
	ParseProjectErrorType errorType)
{
	addError(mem, parser, parserTextLocation(parser, pChar), errorType);
} 

inline static void addError(
	MemStack& mem,
	ProjectParser& parser,
	ParseProjectErrorType errorType)
{
	addError(mem, parser, parserTextLocation(parser), errorType);
} 

inline static bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

inline static bool isWhitespace(char c)
{
	switch (c)
	{
	case '\n':
	case '\r':
	case ' ':
	case '\t':
		return true;
	default:
		return false;
	}
}

inline static void skipChar(ProjectParser& parser, char c)
{
	if (parser.cursor != parser.end && *parser.cursor == c)
	{
		++parser.cursor;
	}
}

static void skipWhitespace(ProjectParser& parser)
{
	while (parser.cursor != parser.end)
	{
		switch (*parser.cursor)
		{
		case '\n':
			++parser.cursor;
			++parser.lineNumber;
			skipChar(parser, '\r');
			parser.lineBegin = parser.cursor;
			break;
		case '\r':
			++parser.cursor;
			++parser.lineNumber;
			skipChar(parser, '\n');
			parser.lineBegin = parser.cursor;
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

static StringSlice readToken(ProjectParser& parser)
{
	skipWhitespace(parser);

	StringSlice token = {};
	token.begin = parser.cursor;
	while (parser.cursor != parser.end && !isWhitespace(*parser.cursor))
	{
		++parser.cursor;
	}
	token.end = parser.cursor;
	return token;
}

static bool parseU32Base10(StringSlice str, u32& result)
{
	auto p = str.begin;
	result = 0;
	while (p != str.end)
	{
		auto c = *p;
		if (!isDigit(c))
		{
			return false;
		}
//TODO check for overflow
		result = result * 10 + (c - '0');

		++p;
	}
	return str.end != str.begin;
}

static bool readHereString(MemStack& mem, ProjectParser& parser, StringSlice& result)
{
	auto hereStringLocation = parserTextLocation(parser);
	if (parser.cursor == parser.end)
	{
		addError(mem, parser, hereStringLocation, ParseProjectErrorType::MissingHereStringMarker);
		return false;
	}

	for (;;)
	{
		if (parser.cursor == parser.end)
		{
			addError(mem, parser, hereStringLocation, ParseProjectErrorType::UnclosedHereStringMarker);
			return false;
		}

		if (isWhitespace(*parser.cursor))
		{
			addError(mem, parser, hereStringLocation, ParseProjectErrorType::HereStringMarkerWhitespace);
			return false;
		}

		if (*parser.cursor == ':')
		{
			break;
		}

		++parser.cursor;
	}
	auto hereStringMarker = StringSlice{hereStringLocation.srcPtr, parser.cursor};
	auto markerLength = stringSliceLength(hereStringMarker);
	if (markerLength == 0)
	{
		addError(mem, parser, hereStringLocation, ParseProjectErrorType::EmptyHereStringMarker);
		return false;
	}
	++parser.cursor;

	auto strBegin = parser.cursor;
	for (;;)
	{
		// if enough characters are available, check if the here string marker has been encounted
		size_t strLength = parser.cursor - strBegin;
		if (strLength >= markerLength)
		{
			auto pStr = parser.cursor - markerLength;
			auto pMarker = hereStringMarker.begin;
			for (;;)
			{
				if (pMarker == hereStringMarker.end)
				{
					result.begin = strBegin;
					result.end = parser.cursor - markerLength;
					return true;
				}
				if (*pMarker != *pStr)
				{
					break;
				}
				++pMarker;
				++pStr;
			}
		}

		skipWhitespace(parser);

		if (parser.cursor == parser.end)
		{
			addError(mem, parser, hereStringLocation, ParseProjectErrorType::UnclosedHereString);
			return false;
		}

		++parser.cursor;
	}
}

static bool parseShader(MemStack& mem, ProjectParser& parser, ShaderType shaderType)
{
	auto shaderIdentifier = readToken(parser);
	auto shaderLocation = parserTextLocation(parser, shaderIdentifier.begin);
	if (stringSliceLength(shaderIdentifier) == 0)
	{
		addError(mem, parser, shaderLocation, ParseProjectErrorType::ShaderMissingIdentifier);
		return false;
	}
	skipWhitespace(parser);

	StringSlice shaderSource = {};
	if (!readHereString(mem, parser, shaderSource))
	{
		return false;
	}
	
	auto shader = memStackPushType(mem, RawShader);
	shader->location = shaderLocation;
	shader->identifier = shaderIdentifier;
	shader->type = shaderType;
	shader->source = shaderSource;
	shader->next = parser.shaders;
	parser.shaders = shader;
	++parser.shaderCount;
	return true;
}

inline static void attachShaderToProgram(
	MemStack& mem, ProjectParser& parser, RawProgram& program, char *identifierBegin)
{
	assert(identifierBegin != parser.cursor);

	auto shader = memStackPushType(mem, RawAttachedShader);
	shader->location = parserTextLocation(parser, identifierBegin);
	shader->identifier.begin = identifierBegin;
	shader->identifier.end = parser.cursor;
	++program.attachedShaderCount;
}

static bool parseProgram(MemStack& mem, ProjectParser& parser)
{
	skipWhitespace(parser);
	auto programLocation = parserTextLocation(parser);

	auto program = memStackPushType(mem, RawProgram);
	program->location = programLocation;
	program->identifier = {};
	program->attachedShaderCount = 0;
	program->attachedShaders = (RawAttachedShader*) mem.top;
	program->next = parser.programs;
	parser.programs = program;
	++parser.programCount;
	
	program->identifier.begin = parser.cursor;
	for (;;)
	{
		if (parser.cursor == parser.end)
		{
			addError(mem, parser, ParseProjectErrorType::IncompleteProgramStatement);
			return false;
		}

		if (*parser.cursor == '{')
		{
			program->identifier.end = parser.cursor;
			break;
		}

		if (isWhitespace(*parser.cursor))
		{
			program->identifier.end = parser.cursor;
			skipWhitespace(parser);
			if (parser.cursor == parser.end || *parser.cursor != '{')
			{
				addError(mem, parser, program->identifier.end, ParseProjectErrorType::ProgramMissingShaderList);
				return false;
			}
			break;
		}

		++parser.cursor;
	}

	assert(stringSliceLength(program->identifier) != 0);
	assert(*parser.cursor == '{');
	++parser.cursor;
	skipWhitespace(parser);

	for (;;)
	{
		auto shaderIdentifierBegin = parser.cursor;

		for (;;)
		{
			if (parser.cursor == parser.end)
			{
				addError(mem, parser, programLocation, ParseProjectErrorType::ProgramUnclosedShaderList);
				return false;
			}

			if (*parser.cursor == '}')
			{
				// This case is only relevant if the shader list is empty. In this case,
				// the shader identifier will be the empty string, so it should not be
				// attached to the program.
				if (parser.cursor != shaderIdentifierBegin)
				{
					attachShaderToProgram(mem, parser, *program, shaderIdentifierBegin);
				}
				++parser.cursor;
				return true;
			}

			if (isWhitespace(*parser.cursor))
			{
				attachShaderToProgram(mem, parser, *program, shaderIdentifierBegin);
				skipWhitespace(parser);
				break;
			}

			++parser.cursor;
		}
	}
}

Project parseProject(MemStack& mem, StringSlice projectText, ParseProjectError*& parseErrors)
{
	ProjectParser parser = {};
	parser.cursor = projectText.begin;
	parser.end = projectText.end;
	parser.lineNumber = 1;
	parser.lineBegin = projectText.begin;

	Project project = {};

	{
		auto versionToken = readToken(parser);
		if (versionToken != "Version")
		{
			addError(mem, parser, versionToken.begin, ParseProjectErrorType::MissingVersionStatement);
			goto returnResult;
		}
	}

	{
		auto versionNumberToken = readToken(parser);
		char *pDot = versionNumberToken.begin;
		for (;;)
		{
			if (pDot == versionNumberToken.end)
			{
				addError(mem, parser, versionNumberToken.begin, ParseProjectErrorType::VersionMissingDot);
				goto returnResult;
			}
			
			if (*pDot == '.')
			{
				break;
			}
			
			++pDot;
		}

		char *pFirstDot = pDot;

		if (pFirstDot == versionNumberToken.begin)
		{
			addError(mem, parser, versionNumberToken.begin, ParseProjectErrorType::VersionMissingMajor);
			goto returnResult;
		}
		if (pFirstDot + 1 == versionNumberToken.end)
		{
			addError(mem, parser, pFirstDot, ParseProjectErrorType::VersionMissingMinor);
			goto returnResult;
		}

		++pDot;
		for (;;)
		{
			if (pDot == versionNumberToken.end)
			{
				break;
			}
			
			if (*pDot == '.')
			{
				addError(mem, parser, pDot, ParseProjectErrorType::VersionExtraDot);
				goto returnResult;
			}
			
			++pDot;
		}

		bool success = true;
		auto majorStr = StringSlice{versionNumberToken.begin, pFirstDot};
		if (!parseU32Base10(majorStr, project.version.major))
		{
			addError(mem, parser, majorStr.begin, ParseProjectErrorType::VersionMajorNumberInvalid);
			success = false;
		}
		auto minorStr = StringSlice{pFirstDot + 1, versionNumberToken.end};
		if (!parseU32Base10(minorStr, project.version.minor))
		{
			addError(mem, parser, minorStr.begin, ParseProjectErrorType::VersionMinorNumberInvalid);
			success = false;
		}
		if (!success)
		{
			goto returnResult;
		}

		if (!(project.version.major == 1 && project.version.minor == 0))
		{
			addError(mem, parser, versionNumberToken.begin, ParseProjectErrorType::UnsupportedVersion);
			goto returnResult;
		}
	}

	for (;;)
	{
		auto valueType = readToken(parser);
		auto valueLocation = parserTextLocation(parser, valueType.begin);
		if (stringSliceLength(valueType) == 0)
		{
			// reached the end of input
			break;
		}

		if (valueType == "VertexShader")
		{
			bool success = parseShader(mem, parser, ShaderType::Vertex);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType == "FragmentShader")
		{
			bool success = parseShader(mem, parser, ShaderType::Fragment);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType == "Program")
		{
			bool success = parseProgram(mem, parser);
			if (!success)
			{
				goto returnResult;
			}
		} else
		{
			addError(mem, parser, valueLocation, ParseProjectErrorType::UnknownValueType);
			goto returnResult;
		}
	}

	// Copy the shaders and programs to permanent storage. Copying is done
	// in reverse order so that arrays are in the same order as in the file.

	auto shaders = memStackPushArray(mem, RawShader, parser.shaderCount);
	if (parser.shaderCount > 0)
	{
		auto pShader = parser.shaders;
		auto i = parser.shaderCount - 1;
		while (pShader != nullptr)
		{
			shaders[i] = *pShader;
			pShader = pShader->next;
			--i;
		}
	}
	project.shaderCount = parser.shaderCount;
	project.shaders = shaders;

	auto programs = memStackPushArray(mem, RawProgram, parser.programCount);
	if (parser.programCount > 0)
	{
		auto pProgram = parser.programs;
		auto i = parser.programCount - 1;
		while (pProgram != nullptr)
		{
			programs[i] = *pProgram;
			pProgram = pProgram->next;
			--i;
		}
	}
	project.programCount = parser.programCount;
	project.programs = programs;

returnResult:
	parseErrors = parser.errors;
	return project;
}

