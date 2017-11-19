// typecheck.h
// Copyright (c) 2014 - 2017, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#pragma once
#include "defs.h"
#include "sst.h"

#include "precompile.h"

#include <unordered_set>

namespace parser
{
	struct ParsedFile;
}

namespace frontend
{
	struct CollectorState;
}

namespace pts
{
	struct Type;
}

namespace ast
{
	struct Stmt;
	struct FuncDefn;
}

namespace sst
{
	struct StateTree
	{
		StateTree(std::string nm, std::string filename, StateTree* p) : name(nm), topLevelFilename(filename), parent(p) { }

		std::string name;
		std::string topLevelFilename;

		StateTree* parent = 0;

		std::unordered_map<std::string, StateTree*> subtrees;
		std::unordered_map<std::string, std::vector<ast::FuncDefn*>> unresolvedGenericFunctions;

		using DefnMap = std::unordered_map<std::string, std::vector<Defn*>>;

		// maps from filename to defnmap -- allows tracking definitions by where they came from
		// so we can resolve the import duplication bullshit
		std::unordered_map<std::string, DefnMap> definitions;

		DefnMap getAllDefinitions();

		std::vector<Defn*> getDefinitionsWithName(std::string name);
		void addDefinition(std::string name, Defn* def);
		void addDefinition(std::string sourceFile, std::string name, Defn* def);
	};

	struct DefinitionTree
	{
		DefinitionTree(StateTree* st) : base(st) { }

		StateTree* base = 0;
		NamespaceDefn* topLevel = 0;
		std::unordered_set<std::string> thingsImported;
	};

	struct TypecheckState
	{
		TypecheckState(StateTree* st) : dtree(new DefinitionTree(st)), stree(dtree->base) { }

		std::string moduleName;
		sst::NamespaceDefn* topLevelNamespace = 0;

		DefinitionTree* dtree = 0;
		StateTree*& stree;

		std::unordered_map<fir::Type*, TypeDefn*> typeDefnMap;

		std::vector<std::unordered_map<std::string, VarDefn*>> symbolTableStack;

		std::vector<Location> locationStack;

		void pushLoc(const Location& l);
		void pushLoc(ast::Stmt* stmt);

		std::vector<FunctionDefn*> currentFunctionStack;
		bool isInFunctionBody();

		FunctionDefn* getCurrentFunction();
		void enterFunctionBody(FunctionDefn* fn);
		void leaveFunctionBody();

		int breakableBodyNest = 0;
		void enterBreakableBody();
		void leaveBreakableBody();
		bool isInBreakableBody();

		int deferBlockNest = 0;
		void enterDeferBlock();
		void leaveDeferBlock();
		bool isInDeferBlock();

		std::string getAnonymousScopeName();

		Location loc();
		Location popLoc();

		void pushTree(std::string name);
		StateTree* popTree();

		StateTree* recursivelyFindTreeUpwards(std::string name);

		std::string serialiseCurrentScope();
		std::vector<std::string> getCurrentScope();
		void teleportToScope(const std::vector<std::string>& scope);

		std::vector<Defn*> getDefinitionsWithName(std::string name, StateTree* tree = 0);
		bool checkForShadowingOrConflictingDefinition(Defn* def, std::string kind,
			std::function<bool (TypecheckState* fs, Defn* other)> checkConflicting, StateTree* tree = 0);

		fir::Type* getBinaryOpResultType(fir::Type* a, fir::Type* b, Operator op);

		// things that i might want to make non-methods someday
		fir::Type* convertParserTypeToFIR(pts::Type* pt);
		fir::Type* inferCorrectTypeForLiteral(Expr* lit);

		bool checkAllPathsReturn(FunctionDefn* fn);

		struct PrettyError
		{
			std::string errorStr;
			std::vector<std::pair<Location, std::string>> infoStrs;
		};

		Defn* resolveFunction(std::string name, std::vector<FunctionDecl::Param> arguments, PrettyError* errs, bool traverseUp);
		Defn* resolveFunctionFromCandidates(std::vector<Defn*> fs, std::vector<FunctionDecl::Param> arguments,
			PrettyError* errs, bool allowImplicitSelf);
	};

	DefinitionTree* typecheck(frontend::CollectorState* cs, const parser::ParsedFile& file,
		std::vector<std::pair<std::string, StateTree*>> imports);
}














