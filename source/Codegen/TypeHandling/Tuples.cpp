// TupleCodegen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.


#include "ast.h"
#include "codegen.h"

using namespace Ast;
using namespace Codegen;


llvm::StructType* Tuple::getType(CodegenInstance* cgi)
{
	// todo: handle named tuples.
	// would probably just be handled as implicit anon structs
	// (randomly generated name or something), with appropriate code to handle
	// assignment to and from.

	if(this->ltypes.size() == 0)
	{
		iceAssert(!this->didCreateType);

		for(Expr* e : this->values)
			this->ltypes.push_back(cgi->getLlvmType(e));

		this->name = "__anonymoustuple_" + std::to_string(cgi->typeMap.size());
		this->cachedLlvmType = llvm::StructType::get(cgi->getContext(), this->ltypes);
		this->didCreateType = true;

		// todo: debate, should we add this?
		cgi->addNewType(this->cachedLlvmType, this, TypeKind::Tuple);
	}

	return this->cachedLlvmType;
}

llvm::Type* Tuple::createType(CodegenInstance* cgi)
{
	(void) cgi;
	return 0;
}

Result_t CodegenInstance::doTupleAccess(llvm::Value* selfPtr, Number* num, bool createPtr)
{
	iceAssert(selfPtr);
	iceAssert(num);

	llvm::Type* type = selfPtr->getType()->getPointerElementType();
	iceAssert(type->isStructTy());

	// quite simple, just get the number (make sure it's a Ast::Number)
	// and do a structgep.

	if(num->ival >= type->getStructNumElements())
		error(this, num, "Tuple does not have %d elements, only %d", (int) num->ival + 1, type->getStructNumElements());

	llvm::Value* gep = this->builder.CreateStructGEP(selfPtr, num->ival);
	return Result_t(this->builder.CreateLoad(gep), createPtr ? gep : 0);
}

Result_t Tuple::codegen(CodegenInstance* cgi, llvm::Value* lhsPtr, llvm::Value* rhs)
{
	(void) rhs;

	llvm::Value* gep = 0;
	if(lhsPtr)
	{
		gep = lhsPtr;
	}
	else
	{
		gep = cgi->allocateInstanceInBlock(this->getType(cgi));
	}

	llvm::Type* strtype = gep->getType()->getPointerElementType();

	iceAssert(gep);
	iceAssert(strtype->isStructTy());

	// set all the values.
	// do the gep for each.

	iceAssert(strtype->getStructNumElements() == this->values.size());

	for(unsigned int i = 0; i < strtype->getStructNumElements(); i++)
	{
		llvm::Value* member = cgi->builder.CreateStructGEP(gep, i);
		llvm::Value* val = this->values[i]->codegen(cgi).result.first;

		// printf("%s -> %s\n", cgi->getReadableType(val).c_str(), cgi->getReadableType(member->getType()->getPointerElementType()).c_str());
		cgi->autoCastType(member->getType()->getPointerElementType(), val);

		if(val->getType() != member->getType()->getPointerElementType())
			error(cgi, this, "Element %d of tuple is mismatched, expected '%s' but got '%s'", i,
				cgi->getReadableType(member->getType()->getPointerElementType()).c_str(), cgi->getReadableType(val).c_str());

		cgi->builder.CreateStore(val, member);
	}

	return Result_t(cgi->builder.CreateLoad(gep), gep);
}



















