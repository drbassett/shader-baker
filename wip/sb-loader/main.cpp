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
#include "ShaderBakerObjects.h"
#include "loader.h"
#include "parser.h"

#include <cstdio>

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
	LoaderError loaderErrors[64];
};

inline size_t megabytes(size_t n)
{
	return 1024 * 1024 * n;
}

char* loaderErrorTypeToString(LoaderErrorType errorType)
{
	switch (errorType)
	{
	case LoaderErrorType::MissingVersionStatement:
		return "The Version statement must be the first one";
	case LoaderErrorType::MissingMajorVersion:
		return "Missing major version";
	case LoaderErrorType::MissingMinorVersion:
		return "Missing minor version";
	case LoaderErrorType::VersionMissingDot:
		return "Expected '.' character after major version";
	case LoaderErrorType::UnsupportedVersion:
		return "Version is unsupported by this version of Shader Baker";
	case LoaderErrorType::MissingBlockBegin:
		return "Missing start of block";
	case LoaderErrorType::UnclosedBlock:
		return "Unclosed block";
	case LoaderErrorType::MissingPathBegin:
		return "Missing start of path";
	case LoaderErrorType::UnclosedPath:
		return "Unclosed path";
	case LoaderErrorType::MissingIdentifier:
		return "Statement must begin with an identifier";
	case LoaderErrorType::MissingBlockType:
		return "Statement type must follow identifier";
	case LoaderErrorType::EmptyTupleWord:
		return "Tuple contains an empty word";
	case LoaderErrorType::InvalidWordCharacter:
		return "Invalid character in word";
	case LoaderErrorType::RenderConfigMissingProgram:
		return "Missing Program block in a RenderConfig block";
	case LoaderErrorType::RenderConfigMultiplePrograms:
		return "Multiple Program blocks declared in a RenderConfig block";
	case LoaderErrorType::RenderConfigMissingPrimitive:
		return "Missing Primitive block in a RenderConfig block";
	case LoaderErrorType::UnknownDrawPrimitive:
		return "Unknown draw primitive type in a Primitive block";
	case LoaderErrorType::RenderConfigMultiplePrimitives:
		return "Multiple Primitive blocks declared in a RenderConfig block";
	case LoaderErrorType::RenderConfigMissingCount:
		return "Missing Count block in a RenderConfig block";
	case LoaderErrorType::RenderConfigMultipleCounts:
		return "Multiple Count blocks declared in a RenderConfig block";
	case LoaderErrorType::RenderConfigEmptyProgramName:
		return "Program name is empty";
	case LoaderErrorType::RenderConfigEmptyOrInvalidCount:
		return "Count value is empty or invalid";
	case LoaderErrorType::UnexpectedBlockType:
		return "Unexpected block type";
	case LoaderErrorType::OutOfElementSpace:
		return "Too many elements!";
	case LoaderErrorType::DuplicateShaderName:
		return "Another shader already has this name";
	case LoaderErrorType::DuplicateProgramName:
		return "Another program already has this name";
	case LoaderErrorType::ProgramUnresolvedAttachedShaderName:
		return "No shader has this name";
	case LoaderErrorType::DuplicateRenderConfigName:
		return "Another rendering configuration already has this name";
	case LoaderErrorType::RenderConfigUnresolvedProgramName:
		return "No program has this name";
	default:
		unreachable();
		return "";
	}
}

void printLoaderErrors(LoaderError* begin, LoaderError* end)
{
	while (begin != end)
	{
		printf(
			"ERROR %d (line %d, character %d): %s\n",
			(unsigned) begin->type,
			begin->location.lineNumber,
			begin->location.charNumber,
			loaderErrorTypeToString(begin->type));
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

	parser.errorCollector.next = appState.loaderErrors;
	parser.errorCollector.end = ArrayEnd(appState.loaderErrors);

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

		auto version = parser.version;

		auto errorsBegin = appState->loaderErrors;
		auto parserErrorsEnd = parser.errorCollector.next;
		bool hasParseErrors = errorsBegin != parserErrorsEnd;
		if (hasParseErrors)
		{
			puts("Parsing failed\n");
			printLoaderErrors(errorsBegin, parserErrorsEnd);
		} else
		{
			LoaderErrorCollector errorCollector{errorsBegin, ArrayEnd(appState->loaderErrors)};
			auto objects = processParseElements(
				ParseElements{appState->elements, parser.nextElementBegin}, errorCollector);
			auto loaderErrorsEnd = errorCollector.next;
			bool hasSemanticErrors = errorsBegin != loaderErrorsEnd;
			if (hasSemanticErrors)
			{
				puts("Loading failed\n");
				printLoaderErrors(errorsBegin, loaderErrorsEnd);
			} else
			{
				printf("Version %d.%d\n", version.major, version.minor);
				printShaderBakerObjects(objects);
			}

			free(objects.memoryBlock);
		}
	}

cleanup:
	memoryArenaDestroy(appState->transientArena);
	free(appState);
	return result;
}
