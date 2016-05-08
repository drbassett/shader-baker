#include "ProjectLoader.h"

//TODO consider restricting the available characters for identifiers

inline static TextLocation parserTextLocation(ProjectParser& parser)
{
	auto charNumber = (u32) (parser.cursor - parser.lineBegin + 1);
	return TextLocation{parser.cursor, parser.lineNumber, charNumber};
}

inline static void addError(
	MemStack& mem, ProjectParser& parser, TextLocation location, ParseProjectErrorType errorType)
{
	auto error = memStackPushType(mem, ParseProjectError);
	error->type = errorType;
	error->location = location;
	error->next = parser.errors;
	parser.errors = error;
}

inline static void addError(MemStack& mem, ProjectParser& parser, ParseProjectErrorType errorType)
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

static Token readToken(ProjectParser& parser)
{
	skipWhitespace(parser);

	Token result = {};
	result.location = parserTextLocation(parser);

	result.str.begin = parser.cursor;
	while (parser.cursor != parser.end && !isWhitespace(*parser.cursor))
	{
		++parser.cursor;
	}
	result.str.end = parser.cursor;
	return result;
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
	auto shaderToken = readToken(parser);
	if (stringSliceLength(shaderToken.str) == 0)
	{
		addError(mem, parser, shaderToken.location, ParseProjectErrorType::ShaderMissingIdentifier);
		return false;
	}
	skipWhitespace(parser);

	StringSlice shaderSource = {};
	if (!readHereString(mem, parser, shaderSource))
	{
		return false;
	}
	
	auto shader = memStackPushType(mem, RawShader);
	shader->location = shaderToken.location;
	shader->identifier = shaderToken.str;
	shader->type = shaderType;
	shader->source = shaderSource;
	shader->next = parser.shaders;
	parser.shaders = shader;
	++parser.shaderCount;
	return true;
}

inline static void attachShaderToProgram(
	MemStack& mem, ProjectParser& parser, RawProgram& program, TextLocation identifierLocation)
{
	assert(identifierLocation.srcPtr != parser.cursor);

	auto shader = memStackPushType(mem, RawAttachedShader);
	shader->location = identifierLocation;
	shader->identifier.begin = identifierLocation.srcPtr;
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
			addError(mem, parser, programLocation, ParseProjectErrorType::IncompleteProgramStatement);
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
				addError(mem, parser, programLocation, ParseProjectErrorType::ProgramMissingShaderList);
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
		auto textLocation = parserTextLocation(parser);

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
					attachShaderToProgram(mem, parser, *program, textLocation);
				}
				++parser.cursor;
				return true;
			}

			if (isWhitespace(*parser.cursor))
			{
				attachShaderToProgram(mem, parser, *program, textLocation);
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
		if (versionToken.str != "Version")
		{
			addError(mem, parser, versionToken.location, ParseProjectErrorType::MissingVersionStatement);
			goto returnResult;
		}
	}

	{
		auto versionNumberToken = readToken(parser);
		auto tokenLocation = versionNumberToken.location;
		char *pDot = versionNumberToken.str.begin;
		for (;;)
		{
			if (pDot == versionNumberToken.str.end)
			{
				addError(mem, parser, tokenLocation, ParseProjectErrorType::VersionInvalidFormat);
				goto returnResult;
			}
			
			if (*pDot == '.')
			{
				break;
			}
			
			++pDot;
		}

		char *pFirstDot = pDot;

		if (pFirstDot == versionNumberToken.str.begin)
		{
			addError(mem, parser, tokenLocation, ParseProjectErrorType::VersionInvalidFormat);
			goto returnResult;
		}
		if (pFirstDot + 1 == versionNumberToken.str.end)
		{
			addError(mem, parser, tokenLocation, ParseProjectErrorType::VersionInvalidFormat);
			goto returnResult;
		}

		++pDot;
		for (;;)
		{
			if (pDot == versionNumberToken.str.end)
			{
				break;
			}
			
			if (*pDot == '.')
			{
				addError(mem, parser, tokenLocation, ParseProjectErrorType::VersionInvalidFormat);
				goto returnResult;
			}
			
			++pDot;
		}

		bool success = true;
		auto majorStr = StringSlice{versionNumberToken.str.begin, pFirstDot};
		if (!parseU32Base10(majorStr, project.version.major))
		{
			addError(mem, parser, tokenLocation, ParseProjectErrorType::VersionInvalidFormat);
			success = false;
		}
		auto minorStr = StringSlice{pFirstDot + 1, versionNumberToken.str.end};
		if (!parseU32Base10(minorStr, project.version.minor))
		{
			addError(mem, parser, tokenLocation, ParseProjectErrorType::VersionInvalidFormat);
			success = false;
		}
		if (!success)
		{
			goto returnResult;
		}

		if (!(project.version.major == 1 && project.version.minor == 0))
		{
			addError(mem, parser, tokenLocation, ParseProjectErrorType::UnsupportedVersion);
			goto returnResult;
		}
	}

	for (;;)
	{
		auto valueType = readToken(parser);
		auto valueLocation = valueType.location;
		if (stringSliceLength(valueType.str) == 0)
		{
			// reached the end of input
			break;
		}

		if (valueType.str == "VertexShader")
		{
			bool success = parseShader(mem, parser, ShaderType::Vertex);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "FragmentShader")
		{
			bool success = parseShader(mem, parser, ShaderType::Fragment);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "Program")
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

