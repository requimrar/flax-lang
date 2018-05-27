// assign.cpp
// Copyright (c) 2014 - 2017, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "sst.h"
#include "codegen.h"
#include "gluecode.h"

sst::AssignOp::AssignOp(const Location& l) : Expr(l, fir::Type::getVoid()) { this->readableName = "assignment statement"; }
sst::TupleAssignOp::TupleAssignOp(const Location& l) : Expr(l, fir::Type::getVoid()) { this->readableName = "destructuring assignment statement"; }


CGResult sst::AssignOp::_codegen(cgn::CodegenState* cs, fir::Type* infer)
{
	cs->pushLoc(this);
	defer(cs->popLoc());

	auto lr = this->left->codegen(cs);
	auto lt = lr.value->getType();

	if(!lr.pointer || lr.kind != CGResult::VK::LValue)
	{
		SpanError(SimpleError::make(this, "Cannot assign to non-lvalue (most likely a temporary) expression"))
			.add(SpanError::Span(this->left->loc, "here"))
			.postAndQuit();
	}

	if(lr.pointer && lr.pointer->getType()->isImmutablePointer())
	{
		SpanError(SimpleError::make(this, "Cannot assign to immutable expression"))
			.add(SpanError::Span(this->left->loc, "here"))
			.postAndQuit();
	}


	// okay, i guess
	auto rr = this->right->codegen(cs, lt);
	auto rt = rr.value->getType();

	if(this->op != Operator::Assign)
	{
		// ok it's a compound assignment
		// auto [ newl, newr ] = cs->autoCastValueTypes(lr, rr);
		auto nonass = Operator::getNonAssignmentVersion(this->op);

		// some things -- if we're doing +=, and the types are supported, then just call the actual
		// append function, instead of doing the + first then assigning it.

		if(nonass == Operator::Plus)
		{
			if(lt->isDynamicArrayType() && lt == rt)
			{
				// right then.
				if(lr.kind != CGResult::VK::LValue)
					error(this, "Cannot append to an r-value array");

				iceAssert(lr.pointer);
				auto appendf = cgn::glue::array::getAppendFunction(cs, lt->toDynamicArrayType());

				//? are there any ramifications for these actions for ref-counted things?
				auto res = cs->irb.Call(appendf, lr.value, rr.value);

				cs->irb.Store(res, lr.pointer);
				return CGResult(0);
			}
			else if(lt->isDynamicArrayType() && lt->getArrayElementType() == rt)
			{
				// right then.
				if(lr.kind != CGResult::VK::LValue)
					error(this, "Cannot append to an r-value array");

				iceAssert(lr.pointer);
				auto appendf = cgn::glue::array::getElementAppendFunction(cs, lt->toDynamicArrayType());

				//? are there any ramifications for these actions for ref-counted things?
				auto res = cs->irb.Call(appendf, lr.value, rr.value);

				cs->irb.Store(res, lr.pointer);
				return CGResult(0);
			}
			else if(lt->isStringType() && lt == rt)
			{
				// right then.
				if(lr.kind != CGResult::VK::LValue)
					error(this, "Cannot append to an r-value string");

				iceAssert(lr.pointer);
				auto appendf = cgn::glue::string::getAppendFunction(cs);

				//? are there any ramifications for these actions for ref-counted things?
				auto res = cs->irb.Call(appendf, lr.value, cs->irb.CreateSliceFromString(rr.value, true));

				cs->irb.Store(res, lr.pointer);
				return CGResult(0);
			}
			else if(lt->isStringType() && rt->isCharType())
			{
				// right then.
				if(lr.kind != CGResult::VK::LValue)
					error(this, "Cannot append to an r-value string");

				iceAssert(lr.pointer);
				auto appendf = cgn::glue::string::getCharAppendFunction(cs);

				//? are there any ramifications for these actions for ref-counted things?
				auto res = cs->irb.Call(appendf, lr.value, rr.value);

				cs->irb.Store(res, lr.pointer);
				return CGResult(0);
			}
		}


		// do the op first
		auto res = cs->performBinaryOperation(this->loc, { this->left->loc, lr }, { this->right->loc, rr }, nonass);

		// assign the res to the thing
		rr = res;
	}

	rr = cs->oneWayAutocast(rr, lt);

	if(rr.value == 0)
	{
		error(this, "Invalid assignment from value of type '%s' to expected type '%s'", rr.value->getType(), lt);
	}

	// ok then
	if(lt != rr.value->getType())
		error(this, "What? left = %s, right = %s", lt, rr.value->getType());

	iceAssert(lr.pointer);
	iceAssert(rr.value->getType() == lr.pointer->getType()->getPointerElementType());

	cs->autoAssignRefCountedValue(lr, rr, false, true);

	return CGResult(0);
}





CGResult sst::TupleAssignOp::_codegen(cgn::CodegenState* cs, fir::Type* infer)
{
	cs->pushLoc(this);
	defer(cs->popLoc());

	auto tuple = this->right->codegen(cs).value;
	if(!tuple->getType()->isTupleType())
		error(this->right, "Expected tuple type in assignment to tuple on left-hand-side; found type '%s' instead", tuple->getType());

	auto tty = tuple->getType()->toTupleType();

	std::vector<CGResult> results;

	size_t idx = 0;
	for(auto v : this->lefts)
	{
		auto res = v->codegen(cs, tty->getElementN(idx));
		if(res.kind != CGResult::VK::LValue)
			error(v, "Cannot assign to non-lvalue expression in tuple assignment");

		if(!res.pointer)
			error(v, "didn't get pointer???");


		iceAssert(res.pointer);
		results.push_back(res);

		idx++;
	}

	for(size_t i = 0; i < idx; i++)
	{
		auto lr = results[i];
		auto val = cs->irb.ExtractValue(tuple, { i });

		auto rr = cs->oneWayAutocast(CGResult(val, 0), lr.value->getType());
		if(!rr.value || rr.value->getType() != lr.value->getType())
		{
			error(this->right, "Mismatched types in assignment to tuple element %d; assigning type '%s' to '%s'",
				val->getType(), lr.value->getType());
		}

		cs->autoAssignRefCountedValue(lr, rr, false, true);
	}

	return CGResult(0);
}



































