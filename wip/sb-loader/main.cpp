//TODO Need to distinguish between soft and hard parsing errors. For example,
//     a type missing the start of block will likely make the rest of the file
//     impossible to parse, so we should stop parsing immediately (a hard error).
//     However, a type that is unrecognized by the parser can be easily ignored
//     by skipping its block. Currently, the parser returns simple boolean values
//     for failure conditions, which do not have information to distinguish
//     between these cases.

//TODO Clean up skipWhitespace() calls.

#include <cassert>
#include <cstdlib>

#include "StringSlice.h"
#include "StringReference.h"
#include "loader.h"
#include "parser.h"

struct ParseResult
{
	Version version;
	
	char *elementsBegin;
	char *elementsEnd;

	ParseError *errorsBegin;
	ParseError *errorsEnd;
};

struct Shader
{
	StringReference name;
	ShaderType type;
	StringReference path;
};

struct Program
{
	StringReference name;
	Shader **attachedShadersBegin, **attachedShadersEnd;
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

struct RenderConfig
{
	StringReference name;
	Program *program;
	DrawPrimitive primitive;
	unsigned drawCount;
};

struct Shaders
{
	Shader *begin, *end;
};

struct Programs
{
	Program *begin, *end;
};

struct AttachedShaders
{
	Shader **begin, **end;
};

struct RenderConfigs
{
	RenderConfig *begin, *end;
};

struct ShaderBakerObjects
{
	void *memoryBlock;

	char* stringPool;
	Shaders shaders;
	Programs programs;
	AttachedShaders attachedShaders;
	RenderConfigs renderConfigs;
};

struct ElementValue
{
	ElementType elementType;

	union
	{
		ShaderElement *shader;

		struct
		{
			ProgramElement *ptr;
			StringSlice *attachedShadersBegin;
		} program;

		RenderConfigElement *renderConfig;
	} element;
};

struct StringAllocator
{
	char* next;
};

struct ParseResultCounts
{
	unsigned shaderCount;
	unsigned programCount;
	unsigned renderConfigCount;
	unsigned attachedShadersCount;
	unsigned stringCount;

	size_t stringPoolSize;
};

StringReference copyStringSlice(StringAllocator& allocator, StringSlice string)
{
	StringReference result{allocator.next};

	auto pStringLength = (size_t*) allocator.next;
	*pStringLength = stringSliceLength(string);
	allocator.next += sizeof(size_t);

	while (string.begin != string.end)
	{
		*allocator.next = *string.begin;
		++string.begin;
		++allocator.next;
	}

	return result;
}

// Paths escape a single quote by including two single quotes in a
// row. This function copies a string, skipping extra single quotes
// in the process.
StringReference copyRawPath(StringAllocator& allocator, StringSlice string)
{
	StringReference result{allocator.next};

	auto pStringLength = (size_t*) allocator.next;
	allocator.next += sizeof(size_t);

	auto stringBegin = allocator.next;
	while (string.begin != string.end)
	{
		*allocator.next = *string.begin;
		if (*string.begin == '\'')
		{
			++string.begin;
			assert(*string.begin == '\'');
		}
		++string.begin;
		++allocator.next;
	}

	*pStringLength = allocator.next - stringBegin;

	return result;
}

static char* readElementValue(char *pElement, ElementValue &result)
{
	result = {};
	auto pElementType = (ElementType*) pElement;
	result.elementType = *pElementType;
	auto pElementValue = pElementType + 1;
	char *next = nullptr;
	switch (result.elementType)
	{
	case ElementType::Shader:
	{
		auto pShader = (ShaderElement*) pElementValue;
		result.element.shader = pShader;
		next = (char*) (pShader + 1);
	} break;
	case ElementType::Program:
	{
		auto pProgram = (ProgramElement*) pElementValue;
		auto attachedShadersBegin = (StringSlice*) (pProgram + 1);
		result.element.program = {pProgram, attachedShadersBegin};
		next = (char*) (attachedShadersBegin + pProgram->attachedShaderCount);
	} break;
	case ElementType::RenderConfig:
	{
		auto pRenderConfig = (RenderConfigElement*) pElementValue;
		result.element.renderConfig = pRenderConfig;
		next = (char*) (pRenderConfig + 1);
	} break;
	default:
	{
		unreachable();
	} break;
	}

	return next;
}

ParseResultCounts countParseResult(ParseResult const& parseResult)
{
	ParseResultCounts result = {};

	auto pElements = parseResult.elementsBegin;
	while (pElements != parseResult.elementsEnd)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::Shader:
		{
			auto pShader = elementValue.element.shader;
			++result.shaderCount;
			result.stringCount += 2;
			result.stringPoolSize += stringSliceLength(pShader->name);
			result.stringPoolSize += stringSliceLength(pShader->path);
		} break;
		case ElementType::Program:
		{
			auto pProgram = elementValue.element.program.ptr;
			auto attachedShadersBegin = elementValue.element.program.attachedShadersBegin;
			auto attachedShadersEnd = attachedShadersBegin + pProgram->attachedShaderCount;
			auto attachedShaderCount = (unsigned) (attachedShadersEnd - attachedShadersBegin);

			++result.programCount;
			result.attachedShadersCount += attachedShaderCount;
			++result.stringCount;
			result.stringPoolSize += stringSliceLength(pProgram->name);
		} break;
		case ElementType::RenderConfig:
		{
			auto pRenderConfig = elementValue.element.renderConfig;
			++result.renderConfigCount;
			++result.stringCount;
			result.stringPoolSize += stringSliceLength(pRenderConfig->name);
		} break;
		default:
			unreachable();
			break;
		}
	}

	return result;
}

