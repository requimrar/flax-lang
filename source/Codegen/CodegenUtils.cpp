// LlvmCodeGen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include <map>
#include <vector>
#include <memory>
#include <cfloat>
#include <utility>
#include <fstream>
#include <stdint.h>
#include <typeinfo>
#include <iostream>
#include <cinttypes>
#include "../include/parser.h"
#include "../include/codegen.h"
#include "../include/llvm_all.h"
#include "../include/compiler.h"
#include "llvm/Support/Host.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"

using namespace Ast;
using namespace Codegen;


namespace Codegen
{
	void doCodegen(std::string filename, Root* root, CodegenInstance* cgi)
	{
		cgi->module = new llvm::Module(Parser::getModuleName(filename), llvm::getGlobalContext());
		cgi->rootNode = root;

		std::string err;
		cgi->execEngine = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(cgi->module))
							.setErrorStr(&err)
							.setMCJITMemoryManager(llvm::make_unique<llvm::SectionMemoryManager>())
							.create();

		if(!cgi->execEngine)
		{
			fprintf(stderr, "%s", err.c_str());
			exit(1);
		}

		llvm::FunctionPassManager functionPassManager = llvm::FunctionPassManager(cgi->module);

		if(Compiler::getOptimisationLevel() > 0)
		{
			// Provide basic AliasAnalysis support for GVN.
			functionPassManager.add(llvm::createBasicAliasAnalysisPass());

			// Do simple "peephole" optimisations and bit-twiddling optzns.
			functionPassManager.add(llvm::createInstructionCombiningPass());

			// Reassociate expressions.
			functionPassManager.add(llvm::createReassociatePass());

			// Eliminate Common SubExpressions.
			functionPassManager.add(llvm::createGVNPass());

			// Simplify the control flow graph (deleting unreachable blocks, etc).
			functionPassManager.add(llvm::createCFGSimplificationPass());

			// hmm.
			// fuck it, turn everything on.
			functionPassManager.add(llvm::createLoadCombinePass());
			functionPassManager.add(llvm::createConstantHoistingPass());
			functionPassManager.add(llvm::createLICMPass());
			functionPassManager.add(llvm::createDelinearizationPass());
			functionPassManager.add(llvm::createFlattenCFGPass());
			functionPassManager.add(llvm::createScalarizerPass());
			functionPassManager.add(llvm::createSinkingPass());
			functionPassManager.add(llvm::createStructurizeCFGPass());
			functionPassManager.add(llvm::createInstructionSimplifierPass());
			functionPassManager.add(llvm::createDeadStoreEliminationPass());
			functionPassManager.add(llvm::createDeadInstEliminationPass());
			functionPassManager.add(llvm::createMemCpyOptPass());

			functionPassManager.add(llvm::createSCCPPass());
			functionPassManager.add(llvm::createAggressiveDCEPass());

			functionPassManager.add(llvm::createTailCallEliminationPass());
		}

		// optimisation level -1 disables *everything*
		// mostly for reading the IR to debug codegen.
		if(Compiler::getOptimisationLevel() >= 0)
		{
			// always do the mem2reg pass, our generated code is too inefficient
			functionPassManager.add(llvm::createPromoteMemoryToRegisterPass());
			functionPassManager.add(llvm::createMergedLoadStoreMotionPass());
			functionPassManager.add(llvm::createScalarReplAggregatesPass());
			functionPassManager.add(llvm::createConstantPropagationPass());
			functionPassManager.add(llvm::createDeadCodeEliminationPass());
		}


		functionPassManager.doInitialization();

		// Set the global so the code gen can use this.
		cgi->Fpm = &functionPassManager;
		cgi->pushScope();

		// add the generic functions from previous shits.
		for(auto fd : cgi->rootNode->externalGenericFunctions)
		{
			cgi->rootNode->genericFunctions.push_back(fd);
		}

		for(auto pair : cgi->rootNode->externalFuncs)
		{
			auto func = pair.second;
			iceAssert(func);

			// add to the func table
			auto lf = cgi->module->getFunction(func->getName());
			if(!lf)
			{
				cgi->module->getOrInsertFunction(func->getName(), func->getFunctionType());
				lf = cgi->module->getFunction(func->getName());
			}

			llvm::Function* f = llvm::cast<llvm::Function>(lf);

			f->deleteBody();
			cgi->addFunctionToScope(func->getName(), FuncPair_t(f, pair.first));
		}

		for(auto pair : cgi->rootNode->externalTypes)
		{
			// "Type" and "Any" are always generated by the compiler
			// and should not be imported.

			llvm::StructType* str = llvm::cast<llvm::StructType>(pair.second);
			if(pair.first->name == "Any" || pair.first->name == "Type")
				continue;

			if(cgi->getType(str) == 0)
			{
				cgi->addNewType(str, pair.first, TypeKind::Struct);
			}
		}

		cgi->rootNode->codegen(cgi);

		cgi->popScope();

		// free the memory
		cgi->clearScope();

