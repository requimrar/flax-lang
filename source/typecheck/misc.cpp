// misc.cpp
// Copyright (c) 2017, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "ast.h"
#include "errors.h"
#include "typecheck.h"

sst::Expr* ast::TypeExpr::typecheck(sst::TypecheckState* fs, fir::Type* inferred)
{
	auto ret = new sst::TypeExpr(this->loc, fs->convertParserTypeToFIR(this->type));
	return ret;
}

sst::Expr* ast::MutabilityTypeExpr::typecheck(sst::TypecheckState* fs, fir::Type* inferred)
{
	error(this, "Unable to typecheck mutability cast, this shouldn't happen!");
}

sst::Stmt* ast::ImportStmt::typecheck(sst::TypecheckState* fs, fir::Type* inferred)
{
	// nothing to check??
	unexpected(this->loc, "import statement");
}

sst::Expr* ast::SplatOp::typecheck(sst::TypecheckState* fs, fir::Type* inferred)
{
	error(this, "Unable to typecheck splat op, this shouldn't happen!");
}

sst::Defn* ast::Declarable::typecheck(sst::TypecheckState* fs, fir::Type* inferred)
{
	return this->typecheck(fs, inferred, { });
}