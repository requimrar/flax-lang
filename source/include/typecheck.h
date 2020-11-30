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
		StateTree(const std::string& nm, StateTree* p, bool anon = false) : name(nm), parent(p), isAnonymous(anon) { }

		std::string name;

		StateTree* parent = 0;

		// for those anonymous scopes (with numbers) that we create.
		// currently we only keep track of this for scope-path resolution, so we can skip them
		// when we do '^' -- if not we'll end up in the middle of something and the user doesn't expect
		// there to be scopes where there are no braces!
		bool isAnonymous = false;

		// this flag allows us to skip definitions that are generated by the compiler. every module
		// needs these definitions to be generated, but they should *not* be imported into other modules
		// like normal things.
		bool isCompilerGenerated = false;

		util::hash_map<std::string, StateTree*> subtrees;
		util::hash_map<std::string, std::vector<ast::Parameterisable*>> unresolvedGenericDefs;
		util::hash_map<std::pair<ast::Parameterisable*, util::hash_map<std::string, TypeConstraints_t>>, sst::Defn*> resolvedGenericDefs;

		util::hash_map<std::string, std::vector<Defn*>> definitions2;

		std::vector<Defn*> exports;
		std::vector<StateTree*> imports;
		std::vector<StateTree*> reexports;

		StateTree* proxyOf = nullptr;

		// this is a mapping from every StateTree in `imports` to the location of the `import` or `using` statement.
		// it only used for error-reporting, so it is stored out-of-line so the usage of `this->imports` is not more
		// cumbersome than it needs to be. the string in the pair stores the name of the module *IMPORTING* the module,
		// not the name of the module being imported (that can be gotten from the StateTree).
		std::map<const StateTree*, std::pair<Location, std::string>> importMetadata;

		// the same thing, but for reexports (ie. public imports).
		std::map<const StateTree*, std::pair<Location, std::string>> reexportMetadata;


		// what's there to explain? a simple map of operators to their functions. we use
		// function overload resolution to determine which one to call, and ambiguities are
		// handled the usual way.
		util::hash_map<std::string, std::vector<sst::FunctionDefn*>> infixOperatorOverloads;
		util::hash_map<std::string, std::vector<sst::FunctionDefn*>> prefixOperatorOverloads;
		util::hash_map<std::string, std::vector<sst::FunctionDefn*>> postfixOperatorOverloads;

		Scope cachedScope;
		const Scope& getScope();

		StateTree* findSubtree(const std::string& name);
		StateTree* findOrCreateSubtree(const std::string& name, bool anonymous = false);

		std::vector<Defn*> getAllDefinitions();

		std::vector<Defn*> getDefinitionsWithName(const std::string& name);
		std::vector<ast::Parameterisable*> getUnresolvedGenericDefnsWithName(const std::string& name);

		void addDefinition(const std::string& name, Defn* def, const TypeParamMap_t& gmaps = { });
	};

	struct DefinitionTree
	{
		DefinitionTree(StateTree* st) : base(st) { }

		StateTree* base = 0;
		NamespaceDefn* topLevel = 0;
		std::unordered_set<std::string> thingsImported;

		util::hash_map<fir::Type*, TypeDefn*> typeDefnMap;
		util::hash_map<std::string, sst::Defn*> compilerSupportDefinitions;
	};

	struct TypecheckState
	{
		TypecheckState(StateTree* st) : dtree(new DefinitionTree(st)), stree(dtree->base), typeDefnMap(dtree->typeDefnMap) { }

		std::string moduleName;

		DefinitionTree* dtree = 0;
		StateTree* stree = 0;

		util::hash_map<fir::Type*, TypeDefn*>& typeDefnMap;

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


		std::vector<fir::Type*> selfContextStack;
		fir::Type* getCurrentSelfContext();
		void pushSelfContext(fir::Type* str);
		void popSelfContext();
		bool hasSelfContext();


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

		Scope getCurrentScope2();

		std::vector<StateTree*> teleportationStack;
		void teleportInto(const Scope& scope);
		void teleportOut();



		std::vector<Defn*> getDefinitionsWithName(const std::string& name, StateTree* tree = 0);
		ErrorMsg* checkForShadowingOrConflictingDefinition(Defn* def,
			std::function<bool (TypecheckState* fs, Defn* other)> checkConflicting, StateTree* tree = 0);

		fir::Type* getBinaryOpResultType(fir::Type* a, fir::Type* b, const std::string& op, sst::FunctionDefn** overloadFn = 0);

		// things that i might want to make non-methods someday
		fir::Type* convertParserTypeToFIR(pts::Type* pt, bool allowFailure = false);
		fir::Type* inferCorrectTypeForLiteral(fir::ConstantNumberType* lit);

		fir::Type* checkIsBuiltinConstructorCall(const std::string& name, const std::vector<FnCallArgument>& arguments);

		bool checkAllPathsReturn(FunctionDefn* fn);

		std::pair<util::hash_map<std::string, size_t>, SimpleError> verifyStructConstructorArguments(const std::string& name,
			const std::set<std::string>& fieldNames, const std::vector<FnCallArgument>& params);

		DecompMapping typecheckDecompositions(const DecompMapping& bind, fir::Type* rhs, bool immut, bool allowref);
	};



	bool isDuplicateOverload(const std::vector<FnParam>& a, const std::vector<FnParam>& b);
	int getOverloadDistance(const std::vector<fir::Type*>& a, const std::vector<fir::Type*>& b);

	DefinitionTree* typecheck(frontend::CollectorState* cs, const parser::ParsedFile& file,
		const std::vector<std::pair<frontend::ImportThing, DefinitionTree*>>& imports, bool addPreludeDefinitions);

	void mergeExternalTree(const Location& importer, const char* kind, sst::StateTree* base, sst::StateTree* branch);
}















