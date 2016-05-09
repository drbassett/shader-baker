#include "Project.h"

//TODO consider restricting the available characters for identifiers

inline static TextLocation parserTextLocation(ProjectParser& parser)
{
	auto charNumber = (u32) (parser.cursor - parser.lineBegin + 1);
	return TextLocation{parser.cursor, parser.lineNumber, charNumber};
}

inline static void addError(
	MemStack& mem, ProjectParser& parser, TextLocation location, ProjectErrorType errorType)
{
	auto error = memStackPushType(mem, ParseProjectError);
	error->type = errorType;
	error->location = location;
	error->next = parser.errors;
	parser.errors = error;
	++parser.errorCount;
}

inline static void addError(MemStack& mem, ProjectParser& parser, ProjectErrorType errorType)
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
		addError(mem, parser, hereStringLocation, ProjectErrorType::MissingHereStringMarker);
		return false;
	}

	for (;;)
	{
		if (parser.cursor == parser.end)
		{
			addError(mem, parser, hereStringLocation, ProjectErrorType::UnclosedHereStringMarker);
			return false;
		}

		if (isWhitespace(*parser.cursor))
		{
			addError(mem, parser, hereStringLocation, ProjectErrorType::HereStringMarkerWhitespace);
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
		addError(mem, parser, hereStringLocation, ProjectErrorType::EmptyHereStringMarker);
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
			addError(mem, parser, hereStringLocation, ProjectErrorType::UnclosedHereString);
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
		addError(mem, parser, shaderToken.location, ProjectErrorType::ShaderMissingIdentifier);
		return false;
	}
	skipWhitespace(parser);

	StringSlice shaderSource = {};
	if (!readHereString(mem, parser, shaderSource))
	{
		return false;
	}
	
	auto shader = memStackPushType(mem, ShaderToken);
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
	MemStack& mem, ProjectParser& parser, ProgramToken& program, TextLocation identifierLocation)
{
	assert(identifierLocation.srcPtr != parser.cursor);

	auto shader = memStackPushType(mem, AttachedShaderToken);
	shader->location = identifierLocation;
	shader->identifier.begin = identifierLocation.srcPtr;
	shader->identifier.end = parser.cursor;
	++program.attachedShaderCount;
}