Shader* findShaderWithName(Shader *begin, Shader *end, StringSlice name)
{
	while (begin != end)
	{
		if (stringReferenceDeref(begin->name) == name)
		{
			return begin;
		}
		++begin;
	}
	return nullptr;
}

inline Shader* findShaderWithName(Shader *begin, Shader *end, StringReference name)
{
	return findShaderWithName(begin, end, stringReferenceDeref(name));
}

void processShaders(
	ParseResult const& parseResult,
	StringAllocator& stringAllocator,
	Shader *shaderStorage)
{
	auto pElements = parseResult.elementsBegin;
	auto nextShader = shaderStorage;
	while (pElements != parseResult.elementsEnd)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::Shader:
		{
			auto pShader = elementValue.element.shader;

			auto shaderName = copyStringSlice(stringAllocator, pShader->name);
			auto shaderPath = copyRawPath(stringAllocator, pShader->path);

			if (findShaderWithName(shaderStorage, nextShader, shaderName) != nullptr)
			{
//TODO ERROR duplicate shader name
			}

			*nextShader = {};
			nextShader->name = shaderName;
			nextShader->type = pShader->type;
			nextShader->path = shaderPath;
			++nextShader;
		} break;
		}
	}
}

Program* findProgramWithName(Program *begin, Program *end, StringSlice name)
{
	while (begin != end)
	{
		if (stringReferenceDeref(begin->name) == name)
		{
			return begin;
		}
		++begin;
	}
	return nullptr;
}

inline Program* findProgramWithName(Program *begin, Program *end, StringReference name)
{
	return findProgramWithName(begin, end, stringReferenceDeref(name));
}

void processPrograms(
	ParseResult const& parseResult,
	StringAllocator& stringAllocator,
	Shaders shaders,
	Program *programStorage,
	Shader **attachedShaderStorage)
{
	auto pElements = parseResult.elementsBegin;
	auto nextProgram = programStorage;
	auto nextAttachedShader = attachedShaderStorage;
	while (pElements != parseResult.elementsEnd)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::Program:
		{
			auto pProgram = elementValue.element.program.ptr;
			auto attachedShaderNamesBegin = elementValue.element.program.attachedShadersBegin;
			auto attachedShadersEnd = attachedShaderNamesBegin + pProgram->attachedShaderCount;

			auto programName = copyStringSlice(stringAllocator, pProgram->name);
			if (findProgramWithName(programStorage, nextProgram, programName) != nullptr)
			{
//TODO ERROR duplicate shader name
			}

			auto attachedShadersBegin = nextAttachedShader;
			while (attachedShaderNamesBegin != attachedShadersEnd)
			{
				auto attachedShader = findShaderWithName(shaders.begin, shaders.end, *attachedShaderNamesBegin);
				if (attachedShader == nullptr)
				{
//TODO ERROR could not find shader with name
				} else
				{
					*nextAttachedShader = attachedShader;
				}

				++attachedShaderNamesBegin;
				++nextAttachedShader;
			}

//TODO Check that only one of each shader type (vertex, fragment, etc) is attached

			*nextProgram = {};
			nextProgram->name = programName;
			nextProgram->attachedShadersBegin = attachedShadersBegin;
			nextProgram->attachedShadersEnd = nextAttachedShader;
			++nextProgram;
		} break;
		}
	}
}

