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

Project parseProject(MemStack& permMem, MemStack& scratchMem, StringSlice projectText, ProjectErrors& errors);

