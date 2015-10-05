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
#include "../include/compiler.h"

#include "llvm/Support/Host.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
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
			cgi->rootNode->genericFunctions.push_back(fd.first);
		}

		cgi->rootNode->rootFuncStack->nsName = "__#root_" + cgi->module->getName().str();
		cgi->rootNode->publicFuncTree->nsName = "__#rootPUB_" + cgi->module->getName().str();


		// rootFuncStack should really be empty, except we know that there should be
		// stuff inside from imports.
		// thus, solidify the insides of these, by adding the function to llvm::Module.

		cgi->cloneFunctionTree(cgi->rootNode->rootFuncStack, cgi->rootNode->rootFuncStack, true);

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

	void CodegenInstance::popScope()
	{
		this->symTabStack.pop_back();
	}

	void CodegenInstance::clearScope()
	{
		this->symTabStack.clear();
		this->clearNamespaceScope();
	}

	void CodegenInstance::pushScope()
	{
		this->symTabStack.push_back(SymTab_t());
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
			return pair->first;
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
		SymbolPair_t sp(ai, vardecl);
		this->getSymTab()[name] = sp;
	}


	bool CodegenInstance::areEqualTypes(llvm::Type* a, llvm::Type* b)
	{
		if(a == b) return true;
		else if(a->isStructTy() && b->isStructTy())
		{
			llvm::StructType* sa = llvm::cast<llvm::StructType>(a);
			llvm::StructType* sb = llvm::cast<llvm::StructType>(b);

			// get the first part of the name.
			if(!sa->isLiteral() && !sb->isLiteral())
			{
				std::string an = sa->getName();
				std::string bn = sb->getName();

				std::string fan = an.substr(0, an.find_first_of('.'));
				std::string fbn = bn.substr(0, bn.find_first_of('.'));

				if(fan != fbn) return false;
			}

			return sa->isLayoutIdentical(sb);
		}

		return false;
	}

	void CodegenInstance::addNewType(llvm::Type* ltype, StructBase* atype, TypeKind e)
	{
		TypePair_t tpair(ltype, TypedExpr_t(atype, e));

		FunctionTree* ftree = this->getCurrentFuncTree();
		iceAssert(ftree);

		if(ftree->types.find(atype->name) != ftree->types.end())
		{
			// only if there's an actual, llvm::Type* there.
			if(ftree->types[atype->name].first)
				error(atype, "Duplicate type %s (in ftree %s:%d)", atype->name.c_str(), ftree->nsName.c_str(), ftree->id);
		}

		// if there isn't one, replace it.
		ftree->types[atype->name] = tpair;


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
			error(atype, "Duplicate type %s", atype->name.c_str());
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
		fprintf(stderr, "finding %s\n{\n", name.c_str());
		for(auto p : this->typeMap)
			fprintf(stderr, "\t%s\n", p.first.c_str());

		fprintf(stderr, "}\n");
		#endif
		if(name == "Inferred" || name == "_ZN8Inferred")
			iceAssert(!"Tried to get type on inferred vardecl!");

		if(this->typeMap.find(name) != this->typeMap.end())
			return &(this->typeMap[name]);


		// find nested types.
		if(this->nestedTypeStack.size() > 0)
		{
			Class* cls = this->nestedTypeStack.back();

			// only allow one level of implicit use
			for(auto n : cls->nestedTypes)
			{
				if(n.first->name == name)
					return this->getType(n.second);
			}
		}


		// try generic types.
		{
			// this is somewhat complicated.
			// resolveGenericType returns an llvm::Type*.
			// we need to return a TypePair_t* here. So... we should be able to "reverse-find"
			// the actual TypePair_t by calling the other version of getType(llvm::Type*).

			// confused? source code explains better than I can.
			llvm::Type* possibleGeneric = this->resolveGenericType(name);
			if(possibleGeneric)
			{
				if(this->isBuiltinType(possibleGeneric))
				{
					// create a typepair. allows constructor syntax
					// only applicable in generic functions.
					// todo: this will leak...

					return new TypePair_t(possibleGeneric, std::make_pair(nullptr, TypeKind::Struct));
				}

				TypePair_t* tp = this->getType(possibleGeneric);
				iceAssert(tp);

				return tp;
			}
		}

		return 0;
	}

	TypePair_t* CodegenInstance::getType(llvm::Type* type)
	{
		if(!type)
			return nullptr;

		for(auto pair : this->typeMap)
		{
			if(pair.second.first == type)
			{
				return &this->typeMap[pair.first];
			}
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




	void CodegenInstance::pushNestedTypeScope(Class* nest)
	{
		this->nestedTypeStack.push_back(nest);
	}

	void CodegenInstance::popNestedTypeScope()
	{
		iceAssert(this->nestedTypeStack.size() > 0);
		this->nestedTypeStack.pop_back();
	}

	std::deque<std::string> CodegenInstance::getFullScope()
	{
		std::deque<std::string> full = this->namespaceStack;
		for(auto s : this->nestedTypeStack)
			full.push_back(s->name);

		return full;
	}






	// generic type stacks
	void CodegenInstance::pushGenericTypeStack()
	{
		auto newPart = std::map<std::string, llvm::Type*>();
		this->instantiatedGenericTypeStack.push_back(newPart);
	}

	void CodegenInstance::pushGenericType(std::string id, llvm::Type* type)
	{
		iceAssert(this->instantiatedGenericTypeStack.size() > 0);
		if(this->resolveGenericType(id) != 0)
			error(0, "Error: generic type %s already exists; types cannot be shadowed", id.c_str());

		this->instantiatedGenericTypeStack.back()[id] = type;
	}

	llvm::Type* CodegenInstance::resolveGenericType(std::string id)
	{
		for(int i = this->instantiatedGenericTypeStack.size(); i-- > 0;)
		{
			auto& map = this->instantiatedGenericTypeStack[i];
			if(map.find(id) != map.end())
				return map[id];
		}

		return 0;
	}

	void CodegenInstance::popGenericTypeStack()
	{
		iceAssert(this->instantiatedGenericTypeStack.size() > 0);
		this->instantiatedGenericTypeStack.pop_back();
	}












	// funcs
	void CodegenInstance::addPublicFunc(FuncPair_t fp)
	{
		this->addFunctionToScope(fp, this->rootNode->publicFuncTree);
	}

	void CodegenInstance::cloneFunctionTree(FunctionTree* ft, FunctionTree* clone, bool deep)
	{
		clone->nsName = ft->nsName;

		for(auto pair : ft->funcs)
		{
			bool existing = false;
			for(auto f : clone->funcs)
			{
				if((f.first && pair.first && (f.first == pair.first))
					|| (f.second && pair.second && (f.second->mangledName == pair.second->mangledName)))
				{
					existing = true;
					break;
				}
			}


			if(deep)
			{
				llvm::Function* func = pair.first;
				if(func)
				{
					iceAssert(func && func->hasName());

					// add to the func table
					auto lf = this->module->getFunction(func->getName());
					if(!lf)
					{
						this->module->getOrInsertFunction(func->getName(), func->getFunctionType());
						lf = this->module->getFunction(func->getName());
					}

					llvm::Function* f = llvm::cast<llvm::Function>(lf);

					f->deleteBody();
					this->addFunctionToScope(FuncPair_t(f, pair.second));
					pair.first = f;
				}
				else
				{
					// generic functions are not instantiated
					if(pair.second->genericTypes.size() == 0)
						error(pair.second, "!func (%s)", pair.second->mangledName.c_str());
				}
			}

			if(!existing)
			{
				// printf("add func %s to clone %d\n", pair.first->getName().bytes_begin(), clone->id);
				clone->funcs.push_back(pair);
			}
		}

		for(auto t : ft->types)
		{
			bool found = false;
			for(auto tt : clone->types)
			{
				if(tt.first == t.first)
				{
					if(!(deep && this->typeMap.find(t.first) == this->typeMap.end()))
					{
						found = true;
						break;
					}
				}
			}

			if(!found && t.first != "Type" && t.first != "Any")
			{
				if(StructBase* sb = dynamic_cast<StructBase*>(t.second.second.first))
				{
					// printf("adding type %s\n", sb->mangledName.c_str());
					clone->types[sb->mangledName] = t.second;

					if(deep)
					{
						// printf("deep type: %s\n", sb->mangledName.c_str());
						this->typeMap[sb->mangledName] = t.second;
					}
				}
			}
		}

		for(auto sub : ft->subs)
		{
			FunctionTree* found = 0;
			for(auto s : clone->subs)
			{
				// printf("clone has: %s, looking for %s\n", s->nsName, );
				if(s->nsName == sub->nsName)
				{
					found = s;
					break;
				}
			}

			if(found)
				this->cloneFunctionTree(sub, found, deep);

			else
			{
				// static int numclones = 0;
				// printf("new clone (%d)\n", numclones++);
				clone->subs.push_back(this->cloneFunctionTree(sub, deep));
			}
		}
	}

	FunctionTree* CodegenInstance::cloneFunctionTree(FunctionTree* ft, bool deep)
	{
		FunctionTree* clone = new FunctionTree();
		this->cloneFunctionTree(ft, clone, deep);
		return clone;
	}


	FunctionTree* CodegenInstance::getCurrentFuncTree(std::deque<std::string>* nses, FunctionTree* root)
	{
		if(root == 0) root = this->rootNode->rootFuncStack;
		if(nses == 0) nses = &this->namespaceStack;

		iceAssert(root);
		iceAssert(nses);

		std::deque<FunctionTree*> ft = root->subs;

		if(nses->size() == 0) return root;

		size_t i = 0;
		size_t max = nses->size();

		// printf("root = %s, %p, %zu\n", root->nsName.c_str(), root, root->subs.size());
		for(auto ns : *nses)
		{
			i++;
			// printf("have %s (%zu/%zu)\n", ns.c_str(), nses->size(), ft.size());

			bool found = false;
			for(auto f : ft)
			{
				// printf("f: %s (%zu)\n", f->nsName.c_str(), ft.size());
				if(f->nsName == ns)
				{
					ft = f->subs;
					// printf("going up: (%zu), (%zu/%zu)\n", ft.size(), i, max);


					if(i == max)
					{
						// printf("ret\n");
						return f;
					}

					found = true;
					break;
				}
			}

			if(!found)
			{
				return 0;
				// // make new.
				// printf("creating new functree %s\n", ns.c_str());
				// FunctionTree* f = new FunctionTree();
				// f->nsName = ns;

				// ft.push_back(f);
			}
		}

		return 0;
	}

	std::pair<TypePair_t*, int> CodegenInstance::findTypeInFuncTree(std::deque<std::string> scope, std::string name)
	{
		auto curDepth = scope;

		// this thing handles pointers properly.
		if(this->getLlvmTypeOfBuiltin(name) != 0)
			return { 0, 0 };

		// not this though.
		int indirections = 0;
		name = this->unwrapPointerType(name, &indirections);

		for(size_t i = 0; i <= curDepth.size(); i++)
		{
			FunctionTree* ft = this->getCurrentFuncTree(&curDepth, this->rootNode->rootFuncStack);
			if(!ft) break;

			for(auto& f : ft->types)
			{
				if(f.first == name)
					return { &f.second, indirections };
			}

			if(curDepth.size() > 0)
				curDepth.pop_back();
		}

		return { 0, -1 };
	}











	void CodegenInstance::pushNamespaceScope(std::string namespc, bool doFuncTree)
	{
		if(doFuncTree)
		{
			bool found = false;
			FunctionTree* existing = this->getCurrentFuncTree();
			for(auto s : existing->subs)
			{
				if(s->nsName == namespc)
				{
					found = true;
					break;
				}
			}

			if(!found)
			{
				FunctionTree* ft = new FunctionTree();
				ft->nsName = namespc;

				existing->subs.push_back(ft);

				FunctionTree* pub = this->getCurrentFuncTree(0, this->rootNode->publicFuncTree);
				pub->subs.push_back(ft);
			}
		}

		this->namespaceStack.push_back(namespc);
	}

	void CodegenInstance::popNamespaceScope()
	{
		// printf("popped namespace %s\n", this->namespaceStack.back().c_str());
		this->namespaceStack.pop_back();
	}

	void CodegenInstance::addFunctionToScope(FuncPair_t func, FunctionTree* root)
	{
		FunctionTree* cur = root;
		if(!cur)
			cur = this->getCurrentFuncTree();

		iceAssert(cur);

		// printf("adding func: %s\n", func.second ? func.second->mangledName.c_str() : func.first->getName().str().c_str());

		for(FuncPair_t& fp : cur->funcs)
		{
			if(fp.first == 0 && fp.second == func.second)
			{
				fp.first = func.first;
				return;
			}
			else if(fp.first == func.first && fp.second == func.second)
			{
				return;
			}
		}

		cur->funcs.push_back(func);
	}

	void CodegenInstance::removeFunctionFromScope(FuncPair_t func)
	{
		FunctionTree* cur = this->getCurrentFuncTree();
		iceAssert(cur);

		auto it = std::find(cur->funcs.begin(), cur->funcs.end(), func);
		if(it != cur->funcs.end())
			cur->funcs.erase(it);
	}

	std::deque<FuncPair_t> CodegenInstance::resolveFunctionName(std::string basename)
	{
		auto curDepth = this->namespaceStack;
		std::deque<FuncPair_t> candidates;


		for(size_t i = 0; i <= curDepth.size(); i++)
		{
			FunctionTree* ft = this->getCurrentFuncTree(&curDepth, this->rootNode->rootFuncStack);
			if(!ft) break;

			for(auto f : ft->funcs)
			{
				auto isDupe = [this, f](FuncPair_t fp) -> bool {

					if(f.first == fp.first || f.second == fp.second) return true;
					if(f.first == 0 || fp.first == 0)
					{
						iceAssert(f.second);
						iceAssert(fp.second);

						if(f.second->params.size() != fp.second->params.size()) return false;

						for(size_t i = 0; i < f.second->params.size(); i++)
						{
							// allowFail = true
							if(this->getLlvmType(f.second->params[i], true) != this->getLlvmType(fp.second->params[i], true))
								return false;
						}

						return true;
					}
					else
					{
						return f.first->getFunctionType() == fp.first->getFunctionType();
					}
				};


				if((f.second ? f.second->name : f.first->getName().str()) == basename)
				{
					if(std::find_if(candidates.begin(), candidates.end(), isDupe) == candidates.end())
					{
						// printf("FOUND (1) %s in search of %s\n", this->printAst(f.second).c_str(), basename.c_str());
						candidates.push_back(f);
					}
				}
			}







			if(curDepth.size() > 0)
				curDepth.pop_back();
		}




		// todo: check if we actually imported the function.
		// this might cause false matches.

		// search in namespace scope.


		// search
		// 1. search own namespace
		// do a top-down search, ensuring we get everything.
		// std::deque<std::string> curDepth;

		// // once with no namespace
		// {
		// 	FunctionTree* ft = this->getCurrentFuncTree(&curDepth);
		// 	if(ft)
		// 	{

		// 	}
		// }

		// for(auto ns : this->namespaceStack)
		// {
		// 	curDepth.push_back(ns);
		// 	FunctionTree* ft = this->getCurrentFuncTree(&curDepth);
		// 	if(!ft) break;

		// 	for(auto f : ft->funcs)
		// 	{
		// 		auto isDupe = [this, f](FuncPair_t fp) -> bool {

		// 			if(f.first == fp.first || f.second == fp.second) return true;
		// 			if(f.first == 0 || fp.first == 0)
		// 			{
		// 				iceAssert(f.second);
		// 				iceAssert(fp.second);

		// 				if(f.second->params.size() != fp.second->params.size()) return false;

		// 				for(size_t i = 0; i < f.second->params.size(); i++)
		// 				{
		// 					// allowFail = true
		// 					if(this->getLlvmType(f.second->params[i], true) != this->getLlvmType(fp.second->params[i], true))
		// 						return false;
		// 				}

		// 				return true;
		// 			}
		// 			else
		// 			{
		// 				return f.first->getFunctionType() == fp.first->getFunctionType();
		// 			}
		// 		};


		// 		if((f.second ? f.second->name : f.first->getName().str()) == basename)
		// 		{
		// 			if(std::find_if(candidates.begin(), candidates.end(), isDupe) == candidates.end())
		// 			{
		// 				// printf("FOUND (2) %s in search of %s\n", this->printAst(f.second).c_str(), basename.c_str());
		// 				candidates.push_back(f);
		// 			}
		// 		}
		// 	}
		// }

		return candidates;
	}

	Resolved_t CodegenInstance::resolveFunctionFromList(Expr* user, std::deque<FuncPair_t> list, std::string basename,
		std::deque<Expr*> params, bool exactMatch)
	{
		std::deque<FuncPair_t> candidates = list;
		if(candidates.size() == 0) return Resolved_t();

		std::deque<std::pair<FuncPair_t, int>> finals;
		for(auto c : candidates)
		{
			int distance = 0;
			if((c.second ? c.second->name : c.first->getName().str()) == basename
				&& this->isValidFuncOverload(c, params, &distance, exactMatch))
			{
				finals.push_back({ c, distance });
			}
		}


		// disambiguate this.
		// with casting distance.
		if(finals.size() > 1)
		{
			// go through each.
			std::deque<std::pair<FuncPair_t, int>> mostViable;
			for(auto f : finals)
			{
				if(mostViable.size() == 0 || mostViable.front().second > f.second)
				{
					mostViable.clear();
					mostViable.push_back(f);
				}
				else if(mostViable.size() > 0 && mostViable.front().second == f.second)
				{
					mostViable.push_back(f);
				}
			}

			if(mostViable.size() == 1)
			{
				return Resolved_t(mostViable.front().first);
			}
			else
			{
				// parameters
				std::string pstr;
				for(auto e : params)
					pstr += this->printAst(e) + ", ";

				if(params.size() > 0)
					pstr = pstr.substr(0, pstr.size() - 2);

				// candidates
				std::string cstr;
				for(auto c : finals)
				{
					if(c.first.second)
						cstr += this->printAst(c.first.second) + "\n";
				}

				error(user, "Ambiguous function call to function %s with parameters: [ %s ], have %zu candidates:\n%s",
					basename.c_str(), pstr.c_str(), finals.size(), cstr.c_str());
			}
		}
		else if(finals.size() == 0)
		{
			return Resolved_t();
		}

		return Resolved_t(finals.front().first);
	}

	Resolved_t CodegenInstance::resolveFunction(Expr* user, std::string basename, std::deque<Expr*> params, bool exactMatch)
	{
		std::deque<FuncPair_t> candidates = this->resolveFunctionName(basename);
		return this->resolveFunctionFromList(user, candidates, basename, params, exactMatch);
	}

	bool CodegenInstance::isValidFuncOverload(FuncPair_t fp, std::deque<Expr*> params, int* castingDistance, bool exactMatch)
	{
		iceAssert(castingDistance);
		*castingDistance = 0;

		if(fp.second)
		{
			FuncDecl* decl = fp.second;

			iceAssert(decl);

			if(decl->params.size() != params.size() && !decl->hasVarArg) return false;
			if(decl->params.size() == 0 && (params.size() == 0 || decl->hasVarArg)) return true;


			#define __min(x, y) ((x) > (y) ? (y) : (x))
			for(size_t i = 0; i < __min(params.size(), decl->params.size()); i++)
			{
				auto t1 = this->getLlvmType(params[i], true);
				auto t2 = this->getLlvmType(decl->params[i], true);

				if(t1 != t2)
				{
					if(exactMatch || t1 == 0 || t2 == 0) return false;

					// try to cast.
					int dist = this->getAutoCastDistance(t1, t2);
					if(dist == -1) return false;

					*castingDistance += dist;
				}
			}

			return true;
		}
		else if(fp.first)
		{
			llvm::Function* lf = fp.first;
			llvm::FunctionType* ft = lf->getFunctionType();

			size_t i = 0;
			for(auto it = ft->param_begin(); it != ft->param_end() && i < params.size(); it++, i++)
			{
				auto t1 = this->getLlvmType(params[i], true);
				auto t2 = *it;

				if(t1 != t2)
				{
					if(exactMatch || t1 == 0 || t2 == 0) return false;

					// try to cast.
					int dist = this->getAutoCastDistance(t1, t2);
					if(dist == -1) return false;

					*castingDistance += dist;
				}
			}

			return true;
		}
		else
		{
			return false;
		}
	}























	void CodegenInstance::clearNamespaceScope()
	{
		this->namespaceStack.clear();
	}



	FuncPair_t* CodegenInstance::getOrDeclareLibCFunc(std::string name)
	{
		std::deque<FuncPair_t> fps = this->resolveFunctionName(name);
		FuncPair_t* fp = 0;

		for(auto f : fps)
		{
			iceAssert(f.second->name == name);
			if(f.second->isFFI)
			{
				fp = &f;
				break;
			}
		}

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
			else if(name == "memset")
			{
				VarDecl* fakefdmvd1 = new VarDecl(Parser::PosInfo(), "ptr", false);
				fakefdmvd1->type = "Int8*";
				params.push_back(fakefdmvd1);

				VarDecl* fakefdmvd2 = new VarDecl(Parser::PosInfo(), "val", false);
				fakefdmvd2->type = "Int8";
				params.push_back(fakefdmvd2);

				VarDecl* fakefdmvd3 = new VarDecl(Parser::PosInfo(), "size", false);
				fakefdmvd3->type = "Uint64";
				params.push_back(fakefdmvd3);

				retType = "Int8*";
			}
			else
			{
				error("enotsup: %s", name.c_str());
			}

			FuncDecl* fakefm = new FuncDecl(Parser::PosInfo(), name, params, retType);
			fakefm->isFFI = true;
			fakefm->codegen(this);
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














	std::deque<std::string> CodegenInstance::unwrapNamespacedType(std::string raw)
	{
		iceAssert(raw.size() > 0);
		if(raw.front() == '(')
		{
			error("enosup");
		}
		else if(raw.front() == '[')
		{
			error("enosup");
		}
		else if(raw.find("::") == std::string::npos)
		{
			return { raw };
		}

		// else
		std::deque<std::string> nses;
		while(true)
		{
			size_t pos = raw.find("::");
			if(pos == std::string::npos) break;

			std::string ns = raw.substr(0, pos);
			nses.push_back(ns);

			raw = raw.substr(pos + 2);
		}

		if(raw.size() > 0)
			nses.push_back(raw);

		return nses;
	}




	std::string CodegenInstance::mangleLlvmType(llvm::Type* type)
	{
		std::string r = this->getReadableType(type);

		int ind = 0;
		r = this->unwrapPointerType(r, &ind);

		if(r == "Int8")			r = "a";
		else if(r == "Int16")	r = "s";
		else if(r == "Int32")	r = "i";
		else if(r == "Int64")	r = "l";
		else if(r == "Int")		r = "l";

		else if(r == "Uint8")	r = "h";
		else if(r == "Uint16")	r = "t";
		else if(r == "Uint32")	r = "j";
		else if(r == "Uint64")	r = "m";
		else if(r == "Uint")	r = "m";

		else if(r == "Float32")	r = "f";
		else if(r == "Float")	r = "f";

		else if(r == "Float64")	r = "d";
		else if(r == "Double")	r = "d";


		else if(r == "Void")	r = "v";
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
		while((next = original.find("::")) != std::string::npos)
		{
			std::string ns = original.substr(0, next);
			ret += std::to_string(ns.length()) + ns;

			original = original.substr(next, -1);

			if(original.compare(0, 2, "::") == 0)
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



	bool CodegenInstance::isDuplicateFuncDecl(FuncDecl* decl)
	{
		if(decl->isFFI) return false;

		std::deque<Expr*> es;
		for(auto p : decl->params) es.push_back(p);

		Resolved_t res = this->resolveFunction(decl, decl->name, es, true);
		if(res.resolved && res.t.first != 0)
		{
			printf("dupe: %s\n", this->printAst(res.t.second).c_str());
			for(size_t i = 0; i < __min(decl->params.size(), res.t.second->params.size()); i++)
			{
				printf("%zu: %s, %s\n", i, getReadableType(decl->params[i]).c_str(), getReadableType(res.t.second->params[i]).c_str());
			}

			return true;
		}
		else
		{
			return false;
		}
	}















	llvm::Function* CodegenInstance::tryResolveAndInstantiateGenericFunction(FuncCall* fc)
	{
		// try and resolve shit
		std::deque<FuncDecl*> candidates;
		std::map<std::string, llvm::Type*> tm;

		auto fpcands = this->resolveFunctionName(fc->name);
		for(FuncPair_t fp : fpcands)
		{
			if(fp.second->genericTypes.size() > 0)
				candidates.push_back(fp.second);
		}

		if(candidates.size() == 0)
		{
			return 0;	// do nothing.
		}

		auto it = candidates.begin();
		while(it != candidates.end())
		{
			FuncDecl* candidate = *it;

			// now check if we *can* instantiate it.
			// first check the number of arguments.
			if(candidate->params.size() != fc->params.size())
			{
				it = candidates.erase(it);
				continue;
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
					llvm::Type* ltype = this->getLlvmType(p, true, false);	// allowFail = true, setInferred = false
					if(!ltype)
					{
						std::string s = p->type.strType;
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
					llvm::Type* ftype = this->getLlvmType(fc->params[pair.second[0]]);
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
					it = candidates.erase(it);
					continue;
				}

				success:
				{
					it++;
				}
			}
		}

		if(candidates.size() == 0)
		{
			return 0;
		}
		else if(candidates.size() > 1)
		{
			std::string cands;
			for(auto c : candidates)
				cands += this->printAst(c) + "\n";

			error(fc, "Ambiguous call to generic function %s, have %zd candidates:\n%s\n", fc->name.c_str(),
				candidates.size(), cands.c_str());
		}

		FuncDecl* candidate = candidates[0];

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

		if(!theFn)
		{
			for(auto pair : this->rootNode->externalGenericFunctions)
			{
				if(pair.first == candidate)
				{
					theFn = pair.second;
					break;
				}
			}
		}





		iceAssert(theFn);
		std::deque<llvm::Type*> instantiatedTypes;
		for(auto p : fc->params)
			instantiatedTypes.push_back(this->getLlvmType(p));


		bool needToCodegen = true;
		for(std::deque<llvm::Type*> inst : theFn->instantiatedGenericVersions)
		{
			if(inst == instantiatedTypes)
			{
				needToCodegen = false;
				break;
			}
		}





		// we need to push a new "generic type stack", and add the types that we resolved into it.
		// todo: might be inefficient.
		// todo: look into creating a version of pushGenericTypeStack that accepts a std::map<string, llvm::Type*>
		// so we don't have to iterate etc etc.
		// I don't want to access cgi->instantiatedGenericTypeStack directly.
		this->pushGenericTypeStack();
		for(auto pair : tm)
			this->pushGenericType(pair.first, pair.second);



		llvm::Function* ffunc = nullptr;
		if(needToCodegen)
		{
			Result_t res = candidate->generateDeclForGenericType(this, tm);
			ffunc = (llvm::Function*) res.result.first;
		}
		else
		{
			std::deque<Expr*> es;
			for(auto p : candidate->params) es.push_back(p);

			Resolved_t rt = this->resolveFunction(fc, candidate->name, es, true); // exact match
			iceAssert(rt.resolved);

			FuncPair_t fp = rt.t;

			ffunc = fp.first;
			iceAssert(ffunc);
		}

		iceAssert(ffunc);



		fc->cachedGenericFuncTarget = ffunc;


		// i've written this waaayyy too many times... but this. is. super. fucking.
		// SUBOPTIMAL. SLOW. SHITTY. O(INFINITY) TIME COMPLEXITY.
		// FUUUUCK THERE HAS GOT TO BE A BETTER WAY.

		// basically, we can call this function multiple times during the course of codegeneration
		// and typechecking (BOOOOO, EACH CALL IS LIKE 12812479 SECONDS???)

		// especially during type inference. Basically, given a FuncCall*, we need to be able to possibly
		// resolve it into an llvm::Function* to do shit.

		if(needToCodegen)
		{
			theFn->decl->instantiatedGenericTypes = instantiatedTypes;
			theFn->decl->instantiatedGenericReturnType = ffunc->getReturnType();

			// dirty: use 'lhsPtr' to pass the version we want.
			theFn->codegen(this, ffunc);
			theFn->instantiatedGenericVersions.push_back(instantiatedTypes);
		}


		this->removeFunctionFromScope({ 0, candidate });
		this->popGenericTypeStack();

		return ffunc;
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
		return Parser::mangledStringToOperator(this, ch);
	}

	Result_t CodegenInstance::callOperatorOnStruct(Expr* user, TypePair_t* pair, llvm::Value* self,
		ArithmeticOp op, llvm::Value* val, bool fail)
	{
		iceAssert(pair);
		iceAssert(pair->first);
		iceAssert(pair->second.first);

		iceAssert(self);
		iceAssert(val);

		if(pair->second.second != TypeKind::Struct && pair->second.second != TypeKind::Class)
		{
			if(fail)	error("!!??!?!?!");
			else		return Result_t(0, 0);
		}

		StructBase* cls = dynamic_cast<StructBase*>(pair->second.first);
		if(!cls)
			error(user, "LHS of operator expression is not a class");

		iceAssert(cls);

		llvm::Function* opov = nullptr;
		for(auto f : cls->lOpOverloads)
		{
			if(f.first == op && (f.second->getArgumentList().back().getType() == val->getType()))
			{
				opov = f.second;
				break;
			}
		}

		if(!opov)
		{
			if(fail)	GenError::noOpOverload(this, user, cls->name, op);
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
			iceAssert(self);
			iceAssert(val);
			return Result_t(builder.CreateCall2(opov, self, val), 0);
		}

		if(fail)	GenError::noOpOverload(this, user, cls->name, op);
		return Result_t(0, 0);
	}

	llvm::Function* CodegenInstance::getStructInitialiser(Expr* user, TypePair_t* pair, std::vector<llvm::Value*> vals)
	{
		// check if this is a builtin type.
		// allow constructor syntax for that
		// eg. let x = Int64(100).
		// sure, this is stupid, but allows for generic 'K' or 'T' that
		// resolves to Int32 or something.

		if(this->isBuiltinType(pair->first))
		{
			iceAssert(pair->second.first == 0);
			std::string fnName = "__builtin_primitive_init_" + this->getReadableType(pair->first);

			std::vector<llvm::Type*> args { pair->first->getPointerTo(), pair->first };
			llvm::FunctionType* ft = llvm::FunctionType::get(pair->first, args, false);

			this->module->getOrInsertFunction(fnName, ft);
			llvm::Function* fn = this->module->getFunction(fnName);

			if(fn->getBasicBlockList().size() == 0)
			{
				llvm::BasicBlock* prevBlock = this->builder.GetInsertBlock();

				llvm::BasicBlock* block = llvm::BasicBlock::Create(this->getContext(), "entry", fn);
				this->builder.SetInsertPoint(block);

				iceAssert(fn->arg_size() > 1);

				llvm::Value* param = ++fn->arg_begin();
				this->builder.CreateRet(param);

				this->builder.SetInsertPoint(prevBlock);
			}


			int i = 0;
			for(auto it = fn->arg_begin(); it != fn->arg_end(); it++, i++)
			{
				llvm::Value& arg = (*it);
				if(vals[i]->getType() != arg.getType())
					GenError::invalidInitialiser(this, user, this->getReadableType(pair->first), vals);
			}


			return fn;
		}


		// else

		if(pair->second.second == TypeKind::Struct)
		{
			Struct* str = dynamic_cast<Struct*>(pair->second.first);
			iceAssert(str);
			iceAssert(str->initFunc);

			if(!str->initFunc)
				error(user, "Struct '%s' has no intialiser???", str->name.c_str());

			return this->module->getFunction(str->initFunc->getName());
		}
		else if(pair->second.second == TypeKind::TypeAlias)
		{
			iceAssert(pair->second.second == TypeKind::TypeAlias);
			TypeAlias* ta = dynamic_cast<TypeAlias*>(pair->second.first);
			iceAssert(ta);

			TypePair_t* tp = this->getType(ta->origType);
			iceAssert(tp);

			return this->getStructInitialiser(user, tp, vals);
		}
		else if(pair->second.second == TypeKind::Class)
		{
			Class* cls = dynamic_cast<Class*>(pair->second.first);
			iceAssert(cls);

			llvm::Function* initf = 0;
			for(llvm::Function* initers : cls->initFuncs)
			{
				if(initers->arg_size() < 1)
					error(user, "(%s:%d) -> ICE: init() should have at least one (implicit) parameter", __FILE__, __LINE__);

				if(initers->arg_size() != vals.size())
					continue;

				int i = 0;
				for(auto it = initers->arg_begin(); it != initers->arg_end(); it++, i++)
				{
					llvm::Value& arg = (*it);
					if(vals[i]->getType() != arg.getType())
						goto breakout;
				}

				// todo: fuuuuuuuuck this is ugly
				initf = initers;
				break;

				breakout:
				continue;
			}

			if(!initf)
				GenError::invalidInitialiser(this, user, cls->name, vals);

			return this->module->getFunction(initf->getName());
		}
		else
		{
			error(user, "Invalid expr type (%s)", typeid(*pair->second.first).name());
		}
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


		// this is the properly adjusted int to add/sub by
		llvm::Value* newrhs = this->builder.CreateMul(rhs, intval);

		// convert the lhs pointer to an int value, so we can add/sub on it
		llvm::Value* ptrval = this->builder.CreatePtrToInt(lhs, newrhs->getType());

		// create the add/sub
		llvm::Value* res = this->builder.CreateBinOp(lop, ptrval, newrhs);

		// turn the int back into a pointer, so we can store it back into the var.
		llvm::Value* tempRes = (lhsPtr && (op == ArithmeticOp::PlusEquals || op == ArithmeticOp::MinusEquals)) ?
			lhsPtr : this->allocateInstanceInBlock(lhs->getType());


		llvm::Value* properres = this->builder.CreateIntToPtr(res, lhs->getType());
		this->builder.CreateStore(properres, tempRes);
		return Result_t(properres, tempRes);
	}


	static void _errorNoReturn(Expr* e)
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
				error(r, "Function has return type '%s', but return statement returned value of type '%s' instead",
					cgi->getReadableType(expected).c_str(), cgi->getReadableType(have).c_str());


			return true;
		}
		else
		{
			return false;
		}
	}

	static Return* recursiveVerifyBranch(CodegenInstance* cgi, Func* f, IfStmt* ifbranch, bool checkType, llvm::Type* retType);
	static Return* recursiveVerifyBlock(CodegenInstance* cgi, Func* f, BracedBlock* bb, bool checkType, llvm::Type* retType)
	{
		if(bb->statements.size() == 0)
			_errorNoReturn(bb);

		Return* r = nullptr;
		for(Expr* e : bb->statements)
		{
			IfStmt* i = nullptr;
			if((i = dynamic_cast<IfStmt*>(e)))
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

	static Return* recursiveVerifyBranch(CodegenInstance* cgi, Func* f, IfStmt* ib, bool checkType, llvm::Type* retType)
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
			error(func, "Function %s has return type '%s', but returns nothing:\n%s", func->decl->name.c_str(),
				func->decl->type.strType.c_str(), this->printAst(func->decl).c_str());
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

			IfStmt* i = dynamic_cast<IfStmt*>(e);
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

	Expr* CodegenInstance::cloneAST(Expr* expr)
	{
		if(expr == 0) return 0;

		if(ComputedProperty* cp = dynamic_cast<ComputedProperty*>(expr))
		{
			ComputedProperty* clone = new ComputedProperty(cp->posinfo, cp->name);

			// copy the rest.
			clone->getterFunc		= (FuncDecl*) this->cloneAST(cp->getterFunc);
			clone->setterFunc		= (FuncDecl*) this->cloneAST(cp->setterFunc);

			// there's no need to actually clone the block.
			clone->getter			= cp->getter;
			clone->setter			= cp->setter;

			clone->setterArgName	= cp->setterArgName;

			clone->inferredLType	= cp->inferredLType;
			clone->initVal			= cp->initVal;
			clone->attribs			= cp->attribs;
			clone->type				= cp->type;

			return clone;
		}
		else if(Func* fn = dynamic_cast<Func*>(expr))
		{
			FuncDecl* cdecl = (FuncDecl*) this->cloneAST(fn->decl);
			BracedBlock* cblock = fn->block;

			Func* clone = new Func(fn->posinfo, cdecl, cblock);

			clone->instantiatedGenericVersions = fn->instantiatedGenericVersions;
			clone->type	= fn->type;

			return clone;
		}
		else if(FuncDecl* fd = dynamic_cast<FuncDecl*>(expr))
		{
			FuncDecl* clone = new FuncDecl(fd->posinfo, fd->name, fd->params, fd->type.strType);

			// copy the rest
			clone->mangledName						= fd->mangledName;
			clone->parentClass						= fd->parentClass;
			clone->mangledNamespaceOnly				= fd->mangledNamespaceOnly;
			clone->genericTypes						= fd->genericTypes;
			clone->instantiatedGenericReturnType	= fd->instantiatedGenericReturnType;
			clone->instantiatedGenericTypes			= fd->instantiatedGenericTypes;
			clone->type								= fd->type;
			clone->attribs							= fd->attribs;

			return clone;
		}
		else
		{
			error(expr, "cannot clone, enosup (%s)", typeid(*expr).name());
		}
	}














}
