// Type.cpp
// Copyright (c) 2014 - 2016, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "ir/type.h"

namespace fir
{
	PrimitiveType::PrimitiveType(size_t bits, Kind kind, bool islit)
	{
		this->bitWidth = bits;
		this->primKind = kind;
		this->isUnspecifiedLiteral = islit;
	}


	PrimitiveType* PrimitiveType::getBool(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		// bitwidth = 1
		std::vector<PrimitiveType*> bools = tc->primitiveTypes[1];

		// how do we have more than 1?
		iceAssert(bools.size() == 1 && "???? more than 1 bool??");
		iceAssert(bools.front()->bitWidth == 1 && "not bool purporting to be bool???");

		return bools.front();
	}








	PrimitiveType* PrimitiveType::getIntWithBitWidthAndSignage(FTContext* tc, size_t bits, bool issigned)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		std::vector<PrimitiveType*> types = tc->primitiveTypes[bits];

		iceAssert(types.size() > 0 && "no types of this kind??");

		for(auto t : types)
		{
			iceAssert(t->bitWidth == bits);
			if(t->isIntegerType() && !t->isFloatingPointType() && (t->isSigned() == issigned))
				return t;
		}

		iceAssert(false);
		return 0;
	}

	PrimitiveType* PrimitiveType::getFloatWithBitWidth(FTContext* tc, size_t bits)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");


		std::vector<PrimitiveType*> types = tc->primitiveTypes[bits];

		iceAssert(types.size() > 0 && "no types of this kind??");

		for(auto t : types)
		{
			iceAssert(t->bitWidth == bits);
			if(t->isFloatingPointType())
				return t;
		}

		iceAssert(false);
		return 0;
	}



	PrimitiveType* PrimitiveType::getIntN(size_t bits, FTContext* tc)
	{
		return PrimitiveType::getIntWithBitWidthAndSignage(tc, bits, true);
	}

	PrimitiveType* PrimitiveType::getUintN(size_t bits, FTContext* tc)
	{
		return PrimitiveType::getIntWithBitWidthAndSignage(tc, bits, false);
	}





	PrimitiveType* PrimitiveType::getInt8(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 8, true);
	}

	PrimitiveType* PrimitiveType::getInt16(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 16, true);
	}

	PrimitiveType* PrimitiveType::getInt32(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 32, true);
	}

	PrimitiveType* PrimitiveType::getInt64(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 64, true);
	}

	PrimitiveType* PrimitiveType::getInt128(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 128, true);
	}





	PrimitiveType* PrimitiveType::getUint8(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 8, false);
	}

	PrimitiveType* PrimitiveType::getUint16(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 16, false);
	}

	PrimitiveType* PrimitiveType::getUint32(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 32, false);
	}

	PrimitiveType* PrimitiveType::getUint64(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 64, false);
	}

	PrimitiveType* PrimitiveType::getUint128(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getIntWithBitWidthAndSignage(tc, 128, false);
	}




	PrimitiveType* PrimitiveType::getFloat32(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getFloatWithBitWidth(tc, 32);
	}

	PrimitiveType* PrimitiveType::getFloat64(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getFloatWithBitWidth(tc, 64);
	}

	PrimitiveType* PrimitiveType::getFloat80(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getFloatWithBitWidth(tc, 80);
	}

	PrimitiveType* PrimitiveType::getFloat128(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return PrimitiveType::getFloatWithBitWidth(tc, 128);
	}







	PrimitiveType* PrimitiveType::getUnspecifiedLiteralInt(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		auto t = new PrimitiveType(64, Kind::Integer, true);
		t->isTypeSigned = true;

		return dynamic_cast<PrimitiveType*>(tc->normaliseType(t));
	}

	PrimitiveType* PrimitiveType::getUnspecifiedLiteralUint(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		auto t = new PrimitiveType(64, Kind::Integer, true);
		t->isTypeSigned = false;

		return dynamic_cast<PrimitiveType*>(tc->normaliseType(t));
	}

	PrimitiveType* PrimitiveType::getUnspecifiedLiteralFloat(FTContext* tc)
	{
		if(!tc) tc = getDefaultFTContext();
		iceAssert(tc && "null type context");

		return dynamic_cast<PrimitiveType*>(tc->normaliseType(new PrimitiveType(64, Kind::Floating, true)));
	}

	PrimitiveType* PrimitiveType::getUnliteralType(FTContext* tc)
	{
		if(!this->isLiteralType())
		{
			return this;
		}

		// ok
		if(this->isIntegerType())
		{
			if(this->isSigned())
			{
				return fir::PrimitiveType::getIntN(this->getIntegerBitWidth());
			}
			else
			{
				return fir::PrimitiveType::getUintN(this->getIntegerBitWidth());
			}
		}
		else
		{
			return fir::PrimitiveType::getFloatWithBitWidth(tc, this->getFloatingPointBitWidth());
		}
	}





	// various
	std::string PrimitiveType::str()
	{
		// is primitive.
		std::string ret;

		if(this->isLiteralType())
		{
			if(this->primKind == Kind::Integer)
				return "int?";

			else
				return "float?";
		}


		if(this->primKind == Kind::Integer)
		{
			if(!this->isSigned() && this->getIntegerBitWidth() == 1)
				return "bool";

			if(this->isSigned())	ret = "i";
			else					ret = "u";

			ret += std::to_string(this->getIntegerBitWidth());
		}
		else if(this->primKind == Kind::Floating)
		{
			ret = "f" + std::to_string(this->getFloatingPointBitWidth());
		}
		else
		{
			ret = "??";
		}

		return ret;
	}

	std::string PrimitiveType::encodedStr()
	{
		return this->str();
	}


	bool PrimitiveType::isTypeEqual(Type* other)
	{
		PrimitiveType* po = dynamic_cast<PrimitiveType*>(other);
		if(!po) return false;
		if(this->primKind != po->primKind) return false;
		if(this->bitWidth != po->bitWidth) return false;
		if(this->isTypeSigned != po->isTypeSigned) return false;
		if(this->isUnspecifiedLiteral != po->isUnspecifiedLiteral) return false;

		return true;
	}




	bool PrimitiveType::isSigned()
	{
		iceAssert(this->primKind == Kind::Integer && "not integer type");
		return this->isTypeSigned;
	}

	bool PrimitiveType::isLiteralType()
	{
		return this->isUnspecifiedLiteral;
	}

	size_t PrimitiveType::getIntegerBitWidth()
	{
		iceAssert(this->primKind == Kind::Integer && "not integer type");
		return this->bitWidth;
	}


	// float stuff
	size_t PrimitiveType::getFloatingPointBitWidth()
	{
		iceAssert(this->primKind == Kind::Floating && "not floating point type");
		return this->bitWidth;
	}

	PrimitiveType* PrimitiveType::getOppositeSignedType()
	{
		if(this == Type::getInt8())
		{
			return Type::getUint8();
		}
		else if(this == Type::getInt16())
		{
			return Type::getUint16();
		}
		else if(this == Type::getInt32())
		{
			return Type::getUint32();
		}
		else if(this == Type::getInt64())
		{
			return Type::getUint64();
		}
		else if(this == Type::getInt128())
		{
			return Type::getUint128();
		}
		else if(this == Type::getUint8())
		{
			return Type::getInt8();
		}
		else if(this == Type::getUint16())
		{
			return Type::getInt16();
		}
		else if(this == Type::getUint32())
		{
			return Type::getInt32();
		}
		else if(this == Type::getUint64())
		{
			return Type::getInt64();
		}
		else if(this == Type::getUint128())
		{
			return Type::getInt128();
		}
		else
		{
			return this;
		}
	}




	PrimitiveType* PrimitiveType::reify(std::map<std::string, Type*> names, FTContext* tc)
	{
		// do nothing
		return this;
	}
}


















