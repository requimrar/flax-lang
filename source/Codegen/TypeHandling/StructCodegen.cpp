// StructCodegen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.


#include "ast.h"
#include "codegen.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"

using namespace Ast;
using namespace Codegen;


Result_t Struct::codegen(CodegenInstance* cgi, llvm::Value* lhsPtr, llvm::Value* rhs)
{
	iceAssert(this->didCreateType);
	TypePair_t* _type = cgi->getType(this->name);
	if(!_type)
		_type = cgi->getType(this->mangledName);

	if(!_type)
		GenError::unknownSymbol(cgi, this, this->name + " (mangled: " + this->mangledName + ")", SymbolType::Type);



	llvm::GlobalValue::LinkageTypes linkageType;
	if(this->attribs & Attr_VisPublic)
	{
		linkageType = llvm::Function::ExternalLinkage;
	}
	else
	{
		linkageType = llvm::Function::InternalLinkage;
	}






	// see if we have nested types
	for(auto nested : this->nestedTypes)
	{
		cgi->pushNestedTypeScope(this);
		nested.first->codegen(cgi);
		cgi->popNestedTypeScope();
	}


	llvm::StructType* str = llvm::cast<llvm::StructType>(_type->first);

	// generate initialiser
	llvm::Function* defaultInitFunc = llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(llvm::getGlobalContext()), llvm::PointerType::get(str, 0), false), linkageType, "__automatic_init__" + this->mangledName, cgi->module);

	{
		VarDecl* fakeSelf = new VarDecl(this->posinfo, "self", true);
		fakeSelf->type = this->name + "*";

		FuncDecl* fd = new FuncDecl(this->posinfo, defaultInitFunc->getName(), { fakeSelf }, "Void");
		cgi->addFunctionToScope({ defaultInitFunc, fd });
	}


	llvm::BasicBlock* iblock = llvm::BasicBlock::Create(llvm::getGlobalContext(), "initialiser", defaultInitFunc);
	cgi->builder.SetInsertPoint(iblock);

	// create the local instance of reference to self
	llvm::Value* self = &defaultInitFunc->getArgumentList().front();



	for(VarDecl* var : this->members)
	{
		// not supported in structs
		iceAssert(!var->isStatic);

		int i = this->nameMap[var->name];
		iceAssert(i >= 0);

		llvm::Value* ptr = cgi->builder.CreateStructGEP(self, i, "memberPtr_" + var->name);

		auto r = var->initVal ? var->initVal->codegen(cgi).result : ValPtr_t(0, 0);
		var->doInitialValue(cgi, cgi->getType(var->type.strType), r.first, r.second, ptr, false);
	}


	cgi->builder.CreateRetVoid();
	llvm::verifyFunction(*defaultInitFunc);

	this->initFuncs.push_back(defaultInitFunc);


	cgi->rootNode->publicTypes.push_back(std::pair<StructBase*, llvm::Type*>(this, str));
	cgi->addPublicFunc({ defaultInitFunc, 0 });


	return Result_t(nullptr, nullptr);
}








