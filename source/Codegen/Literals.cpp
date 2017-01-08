// LiteralCodegen.cpp
// Copyright (c) 2014 - 2015, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "codegen.h"

using namespace Ast;
using namespace Codegen;

Result_t Number::codegen(CodegenInstance* cgi, fir::Value* extra)
{
	// check builtin type
	if(this->decimal)
	{
		return Result_t(fir::ConstantFP::get(fir::PrimitiveType::getUnspecifiedLiteralFloat(), this->dval), 0);
	}
	else if(this->needUnsigned)
	{
		return Result_t(fir::ConstantInt::get(fir::PrimitiveType::getUnspecifiedLiteralUint(), (uint64_t) this->ival), 0);
	}
	else
	{
		return Result_t(fir::ConstantInt::get(fir::PrimitiveType::getUnspecifiedLiteralInt(), this->ival), 0);
	}
}

static fir::Type* _makeReal(fir::Type* pt)
{
	if(pt->isPrimitiveType() && pt->toPrimitiveType()->isLiteralType())
	{
		if(pt->toPrimitiveType()->isFloatingPointType())
			return fir::Type::getFloat64();

		else if(pt->toPrimitiveType()->isSignedIntType())
			return fir::Type::getInt64();

		else
			return fir::Type::getUint64();
	}

	return pt;
}

static fir::ConstantValue* _makeReal(fir::ConstantValue* cv)
{
	if(fir::ConstantInt* ci = dynamic_cast<fir::ConstantInt*>(cv))
	{
		return fir::ConstantInt::get(_makeReal(ci->getType()), ci->getSignedValue());
	}
	else if(fir::ConstantFP* cf = dynamic_cast<fir::ConstantFP*>(cv))
	{
		return fir::ConstantFP::get(_makeReal(cf->getType()), cf->getValue());
	}
	else if(fir::ConstantArray* ca = dynamic_cast<fir::ConstantArray*>(cv))
	{
		std::vector<fir::ConstantValue*> vs;
		for(auto v : ca->getValues())
			vs.push_back(_makeReal(v));

		return fir::ConstantArray::get(fir::ArrayType::get(vs.front()->getType(), vs.size()), vs);
	}

	return cv;
}


fir::Type* Number::getType(CodegenInstance* cgi, bool allowFail, fir::Value* extra)
{
	if(this->decimal)
	{
		return fir::PrimitiveType::getUnspecifiedLiteralFloat();
	}
	else if(this->needUnsigned)
	{
		return fir::PrimitiveType::getUnspecifiedLiteralUint();
	}
	else
	{
		return fir::PrimitiveType::getUnspecifiedLiteralInt();
	}
}






Result_t BoolVal::codegen(CodegenInstance* cgi, fir::Value* extra)
{
	return Result_t(fir::ConstantInt::getBool(this->val), 0);
}

fir::Type* BoolVal::getType(CodegenInstance* cgi, bool allowFail, fir::Value* extra)
{
	return fir::Type::getBool();
}







Result_t NullVal::codegen(CodegenInstance* cgi, fir::Value* extra)
{
	return Result_t(fir::ConstantValue::getNull(), 0);
}

fir::Type* NullVal::getType(CodegenInstance* cgi, bool allowFail, fir::Value* extra)
{
	return fir::Type::getVoid()->getPointerTo();
}






Result_t StringLiteral::codegen(CodegenInstance* cgi, fir::Value* extra)
{
	if(this->isRaw)
	{
		// good old Int8*
		fir::Value* stringVal = cgi->module->createGlobalString(this->str);
		return Result_t(stringVal, 0);
	}
	else
	{
		if(extra && extra->getType()->getPointerElementType()->isStringType())
		{
			// these things can't be const
			iceAssert(extra->getType()->getPointerElementType()->isStringType());

			// we don't (and can't) set the refcount, because it's probably in read-only memory.

			fir::ConstantString* cs = fir::ConstantString::get(this->str);
			cgi->irb.CreateStore(cs, extra);

			if(!extra->hasName())
				extra->setName("strlit");

			return Result_t(cs, extra);
		}
		else if(extra && extra->getType()->getPointerElementType()->isCharType())
		{
			if(this->str.length() == 0)
				error(this, "Character literal cannot be empty");

			else if(this->str.length() > 1)
				error(this, "Character literal can have at most 1 (ASCII) character");

			char c = this->str[0];
			fir::ConstantValue* cv = fir::ConstantChar::get(c);
			cgi->irb.CreateStore(cv, extra);

			return Result_t(cv, extra);
		}
		else
		{
			return cgi->makeStringLiteral(this->str);
		}
	}
}

fir::Type* StringLiteral::getType(CodegenInstance* cgi, bool allowFail, fir::Value* extra)
{
	if(this->isRaw)
		return fir::Type::getInt8Ptr();

	else
		return fir::Type::getStringType();
}












