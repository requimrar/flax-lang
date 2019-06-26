// typecheck.h
// Copyright (c) 2014 - 2017, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once
#include "sst.h"

#include "precompile.h"

#include <set>
#include <unordered_set>

namespace parser
{
	struct ParsedFile;
}

namespace frontend
{
	struct CollectorState;
	struct ImportThing;
}

namespace pts
{
	struct Type;
}

namespace ast
{
	struct Stmt;
	struct TypeDefn;
	struct FuncDefn;
	struct FunctionCall;
	struct Parameterisable;
}

namespace fir
{
	struct ConstantNumberType;
}

namespace sst
{
	namespace poly
	{
		struct Solution_t;
	}

	struct StateTree
	{
		StateTree(const std::string& nm, const std::string& filename, StateTree* p, bool anon = false)
			: name(nm), topLevelFilename(filename), parent(p), isAnonymous(anon) { }

		std::string name;
		std::string topLevelFilename;

		StateTree* parent = 0;
		TreeDefn* treeDefn = 0;

		// for those anonymous scopes (with numbers) that we create.
		// currently we only keep track of this for scope-path resolution, so we can skip them
		// when we do '^' -- if not we'll end up in the middle of something and the user doesn't expect
		// there to be scopes where there are no braces!
		bool isAnonymous = false;

		util::hash_map<std::string, StateTree*> subtrees;
		util::hash_map<std::string, std::vector<ast::Parameterisable*>> unresolvedGenericDefs;
		util::hash_map<std::pair<ast::Parameterisable*, util::hash_map<std::string, TypeConstraints_t>>, sst::Defn*> resolvedGenericDefs;

		struct DefnMap
		{
			bool wasPublicImport = false;
			util::hash_map<std::string, std::vector<Defn*>> defns;
		};

		// maps from filename to defnmap -- allows tracking definitions by where they came from
		// so we can resolve the import duplication bullshit
		util::hash_map<std::string, DefnMap> definitions;

		// what's there to explain? a simple map of operators to their functions. we use
		// function overload resolution to determine which one to call, and ambiguities are
		// handled the usual way.
		util::hash_map<std::string, std::vector<sst::FunctionDefn*>> infixOperatorOverloads;
		util::hash_map<std::string, std::vector<sst::FunctionDefn*>> prefixOperatorOverloads;
		util::hash_map<std::string, std::vector<sst::FunctionDefn*>> postfixOperatorOverloads;

		std::vector<std::string> getScope();
		StateTree* searchForName(const std::string& name);

		util::hash_map<std::string, std::vector<Defn*>> getAllDefinitions();

		std::vector<Defn*> getDefinitionsWithName(const std::string& name);
		std::vector<ast::Parameterisable*> getUnresolvedGenericDefnsWithName(const std::string& name);

		void addDefinition(const std::string& name, Defn* def, const TypeParamMap_t& gmaps = { });
		void addDefinition(const std::string& sourceFile, const std::string& name, Defn* def, const TypeParamMap_t& gmaps = { });
	};

	struct DefinitionTree
	{
		DefinitionTree(StateTree* st) : base(st) { }

		StateTree* base = 0;
		NamespaceDefn* topLevel = 0;
		std::unordered_set<std::string> thingsImported;

		util::hash_map<fir::Type*, TypeDefn*> typeDefnMap;
	};

	struct TypecheckState
	{
		TypecheckState(StateTree* st) : dtree(new DefinitionTree(st)), stree(dtree->base) { }

		std::string moduleName;

		DefinitionTree* dtree = 0;
		StateTree* stree = 0;

		util::hash_map<fir::Type*, TypeDefn*> typeDefnMap;

		std::vector<Location> locationStack;

		void pushLoc(const Location& l);
		void pushLoc(ast::Stmt* stmt);

		std::vector<int> bodyStack;
		std::vector<FunctionDefn*> currentFunctionStack;
		bool isInFunctionBody();


		FunctionDefn* getCurrentFunction();
		void enterFunctionBody(FunctionDefn* fn);
		void leaveFunctionBody();


		std::vector<Expr*> subscriptArrayStack;
		Expr* getCurrentSubscriptArray();
		void enterSubscript(Expr* arr);
		void leaveSubscript();
		bool isInSubscript();


		std::vector<TypeDefn*> structBodyStack;
		TypeDefn* getCurrentStructBody();
		bool isInStructBody();
		void enterStructBody(TypeDefn* str);
		void leaveStructBody();


		std::vector<TypeParamMap_t> genericContextStack;
		std::vector<TypeParamMap_t> getGenericContextStack();


		void pushGenericContext();
		fir::Type* findGenericMapping(const std::string& name, bool allowFail);
		void addGenericMapping(const std::string& name, fir::Type* ty);
		void removeGenericMapping(const std::string& name);
		void popGenericContext();


		int breakableBodyNest = 0;
		void enterBreakableBody();
		void leaveBreakableBody();
		bool isInBreakableBody();

		int deferBlockNest = 0;
		void enterDeferBlock();
		void leaveDeferBlock();
		bool isInDeferBlock();

		Location loc();
		Location popLoc();

		void pushTree(const std::string& name, bool createAnonymously = false);
		StateTree* popTree();

		void pushAnonymousTree();

		std::string serialiseCurrentScope();
		std::vector<std::string> getCurrentScope();
		void teleportToScope(const std::vector<std::string>& scope);
		StateTree* getTreeOfScope(const std::vector<std::string>& scope);

		std::vector<Defn*> getDefinitionsWithName(const std::string& name, StateTree* tree = 0);
		bool checkForShadowingOrConflictingDefinition(Defn* def, std::function<bool (TypecheckState* fs, Defn* other)> checkConflicting, StateTree* tree = 0);

		fir::Type* getBinaryOpResultType(fir::Type* a, fir::Type* b, const std::string& op, sst::FunctionDefn** overloadFn = 0);

		// things that i might want to make non-methods someday
		fir::Type* convertParserTypeToFIR(pts::Type* pt, bool allowFailure = false);
		fir::Type* inferCorrectTypeForLiteral(fir::ConstantNumberType* lit);


		fir::Type* checkIsBuiltinConstructorCall(const std::string& name, const std::vector<FnCallArgument>& arguments);


		bool checkAllPathsReturn(FunctionDefn* fn);

		std::pair<util::hash_map<std::string, size_t>, SimpleError> verifyStructConstructorArguments(const std::string& name,
			const std::set<std::string>& fieldNames, const std::vector<FnCallArgument>& params);

		DecompMapping typecheckDecompositions(const DecompMapping& bind, fir::Type* rhs, bool immut, bool allowref);

		int getOverloadDistance(const std::vector<fir::Type*>& a, const std::vector<fir::Type*>& b);
		bool isDuplicateOverload(const std::vector<FnParam>& a, const std::vector<FnParam>& b);
	};

	DefinitionTree* typecheck(frontend::CollectorState* cs, const parser::ParsedFile& file,
		const std::vector<std::pair<frontend::ImportThing, StateTree*>>& imports, bool addPreludeDefinitions);


	StateTree* addTreeToExistingTree(StateTree* to, StateTree* from, StateTree* commonParent, bool pubImport, bool ignoreVis);

	std::vector<TypeParamMap_t> collateGenericArgStacks();
}















