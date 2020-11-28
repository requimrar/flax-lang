// toplevel.cpp
// Copyright (c) 2014 - 2017, zhiayang
// Licensed under the Apache License Version 2.0.

#include "defs.h"
#include "sst.h"
#include "ast.h"
#include "pts.h"
#include "errors.h"
#include "parser.h"
#include "frontend.h"
#include "typecheck.h"
#include "string_consts.h"

#include "ir/type.h"
#include "memorypool.h"

using namespace ast;

namespace sst
{
	struct OsStrings
	{
		std::string name;
		std::string vendor;
	};
	static OsStrings getOsStrings();
	static void generatePreludeDefinitions(TypecheckState* fs);

	static void mergeTrees(StateTree* base, StateTree* branch)
	{
	}

	static void checkConflictingDefinitions(sst::StateTree* base, sst::StateTree* branch)
	{
		// for(const auto& [ _, defns ] : base->definitions)
		// {
		// 	for(const auto& [ name, def ] : defns.defns)
		// 	{

		// 	}
		// }
	}

	void mergeExternalTree(sst::StateTree* base, sst::StateTree* branch)
	{

	}













	using frontend::CollectorState;
	DefinitionTree* typecheck(CollectorState* cs, const parser::ParsedFile& file,
		const std::vector<std::pair<frontend::ImportThing, DefinitionTree*>>& imports, bool addPreludeDefinitions)
	{
		StateTree* tree = new StateTree(file.moduleName, file.name, 0);
		tree->treeDefn = util::pool<TreeDefn>(Location());
		tree->treeDefn->enclosingScope = Scope();
		tree->treeDefn->tree = tree;

		auto fs = new TypecheckState(tree);

		for(auto [ ithing, import ] : imports)
		{
			info(ithing.loc, "import: %s", ithing.name);

			auto ias = ithing.importAs;
			if(ias.empty())
				ias = cs->parsed[ithing.name].modulePath + cs->parsed[ithing.name].moduleName;

			StateTree* insertPoint = tree;
			if(ias.size() == 1 && ias[0] == "_")
			{
				// do nothing.
				// insertPoint = tree;
			}
			else
			{
				StateTree* curinspt = insertPoint;

				// iterate through the import-as list, which is a list of nested scopes to import into
				// eg we can `import foo as some::nested::namespace`, which means we need to create
				// the intermediate trees.

				for(const auto& impas : ias)
				{
					if(impas == curinspt->name)
					{
						// skip it.
					}
					else if(auto it = curinspt->subtrees.find(impas); it != curinspt->subtrees.end())
					{
						curinspt = it->second;
					}
					else
					{
						auto newinspt = util::pool<sst::StateTree>(impas, file.name, curinspt);
						curinspt->subtrees[impas] = newinspt;

						auto treedef = util::pool<sst::TreeDefn>(cs->dtrees[ithing.name]->topLevel->loc);
						treedef->id = Identifier(impas, IdKind::Name);
						treedef->tree = newinspt;

						// this is the parent scope!
						treedef->enclosingScope = Scope(curinspt);

						treedef->tree->treeDefn = treedef;
						treedef->visibility = ithing.pubImport
							? VisibilityLevel::Public
							: VisibilityLevel::Private;

						curinspt->addDefinition(file.name, impas, treedef);

						curinspt = newinspt;
					}
				}

				insertPoint = curinspt;
			}

			iceAssert(insertPoint);

			insertPoint->imports.push_back(import->base);
			{
				// make a new treedef referring to the newly-imported tree.
				auto treedef = util::pool<sst::TreeDefn>(ithing.loc);
				treedef->tree = import->base;
				treedef->enclosingScope = sst::Scope(insertPoint);

				insertPoint->addDefinition(import->base->name, treedef);


				zpr::println("import: using tree %p, defn = %p", import->base, treedef);
			}


			if(ithing.pubImport)
				insertPoint->reexports.push_back(import->base);

			// _addTreeToExistingTree(fs->dtree->thingsImported, insertPoint, import->base,
			// 	/* commonParent: */ nullptr, ithing.pubImport,
			// 	/* ignoreVis: */ false, file.name);

			fs->dtree->thingsImported.insert(ithing.name);
			fs->dtree->typeDefnMap.insert(import->typeDefnMap.begin(), import->typeDefnMap.end());

			// merge the things. hopefully there are no conflicts????
			// TODO: check for conflicts!
			fs->dtree->compilerSupportDefinitions.insert(import->compilerSupportDefinitions.begin(),
				import->compilerSupportDefinitions.end());
		}

		if(addPreludeDefinitions)
			generatePreludeDefinitions(fs);

		// handle exception here:
		try {
			auto tns = dcast(NamespaceDefn, file.root->typecheck(fs).stmt());
			iceAssert(tns);

			tns->name = file.moduleName;

			fs->dtree->topLevel = tns;
		}
		catch(ErrorException& ee)
		{
			ee.err->postAndQuit();
		}

		return fs->dtree;
	}

