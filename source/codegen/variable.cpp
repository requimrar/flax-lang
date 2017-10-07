// variable.cpp
// Copyright (c) 2014 - 2017, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "sst.h"
#include "codegen.h"


CGResult sst::VarDefn::_codegen(cgn::CodegenState* cs, fir::Type* infer)
{
	cs->pushLoc(this);
	defer(cs->popLoc());

	// ok.
	// add a new thing to the thing

	if(this->global)
	{
		warn(this, "globals not supported");
	}

	fir::Value* val = 0;
	fir::Value* alloc = 0;

	CGResult res;
	bool refcounted = cs->isRefCountedType(this->type);
	if(this->init)
	{
		res = this->init->codegen(cs, this->type);
		iceAssert(res.value);

		val = res.value;
	}

	if(!val) val = cs->getDefaultValue(this->type);


	fir::Value* nv = val;
	if(val->getType() != this->type)
		nv = cs->oneWayAutocast(CGResult(val), this->type).value;


	if(!nv)
	{
		iceAssert(this->init);

		HighlightOptions hs;
		hs.underlines.push_back(this->init->loc);
		error(this, hs, "Cannot initialise variable of type '%s' with a value of type '%s'", this->type->str(), val->getType()->str());
	}

	if(this->init && refcounted)
	{
		cs->performRefCountingAssignment(0, res, true);
	}


	if(this->immutable)
	{
		iceAssert(val);
		alloc = cs->irb.CreateImmutStackAlloc(this->type, val, this->id.name);
	}
	else
	{
		alloc = cs->irb.CreateStackAlloc(this->type, this->id.name);

		cs->irb.CreateStore(val, alloc);
	}

	cs->valueMap[this] = CGResult(0, alloc, CGResult::VK::LValue);
	cs->vtree->values[this->id.name].push_back(CGResult(0, alloc, CGResult::VK::LValue));

	if(refcounted) cs->addRefCountedPointer(alloc);

	return CGResult(0, alloc);
}

CGResult sst::VarRef::_codegen(cgn::CodegenState* cs, fir::Type* infer)
{
	cs->pushLoc(this);
	defer(cs->popLoc());

	auto it = cs->valueMap.find(this->def);
	if(it == cs->valueMap.end())
		error(this->def, "wtf?");

	fir::Value* value = 0;
	auto defn = it->second;

	// warn(this, "%p, %p", defn.value, defn.pointer);
	if(!defn.pointer)
	{
		iceAssert(defn.value);
		value = defn.value;
	}
	else
	{
		iceAssert(defn.pointer);
		value = cs->irb.CreateLoad(defn.pointer);
	}

	// make sure types match... should we bother?
	if(value->getType() != this->type)
		error(this, "Type mismatch; typechecking found type '%s', codegen gave type '%s'", this->type->str(), value->getType()->str());

	return CGResult(value, defn.pointer, CGResult::VK::LValue);
}
















