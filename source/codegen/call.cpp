// call.cpp
// Copyright (c) 2014 - 2017, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "sst.h"
#include "codegen.h"

#define dcast(t, v)		dynamic_cast<t*>(v)

CGResult sst::FunctionCall::_codegen(cgn::CodegenState* cs, fir::Type* infer)
{
	cs->pushLoc(this);
	defer(cs->popLoc());

	if(!this->target)
		error(this, "Failed to find target for function call to '%s'", this->name);

	auto vf = this->target->codegen(cs).value;
	fir::FunctionType* ft = 0;

	if(vf->getType()->isFunctionType())
	{
		ft = vf->getType()->toFunctionType();
	}
	else
	{
		auto vt = vf->getType();
		iceAssert(vt->isPointerType() && vt->getPointerElementType()->isFunctionType());

		ft = vt->getPointerElementType()->toFunctionType();

		warn(this, "Prefer using functions to function pointers");
	}

	iceAssert(ft);

	// auto fn = dynamic_cast<fir::Function*>(vf);
	// iceAssert(fn);

	size_t numArgs = ft->getArgumentTypes().size();
	if(!ft->isCStyleVarArg() && this->arguments.size() != numArgs)
	{
		error(this, "Mismatch in number of arguments in call to '%s'; %zu %s provided, but %zu %s expected",
			this->name, this->arguments.size(), this->arguments.size() == 1 ? "was" : "were", numArgs,
			numArgs == 1 ? "was" : "were");
	}
	else if(ft->isCStyleVarArg() && this->arguments.size() < numArgs)
	{
		error(this, "Need at least %zu arguments to call variadic function '%s', only have %zu",
			numArgs, this->name, this->arguments.size());
	}


	size_t i = 0;
	std::vector<fir::Value*> args;
	for(auto arg : this->arguments)
	{
		fir::Type* inf = 0;
		if(i < numArgs)
			inf = ft->getArgumentN(i);

		auto vr = arg->codegen(cs, inf);
		auto val = vr.value;

		if(val->getType()->isConstantNumberType())
		{
			auto cv = dcast(fir::ConstantValue, val);
			iceAssert(cv);

			val = cs->unwrapConstantNumber(cv);
		}

		if(i < numArgs)
		{
			if(val->getType() != ft->getArgumentN(i))
			{
				vr = cs->oneWayAutocast(vr, ft->getArgumentN(i));
				val = vr.value;
			}

			// still?
			if(val->getType() != ft->getArgumentN(i))
			{
				error(arg, "Mismatched type in function call; parameter has type '%s', but given argument has type '%s'",
					ft->getArgumentN(i)->str(), val->getType()->str());
			}
		}
		else if(val->getType()->isStringType())
		{
			// auto-convert strings into char* when passing to va_args
			val = cs->irb.CreateGetStringData(val);
		}

		args.push_back(val);
		i++;
	}

	fir::Value* ret = 0;

	if(fir::Function* func = dcast(fir::Function, vf))
	{
		ret = cs->irb.CreateCall(func, args);
	}
	else
	{
		iceAssert(vf->getType()->getPointerElementType()->isFunctionType());
		auto fptr = cs->irb.CreateLoad(vf);

		ret = cs->irb.CreateCallToFunctionPointer(fptr, ft, args);
	}

	// do the refcounting if we need to
	if(cs->isRefCountedType(ret->getType()))
		cs->addRefCountedValue(ret);

	return CGResult(ret);
}



CGResult sst::ExprCall::_codegen(cgn::CodegenState* cs, fir::Type* infer)
{
	cs->pushLoc(this);
	defer(cs->popLoc());

	fir::Value* fn = this->callee->codegen(cs).value;
	iceAssert(fn->getType()->isFunctionType());

	auto ft = fn->getType()->toFunctionType();

	if(ft->getArgumentTypes().size() != this->arguments.size() && !ft->isVariadicFunc())
	{
		error(this, "Mismatched number of arguments; expected %zu, but %zu were given",
			ft->getArgumentTypes().size(), this->arguments.size());
	}

	std::vector<fir::Value*> args;
	for(size_t i = 0; i < this->arguments.size(); i++)
	{
		fir::Type* inf = 0;

		if(i < ft->getArgumentTypes().size())
			inf = ft->getArgumentN(i);

		else
			inf = ft->getArgumentTypes().back()->getArrayElementType();

		auto rarg = this->arguments[i]->codegen(cs, inf);
		auto arg = cs->oneWayAutocast(rarg, inf).value;

		if(!arg || arg->getType() != inf)
		{
			error(this->arguments[i], "Mismatched types in argument %zu; expected type '%s', but given type '%s'", inf->str(),
				arg ? "??" : arg->getType()->str());
		}

		args.push_back(arg);
	}

	auto ret = cs->irb.CreateCallToFunctionPointer(fn, ft, args);
	return CGResult(ret);
}


















