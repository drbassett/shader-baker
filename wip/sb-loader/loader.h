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

struct ShaderElement
{
	StringSlice name;
	ShaderType type;
	StringSlice path;
};

struct ProgramElement
{
	StringSlice name;
	size_t attachedShaderCount;
};

struct RenderConfigElement
{
	StringSlice name;
	StringSlice programName;
	StringSlice primitive;
	unsigned drawCount;
};

inline void unreachable()
{
	assert(false);
}