	static OsStrings getOsStrings()
	{
		// TODO: handle cygwin/msys/mingw???
		// like how do we want to expose these? at the end of the day the os is still windows...

		OsStrings ret;

		#if defined(_WIN32)
			ret.name = "windows";
			ret.vendor = "microsoft";
		#elif __MINGW__
			ret.name = "mingw";
		#elif __CYGWIN__
			ret.name = "cygwin";
		#elif __APPLE__
			ret.vendor = "apple";
			#include "TargetConditionals.h"
			#if TARGET_IPHONE_SIMULATOR
				ret.name = "iossimulator";
			#elif TARGET_OS_IOS
				ret.name = "ios";
			#elif TARGET_OS_WATCH
				ret.name = "watchos";
			#elif TARGET_OS_TV
				ret.name = "tvos";
			#elif TARGET_OS_OSX
				ret.name = "macos";
			#else
				#error "unknown apple operating system"
			#endif
		#elif __ANDROID__
			ret.vendor = "google";
			ret.name = "android";
		#elif __linux__ || __linux || linux
			ret.name = "linux";
		#elif __FreeBSD__
			ret.name = "freebsd";
		#elif __OpenBSD__
			ret.name = "openbsd";
		#elif __NetBSD__
			ret.name = "netbsd";
		#elif __DragonFly__
			ret.name = "dragonflybsd";
		#elif __unix__
			ret.name = "unix";
		#elif defined(_POSIX_VERSION)
			ret.name = "posix";
		#endif

		return ret;
	}

	static void generatePreludeDefinitions(TypecheckState* fs)
	{
		auto loc = Location();
		loc.fileID = frontend::getFileIDFromFilename(fs->stree->topLevelFilename);

		auto strings = getOsStrings();

		fs->pushTree("os");
		fs->stree->isCompilerGenerated = true;

		defer(fs->popTree());

		// manually add the definition, because we didn't typecheck a namespace or anything.
		fs->stree->parent->addDefinition(fs->stree->name, fs->stree->treeDefn);

		auto strty = fir::Type::getCharSlice(false);

		{
			// add the name
			auto name_def = util::pool<sst::VarDefn>(loc);
			name_def->id = Identifier("name", IdKind::Name);
			name_def->type = strty;
			name_def->global = true;
			name_def->immutable = true;
			name_def->visibility = VisibilityLevel::Private;

			auto s = util::pool<sst::LiteralString>(loc, strty);
			s->str = strings.name;

			name_def->init = s;
			fs->stree->addDefinition("name", name_def);
		}
		{
			// add the name
			auto vendor_def = util::pool<sst::VarDefn>(loc);
			vendor_def->id = Identifier("vendor", IdKind::Name);
			vendor_def->type = strty;
			vendor_def->global = true;
			vendor_def->immutable = true;
			vendor_def->visibility = VisibilityLevel::Private;

			auto s = util::pool<sst::LiteralString>(loc, strty);
			s->str = strings.vendor;

			vendor_def->init = s;
			fs->stree->addDefinition("vendor", vendor_def);
		}
	}
}


static void visitDeclarables(sst::TypecheckState* fs, ast::TopLevelBlock* top)
{
	for(auto stmt : top->statements)
	{
		if(auto decl = dcast(ast::Parameterisable, stmt))
		{
			decl->realScope = fs->getCurrentScope();
			decl->enclosingScope = fs->getCurrentScope2();
			decl->generateDeclaration(fs, 0, { });
		}

		else if(auto ffd = dcast(ast::ForeignFuncDefn, stmt))
			ffd->typecheck(fs);

		else if(auto ns = dcast(ast::TopLevelBlock, stmt))
		{
			fs->pushTree(ns->name);
			visitDeclarables(fs, ns);
			fs->popTree();
		}
	}
}


TCResult ast::TopLevelBlock::typecheck(sst::TypecheckState* fs, fir::Type* inferred)
{
	auto ret = util::pool<sst::NamespaceDefn>(this->loc);

	if(this->name != "")
		fs->pushTree(this->name);

	sst::StateTree* tree = fs->stree;

	if(!fs->isInFunctionBody())
	{
		// visit all functions first, to get out-of-order calling -- but only at the namespace level, not inside functions.
		// once we're in function-body-land, everything should be imperative-driven, and you shouldn't
		// be able to see something before it is defined/declared

		visitDeclarables(fs, this);
	}


	for(auto stmt : this->statements)
	{
		if(dcast(ast::ImportStmt, stmt))
			continue;

		auto tcr = stmt->typecheck(fs);
		if(tcr.isError())
			return TCResult(tcr.error());

		else if(!tcr.isParametric() && !tcr.isDummy())
			ret->statements.push_back(tcr.stmt());

		if(tcr.isDefn() && tcr.defn()->visibility == VisibilityLevel::Public)
			tree->exports.push_back(tcr.defn());

		// check for compiler support so we can add it to the big list of things.
		if((tcr.isStmt() || tcr.isDefn()) && tcr.stmt()->attrs.has(strs::attrs::COMPILER_SUPPORT))
		{
			if(!tcr.isDefn())
				error(tcr.stmt(), "@compiler_support can only be applied to definitions");

			auto ua = tcr.stmt()->attrs.get(strs::attrs::COMPILER_SUPPORT);
			iceAssert(!ua.name.empty() && ua.args.size() == 1);

			fs->dtree->compilerSupportDefinitions[ua.args[0]] = tcr.defn();
		}
	}

	if(tree->parent)
	{
		auto td = tree->treeDefn;
		iceAssert(td);

		td->visibility = this->visibility;

		if(auto err = fs->checkForShadowingOrConflictingDefinition(td, [](auto, auto) -> bool { return true; }, tree->parent))
			return TCResult(err);

		tree->parent->addDefinition(tree->topLevelFilename, td->id.name, td);
	}


	if(this->name != "")
		fs->popTree();

	ret->name = this->name;

	return TCResult(ret);
}
















