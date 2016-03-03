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

#define ArrayLength(a) sizeof(a) / sizeof(a[0])
#define ArrayEnd(a) (a) + sizeof(a) / sizeof(a[0])

struct ApplicationState
{
	char elements[1024 * 1024 * 1];

	// 64 errors should be plenty for a person to deal with at once
	LoaderError loaderErrors[64];
};

inline size_t kilobytes(size_t n)
{
	return 1024 * n;
}

inline size_t megabytes(size_t n)
{
	return 1024 * kilobytes(n);
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

static bool readProjectFile(char *fileName, char*& fileContents, size_t& fileSize)
{
	// 128 KB should be more than enough space to read in the project
	// file, for the foreseeable future. If not, the loop below is robust
	// enough to allocate more space.
	auto blockSize = kilobytes(128);
	fileContents = (char*) malloc(blockSize);
	if (fileContents == nullptr)
	{
		puts("ERROR: unable to allocate enough memory to read project file");
		return false;
	}

	auto file = fopen(fileName, "rb");
	if (!file)
	{
		perror("Failed to open project file\n");
		return false;
	}
	
	fileSize = 0;
	bool result = true;
	for (;;)
	{
		auto readAddress = fileContents + fileSize;
		auto readSize = fread(readAddress, sizeof(char), blockSize, file);
		fileSize += blockSize;
		if (readSize < blockSize)
		{
			auto extraBytes = blockSize - readSize;
			fileSize -= extraBytes;
			break;
		}

		auto newMemory = (char*) realloc(fileContents, fileSize + blockSize);
		if (newMemory == nullptr)
		{
			free(fileContents);
			fileContents = nullptr;
			fileSize = 0;
			result = false;
			puts("ERROR: unable to allocate enough memory to read project file");
			goto closeFile;
		}

		fileContents = newMemory;
	}

	if (ferror(file))
	{
		perror("Unable to read project file\n");
		result = false;
		goto closeFile;
	}

	if (!feof(file))
	{
		puts("Failed to read the entire project file");
		result = false;
		goto closeFile;
	}

closeFile:
	if (fclose(file) != 0)
	{
		fputs("WARNING: ", stdout);
		perror("failed to close project file\n");
	}

	return result;
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

	int result = 0;
	char* projectFileContents;
	size_t projectFileLength;
	if (!readProjectFile(projectFileName, projectFileContents, projectFileLength))
	{
		result = 1;
	} else
	{
		Parser parser = {};
		StringSlice input{projectFileContents, projectFileContents + projectFileLength};
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
			result = 1;
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
				result = 1;
			} else
			{
				printf("Version %d.%d\n", version.major, version.minor);
				printShaderBakerObjects(objects);
			}

			free(objects.memoryBlock);
		}

		free(projectFileContents);
	}

	free(appState);
	return result;
}