static bool parseProgram(MemStack& mem, ProjectParser& parser)
{
	skipWhitespace(parser);
	auto programLocation = parserTextLocation(parser);

	auto program = memStackPushType(mem, ProgramToken);
	program->location = programLocation;
	program->identifier = {};
	program->attachedShaderCount = 0;
	program->attachedShaders = (AttachedShaderToken*) mem.top;
	program->next = parser.programs;
	parser.programs = program;
	++parser.programCount;
	
	program->identifier.begin = parser.cursor;
	for (;;)
	{
		if (parser.cursor == parser.end)
		{
			addError(mem, parser, programLocation, ProjectErrorType::ProgramMissingShaderList);
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
				addError(mem, parser, programLocation, ProjectErrorType::ProgramMissingShaderList);
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
				addError(mem, parser, programLocation, ProjectErrorType::ProgramUnclosedShaderList);
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

Project parseProject(MemStack& permMem, MemStack& scratchMem, StringSlice projectText, ProjectErrors& errors)
{
	ProjectParser parser = {};
	parser.cursor = projectText.begin;
	parser.end = projectText.end;
	parser.lineNumber = 1;
	parser.lineBegin = projectText.begin;

	Project project = {};

	Version version;
	{
		auto versionToken = readToken(parser);
		if (versionToken.str != "Version")
		{
			addError(scratchMem, parser, versionToken.location, ProjectErrorType::MissingVersionStatement);
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
				addError(scratchMem, parser, tokenLocation, ProjectErrorType::VersionInvalidFormat);
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
			addError(scratchMem, parser, tokenLocation, ProjectErrorType::VersionInvalidFormat);
			goto returnResult;
		}
		if (pFirstDot + 1 == versionNumberToken.str.end)
		{
			addError(scratchMem, parser, tokenLocation, ProjectErrorType::VersionInvalidFormat);
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
				addError(scratchMem, parser, tokenLocation, ProjectErrorType::VersionInvalidFormat);
				goto returnResult;
			}
			
			++pDot;
		}

		bool success = true;
		auto majorStr = StringSlice{versionNumberToken.str.begin, pFirstDot};
		if (!parseU32Base10(majorStr, version.major))
		{
			addError(scratchMem, parser, tokenLocation, ProjectErrorType::VersionInvalidFormat);
			success = false;
		}
		auto minorStr = StringSlice{pFirstDot + 1, versionNumberToken.str.end};
		if (!parseU32Base10(minorStr, version.minor))
		{
			addError(scratchMem, parser, tokenLocation, ProjectErrorType::VersionInvalidFormat);
			success = false;
		}
		if (!success)
		{
			goto returnResult;
		}

		if (!(version.major == 1 && version.minor == 0))
		{
			addError(scratchMem, parser, tokenLocation, ProjectErrorType::UnsupportedVersion);
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
			bool success = parseShader(scratchMem, parser, ShaderType::Vertex);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "TessControlShader")
		{
			bool success = parseShader(scratchMem, parser, ShaderType::TessControl);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "TessEvaluationShader")
		{
			bool success = parseShader(scratchMem, parser, ShaderType::TessEvaluation);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "GeometryShader")
		{
			bool success = parseShader(scratchMem, parser, ShaderType::Geometry);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "FragmentShader")
		{
			bool success = parseShader(scratchMem, parser, ShaderType::Fragment);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "ComputeShader")
		{
			bool success = parseShader(scratchMem, parser, ShaderType::Compute);
			if (!success)
			{
				goto returnResult;
			}
		} else if (valueType.str == "Program")
		{
			bool success = parseProgram(scratchMem, parser);
			if (!success)
			{
				goto returnResult;
			}
		} else
		{
			addError(scratchMem, parser, valueLocation, ProjectErrorType::UnknownValueType);
			goto returnResult;
		}
	}

	project.version = version;

	// Copy the shaders and programs to permanent storage. Copying is done
	// in reverse order so that arrays are in the same order as in the file.

//TODO consider using a temporary hashmap to do name lookups. Project files may
// never get large enough to justify this complexity, but if the loading process
// gets sluggish, this is something to consider.

	auto projectMemMarker = memStackMark(permMem);

	project.shaders = memStackPushArray(permMem, Shader, parser.shaderCount);
	project.shaderCount = parser.shaderCount;
	{
		auto pShader = parser.shaders;
		auto shaderIdx = parser.shaderCount - 1;
		while (pShader != nullptr)
		{
			project.shaders[shaderIdx].type = pShader->type;
			project.shaders[shaderIdx].name = packString(permMem, pShader->identifier);
			project.shaders[shaderIdx].source = packString(permMem, pShader->source);

			// check the shader name for uniqueness
			for (auto i = shaderIdx + 1; i < project.shaderCount; ++i)
			{
				if (unpackString(project.shaders[i].name) == pShader->identifier)
				{
					addError(scratchMem, parser, pShader->location, ProjectErrorType::DuplicateShaderName);
					break;
				}
			}

			pShader = pShader->next;
			--shaderIdx;
		}
	}

	project.programCount = parser.programCount;
	project.programs = memStackPushArray(permMem, Program, parser.programCount);
	{
		auto pProgram = parser.programs;
		auto programIdx = parser.programCount - 1;
		while (pProgram != nullptr)
		{
			project.programs[programIdx].name = packString(permMem, pProgram->identifier);
			if (pProgram->attachedShaderCount > 255)
			{
				addError(
					scratchMem,
					parser,
					pProgram->location,
					ProjectErrorType::ProgramExceedsAttachedShaderLimit);
				project.programs[programIdx].attachedShaderCount = 0;
				project.programs[programIdx].attachedShaders = nullptr;
				continue;
			}
			
			// check the program name for uniqueness
			for (auto i = programIdx + 1; i < project.programCount; ++i)
			{
				if (unpackString(project.programs[i].name) == pProgram->identifier)
				{
					addError(scratchMem, parser, pProgram->location, ProjectErrorType::DuplicateProgramName);
					break;
				}
			}

			auto shaderListLength = pProgram->attachedShaderCount;
			project.programs[programIdx].attachedShaderCount = (u8) shaderListLength;
			project.programs[programIdx].attachedShaders = memStackPushArray(permMem, Shader*, shaderListLength);

			// lookup pointers to attached shaders
			for (u32 shaderIdx = 0; shaderIdx < shaderListLength; ++shaderIdx)
			{
				auto shader = pProgram->attachedShaders[shaderIdx];
				for (u32 i = 0; i < project.shaderCount; ++i)
				{
					if (unpackString(project.shaders[i].name) == shader.identifier)
					{
						project.programs[programIdx].attachedShaders[shaderIdx] = project.shaders + i;
						goto LBL_nextShader;
					}
				}
				project.programs[programIdx].attachedShaders[shaderIdx] = nullptr;
				addError(
					scratchMem,
					parser,
					shader.location,
					ProjectErrorType::ProgramUnresolvedShaderIdent);
				LBL_nextShader:;
			}

			pProgram = pProgram->next;
			--programIdx;
		}
	}


	if (parser.errorCount == 0)
	{
		errors = {};
		return project;
	}

	memStackPop(permMem, projectMemMarker);
	project = {};
	
returnResult:
	// Scan through the project text to find line boundaries. This could have
	// been done while the project text was parsed, but most of the time
	// projects will not have any errors, so this code will never get executed
	// anyways. Doing it while parsing adds extra complexity and will usually
	// be a waste of time.
	u32 lineCount = 0;
	char extraLineCharacter;
	auto lines = (StringSlice*) scratchMem.top;
	{
		auto lineBegin = projectText.begin;
		auto cursor = projectText.begin;
		while (cursor != projectText.end)
		{
			switch (*cursor)
			{
			case '\n':
				extraLineCharacter = '\r';
				break;
			case '\r':
				extraLineCharacter = '\n';
				break;
			default:
				++cursor;
				continue;
			}

			auto lineBounds = memStackPushType(scratchMem, StringSlice);
			lineBounds->begin = lineBegin;
			lineBounds->end = cursor;
			++lineCount;
			++cursor;
			if (cursor == projectText.end)
			{
				break;
			}
			if (*cursor == extraLineCharacter)
			{
				++cursor;
			}
			lineBegin = cursor;
		}
	}

	errors.count = parser.errorCount;
	errors.ptr = memStackPushArray(permMem, ProjectError, parser.errorCount);
	{
		auto pError = parser.errors;
		auto errorIdx = parser.errorCount - 1; 
		while (pError != nullptr)
		{
			auto errorLineNumber = pError->location.lineNumber;
			u32 contextLineCount = 2;

			u32 firstContextLineIdx;
			firstContextLineIdx = errorLineNumber;
			if (errorLineNumber > contextLineCount)
			{
				firstContextLineIdx = errorLineNumber - contextLineCount;
			}
			--firstContextLineIdx;

			u32 lastContextLineIdx = errorLineNumber + contextLineCount;
			if (lastContextLineIdx > lineCount)
			{
				lastContextLineIdx = lineCount;
			}

			auto pContextString = memStackPushType(permMem, size_t);
			auto contextStringBegin = (u8*) permMem.top;
			for (u32 i = firstContextLineIdx; i < lastContextLineIdx; ++i)
			{
				auto lineBounds = lines[i];
				auto lineLength = stringSliceLength(lineBounds);

				// add the line number
				{
					char *unused1;
					u32 unused2;
					u32ToString(permMem, i + 1, unused1, unused2);
				}

				auto lineText = memStackPushArray(permMem, char, lineLength + 4);
				lineText[0] = ' ';
				lineText[1] = '|';
				lineText[2] = ' ';
				memcpy(lineText + 3, lineBounds.begin, lineLength);
				lineText[lineLength + 4 - 1] = '\n';
			}
			auto contextStringEnd = (u8*) permMem.top;
			auto contextStringLength = contextStringEnd - contextStringBegin;
			(*pContextString) = contextStringLength;

			errors.ptr[errorIdx].type = pError->type;
			errors.ptr[errorIdx].lineNumber = pError->location.lineNumber;
			errors.ptr[errorIdx].charNumber = pError->location.charNumber;
			errors.ptr[errorIdx].context.ptr = pContextString;

			pError = pError->next;
			--errorIdx;
		}
	}

	return project;
}

