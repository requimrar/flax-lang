// CmdLineArgs.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include <iostream>
#include <fstream>
#include <cassert>
#include <sys/stat.h>
#include <sys/types.h>

#include <vector>
#include "compiler.h"

static std::string parseQuotedString(char** argv, int& i)
{
	std::string ret;
	if(strlen(argv[i]) > 0)
	{
		if(argv[i][0] == '"' || argv[i][0] == '\'')
		{
			while(std::string(argv[i]).back() != '\'' && std::string(argv[i]).back() != '"')
			{
				ret += " " + std::string(argv[i]);
				i++;
			}
		}
		else
		{
			ret = argv[i];
		}
	}
	return ret;
}

namespace Compiler
{
	static bool printModule = false;
	bool getDumpLlvm()
	{
		return printModule;
	}

	static bool printFIR = false;
	bool getDumpFir()
	{
		return printFIR;
	}


	static bool compileOnly = false;
	bool getIsCompileOnly()
	{
		return compileOnly;
	}

	#define DEFAULT_PREFIX		"/usr/local/lib/flaxlibs/"
	std::string getPrefix()
	{
		return DEFAULT_PREFIX;
	}


	static std::string sysroot;
	std::string getSysroot()
	{
		return sysroot;
	}

	static std::string target;
	std::string getTarget()
	{
		return target;
	}

	static int optLevel;
	int getOptimisationLevel()
	{
		return optLevel;
	}

	static uint64_t Flags;
	bool getFlag(Flag f)
	{
		return (Flags & (uint64_t) f);
	}

	static bool isPIC = false;
	bool getIsPositionIndependent()
	{
		return isPIC;
	}

	static std::string mcmodel;
	std::string getMcModel()
	{
		return mcmodel;
	}

	static bool noLowercaseTypes;
	bool getDisableLowercaseBuiltinTypes()
	{
		return noLowercaseTypes;
	}

	static bool printClangOutput;
	bool getPrintClangOutput()
	{
		return printClangOutput;
	}

	static bool runProgramWithJit;
	bool getRunProgramWithJit()
	{
		return runProgramWithJit;
	}

	static bool noAutoGlobalConstructor = false;
	bool getNoAutoGlobalConstructor()
	{
		return noAutoGlobalConstructor;
	}


	static std::vector<Warning> enabledWarnings {
		Warning::UnusedVariable,
		Warning::UseBeforeAssign,
		Warning::UseAfterFree
	};

	bool getWarningEnabled(Warning warning)
	{
		return std::find(enabledWarnings.begin(), enabledWarnings.end(), warning) != enabledWarnings.end();
	}

	static void setWarning(Warning warning, bool enabled)
	{
		auto it = std::find(enabledWarnings.begin(), enabledWarnings.end(), warning);
		if(it == enabledWarnings.end() && enabled == true)
			enabledWarnings.push_back(warning);

		else if(it != enabledWarnings.end() && enabled == false)
			enabledWarnings.erase(it);
	}