llvm::Type* Struct::createType(CodegenInstance* cgi)
{
	if(this->didCreateType)
		return 0;

	if(cgi->isDuplicateType(this->name))
		GenError::duplicateSymbol(cgi, this, this->name, SymbolType::Type);






	// see if we have nested types
	for(auto nested : this->nestedTypes)
	{
		cgi->pushNestedTypeScope(this);
		nested.second = nested.first->createType(cgi);
		cgi->popNestedTypeScope();
	}




	// check our inheritances??
	bool alreadyHaveSuperclass = false;
	for(auto super : this->protocolstrs)
	{
		TypePair_t* type = cgi->getType(super);
		if(type == 0)
			error(cgi, this, "Type %s does not exist", super.c_str());

		if(type->second.second == TypeKind::Struct)
		{
			if(alreadyHaveSuperclass)
			{
				error(cgi, this, "Multiple inheritance is not supported, only one superclass"
					" can be inherited from. Consider using protocols instead");
			}

			alreadyHaveSuperclass = true;
		}
		else if(type->second.second != TypeKind::Protocol)
		{
			error(cgi, this, "%s is neither a protocol nor a class, and cannot be inherited from", super.c_str());
		}


		StructBase* sb = dynamic_cast<StructBase*>(type->second.first);
		assert(sb);

		// this will (should) do a recursive thing where they copy all their superclassed methods into themselves
		// by the time we see it.
		sb->createType(cgi);


		// if it's a struct, copy its members into ourselves.
		if(type->second.second == TypeKind::Struct)
		{
			this->superclass = { sb, llvm::cast<llvm::StructType>(type->first) };

			// normal members
			for(auto mem : sb->members)
			{
				auto pred = [mem](VarDecl* v) -> bool {

					return v->name == mem->name;
				};

				auto it = std::find_if(this->members.begin(), this->members.end(), pred);
				if(it != this->members.end())
				{
					error(cgi, *it, "Struct fields cannot be overriden, only computed properties can");
				}

				this->members.push_back(mem);
			}

			size_t nms = this->nameMap.size();
			for(auto nm : sb->nameMap)
			{
				this->nameMap[nm.first] = nms;
				nms++;
			}

			// functions
			for(auto fn : sb->funcs)
			{
				auto pred = [fn, cgi](Func* f) -> bool {

					if(fn->decl->params.size() != f->decl->params.size())
						return false;

					for(size_t i = 0; i < fn->decl->params.size(); i++)
					{
						if(cgi->getLlvmType(fn->decl->params[i]) != cgi->getLlvmType(f->decl->params[i]))
							return false;
					}

					return fn->decl->name == f->decl->name;
				};


				auto it = std::find_if(this->funcs.begin(), this->funcs.end(), pred);
				if(it != this->funcs.end())
				{
					// check for 'override'
					Func* f = *it;
					if(!(f->decl->attribs & Attr_Override))
					{
						error(cgi, f->decl, "Overriding function '%s' in superclass %s requires 'override' keyword",
							cgi->printAst(f->decl).c_str(), sb->name.c_str());
					}
					else
					{
						// don't add the superclass one.
						continue;
					}
				}

				this->funcs.push_back((Func*) cgi->cloneAST(fn));
			}






			// computed properties
			for(auto cp : sb->cprops)
			{
				auto pred = [cp](ComputedProperty* cpr) -> bool {

					return cp->name == cpr->name;
				};

				auto it = std::find_if(this->cprops.begin(), this->cprops.end(), pred);
				if(it != this->cprops.end())
				{
					// this thing exists.
					// check if ours has an override
					ComputedProperty* ours = *it;
					assert(ours->name == cp->name);

					if(!(ours->attribs & Attr_Override))
					{
						error(cgi, ours, "Overriding computed property '%s' in superclass %s needs 'override' keyword",
							ours->name.c_str(), sb->name.c_str());
					}
					else
					{
						// we have 'override'.
						// disable this property, don't add it.
						continue;
					}
				}

				this->cprops.push_back((ComputedProperty*) cgi->cloneAST(cp));
			}
		}
		else
		{
			// protcols not supported yet.
			error(cgi, this, "enotsup");
		}
	}




	llvm::Type** types = new llvm::Type*[this->members.size()];

	// create a bodyless struct so we can use it
	this->mangledName = cgi->mangleWithNamespace(this->name, cgi->getNestedTypeList(), false);


	llvm::StructType* str = llvm::StructType::create(llvm::getGlobalContext(), this->mangledName);
	this->scope = cgi->namespaceStack;
	cgi->addNewType(str, this, TypeKind::Struct);








	// because we can't (and don't want to) mangle names in the parser,
	// we could only build an incomplete name -> index map
	// finish it here.

	for(auto p : this->opOverloads)
		p->codegen(cgi);

	for(Func* func : this->funcs)
	{
		// only override if we don't have one.
		if(this->attribs & Attr_VisPublic && !(func->decl->attribs & (Attr_VisInternal | Attr_VisPrivate | Attr_VisPublic)))
			func->decl->attribs |= Attr_VisPublic;

		func->decl->parentClass = this;
		std::string mangled = cgi->mangleFunctionName(func->decl->name, func->decl->params);
		if(this->nameMap.find(mangled) != this->nameMap.end())
		{
			error(cgi, this, "Duplicate member '%s'", func->decl->name.c_str());
		}
	}

	for(VarDecl* var : this->members)
	{
		var->inferType(cgi);
		llvm::Type* type = cgi->getLlvmType(var);
		if(type == str)
		{
			error(cgi, this, "Cannot have non-pointer member of type self");
		}

		cgi->applyExtensionToStruct(cgi->mangleWithNamespace(var->type.strType));
		if(!var->isStatic)
		{
			int i = this->nameMap[var->name];
			iceAssert(i >= 0);

			types[i] = cgi->getLlvmType(var);
		}
	}










	std::vector<llvm::Type*> vec(types, types + this->nameMap.size());
	str->setBody(vec, this->packed);

	this->didCreateType = true;

	delete types;

	return str;
}




















