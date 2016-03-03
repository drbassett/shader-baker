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

inline void unreachable()
{
	assert(false);
}