	std::pair<std::string, std::string> parseCmdLineArgs(int argc, char** argv)
	{
		// parse arguments
		std::string filename;
		std::string outname;

		if(argc > 1)
		{
			// parse the command line opts
			for(int i = 1; i < argc; i++)
			{
				if(!strcmp(argv[i], "-sysroot"))
				{
					if(i != argc - 1)
					{
						i++;
						Compiler::sysroot = parseQuotedString(argv, i);
						continue;
					}
					else
					{
						fprintf(stderr, "Error: Expected directory name after '-sysroot' option\n");
						exit(-1);
					}
				}
				if(!strcmp(argv[i], "-target"))
				{
					if(i != argc - 1)
					{
						i++;
						Compiler::target = parseQuotedString(argv, i);
						continue;
					}
					else
					{
						fprintf(stderr, "Error: Expected target string after '-target' option\n");
						exit(-1);
					}
				}
				else if(!strcmp(argv[i], "-o"))
				{
					if(i != argc - 1)
					{
						i++;
						outname = parseQuotedString(argv, i);
						continue;
					}
					else
					{
						fprintf(stderr, "Error: Expected filename name after '-o' option\n");
						exit(-1);
					}
				}
				else if(!strcmp(argv[i], "-fPIC"))
				{
					Compiler::isPIC = true;
				}
				else if(!strcmp(argv[i], "-mcmodel"))
				{
					if(i != argc - 1)
					{
						i++;
						std::string mm = parseQuotedString(argv, i);
						if(mm != "kernel" && mm != "small" && mm != "medium" && mm != "large")
						{
							fprintf(stderr, "Error: valid options for '-mcmodel' are 'small', 'medium', 'large' and 'kernel'.\n");
							exit(-1);
						}

						Compiler::mcmodel = mm;
					}
					else
					{
						fprintf(stderr, "Error: Expected mcmodel name after '-mcmodel' option\n");
						exit(-1);
					}
				}
				else if(!strcmp(argv[i], "-Werror"))
				{
					Compiler::Flags |= (uint64_t) Compiler::Flag::WarningsAsErrors;
				}
				else if(!strcmp(argv[i], "-w"))
				{
					Compiler::Flags |= (uint64_t) Compiler::Flag::NoWarnings;
				}
				else if(!strcmp(argv[i], "-print-ir"))
				{
					Compiler::printModule = true;
				}
				else if(!strcmp(argv[i], "-print-fir"))
				{
					Compiler::printFIR = true;
				}
				else if(!strcmp(argv[i], "-no-lowercase-builtin"))
				{
					Compiler::noLowercaseTypes = true;
				}
				else if(!strcmp(argv[i], "-c"))
				{
					Compiler::compileOnly = true;
				}
				else if(!strcmp(argv[i], "-show-clang"))
				{
					Compiler::printClangOutput = true;
				}
				else if(!strcmp(argv[i], "-jit") || !strcmp(argv[i], "-run"))
				{
					Compiler::runProgramWithJit = true;
				}
				else if(!strcmp(argv[i], "-no-auto-gconstr"))
				{
					Compiler::noAutoGlobalConstructor = true;
				}
				else if(strstr(argv[i], "-O") == argv[i])
				{
					// make sure we have at least 3 chars
					if(strlen(argv[i]) < 3)
					{
						fprintf(stderr, "Error: '-O' is not a valid option on its own\n");
						exit(-1);
					}

					if(argv[i][2] == 'x')
					{
						// literally nothing
						Compiler::optLevel = -1;
					}
					else
					{
						Compiler::optLevel = argv[i][2] - '0';
					}
				}

				// warnings.
				else if(!strcmp(argv[i], "-Wno-unused-variable"))
				{
					Compiler::setWarning(Compiler::Warning::UnusedVariable, false);
				}
				else if(!strcmp(argv[i], "-Wunused-variable"))
				{
					Compiler::setWarning(Compiler::Warning::UnusedVariable, true);
				}

				else if(!strcmp(argv[i], "-Wno-unused"))
				{
					Compiler::setWarning(Compiler::Warning::UnusedVariable, false);
				}
				else if(!strcmp(argv[i], "-Wunused"))
				{
					Compiler::setWarning(Compiler::Warning::UnusedVariable, true);
				}


				else if(!strcmp(argv[i], "-Wno-var-state-checker"))
				{
					Compiler::setWarning(Compiler::Warning::UseAfterFree, false);
					Compiler::setWarning(Compiler::Warning::UseBeforeAssign, false);
				}
				else if(!strcmp(argv[i], "-Wvar-state-checker"))
				{
					Compiler::setWarning(Compiler::Warning::UseAfterFree, true);
					Compiler::setWarning(Compiler::Warning::UseBeforeAssign, true);
				}
				else if(argv[i][0] == '-')
				{
					fprintf(stderr, "Error: Unrecognised option '%s'\n", argv[i]);

					exit(-1);
				}
				else
				{
					filename = argv[i];
					break;
				}
			}
		}
		else
		{
			fprintf(stderr, "Expected at least one argument\n");
			exit(-1);
		}

		return { filename, outname };
	}
}


