RenderConfig* findRenderConfigWithName(
	RenderConfig *begin, RenderConfig *end, StringReference name)
{
	while (begin != end)
	{
		if (begin->name == name)
		{
			return begin;
		}
		++begin;
	}
	return nullptr;
}

bool stringToDrawPrimitive(StringSlice string, DrawPrimitive& result)
{
	if (string == "Points")
	{
		result = DrawPrimitive::Points;
	} else if (string == "Lines")
	{
		result = DrawPrimitive::Lines;
	} else if (string == "LineStrip")
	{
		result = DrawPrimitive::LineStrip;
	} else if (string == "LineLoop")
	{
		result = DrawPrimitive::LineLoop;
	} else if (string == "Triangles")
	{
		result = DrawPrimitive::Triangles;
	} else if (string == "TriangleStrip")
	{
		result = DrawPrimitive::TriangleStrip;
	} else if (string == "TriangleFan")
	{
		result = DrawPrimitive::TriangleFan;
	} else
	{
		return false;
	}

	return true;
}

void processRenderConfigs(
	ParseResult const& parseResult,
	StringAllocator& stringAllocator,
	Programs programs,
	RenderConfig *renderConfigStorage)
{
	auto pElements = parseResult.elementsBegin;
	auto nextRenderConfig = renderConfigStorage;
	while (pElements != parseResult.elementsEnd)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::RenderConfig:
		{
			auto pRenderConfig = elementValue.element.renderConfig;

			auto renderConfigName = copyStringSlice(stringAllocator, pRenderConfig->name);
			if (findRenderConfigWithName(renderConfigStorage, nextRenderConfig, renderConfigName) != nullptr)
			{
//TODO ERROR duplicate render config name
			}

//TODO move the string-to-primitive conversion to the parser
			DrawPrimitive drawPrimitive;
			if (!stringToDrawPrimitive(pRenderConfig->primitive, drawPrimitive))
			{
//TODO ERROR unknown draw primitive type
			}

			auto program = findProgramWithName(programs.begin, programs.end, pRenderConfig->programName);
			if (program == nullptr)
			{
//TODO ERROR could not find program with name
			}

			*nextRenderConfig = {};
			nextRenderConfig->name = renderConfigName;
			nextRenderConfig->program = program;
			nextRenderConfig->primitive = drawPrimitive;
			nextRenderConfig->drawCount = pRenderConfig->drawCount;
			++nextRenderConfig;
		} break;
		}
	}
}

ShaderBakerObjects processParseResult(ParseResult const& parseResult)
{
	auto counts = countParseResult(parseResult);

	auto stringPoolSize = counts.stringCount * sizeof(size_t) + counts.stringPoolSize;
	auto memoryBlockSize =
		+ counts.shaderCount * sizeof(Shader)
		+ counts.programCount * sizeof(Program)
		+ counts.attachedShadersCount * sizeof(Shader*)
		+ counts.renderConfigCount * sizeof(RenderConfig)
		+ stringPoolSize;
	auto memoryBlock = malloc(memoryBlockSize);

	auto shaderStorage = (Shader*) memoryBlock;
	auto programStorage = (Program*) (shaderStorage + counts.shaderCount);
	auto attachedShaderStorage = (Shader**) (programStorage + counts.programCount);
	auto renderConfigStorage = (RenderConfig*) (attachedShaderStorage + counts.attachedShadersCount);
	auto stringPoolBegin = (char*) (renderConfigStorage + counts.renderConfigCount);

	ShaderBakerObjects result = {};
	result.memoryBlock = memoryBlock;
	result.stringPool = stringPoolBegin;
	result.shaders = {shaderStorage, shaderStorage + counts.shaderCount};
	result.programs = {programStorage, programStorage + counts.programCount};
	result.attachedShaders = {attachedShaderStorage, attachedShaderStorage + counts.attachedShadersCount};
	result.renderConfigs = {renderConfigStorage, renderConfigStorage + counts.renderConfigCount};

	StringAllocator stringAllocator{stringPoolBegin};
	processShaders(parseResult, stringAllocator, shaderStorage);
	processPrograms(parseResult, stringAllocator, result.shaders, programStorage, attachedShaderStorage);
	processRenderConfigs(parseResult, stringAllocator, result.programs, renderConfigStorage);

	return result;
}

