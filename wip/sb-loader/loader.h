#include "StringSlice.h"

enum struct ElementType
{
	Shader,
	Program,
	RenderConfig
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

enum struct DrawPrimitive
{
	Points,

	Lines,
	LineStrip,
	LineLoop,

	Triangles,
	TriangleStrip,
	TriangleFan,
};

struct TextDocumentLocation
{
	unsigned lineNumber;
	unsigned charNumber;
};

struct StringToken
{
	StringSlice value;
	TextDocumentLocation location;
};

struct ShaderElement
{
	StringToken nameToken;
	ShaderType type;
	StringToken pathToken;
};

struct ProgramElement
{
	StringToken nameToken;
	size_t attachedShaderCount;
};

struct RenderConfigElement
{
	StringToken nameToken;
	StringToken programNameToken;
	DrawPrimitive primitive;
	unsigned drawCount;
};

enum struct LoaderErrorType
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
	UnknownDrawPrimitive,
	RenderConfigMultiplePrimitives,
	RenderConfigMissingCount,
	RenderConfigMultipleCounts,
	RenderConfigEmptyProgramName,
	RenderConfigEmptyOrInvalidCount,
};

struct LoaderError
{
	LoaderErrorType type;
	TextDocumentLocation location;
};

inline void unreachable()
{
	assert(false);
}

