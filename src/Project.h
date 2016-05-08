#pragma once

enum struct ProjectErrorType
{
	MissingVersionStatement,
	VersionInvalidFormat,
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
	DuplicateShaderName,
	DuplicateProgramName,
	ProgramExceedsAttachedShaderLimit,
	ProgramUnresolvedShaderIdent,
};

struct TextLocation
{
	char *srcPtr;
	u32 lineNumber, charNumber;
};

struct Token
{
	TextLocation location;
	StringSlice str;
};

struct ParseProjectError
{
	ProjectErrorType type;
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

struct ShaderToken
{
	TextLocation location;
	StringSlice identifier;
	ShaderType type;
	StringSlice source;
	ShaderToken *next;
};

struct AttachedShaderToken
{
	TextLocation location;
	StringSlice identifier;
};

struct ProgramToken
{
	TextLocation location;
	StringSlice identifier;
	u32 attachedShaderCount;
	AttachedShaderToken *attachedShaders;
	ProgramToken *next;
};

struct ProjectParser
{
	char *cursor, *end;
	u32 lineNumber;
	char *lineBegin;

	u32 shaderCount;
	ShaderToken *shaders;

	u32 programCount;
	ProgramToken *programs;

	u32 errorCount;
	ParseProjectError *errors;
};

struct Shader
{
	ShaderType type;
	PackedString name;
	PackedString source;
};

struct Program
{
	PackedString name;
	// The attached shader count does not need to be a large number.
	// Programs should have nowhere near 255 shaders attached.
	u8 attachedShaderCount;
	Shader **attachedShaders;
};

struct Project
{
	Version version;

	u32 programCount;
	Program *programs;

	u32 shaderCount;
	Shader *shaders;
};

struct ProjectError
{
	ProjectErrorType type;
	u32 lineNumber, charNumber;
	PackedString context;
};

struct ProjectErrors
{
	u32 count;
	ProjectError *ptr;
};

Project parseProject(MemStack& permMem, MemStack& scratchMem, StringSlice projectText, ProjectErrors*& errors);

inline void DEBUG_printString(StringSlice str)
{
	while (str.begin != str.end)
	{
		putchar(*str.begin);
		++str.begin;
	}
}

inline void DEBUG_printString(PackedString str)
{
	DEBUG_printString(unpackString(str));
}

char* DEBUG_errorTypeToString(ProjectErrorType errorType)
{
	switch (errorType)
	{
	case ProjectErrorType::MissingVersionStatement:
		return "First statement in document should be a 'Version' statement";
	case ProjectErrorType::VersionInvalidFormat:
		return "Version number is not correctly formatted. It should have the syntax \"Major.Minor\", where \"Major\" and \"Minor\" are numbers";
	case ProjectErrorType::UnsupportedVersion:
		return "Unsupported version - this parser only supports version 1.0";
	case ProjectErrorType::UnknownValueType:
		return "Unknown type for value";
	case ProjectErrorType::MissingHereStringMarker:
		return "Expected marker token for here string";
	case ProjectErrorType::UnclosedHereStringMarker:
		return "Unclosed here string marker. Markers must be closed with a ':'";
	case ProjectErrorType::HereStringMarkerWhitespace:
		return "Here string markers contains whitespace";
	case ProjectErrorType::EmptyHereStringMarker:
		return "Here string marker is empty";
	case ProjectErrorType::UnclosedHereString:
		return "Here string not closed. Make sure its marker ends with a ':'";
	case ProjectErrorType::ShaderMissingIdentifier:
		return "Expected name for shader";
	case ProjectErrorType::ProgramMissingShaderList:
		return "Expected a shader list to follow the program name";
	case ProjectErrorType::ProgramUnclosedShaderList:
		return "Unclosed attached shader list";
	case ProjectErrorType::DuplicateShaderName:
		return "Another shader in this project has the same name";
	case ProjectErrorType::DuplicateProgramName:
		return "Another program in this project has the same name";
	case ProjectErrorType::ProgramExceedsAttachedShaderLimit:
		return "Programs cannot have more than 255 shaders attached";
	case ProjectErrorType::ProgramUnresolvedShaderIdent:
		return "No shader with this name exists in this project";
	default:
		unreachable();
		return "???";
	}
}

void DEBUG_printErrors(ProjectErrors const& errors)
{
	for (u32 i = 0; i < errors.count; ++i)
	{
		auto error = errors.ptr[i];
		printf(
			"Line %d, char %d: %s\n>>>>>\n",
			error.lineNumber,
			error.charNumber,
			DEBUG_errorTypeToString(error.type));
		DEBUG_printString(error.context);
		printf(">>>>>\n\n");
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

void DEBUG_printProject(Project& project)
{
	printf("Version: %d.%d\n\n", project.version.major, project.version.minor);

	if (project.shaderCount > 0)
	{
		printf("Shaders:\n");
	}
	for (u32 shaderIdx = 0; shaderIdx < project.shaderCount; ++shaderIdx)
	{
		auto shader = project.shaders[shaderIdx];
		DEBUG_printString(shader.name);
		printf(" (%s shader):\n>>>", DEBUG_shaderTypeToString(shader.type));
		DEBUG_printString(shader.source);
		fputs(">>>\n\n", stdout);
	}

	if (project.programCount > 0)
	{
		printf("Programs:\n");
	}
	for (u32 programIdx = 0; programIdx < project.programCount; ++programIdx)
	{
		auto program = project.programs[programIdx];
		DEBUG_printString(program.name);
		fputs(" (Program): ", stdout);

		if (program.attachedShaderCount > 0)
		{
			DEBUG_printString(program.attachedShaders[0]->name);
		}
		for (u32 shaderIdx = 1; shaderIdx < program.attachedShaderCount; ++shaderIdx)
		{
			fputs(", ", stdout);
			DEBUG_printString(program.attachedShaders[shaderIdx]->name);
		}
		putchar('\n');
	}
}

