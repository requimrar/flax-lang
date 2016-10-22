// ConstantValue.cpp
// Copyright (c) 2014 - 2016, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "ir/value.h"
#include "ir/constant.h"

#include <float.h>

namespace fir
{
	ConstantValue::ConstantValue(Type* t) : Value(t)
	{
		// nothing.
	}

	ConstantValue* ConstantValue::getNullValue(Type* type)
	{
		iceAssert(!type->isVoidType() && "cannot make void constant");

		auto ret = new ConstantValue(type);
		return ret;
	}

	ConstantValue* ConstantValue::getNull()
	{
		auto ret = new ConstantValue(fir::Type::getVoid()->getPointerTo());
		return ret;
	}


	// todo: unique these values.
	ConstantInt* ConstantInt::get(Type* intType, size_t val)
	{
		iceAssert(intType->isIntegerType() && "not integer type");
		return new ConstantInt(intType, val);
	}

	ConstantInt::ConstantInt(Type* type, ssize_t val) : ConstantValue(type)
	{
		this->value = val;
	}

	ConstantInt::ConstantInt(Type* type, size_t val) : ConstantValue(type)
	{
		this->value = val;
	}

	ssize_t ConstantInt::getSignedValue()
	{
		return (ssize_t) this->value;
	}

	size_t ConstantInt::getUnsignedValue()
	{
		return this->value;
	}

	ConstantInt* ConstantInt::getBool(bool value, FTContext* tc)
	{
		Type* t = Type::getBool(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getInt8(int8_t value, FTContext* tc)
	{
		Type* t = Type::getInt8(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getInt16(int16_t value, FTContext* tc)
	{
		Type* t = Type::getInt16(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getInt32(int32_t value, FTContext* tc)
	{
		Type* t = Type::getInt32(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getInt64(int64_t value, FTContext* tc)
	{
		Type* t = Type::getInt64(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getUint8(uint8_t value, FTContext* tc)
	{
		Type* t = Type::getUint8(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getUint16(uint16_t value, FTContext* tc)
	{
		Type* t = Type::getUint16(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getUint32(uint32_t value, FTContext* tc)
	{
		Type* t = Type::getUint32(tc);
		return ConstantInt::get(t, value);
	}

	ConstantInt* ConstantInt::getUint64(uint64_t value, FTContext* tc)
	{
		Type* t = Type::getUint64(tc);
		return ConstantInt::get(t, value);
	}





























	ConstantFP* ConstantFP::get(Type* type, float val)
	{
		iceAssert(type->isFloatingPointType() && "not floating point type");
		return new ConstantFP(type, val);
	}

	ConstantFP* ConstantFP::get(Type* type, double val)
	{
		iceAssert(type->isFloatingPointType() && "not floating point type");
		return new ConstantFP(type, val);
	}

	ConstantFP::ConstantFP(Type* type, float val) : fir::ConstantValue(type)
	{
		this->value = (double) val;
	}

	ConstantFP::ConstantFP(Type* type, double val) : fir::ConstantValue(type)
	{
		this->value = val;
	}

	double ConstantFP::getValue()
	{
		return this->value;
	}

	ConstantFP* ConstantFP::getFloat32(float value, FTContext* tc)
	{
		Type* t = Type::getFloat32(tc);
		return ConstantFP::get(t, value);
	}

	ConstantFP* ConstantFP::getFloat64(double value, FTContext* tc)
	{
		Type* t = Type::getFloat64(tc);
		return ConstantFP::get(t, value);
	}










	ConstantArray* ConstantArray::get(Type* type, std::vector<ConstantValue*> vals)
	{
		return new ConstantArray(type, vals);
	}

	ConstantArray::ConstantArray(Type* type, std::vector<ConstantValue*> vals) : fir::ConstantValue(type)
	{
		this->values = vals;
	}






	bool checkSignedIntLiteralFitsIntoType(fir::PrimitiveType* type, ssize_t val)
	{
		iceAssert(type->isIntegerType());
		if(type->isSigned())
		{
			ssize_t max = ((size_t) 1 << (type->getIntegerBitWidth() - 1)) - 1;
			ssize_t min = -max - 1;

			return val <= max && val >= min;
		}
		else
		{
			size_t max = 0;
			switch(type->getIntegerBitWidth())
			{
				case 8: 	max = UINT8_MAX; break;
				case 16:	max = UINT16_MAX; break;
				case 32:	max = UINT32_MAX; break;
				case 64:	max = UINT64_MAX; break;
				default:	iceAssert(0);
			}

			// won't get overflow problems, because short-circuiting makes sure val is positive.
			return val >= 0 && (size_t) val <= max;
		}
	}

	bool checkUnsignedIntLiteralFitsIntoType(fir::PrimitiveType* type, size_t val)
	{
		iceAssert(type->isIntegerType());
		size_t max = 0;

		if(type->isSigned())
		{
			max = (1 << (type->getIntegerBitWidth() - 1)) - 1;
			return val <= max;
		}
		else
		{
			switch(type->getIntegerBitWidth())
			{
				case 8: 	max = UINT8_MAX; break;
				case 16:	max = UINT16_MAX; break;
				case 32:	max = UINT32_MAX; break;
				case 64:	max = UINT64_MAX; break;
				default:	iceAssert(0);
			}

			return val <= max;
		}
	}


	bool checkFloatingPointLiteralFitsIntoType(fir::PrimitiveType* type, double val)
	{
		if(type->getFloatingPointBitWidth() == 32)
			return (double) ((float) val) == val;

		else if(type->getFloatingPointBitWidth() == 64)
			return true;

		else
			iceAssert(0);
	}
}






