		// this is all in llvm-space. no scopes needed.
		cgi->finishGlobalConstructors();
	}

	void writeBitcode(std::string filename, CodegenInstance* cgi)
	{
		std::error_code e;
		llvm::sys::fs::OpenFlags of = (llvm::sys::fs::OpenFlags) 0;
		size_t lastdot = filename.find_last_of(".");
		std::string oname = (lastdot == std::string::npos ? filename : filename.substr(0, lastdot));
		oname += ".bc";

		llvm::raw_fd_ostream rso(oname.c_str(), e, of);

		llvm::WriteBitcodeToFile(cgi->module, rso);
		rso.close();
	}



















	llvm::LLVMContext& CodegenInstance::getContext()
	{
		return llvm::getGlobalContext();
	}

	Root* CodegenInstance::getRootAST()
	{
		return rootNode;
	}





	void CodegenInstance::popScope()
	{
		this->symTabStack.pop_back();
		this->funcStack.pop_back();
	}

	void CodegenInstance::clearScope()
	{
		this->symTabStack.clear();
		this->funcStack.clear();

		this->clearNamespaceScope();
	}

	void CodegenInstance::pushScope()
	{
		this->symTabStack.push_back(SymTab_t());
		this->funcStack.push_back(FuncMap_t());
	}

	Func* CodegenInstance::getCurrentFunctionScope()
	{
		return this->funcScopeStack.size() > 0 ? this->funcScopeStack.back() : 0;
	}

	void CodegenInstance::setCurrentFunctionScope(Func* f)
	{
		this->funcScopeStack.push_back(f);
	}

	void CodegenInstance::clearCurrentFunctionScope()
	{
		this->funcScopeStack.pop_back();
	}


	SymTab_t& CodegenInstance::getSymTab()
	{
		return this->symTabStack.back();
	}

	SymbolPair_t* CodegenInstance::getSymPair(Expr* user, const std::string& name)
	{
		for(int i = symTabStack.size(); i-- > 0;)
		{
			SymTab_t& tab = symTabStack[i];
			if(tab.find(name) != tab.end())
				return &(tab[name]);
		}

		return nullptr;
	}

	llvm::Value* CodegenInstance::getSymInst(Expr* user, const std::string& name)
	{
		SymbolPair_t* pair = getSymPair(user, name);
		if(pair)
		{
			if(pair->first.second != SymbolValidity::Valid)
				GenError::useAfterFree(this, user, name);

			return pair->first.first;
		}

		return nullptr;
	}

	VarDecl* CodegenInstance::getSymDecl(Expr* user, const std::string& name)
	{
		SymbolPair_t* pair = nullptr;
		if((pair = getSymPair(user, name)))
			return pair->second;

		return nullptr;
	}

	bool CodegenInstance::isDuplicateSymbol(const std::string& name)
	{
		return getSymTab().find(name) != getSymTab().end();
	}

	void CodegenInstance::addSymbol(std::string name, llvm::Value* ai, VarDecl* vardecl)
	{
		SymbolValidity_t sv(ai, SymbolValidity::Valid);
		SymbolPair_t sp(sv, vardecl);

		this->getSymTab()[name] = sp;
	}


	void CodegenInstance::addNewType(llvm::Type* ltype, StructBase* atype, TypeKind e)
	{
		TypePair_t tpair(ltype, TypedExpr_t(atype, e));
		std::string mangled = this->mangleWithNamespace(atype->name, atype->scope, false);
		if(atype->mangledName.empty())
			atype->mangledName = mangled;

		// iceAssert(mangled == atype->mangledName);

		if(this->typeMap.find(mangled) == this->typeMap.end())
		{
			this->typeMap[mangled] = tpair;
		}
		else
		{
			error(this, atype, "Duplicate type %s", atype->name.c_str());
		}

		#if 0
		printf("adding type %s, mangled %s\n", atype->name.c_str(), mangled.c_str());
		#endif
		TypeInfo::addNewType(this, ltype, atype, e);
	}


	void CodegenInstance::removeType(std::string name)
	{
		if(this->typeMap.find(name) == this->typeMap.end())
			error("Type '%s' does not exist, cannot remove", name.c_str());

		this->typeMap.erase(name);
	}

	TypePair_t* CodegenInstance::getType(std::string name)
	{
		#if 0
		printf("finding %s\n{\n", name.c_str());
		for(auto p : this->typeMap)
			printf("\t%s\n", p.first.c_str());

		printf("}\n");
		#endif
		if(name == "Inferred" || name == "_ZN8Inferred")
			iceAssert(0);		// todo: see if this ever fires.

		if(this->typeMap.find(name) != this->typeMap.end())
			return &(this->typeMap[name]);

		return nullptr;
	}

	TypePair_t* CodegenInstance::getType(llvm::Type* type)
	{
		if(!type)
			return nullptr;

		for(auto pair : this->typeMap)
		{
			if(pair.second.first == type)
				return &this->typeMap[pair.first];
		}

		return nullptr;
	}

	bool CodegenInstance::isDuplicateType(std::string name)
	{
		return getType(name) != nullptr;
	}

	void CodegenInstance::popBracedBlock()
	{
		this->blockStack.pop_back();
	}

	BracedBlockScope* CodegenInstance::getCurrentBracedBlockScope()
	{
		return this->blockStack.size() > 0 ? &this->blockStack.back() : 0;
	}

	void CodegenInstance::pushBracedBlock(BreakableBracedBlock* block, llvm::BasicBlock* body, llvm::BasicBlock* after)
	{
		BracedBlockScope cs = std::make_pair(block, std::make_pair(body, after));
		this->blockStack.push_back(cs);
	}
















	// funcs
	void CodegenInstance::pushNamespaceScope(std::string namespc)
	{
		this->namespaceStack.push_back(namespc);
	}

	bool CodegenInstance::isValidNamespace(std::string namespc)
	{
		// check if it's imported anywhere
		for(auto nses : this->importedNamespaces)
		{
			for(std::string ns : nses)
			{
				if(ns == namespc)
					return true;
			}
		}

		return false;
	}

	void CodegenInstance::addFunctionToScope(std::string name, FuncPair_t func)
	{
		// this->funcMap[name] = func;
		this->funcStack.back()[name] = func;
	}

	FuncPair_t* CodegenInstance::getDeclaredFunc(std::string name)
	{
		for(ssize_t i = this->funcStack.size() - 1; i >= 0; i--)
		{
			FuncMap_t& tab = this->funcStack[i];

			#if 0
			printf("find %s:\n{\n", name.c_str());
			for(auto p : tab) printf("\t%s\n", p.first.c_str());
			printf("}\n");
			#endif

			if(tab.find(name) != tab.end())
				return &tab[name];
		}

		return nullptr;
	}

	static FuncPair_t* searchDeclaredFuncElsewhere(CodegenInstance* cgi, FuncCall* fc)
	{
		// mangled name
		FuncPair_t* fp = cgi->getDeclaredFunc(cgi->mangleFunctionName(cgi->mangleWithNamespace(fc->name), fc->params));
		if(fp) return fp;

		// search inside imported namespaces.
		for(auto ns : cgi->importedNamespaces)
		{
			fp = cgi->getDeclaredFunc(cgi->mangleFunctionName(cgi->mangleWithNamespace(fc->name, ns), fc->params));
			if(fp) return fp;
		}

		return 0;
	}

	FuncPair_t* CodegenInstance::getDeclaredFunc(FuncCall* fc)
	{
		// step one: unmangled name
		FuncPair_t* fp = this->getDeclaredFunc(fc->name);
		if(fp) return fp;

		fp = searchDeclaredFuncElsewhere(this, fc);
		if(fp) return fp;

		return 0;
	}

	bool CodegenInstance::isDuplicateFuncDecl(std::string name)
	{
		return this->funcStack.back().find(name) != this->funcStack.back().end();
	}

	void CodegenInstance::popNamespaceScope()
	{
		this->namespaceStack.pop_back();
	}

	void CodegenInstance::clearNamespaceScope()
	{
		this->namespaceStack.clear();
	}



	FuncPair_t* CodegenInstance::getOrDeclareLibCFunc(std::string name)
	{
		FuncPair_t* fp = this->getDeclaredFunc(name);
		if(!fp)
		{
			std::string retType;
			std::deque<VarDecl*> params;
			if(name == "malloc")
			{
				VarDecl* fakefdmvd = new VarDecl(Parser::PosInfo(), "size", false);
				fakefdmvd->type = "Uint64";
				params.push_back(fakefdmvd);

				retType = "Int8*";
			}
			else if(name == "free")
			{
				VarDecl* fakefdmvd = new VarDecl(Parser::PosInfo(), "ptr", false);
				fakefdmvd->type = "Int8*";
				params.push_back(fakefdmvd);

				retType = "Int8*";
			}
			else if(name == "strlen")
			{
				VarDecl* fakefdmvd = new VarDecl(Parser::PosInfo(), "str", false);
				fakefdmvd->type = "Int8*";
				params.push_back(fakefdmvd);

				retType = "Int64";
			}

			FuncDecl* fakefm = new FuncDecl(Parser::PosInfo(), name, params, retType);
			fakefm->isFFI = true;
			fakefm->codegen(this);

			iceAssert((fp = this->getDeclaredFunc(name)));
		}

		return fp;
	}


	static void searchForAndApplyExtension(CodegenInstance* cgi, std::deque<Expr*> exprs, std::string extName)
	{
		for(Expr* e : exprs)
		{
			Extension* ext		= dynamic_cast<Extension*>(e);
			NamespaceDecl* ns	= dynamic_cast<NamespaceDecl*>(e);

			if(ext && ext->mangledName == extName)
				ext->createType(cgi);

			else if(ns)
				searchForAndApplyExtension(cgi, ns->innards->statements, extName);
		}
	}

	void CodegenInstance::applyExtensionToStruct(std::string ext)
	{
		searchForAndApplyExtension(this, this->rootNode->topLevelExpressions, ext);
	}




	std::string CodegenInstance::mangleLlvmType(llvm::Type* type)
	{
		std::string r = this->getReadableType(type);

		int ind = 0;
		r = this->unwrapPointerType(r, &ind);

		if(r.find("Int8") == 0)			r = "a";
		else if(r.find("Int16") == 0)	r = "s";
		else if(r.find("Int32") == 0)	r = "i";
		else if(r.find("Int64") == 0)	r = "l";
		else if(r.find("Int") == 0)		r = "l";

		else if(r.find("Uint8") == 0)	r = "h";
		else if(r.find("Uint16") == 0)	r = "t";
		else if(r.find("Uint32") == 0)	r = "j";
		else if(r.find("Uint64") == 0)	r = "m";
		else if(r.find("Uint") == 0)	r = "m";

		else if(r.find("Float32") == 0)	r = "f";
		else if(r.find("Float") == 0)	r = "f";

		else if(r.find("Float64") == 0)	r = "d";
		else if(r.find("Double") == 0)	r = "d";


		else if(r.find("Void") == 0)	r = "v";
		else
		{
			if(r.size() > 0 && r.front() == '%')
				r = r.substr(1);

			// remove anything at the back
			// find first of space, then remove everything after

			size_t firstSpace = r.find_first_of(' ');
			if(firstSpace != std::string::npos)
				r.erase(firstSpace);

			r = std::to_string(r.length()) + r;
		}

		for(int i = 0; i < ind; i++)
			r += "P";

		return r;
	}


	std::string CodegenInstance::mangleMemberFunction(StructBase* s, std::string orig, std::deque<VarDecl*> args, std::deque<std::string> ns,
		bool isStatic)
	{
		std::deque<Expr*> exprs;

		// todo: kinda hack? omit the first vardecl, since it's 'self'

		int i = 0;
		for(auto v : args)
		{
			if(i++ == 0 && !isStatic)		// static funcs don't have 'this'
				continue;

			exprs.push_back(v);
		}

		return this->mangleMemberFunction(s, orig, exprs, ns);
	}

	std::string CodegenInstance::mangleMemberFunction(StructBase* s, std::string orig, std::deque<Expr*> args)
	{
		return this->mangleMemberFunction(s, orig, args, this->namespaceStack);
	}

	std::string CodegenInstance::mangleMemberFunction(StructBase* s, std::string orig, std::deque<Expr*> args, std::deque<std::string> ns)
	{
		std::string mangled;
		mangled = (ns.size() > 0 ? "" : "_ZN") + this->mangleWithNamespace("", ns);

		// last char is 0 or E
		if(mangled.length() > 3)
		{
			if(mangled.back() == 'E')
				mangled = mangled.substr(0, mangled.length() - 1);

			iceAssert(mangled.back() == '0');
			mangled = mangled.substr(0, mangled.length() - 1);
		}

		mangled += std::to_string(s->name.length()) + s->name;
		mangled += this->mangleFunctionName(std::to_string(orig.length()) + orig + "E", args);

		return mangled;
	}

	std::string CodegenInstance::mangleMemberName(StructBase* s, FuncCall* fc)
	{
		std::deque<llvm::Type*> largs;
		iceAssert(this->getType(s->mangledName));

		bool first = true;
		for(Expr* e : fc->params)
		{
			if(!first)
			{
				// we have an implicit self, don't push that
				largs.push_back(this->getLlvmType(e));
			}

			first = false;
		}

		std::string basename = fc->name + "E";
		std::string mangledFunc = this->mangleFunctionName(basename, largs);
		return this->mangleWithNamespace(s->name) + std::to_string(basename.length()) + mangledFunc;
	}

	std::string CodegenInstance::mangleMemberName(StructBase* s, std::string orig)
	{
		return this->mangleWithNamespace(s->name) + std::to_string(orig.length()) + orig;
	}

















	std::string CodegenInstance::mangleFunctionName(std::string base, std::deque<std::string> args)
	{
		std::string mangled;
		for(auto s : args)
			mangled += s;

		return base + (mangled.empty() ? "v" : (mangled));
	}

	std::string CodegenInstance::mangleFunctionName(std::string base, std::deque<llvm::Type*> args)
	{
		std::deque<std::string> strings;

		for(llvm::Type* e : args)
			strings.push_back(this->mangleLlvmType(e));

		return this->mangleFunctionName(base, strings);
	}

	std::string CodegenInstance::mangleFunctionName(std::string base, std::deque<Expr*> args)
	{
		std::deque<llvm::Type*> a;
		for(auto arg : args)
			a.push_back(this->getLlvmType(arg));

		return this->mangleFunctionName(base, a);
	}

	std::string CodegenInstance::mangleFunctionName(std::string base, std::deque<VarDecl*> args)
	{
		std::deque<llvm::Type*> a;
		for(auto arg : args)
			a.push_back(this->getLlvmType(arg));

		return this->mangleFunctionName(base, a);
	}










	std::string CodegenInstance::mangleGenericFunctionName(std::string base, std::deque<VarDecl*> args)
	{
		std::deque<std::string> strs;
		std::map<std::string, int> uniqueGenericTypes;	// only a map because it's easier to .find().

		// TODO: this is very suboptimal
		int runningTypeIndex = 0;
		for(auto arg : args)
		{
			llvm::Type* atype = this->getLlvmType(arg, true);	// same as mangleFunctionName, but allow failures.

			// if there is no llvm type, go ahead with the raw type: T or U or something.
			if(!atype)
			{
				std::string st = arg->type.strType;
				if(uniqueGenericTypes.find(st) == uniqueGenericTypes.end())
				{
					uniqueGenericTypes[st] = runningTypeIndex;
					runningTypeIndex++;
				}
			}
		}

		// very very suboptimal.

		for(auto arg : args)
		{
			llvm::Type* atype = this->getLlvmType(arg, true);	// same as mangleFunctionName, but allow failures.

			// if there is no llvm type, go ahead with the raw type: T or U or something.
			if(!atype)
			{
				std::string st = arg->type.strType;
				iceAssert(uniqueGenericTypes.find(st) != uniqueGenericTypes.end());

				std::string s = "GT" + std::to_string(uniqueGenericTypes[st]) + "_";
				strs.push_back(std::to_string(s.length()) + s);
			}
			else
			{
				strs.push_back(this->mangleLlvmType(atype));
			}
		}

		return this->mangleFunctionName(base, strs);
	}












	std::string CodegenInstance::mangleWithNamespace(std::string original, bool isFunction)
	{
		return this->mangleWithNamespace(original, this->namespaceStack, isFunction);
	}


	std::string CodegenInstance::mangleWithNamespace(std::string original, std::deque<std::string> ns, bool isFunction)
	{
		std::string ret = "_Z";
		ret += (ns.size() > 0 ? "N" : "");

		for(std::string s : ns)
		{
			if(s.length() > 0)
				ret += std::to_string(s.length()) + s;
		}

		ret += std::to_string(original.length()) + original;
		if(ns.size() == 0)
		{
			ret = original;
		}
		else
		{
			if(isFunction)
			{
				ret += "E";
			}
		}

		return ret;
	}

	std::string CodegenInstance::mangleRawNamespace(std::string _orig)
	{
		std::string original = _orig;
		std::string ret = "_ZN";

		// we have a name now
		size_t next = 0;
		while((next = original.find_first_of("::")) != std::string::npos)
		{
			std::string ns = original.substr(0, next);
			ret += std::to_string(ns.length()) + ns;

			original = original.substr(next, -1);

			if(original.find("::") == 0)
				original = original.substr(2);
		}

		if(original.length() > 0)
			ret += std::to_string(original.length()) + original;

		return ret;
	}


	Result_t CodegenInstance::createStringFromInt8Ptr(llvm::StructType* stringType, llvm::Value* int8ptr)
	{
		return Result_t(0, 0);
	}




	void CodegenInstance::tryResolveAndInstantiateGenericFunction(FuncCall* fc)
	{
		// try and resolve shit???
		// first, we need to get strings of every type.

		// TODO: dupe code
		std::deque<FuncDecl*> candidates;
		std::map<std::string, llvm::Type*> tm;

		// TODO: this is really fucking bad, this goes O(n^2)!!! increases with imported namespaces!!!
		for(FuncDecl* fd : this->rootNode->genericFunctions)
		{
			if(fd->mangledNamespaceOnly == this->mangleWithNamespace(fc->name))
				candidates.push_back(fd);

			for(auto ns : this->importedNamespaces)
			{
				if(fd->mangledNamespaceOnly == this->mangleWithNamespace(fc->name, ns))
					candidates.push_back(fd);
			}
		}

		if(candidates.size() == 0)
		{
			// printf("found no generic candidates for func call %s\n", fc->name.c_str());
			return;	// do nothing.
		}

		auto it = candidates.begin();
		for(auto candidate : candidates)
		{
			printf("found candidate function declaration to instantiate: %s, %s, %s\n", candidate->name.c_str(),
				candidate->mangledNamespaceOnly.c_str(), candidate->mangledName.c_str());


			// now check if we *can* instantiate it.
			// first check the number of arguments.
			if(candidate->params.size() != fc->params.size())
			{
				printf("candidate %s rejected (1)\n", candidate->mangledName.c_str());
				it = candidates.erase(it);
			}
			else
			{
				// param count matches...
				// do a similar thing as the actual mangling -- build a list of
				// uniquely named types.

				std::map<std::string, std::vector<int>> typePositions;
				std::vector<int> nonGenericTypes;

				int pos = 0;
				for(auto p : candidate->params)
				{
					llvm::Type* ltype = this->getLlvmType(p, true);
					if(!ltype)
					{
						std::string s = p->type.strType;
						if(typePositions.find(s) == typePositions.end())
							typePositions[s] = std::vector<int>();

						typePositions[s].push_back(pos);
					}
					else
					{
						nonGenericTypes.push_back(pos);
					}

					pos++;
				}


				// this needs to be basically a fully manual check.
				// 1. check that the generic types match.
				for(auto pair : typePositions)
				{
					llvm::Type* ftype = this->getLlvmType(fc->params[0]);
					for(int k : pair.second)
					{
						if(this->getLlvmType(fc->params[k]) != ftype)
							goto fail;	// ew goto
					}
				}

				// 2. check that the concrete types match.
				for(int k : nonGenericTypes)
				{
					llvm::Type* a = this->getLlvmType(fc->params[k]);
					llvm::Type* b = this->getLlvmType(candidate->params[k]);

					if(a != b)
						goto fail;
				}



				// fill in the typemap.
				// note that it's okay if we just have one -- if we did this loop more
				// than once and screwed up the tm, that means we have more than one
				// candidate, and will error anyway.

				for(auto pair : typePositions)
				{
					tm[pair.first] = this->getLlvmType(fc->params[pair.second[0]]);
				}


				goto success;
				fail:
				{
					printf("candidate %s rejected (2)\n", candidate->mangledName.c_str());
					it = candidates.erase(it);
					continue;
				}

				success:
				it++;
			}
		}

		if(candidates.size() > 1)
		{
			std::string cands;
			for(auto c : candidates)
				cands += this->printAst(c) + "\n";

			error(this, fc, "Ambiguous function call to function %s, have %d candidates:\n%s\n", fc->name.c_str(),
				candidates.size(), cands.c_str());
		}

		FuncDecl* candidate = candidates[0];
		Result_t res = candidate->generateDeclForGenericType(this, tm);
		llvm::Function* ffunc = (llvm::Function*) res.result.first;

		iceAssert(ffunc);


		// TODO: this is really super fucking ugly and SUBOPTIMAL
		Func* theFn = 0;
		for(Func* f : this->rootNode->allFunctionBodies)
		{
			if(f->decl == candidate)
			{
				// we've got it.
				theFn = f;
				break;
			}
		}

		iceAssert(theFn);
		std::deque<llvm::Type*> instantiatedTypes;
		for(auto p : fc->params)
			instantiatedTypes.push_back(this->getLlvmType(p));

		theFn->decl->instantiatedGenericTypes = instantiatedTypes;
		theFn->decl->instantiatedGenericReturnType = ffunc->getReturnType();

		fc->cachedGenericFuncTarget = ffunc;
		theFn->codegen(this);
	}






































	llvm::Instruction::BinaryOps CodegenInstance::getBinaryOperator(ArithmeticOp op, bool isSigned, bool isFP)
	{
		using llvm::Instruction;
		switch(op)
		{
			case ArithmeticOp::Add:
			case ArithmeticOp::PlusEquals:
				return !isFP ? Instruction::BinaryOps::Add : Instruction::BinaryOps::FAdd;

			case ArithmeticOp::Subtract:
			case ArithmeticOp::MinusEquals:
				return !isFP ? Instruction::BinaryOps::Sub : Instruction::BinaryOps::FSub;

			case ArithmeticOp::Multiply:
			case ArithmeticOp::MultiplyEquals:
				return !isFP ? Instruction::BinaryOps::Mul : Instruction::BinaryOps::FMul;

			case ArithmeticOp::Divide:
			case ArithmeticOp::DivideEquals:
				return !isFP ? (isSigned ? Instruction::BinaryOps::SDiv : Instruction::BinaryOps::UDiv) : Instruction::BinaryOps::FDiv;

			case ArithmeticOp::Modulo:
			case ArithmeticOp::ModEquals:
				return !isFP ? (isSigned ? Instruction::BinaryOps::SRem : Instruction::BinaryOps::URem) : Instruction::BinaryOps::FRem;

			case ArithmeticOp::ShiftLeft:
			case ArithmeticOp::ShiftLeftEquals:
				return Instruction::BinaryOps::Shl;

			case ArithmeticOp::ShiftRight:
			case ArithmeticOp::ShiftRightEquals:
				return isSigned ? Instruction::BinaryOps::AShr : Instruction::BinaryOps::LShr;

			case ArithmeticOp::BitwiseAnd:
			case ArithmeticOp::BitwiseAndEquals:
				return Instruction::BinaryOps::And;

			case ArithmeticOp::BitwiseOr:
			case ArithmeticOp::BitwiseOrEquals:
				return Instruction::BinaryOps::Or;

			case ArithmeticOp::BitwiseXor:
			case ArithmeticOp::BitwiseXorEquals:
				return Instruction::BinaryOps::Xor;

			default:
				return (Instruction::BinaryOps) 0;
		}
	}





	ArithmeticOp CodegenInstance::determineArithmeticOp(std::string ch)
	{
		return Parser::mangledStringToOperator(ch);
	}

	Result_t CodegenInstance::callOperatorOnStruct(Expr* user, TypePair_t* pair, llvm::Value* self, ArithmeticOp op, llvm::Value* val, bool fail)
	{
		iceAssert(pair);
		iceAssert(pair->first);
		iceAssert(pair->second.first);

		if(pair->second.second != TypeKind::Struct)
		{
			if(fail)	error("!!??!?!?!");
			else		return Result_t(0, 0);
		}

		Struct* str = dynamic_cast<Struct*>(pair->second.first);
		iceAssert(str);

		llvm::Function* opov = nullptr;
		for(auto f : str->lOpOverloads)
		{
			if(f.first == op && (f.second->getArgumentList().back().getType() == val->getType()))
			{
				opov = f.second;
				break;
			}
		}

		if(!opov)
		{
			if(fail)	GenError::noOpOverload(this, user, str->name, op);
			else		return Result_t(0, 0);
		}

		// get the function with the same name in the current module
		opov = this->module->getFunction(opov->getName());
		iceAssert(opov);

		// try the assign op.
		if(op == ArithmeticOp::Assign || op == ArithmeticOp::PlusEquals || op == ArithmeticOp::MinusEquals
		|| op == ArithmeticOp::MultiplyEquals || op == ArithmeticOp::DivideEquals)
		{
			// check args.
			llvm::Value* ret = builder.CreateCall2(opov, self, val);
			return Result_t(ret, self);
		}
		else if(op == ArithmeticOp::CmpEq || op == ArithmeticOp::Add || op == ArithmeticOp::Subtract || op == ArithmeticOp::Multiply
		|| op == ArithmeticOp::Divide)
		{
			// check that both types work
			return Result_t(builder.CreateCall2(opov, self, val), 0);
		}

		if(fail)	GenError::noOpOverload(this, user, str->name, op);
		return Result_t(0, 0);
	}

	llvm::Function* CodegenInstance::getStructInitialiser(Expr* user, TypePair_t* pair, std::vector<llvm::Value*> vals)
	{
		iceAssert(pair);
		iceAssert(pair->first);
		iceAssert(pair->second.first);

		Struct* str = dynamic_cast<Struct*>(pair->second.first);

		if(pair->second.second != TypeKind::Struct)
		{
			iceAssert(pair->second.second == TypeKind::TypeAlias);
			TypeAlias* ta = dynamic_cast<TypeAlias*>(pair->second.first);
			iceAssert(ta);

			TypePair_t* tp = this->getType(ta->origType);
			iceAssert(tp);

			// todo: support typealiases of typealises.
			str = dynamic_cast<Struct*>(tp->second.first);
		}


		iceAssert(str);

		llvm::Function* initf = 0;
		for(llvm::Function* initers : str->initFuncs)
		{
			if(initers->arg_size() < 1)
				error(user, "(%s:%d) -> Internal check failed: init() should have at least one (implicit) parameter", __FILE__, __LINE__);

			if(initers->arg_size() != vals.size())
				continue;

			int i = 0;
			for(auto it = initers->arg_begin(); it != initers->arg_end(); it++, i++)
			{
				llvm::Value& arg = (*it);
				if(vals[i]->getType() != arg.getType())
					goto breakout;
			}

			// fuuuuuuuuck this is ugly
			initf = initers;
			break;

			breakout:
			continue;
		}

		if(!initf)
			GenError::invalidInitialiser(this, user, str, vals);

		return this->module->getFunction(initf->getName());
	}


	Result_t CodegenInstance::assignValueToAny(llvm::Value* lhsPtr, llvm::Value* rhs, llvm::Value* rhsPtr)
	{
		llvm::Value* typegep = this->builder.CreateStructGEP(lhsPtr, 0);	// Any
		typegep = this->builder.CreateStructGEP(typegep, 0, "type");		// Type

		size_t index = TypeInfo::getIndexForType(this, rhs->getType());
		iceAssert(index > 0);

		llvm::Value* constint = llvm::ConstantInt::get(typegep->getType()->getPointerElementType(), index);
		this->builder.CreateStore(constint, typegep);



		llvm::Value* valgep = this->builder.CreateStructGEP(lhsPtr, 1, "value");
		if(rhsPtr)
		{
			// printf("rhsPtr, %s\n", this->getReadableType(valgep).c_str());
			llvm::Value* casted = this->builder.CreatePointerCast(rhsPtr, valgep->getType()->getPointerElementType(), "pcast");
			this->builder.CreateStore(casted, valgep);
		}
		else
		{
			llvm::Type* targetType = rhs->getType()->isIntegerTy() ? valgep->getType()->getPointerElementType() : llvm::IntegerType::getInt64Ty(this->getContext());


			if(rhs->getType()->isIntegerTy())
			{
				llvm::Value* casted = this->builder.CreateIntToPtr(rhs, targetType);
				this->builder.CreateStore(casted, valgep);
			}
			else
			{
				llvm::Value* casted = this->builder.CreateBitCast(rhs, targetType);
				casted = this->builder.CreateIntToPtr(casted, valgep->getType()->getPointerElementType());
				this->builder.CreateStore(casted, valgep);
			}
		}

		return Result_t(this->builder.CreateLoad(lhsPtr), lhsPtr);
	}


	Result_t CodegenInstance::extractValueFromAny(llvm::Type* type, llvm::Value* ptr)
	{
		llvm::Value* valgep = this->builder.CreateStructGEP(ptr, 1);
		llvm::Value* loadedval = this->builder.CreateLoad(valgep);

		if(type->isStructTy())
		{
			// use pointer stuff
			llvm::Value* valptr = this->builder.CreatePointerCast(loadedval, type->getPointerTo());
			llvm::Value* loaded = this->builder.CreateLoad(valptr);

			return Result_t(loaded, valptr);
		}
		else
		{
			// the pointer is actually a literal
			llvm::Type* targetType = type->isIntegerTy() ? type : llvm::IntegerType::getInt64Ty(this->getContext());
			llvm::Value* val = this->builder.CreatePtrToInt(loadedval, targetType);

			if(val->getType() != type)
			{
				val = this->builder.CreateBitCast(val, type);
			}

			return Result_t(val, 0);
		}
	}






	Result_t CodegenInstance::doPointerArithmetic(ArithmeticOp op, llvm::Value* lhs, llvm::Value* lhsPtr, llvm::Value* rhs)
	{
		iceAssert(lhs->getType()->isPointerTy() && rhs->getType()->isIntegerTy()
		&& (op == ArithmeticOp::Add || op == ArithmeticOp::Subtract || op == ArithmeticOp::PlusEquals || op == ArithmeticOp::MinusEquals));

		llvm::Instruction::BinaryOps lop = this->getBinaryOperator(op, false, false);
		iceAssert(lop);


		// first, multiply the RHS by the number of bits the pointer type is, divided by 8
		// eg. if int16*, then +4 would be +4 int16s, which is (4 * (8 / 4)) = 4 * 2 = 8 bytes

		const llvm::DataLayout* dl = this->execEngine->getDataLayout();
		iceAssert(dl);

		uint64_t ptrWidth = dl->getPointerSizeInBits();
		uint64_t typesize = dl->getTypeSizeInBits(lhs->getType()->getPointerElementType()) / 8;
		llvm::APInt apint = llvm::APInt(ptrWidth, typesize);
		llvm::Value* intval = llvm::Constant::getIntegerValue(llvm::IntegerType::getIntNTy(this->getContext(), ptrWidth), apint);

		if(rhs->getType()->getIntegerBitWidth() != ptrWidth)
			rhs = this->builder.CreateIntCast(rhs, intval->getType(), false);


		// this is the properly adjusted thing
		llvm::Value* newrhs = this->builder.CreateMul(rhs, intval);

		// convert the lhs pointer to an int value, so we can add/sub on it
		llvm::Value* ptrval = this->builder.CreatePtrToInt(lhs, newrhs->getType());

		// create the add/sub
		llvm::Value* res = this->builder.CreateBinOp(lop, ptrval, newrhs);

		// turn the int back into a pointer, so we can store it back into the var.
		llvm::Value* tempRes = lhsPtr ? lhsPtr : this->allocateInstanceInBlock(lhs->getType());

		llvm::Value* properres = this->builder.CreateIntToPtr(res, lhs->getType());
		this->builder.CreateStore(properres, tempRes);
		return Result_t(properres, tempRes);
	}


	static void errorNoReturn(Expr* e)
	{
		error(e, "Not all code paths return a value");
	}

	static bool verifyReturnType(CodegenInstance* cgi, Func* f, BracedBlock* bb, Return* r, llvm::Type* retType)
	{
		if(r)
		{
			llvm::Type* expected = 0;
			llvm::Type* have = 0;

			if(r->actualReturnValue)
				have = r->actualReturnValue->getType();

			if((have ? have : have = cgi->getLlvmType(r->val)) != (expected = (retType == 0 ? cgi->getLlvmType(f->decl) : retType)))
				error(cgi, r, "Function has return type '%s', but return statement returned value of type '%s' instead",
					cgi->getReadableType(expected).c_str(), cgi->getReadableType(have).c_str());


			return true;
		}
		else
		{
			return false;
		}
	}

	static Return* recursiveVerifyBranch(CodegenInstance* cgi, Func* f, If* ifbranch, bool checkType, llvm::Type* retType);
	static Return* recursiveVerifyBlock(CodegenInstance* cgi, Func* f, BracedBlock* bb, bool checkType, llvm::Type* retType)
	{
		if(bb->statements.size() == 0)
			errorNoReturn(bb);

		Return* r = nullptr;
		for(Expr* e : bb->statements)
		{
			If* i = nullptr;
			if((i = dynamic_cast<If*>(e)))
			{
				Return* tmp = recursiveVerifyBranch(cgi, f, i, checkType, retType);
				if(tmp)
				{
					r = tmp;
					break;
				}
			}

			else if((r = dynamic_cast<Return*>(e)))
				break;
		}

		if(checkType)
		{
			verifyReturnType(cgi, f, bb, r, retType);
		}

		return r;
	}

	static Return* recursiveVerifyBranch(CodegenInstance* cgi, Func* f, If* ib, bool checkType, llvm::Type* retType)
	{
		Return* r = 0;
		bool first = true;
		for(std::pair<Expr*, BracedBlock*> pair : ib->_cases)	// use the preserved one
		{
			Return* tmp = recursiveVerifyBlock(cgi, f, pair.second, checkType, retType);
			if(first)
				r = tmp;

			else if(r != nullptr)
				r = tmp;

			first = false;
		}

		if(ib->final)
		{
			if(r != nullptr)
				r = recursiveVerifyBlock(cgi, f, ib->final, checkType, retType);
		}
		else
		{
			r = nullptr;
		}

		return r;
	}

	// if the function returns void, the return value of verifyAllPathsReturn indicates whether or not
	// all code paths have explicit returns -- if true, Func::codegen is expected to insert a ret void at the end
	// of the body.
	bool CodegenInstance::verifyAllPathsReturn(Func* func, size_t* stmtCounter, bool checkType, llvm::Type* retType)
	{
		if(stmtCounter)
			*stmtCounter = 0;


		bool isVoid = (retType == 0 ? this->getLlvmType(func) : retType)->isVoidTy();

		// check the block
		if(func->block->statements.size() == 0 && !isVoid)
		{
			error(func, "Function %s has return type '%s', but returns nothing", func->decl->name.c_str(), func->decl->type.strType.c_str());
		}
		else if(isVoid)
		{
			return true;
		}


		// now loop through all exprs in the block
		Return* ret = 0;
		Expr* final = 0;
		for(Expr* e : func->block->statements)
		{
			if(stmtCounter)
				(*stmtCounter)++;

			If* i = dynamic_cast<If*>(e);
			final = e;

			if(i)
				ret = recursiveVerifyBranch(this, func, i, !isVoid && checkType, retType);

			// "top level" returns we will just accept.
			if(ret || (ret = dynamic_cast<Return*>(e)))
				break;
		}

		if(!ret && (isVoid || !checkType || this->getLlvmType(final) == this->getLlvmType(func)))
			return true;

		if(!ret)
			error(func, "Function '%s' missing return statement", func->decl->name.c_str());

		if(checkType)
		{
			verifyReturnType(this, func, func->block, ret, retType);
		}

		return false;
	}

	static void recursivelyResolveDependencies(Expr* expr, std::deque<Expr*>& resolved, std::deque<Expr*>& unresolved)
	{
		unresolved.push_back(expr);
		for(auto m : expr->dependencies)
		{
			if(std::find(resolved.begin(), resolved.end(), m.dep) == resolved.end())
			{
				if(std::find(unresolved.begin(), unresolved.end(), m.dep) != unresolved.end())
					error(0, expr, "Circular dependency!");

				recursivelyResolveDependencies(m.dep, resolved, unresolved);
			}
		}

		resolved.push_back(expr);
		unresolved.erase(std::find(unresolved.begin(), unresolved.end(), expr));
	}

	void CodegenInstance::evaluateDependencies(Expr* expr)
	{
		std::deque<Expr*> resolved;
		std::deque<Expr*> unresolved;
		recursivelyResolveDependencies(expr, resolved, unresolved);
	}

}