Result_t ArrayLiteral::codegen(CodegenInstance* cgi, fir::Value* extra)
{
	fir::Type* tp = 0;
	std::vector<fir::ConstantValue*> vals;

	if(this->values.size() == 0)
	{
		if(!extra)
		{
			error(this, "Unable to infer type for empty array");
		}

		iceAssert(extra->getType()->isPointerType() && extra->getType()->getPointerElementType()->isArrayType());
		tp = extra->getType()->getPointerElementType()->toArrayType()->getElementType();
	}
	else
	{
		tp = _makeReal(this->values.front()->getType(cgi));

		for(Expr* e : this->values)
		{
			fir::Value* v = e->codegen(cgi).value;
			if(dynamic_cast<fir::ConstantValue*>(v))
			{
				fir::ConstantValue* c = dynamic_cast<fir::ConstantValue*>(v);
				c = _makeReal(c);

				// attempt to make a proper thing if we're int literals
				if(c->getType()->isIntegerType() && tp->isIntegerType())
				{
					fir::ConstantInt* ci = dynamic_cast<fir::ConstantInt*>(c);
					iceAssert(ci);

					// c is a constant.
					bool res = false;
					if(c->getType()->isSignedIntType())
						res = fir::checkSignedIntLiteralFitsIntoType(tp->toPrimitiveType(), ci->getSignedValue());

					else
						res = fir::checkUnsignedIntLiteralFitsIntoType(tp->toPrimitiveType(), ci->getUnsignedValue());


					if(res)
						c = fir::ConstantInt::get(tp, ci->getUnsignedValue());
				}


				if(c->getType() != tp)
				{
					error(e, "Array members must have the same type, got %s and %s",
						c->getType()->str().c_str(), tp->str().c_str());
				}

				vals.push_back(c);
			}
			else
			{
				error(e, "Array literal members must be constant");
			}
		}
	}

	fir::ArrayType* atype = fir::ArrayType::get(tp, this->values.size());
	// fir::Value* alloc = cgi->irb.CreateStackAlloc(atype);
	fir::Value* val = fir::ConstantArray::get(atype, vals);

	// cgi->irb.CreateStore(val, alloc);
	return Result_t(val, /*alloc*/0);
}

fir::Type* ArrayLiteral::getType(CodegenInstance* cgi, bool allowFail, fir::Value* extra)
{
	if(this->values.empty())
	{
		if(!extra)
		{
			error(this, "Unable to infer type for empty array");
		}

		iceAssert(extra->getType()->isPointerType());
		iceAssert(extra->getType()->getPointerElementType()->isArrayType());
		auto tp = extra->getType()->getPointerElementType()->toArrayType()->getElementType();

		return fir::ArrayType::get(tp, 0);
	}
	else
	{
		return fir::ArrayType::get(_makeReal(this->values.front()->getType(cgi)), this->values.size());
	}
}
















static size_t _counter = 0;
fir::TupleType* Tuple::getType(CodegenInstance* cgi, bool allowFail, fir::Value* extra)
{
	// todo: handle named tuples.
	// would probably just be handled as implicit anon structs
	// (randomly generated name or something), with appropriate code to handle
	// assignment to and from.

	if(this->ltypes.size() == 0)
	{
		iceAssert(!this->didCreateType);

		for(Expr* e : this->values)
			this->ltypes.push_back(_makeReal(e->getType(cgi)));

		this->ident.name = "__anonymoustuple_" + std::to_string(_counter++);
		this->createdType = fir::TupleType::get(this->ltypes, cgi->getContext());
		this->didCreateType = true;

		// todo: debate, should we add this?
		// edit: no.
		// cgi->addNewType(this->createdType, this, TypeKind::Tuple);
	}

	return this->createdType;
}

fir::Type* Tuple::createType(CodegenInstance* cgi)
{
	(void) cgi;
	return 0;
}

Result_t Tuple::codegen(CodegenInstance* cgi, fir::Value* extra)
{
	fir::TupleType* tuptype = this->getType(cgi)->toTupleType();
	iceAssert(tuptype);

	iceAssert(tuptype->getElementCount() == this->values.size());

	// first check if we can make a constant.
	bool allConst = true;
	std::deque<fir::Value*> vals;
	for(auto v : this->values)
	{
		auto cgv = v->codegen(cgi).value;
		allConst = allConst && (dynamic_cast<fir::ConstantValue*>(cgv) != 0);

		vals.push_back(cgv);
	}

	if(allConst)
	{
		std::deque<fir::ConstantValue*> cvs;
		for(auto v : vals)
		{
			auto cv = dynamic_cast<fir::ConstantValue*>(v); iceAssert(cv);
			cvs.push_back(_makeReal(cv));
		}

		fir::ConstantTuple* ct = fir::ConstantTuple::get(cvs);
		iceAssert(ct);

		// if all const, fuck storing anything
		// return an rvalue
		return Result_t(ct, 0);
	}
	else
	{
		// set all the values.
		// do the gep for each.

		fir::Value* gep = extra ? extra : cgi->getStackAlloc(this->getType(cgi));
		iceAssert(gep);

		for(size_t i = 0; i < tuptype->getElementCount(); i++)
		{
			fir::Value* member = cgi->irb.CreateStructGEP(gep, i);
			fir::Value* val = vals[i];

			val = cgi->autoCastType(member->getType()->getPointerElementType(), val);

			if(val->getType() != member->getType()->getPointerElementType())
			{
				error(this, "Element %zu of tuple is mismatched, expected '%s' but got '%s'", i,
					member->getType()->getPointerElementType()->str().c_str(), val->getType()->str().c_str());
			}

			cgi->irb.CreateStore(val, member);
		}

		return Result_t(cgi->irb.CreateLoad(gep), gep);
	}
}