#include <cstdio>
#include <cstdlib>

#define ArrayEnd(a) (a) + sizeof(a) / sizeof(a[0])
#define ArrayLength(a) sizeof(a) / sizeof(a[0])

struct MemoryArena
{
	char *begin, *end;
	char *next;
	size_t growSize;
};

bool memoryArenaInit(MemoryArena& arena, size_t initialSize, size_t growSize)
{
	auto memory = (char*) malloc(initialSize);
	if (!memory)
	{
		return false;
	}

	arena.begin = memory;
	arena.end = memory + initialSize;
	arena.next = memory;
	arena.growSize = growSize;
	return true;
}

void memoryArenaDestroy(MemoryArena& arena)
{
	free(arena.begin);
}

void memoryArenaReallocate(MemoryArena& arena)
{
//TODO check for overflow in calculation of newSize
	auto newSize = arena.end - arena.begin + arena.growSize;
	auto memory = (char*) realloc(arena.begin, newSize);
	if (!memory)
	{
//TODO out-of-memory. How should this case be handled? Crash the program?
		memoryArenaDestroy(arena);
		assert(false);
	}

	arena.next = arena.next - arena.begin + memory;
	arena.begin = memory;
	arena.end = memory + newSize;
}

void* memoryArenaAllocate(MemoryArena& arena, size_t size)
{
//TODO check for overflow in calculation of requiredSize
	size_t requiredSize = arena.next - arena.begin + size;

	// Use a while loop in case the allocation is very large. This case should rarely,
	// if ever - the arena's reallocation size should be tuned to prevent this.
	while ((size_t) (arena.end - arena.begin) < requiredSize)
	{
		memoryArenaReallocate(arena);
	}

	auto result = arena.next;
	arena.next += size;

	return result;
}

void memoryArenaFree(MemoryArena& arena, size_t size)
{
	assert((size_t) arena.next - (size_t) arena.begin >= size);
	arena.next -= size;
}

size_t memoryArenaIndex(MemoryArena& arena)
{
	return arena.next - arena.begin;
}

struct IndexedString
{
	size_t begin, length;
};

struct ApplicationState
{
	MemoryArena transientArena;

	char elements[1024 * 1024 * 1];

	// 64 errors should be plenty for a person to deal with at once
	ParseError parseErrors[64];
};

