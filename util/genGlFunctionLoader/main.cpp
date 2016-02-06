#include <cstdio>
#include <cstdlib>
#include <cctype>

#define MallocStruct(type) (type*) malloc(sizeof(type))
#define ArrayLength(a) (sizeof(a) / sizeof(a[0]))
// #define ArrayEnd(a) ((a) + sizeof(a) / sizeof(a[0]))

struct FunctionName
{
	size_t nameLength;
	char *name;
};

struct FunctionNames
{
	size_t numNames;
	FunctionName names[1024];
	char nameStore[1024 * 64];
};

inline char toUppercase(char c)
{
	if (c >= 'a' && c <= 'z')
	{
		return c - ('a' - 'A');
	} else
	{
		return c;
	}
}

void writeUglyProcName(
	FunctionName functionName, FILE *file)
{
	fputs("PFN", file);
	auto name = functionName.name;
	auto nameLength = functionName.nameLength;
	for (size_t j = 0; j < nameLength; ++j)
	{
		fputc(toUppercase(name[j]), file);
	}
	fputs("PROC", file);
}

inline void writeFunctionName(FunctionName functionName, FILE *file)
{
	fwrite(functionName.name, sizeof(char), functionName.nameLength, file);
}

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		printf("\nUsage: gen-fcn-ptrs inputFile outputFile\n");
		return 1;
	}

	char *inputFileName = argv[1];
	char *outputFileName = argv[2];

	auto inputFile = fopen(inputFileName, "rb");
	if (!inputFile)
	{
		perror("Failed to open input file.");
		return 1;
	}

	auto *functionNames = MallocStruct(FunctionNames);
	functionNames->numNames = 0;

	auto numCharsInFile = fread(functionNames->nameStore, sizeof(char), sizeof(functionNames->nameStore), inputFile);
	if (!feof(inputFile))
	{
		printf("Not enough memory to read input file. Increase the size of FunctionNames.nameStore.\n");
		return 1;
	}

	fclose(inputFile);

	// scan the file contents for names
	char *nameStart = functionNames->nameStore;
	char *nameStoreEnd = functionNames->nameStore + numCharsInFile;
	for(;;)
	{
		while (nameStart != nameStoreEnd && std::isspace(*nameStart))
		{
			++nameStart;
		}

		char *nameEnd = nameStart;
		while (nameEnd != nameStoreEnd && !std::isspace(*nameEnd))
		{
			++nameEnd;
		}

		if (nameEnd != nameStart)
		{
			auto numFunctionNames = functionNames->numNames;
			if (numFunctionNames == ArrayLength(functionNames->names))
			{
				printf("Not enough space for all function names. Increase the size of FunctionNames.names.\n");
				return 1;
			}

			FunctionName functionName;
			functionName.nameLength = nameEnd - nameStart;
			functionName.name = nameStart;

			functionNames->names[numFunctionNames] = functionName;
			++(functionNames->numNames);
			nameStart = nameEnd + 1;
		}

		if (nameEnd == nameStoreEnd)
		{
			break;
		}
	}

	auto outputFile = fopen(outputFileName, "wb");
	if (!outputFile)
	{
		perror("Failed to open output file");
		return 1;
	}

	// write function pointer variable block
	for (size_t i = 0; i < functionNames->numNames; ++i)
	{
		auto functionName = functionNames->names[i];
		writeUglyProcName(functionName, outputFile);
		fputc(' ', outputFile);
		writeFunctionName(functionName, outputFile);
		fputs(" = 0;\n", outputFile);
	}

	// write init function
	fputs("\nbool initGlFunctions()\n{\n", outputFile);
	for (size_t i = 0; i < functionNames->numNames; ++i)
	{
		auto functionName = functionNames->names[i];
		fputc('\t', outputFile);
		writeFunctionName(functionName, outputFile);
		fputs(" = (", outputFile);
		writeUglyProcName(functionName, outputFile);
		fputs(") wglGetProcAddress(\"", outputFile);
		writeFunctionName(functionName, outputFile);
		fputs("\");\n", outputFile);
	}
	fputs("\n//TODO check that all function values are non-null\n\treturn true;\n}\n", outputFile);

	if (fclose(outputFile) != 0)
	{
		perror("Failed to close output file. Its contents may not have been written.\n");
		return 1;
	}

	return 0;
}
