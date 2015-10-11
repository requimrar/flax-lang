// Type.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include <map>
#include <unordered_map>

#include "../include/codegen.h"
#include "../include/compiler.h"
#include "../include/flax/type.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"

namespace flax
{
	ArrayType::ArrayType(Type* elm, size_t num) : Type(FTypeKind::Array)
	{
		this->arrayElementType = elm;
		this->arraySize = num;
	}

	ArrayType* ArrayType::getArray(Type* elementType, size_t num, FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		// create.
		ArrayType* type = new ArrayType(elementType, num);
		return dynamic_cast<ArrayType*>(tc->normaliseType(type));
	}

	// various
	std::string ArrayType::str()
	{
		std::string ret = this->arrayElementType->str();
		ret += "[" + std::to_string(this->getArraySize()) + "]";

		return ret;
	}


	bool ArrayType::isTypeEqual(Type* other)
	{
		ArrayType* af = dynamic_cast<ArrayType*>(other);
		if(!af) return false;
		if(this->arraySize != af->arraySize) return false;

		return this->arrayElementType->isTypeEqual(af->arrayElementType);
	}



	llvm::Type* ArrayType::getLlvmType(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		if(this->llvmType == 0)
		{
			// do it.
			this->llvmType = llvm::ArrayType::get(this->arrayElementType->getLlvmType(), this->arraySize);
		}

		return this->llvmType;
	}


	// array stuff
	Type* ArrayType::getElementType()
	{
		iceAssert(this->typeKind == FTypeKind::Array && "not array type");
		return this->arrayElementType;
	}

	size_t ArrayType::getArraySize()
	{
		iceAssert(this->typeKind == FTypeKind::Array && "not array type");
		return this->arraySize;
	}
}


