inline size_t megabytes(size_t n)
{
	return 1024 * 1024 * n;
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

void printStringSlice(StringSlice str)
{
	char *c = str.begin;
	while (c != str.end)
	{
		putchar(*c);
		++c;
	}
}

void printStringReference(StringReference str)
{
	printStringSlice(stringReferenceDeref(str));
}

const char* shaderTypeToStr(ShaderType type)
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

void printShader(Shader const& shader)
{
	printf("%sShader ", shaderTypeToStr(shader.type));
	printStringReference(shader.name);
	fputs(": path = \"", stdout);
	printStringReference(shader.path);
	fputs("\"\n", stdout);
}

void printProgram(Program const& program)
{
	fputs("Program ", stdout);
	printStringReference(program.name);
	fputs(": ", stdout);
	auto nextAttachedShader = program.attachedShadersBegin;
	while (nextAttachedShader != program.attachedShadersEnd)
	{
		printStringReference((*nextAttachedShader)->name);
		fputs(", ", stdout);
		++nextAttachedShader;
	}
	putchar('\n');
}

const char* drawPrimitiveToString(DrawPrimitive primitive)
{
	switch (primitive)
	{
	case DrawPrimitive::Points: return "Points";
	case DrawPrimitive::Lines: return "Lines";
	case DrawPrimitive::LineStrip: return "LineStrip";
	case DrawPrimitive::LineLoop: return "LineLoop";
	case DrawPrimitive::Triangles: return "Triangles";
	case DrawPrimitive::TriangleStrip: return "TriangleStrip";
	case DrawPrimitive::TriangleFan: return "TriangleFan";
	}
	
	unreachable();
	return "";
}

void printRenderConfig(RenderConfig const& renderConfig)
{
	fputs("RenderConfig ", stdout);
	printStringReference(renderConfig.name);
	printf(
		": renders count=%d %s with program '",
		renderConfig.drawCount,
		drawPrimitiveToString(renderConfig.primitive));
	printStringReference(renderConfig.program->name);
	fputs("'\n", stdout);
}

void printShaderBakerObjects(ShaderBakerObjects const& objects)
{
	puts("\nSHADERS:");
	{
		auto *nextShader = objects.shaders.begin;
		auto *shadersEnd = objects.shaders.end;
		while (nextShader != shadersEnd)
		{
			printShader(*nextShader);
			++nextShader;
		}
	}

	puts("\nPROGRAMS:");
	{
		auto *nextProgram = objects.programs.begin;
		auto *programsEnd = objects.programs.end;
		while (nextProgram != programsEnd)
		{
			printProgram(*nextProgram);
			++nextProgram;
		}
	}

	puts("\nRENDERING CONFIGURATIONS:");
	{
		auto *nextRenderConfig = objects.renderConfigs.begin;
		auto *renderConfigsEnd = objects.renderConfigs.end;
		while (nextRenderConfig != renderConfigsEnd)
		{
			printRenderConfig(*nextRenderConfig);
			++nextRenderConfig;
		}
	}
}

static bool readProjectFile(MemoryArena& arena, char *fileName, IndexedString& result)
{
	auto file = fopen(fileName, "rb");
	if (!file)
	{
		perror("Failed to open project file\n");
		return false;
	}

	result.begin = memoryArenaIndex(arena);
	size_t blockSize = 4096;
	for (;;)
	{
		auto readAddress = memoryArenaAllocate(arena, blockSize);
		auto readSize = fread(readAddress, sizeof(char), blockSize, file);
		if (readSize < blockSize)
		{
			auto extraBytes = blockSize - readSize;
			memoryArenaFree(arena, extraBytes);
			break;
		}
	}
	result.length = memoryArenaIndex(arena) - result.begin;

	auto readFileError = ferror(file);
	if (readFileError)
	{
		perror("Unable to read project file\n");
	} else 
	{
		readFileError = !feof(file);
		if (readFileError)
		{
			puts("Failed to read the entire project file");
		}
	}

	if (fclose(file) != 0)
	{
		fputs("WARNING: ", stdout);
		perror("failed to close project file\n");
	}

	return readFileError == 0;
}

void initParser(ApplicationState& appState, Parser& parser, StringSlice input)
{
	parser.cursor = input.begin;
	parser.end = input.end;

	parser.nextElementBegin = appState.elements;
	parser.elementsEnd = ArrayEnd(appState.elements);

	parser.nextErrorSlot = appState.parseErrors;
	parser.errorsEnd = ArrayEnd(appState.parseErrors);

	parser.lineNumber = 1;
	parser.lineBegin = parser.cursor;
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("Usage: sb-load projectFile\n");
		return 0;
	}
	auto projectFileName = argv[1];

	auto appState = (ApplicationState*) malloc(sizeof(ApplicationState));
	memoryArenaInit(appState->transientArena, megabytes(1), megabytes(1));

	int result = 0;
	IndexedString projectFileContents;
	if (!readProjectFile(appState->transientArena, projectFileName, projectFileContents))
	{
		result = 1;
		goto cleanup;
	}

	{
		StringSlice input = {};
		input.begin = appState->transientArena.begin + projectFileContents.begin;
		input.end = input.begin + projectFileContents.length;

		Parser parser = {};
		initParser(*appState, parser, input);
		parse(parser);

		ParseResult parseResult = {};

		parseResult.version = parser.version;

		parseResult.elementsBegin = appState->elements;
		parseResult.elementsEnd = parser.nextElementBegin;

		parseResult.errorsBegin = appState->parseErrors;
		parseResult.errorsEnd = parser.nextErrorSlot;

		bool hasErrors = parseResult.errorsBegin != parseResult.errorsEnd;
		if (hasErrors)
		{
			printParseErrors(parseResult.errorsBegin, parseResult.errorsEnd);
		} else
		{
			puts("Load successful\n");

			printf("Version %d.%d\n", parseResult.version.major, parseResult.version.minor);

			auto objects = processParseResult(parseResult);
			printShaderBakerObjects(objects);
			free(objects.memoryBlock);
		}
	}

cleanup:
	memoryArenaDestroy(appState->transientArena);
	free(appState);
	return result;
}
