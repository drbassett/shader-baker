#pragma once

enum struct ParseProjectErrorType
{
	MissingVersionStatement,
	VersionMissingDot,
	VersionMissingMinor,
	VersionMissingMajor,
	VersionExtraDot,
	VersionMajorNumberInvalid,
	VersionMinorNumberInvalid,
	UnsupportedVersion,
	UnknownValueType,
	ShaderMissingIdentifier,
	UnclosedHereString,
	MissingHereStringMarker,
	UnclosedHereStringMarker,
	HereStringMarkerWhitespace,
	EmptyHereStringMarker,
	ProgramUnclosedShaderList,
	ProgramMissingShaderList,
	IncompleteProgramStatement,
};

struct TextLocation
{
	char *srcPtr;
	u32 lineNumber, charNumber;
};

struct ParseProjectError
{
	ParseProjectErrorType type;
	TextLocation location;
	ParseProjectError *next;
};

struct Version
{
	u32 major, minor;
};

enum struct ShaderType
{
	Vertex,
	Geometry,
	TessControl,
	TessEvaluation,
	Fragment,
	Compute,
};

struct RawShader
{
	TextLocation location;
	StringSlice identifier;
	ShaderType type;
	StringSlice source;
	RawShader *next;
};

struct RawAttachedShader
{
	TextLocation location;
	StringSlice identifier;
};

struct RawProgram
{
	TextLocation location;
	StringSlice identifier;
	u32 attachedShaderCount;
	RawAttachedShader *attachedShaders;
	RawProgram *next;
};

struct ProjectParser
{
	char *cursor, *end;
	u32 lineNumber;
	char *lineBegin;

	u32 shaderCount;
	RawShader *shaders;

	u32 programCount;
	RawProgram *programs;

	ParseProjectError *errors;

	// When set, the rest of the text is impossible or unlikely to parse
	// successfully, and parsing should be halted immediately
	bool hardError;
};

struct Project
{
	Version version;

	u32 shaderCount;
	RawShader *shaders;

	u32 programCount;
	RawProgram *programs;
};

Project parseProject(MemStack& mem, StringSlice projectText, ParseProjectError*& parseErrors);

char* DEBUG_errorTypeToString(ParseProjectErrorType errorType)
{
	switch (errorType)
	{
	case ParseProjectErrorType::MissingVersionStatement:
		return "First statement should be a 'Version' statement";
	case ParseProjectErrorType::VersionMissingDot:
		return "Version missing dot separator character '.'";
	case ParseProjectErrorType::VersionMissingMinor:
		return "Version missing minor version";
	case ParseProjectErrorType::VersionMissingMajor:
		return "Version missing major version";
	case ParseProjectErrorType::VersionExtraDot:
		return "Extra dot separator characters '.' in version";
	case ParseProjectErrorType::VersionMajorNumberInvalid:
		return "Major version is not a valid number";
	case ParseProjectErrorType::VersionMinorNumberInvalid:
		return "Minor version is not a valid number";
	case ParseProjectErrorType::UnsupportedVersion:
		return "Only version 1.0 is supported";
	case ParseProjectErrorType::UnknownValueType:
		return "Unknown type for value";
	case ParseProjectErrorType::MissingHereStringMarker:
		return "Expected marker token for here string";
	case ParseProjectErrorType::UnclosedHereStringMarker:
		return "Unclosed here string marker. Markers must be closed with a ':'";
	case ParseProjectErrorType::HereStringMarkerWhitespace:
		return "Here string markers cannot contain whitespace";
	case ParseProjectErrorType::EmptyHereStringMarker:
		return "Here string marker is empty";
	case ParseProjectErrorType::UnclosedHereString:
		return "Here string not closed. Make sure the marker for it ends with a ':'";
	case ParseProjectErrorType::ShaderMissingIdentifier:
		return "Expected name for shader";
	case ParseProjectErrorType::ProgramMissingShaderList:
		return "Expected a shader list to follow the program name";
	case ParseProjectErrorType::ProgramUnclosedShaderList:
		return "Unclosed program shader list";
	case ParseProjectErrorType::IncompleteProgramStatement:
		return "Incomplete program statement at end of input";
	default:
		unreachable();
		return "???";
	}
}

void DEBUG_printErrors(ParseProjectError* errors)
{
	int errorNumber = 1;
	while (errors != nullptr)
	{
		printf(
			"%d. line %d, char %d: %s\n",
			errorNumber,
			errors->location.lineNumber,
			errors->location.charNumber,
			DEBUG_errorTypeToString(errors->type));
		errors = errors->next;
	}
}

char* DEBUG_shaderTypeToString(ShaderType type)
{
	switch (type)
	{
	case ShaderType::Vertex:
		return "Vertex";
	case ShaderType::Geometry:
		return "Geometry";
	case ShaderType::TessControl:
		return "Tesselation Control";
	case ShaderType::TessEvaluation:
		return "Tesselation Evaluation";
	case ShaderType::Fragment:
		return "Fragment";
	case ShaderType::Compute:
		return "Compute";
	default:
		unreachable();
		return "";
	}
}

void DEBUG_printStringSlice(StringSlice str)
{
	while (str.begin != str.end)
	{
		putchar(*str.begin);
		++str.begin;
	}
}

void DEBUG_printProject(Project& project)
{
	printf("Version: %d.%d\n\n", project.version.major, project.version.minor);

	if (project.shaderCount > 0)
	{
		printf("Shaders:\n");
	}
	for (u32 i = 0; i < project.shaderCount; ++i)
	{
		auto shader = project.shaders[i];
		printf("L%d-C%d ", shader.location.lineNumber, shader.location.charNumber);
		DEBUG_printStringSlice(shader.identifier);
		printf(" (%s shader):\n>>>", DEBUG_shaderTypeToString(shader.type));
		DEBUG_printStringSlice(shader.source);
		fputs(">>>\n\n", stdout);
	}

	if (project.programCount > 0)
	{
		printf("Programs:\n");
	}
	for (u32 i = 0; i < project.programCount; ++i)
	{
		auto program = project.programs[i];
		printf("L%d-C%d ", program.location.lineNumber, program.location.charNumber);
		DEBUG_printStringSlice(program.identifier);
		fputs(" (Program): ", stdout);

		if (program.attachedShaderCount > 0)
		{
			DEBUG_printStringSlice(program.attachedShaders[0].identifier);
		}
		for (u32 j = 1; j < program.attachedShaderCount; ++j)
		{
			fputs(", ", stdout);
			DEBUG_printStringSlice(program.attachedShaders[j].identifier);
		}
		putchar('\n');
	}
}

