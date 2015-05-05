// DotOperatorCodegen.cpp
// Copyright (c) 2014 - The Foreseeable Future, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.



#include "ast.h"
#include "codegen.h"
#include "llvm_all.h"

using namespace Ast;
using namespace Codegen;


static Result_t doFunctionCall(CodegenInstance* cgi, FuncCall* fc, llvm::Value* self, llvm::Value* selfPtr, bool isPtr, Struct* str,
	bool isStaticFunctionCall);

static Result_t doVariable(CodegenInstance* cgi, VarRef* var, llvm::Value* _rhs, llvm::Value* self, llvm::Value* selfPtr,
	bool isPtr, Struct* str, int i);

static Result_t doComputedProperty(CodegenInstance* cgi, VarRef* var, ComputedProperty* cprop, llvm::Value* _rhs, llvm::Value* self, llvm::Value* selfPtr, bool isPtr, Struct* str);

static Result_t doStaticAccess(CodegenInstance* cgi, MemberAccess* ma);














Result_t ComputedProperty::codegen(CodegenInstance* cgi, llvm::Value* lhsPtr, llvm::Value* rhs)
{
	// handled elsewhere.
	return Result_t(0, 0);
}



Result_t MemberAccess::codegen(CodegenInstance* cgi, llvm::Value* lhsPtr, llvm::Value* _rhs)
{
	// check for special cases -- static calling and enums.
	VarRef* _vr = dynamic_cast<VarRef*>(this->target);
	if(_vr)
	{
		// check for type function access
		TypePair_t* tp = 0;
		if((tp = cgi->getType(cgi->mangleWithNamespace(_vr->name, false))))
		{
			if(tp->second.second == TypeKind::Enum)
			{
				return enumerationAccessCodegen(cgi, this->target, this->member);
			}
			else if(tp->second.second == TypeKind::Struct)
			{
				return doStaticAccess(cgi, this);
			}
		}
	}


	// Expr* re = rearrangeNonStaticAccess(cgi, this);
	// (void) re;

	// gen the var ref on the left.
	Result_t res = this->target->codegen(cgi);
	ValPtr_t p = res.result;

	llvm::Value* self = p.first;
	llvm::Value* selfPtr = p.second;


	bool isPtr = false;
	bool isWrapped = false;

	llvm::Type* type = self->getType();
	if(!type)
		error("(%s:%d) -> Internal check failed: invalid type encountered", __FILE__, __LINE__);



	// if(!self)
	// 	warn(cgi, this, "self is null! (%s, %s)", (typeid(*this->target)).name(), cgi->getReadableType(type).c_str());

	// if(!selfPtr)
	// 	warn(cgi, this, "selfptr is null! (%s, %s)", (typeid(*this->target)).name(), cgi->getReadableType(type).c_str());




	if(cgi->isTypeAlias(type))
	{
		iceAssert(type->isStructTy());
		iceAssert(type->getStructNumElements() == 1);
		type = type->getStructElementType(0);

		warn(cgi, this, "typealias encountered");
		isWrapped = true;
	}


	if(!type->isStructTy())
	{
		if(type->isPointerTy() && type->getPointerElementType()->isStructTy())
		{
			type = type->getPointerElementType(), isPtr = true;
		}
		else
		{
			error(cgi, this, "Cannot do member access on non-struct types");
		}
	}


	// find out whether we need self or selfptr.
	if(selfPtr == nullptr && !isPtr)
	{
		// we don't have a pointer value for this
		// it's required for CreateStructGEP, so we'll have to make a temp variable
		// then store the result of the LHS into it.

		if(lhsPtr && lhsPtr->getType() == type->getPointerTo())
		{
			selfPtr = lhsPtr;
		}
		else
		{
			selfPtr = cgi->allocateInstanceInBlock(type);
			cgi->mainBuilder.CreateStore(self, selfPtr);
		}
	}


	// handle type aliases
	if(isWrapped)
	{
		bool wasSelfPtr = false;

		if(selfPtr)
		{
			selfPtr = cgi->lastMinuteUnwrapType(this, selfPtr);
			wasSelfPtr = true;
			isPtr = false;
		}
		else
		{
			self = cgi->lastMinuteUnwrapType(this, self);
		}


		// if we're faced with a double pointer, we need to load it once
		if(wasSelfPtr)
		{
			if(selfPtr->getType()->isPointerTy() && selfPtr->getType()->getPointerElementType()->isPointerTy())
				selfPtr = cgi->mainBuilder.CreateLoad(selfPtr);
		}
		else
		{
			if(self->getType()->isPointerTy() && self->getType()->getPointerElementType()->isPointerTy())
				self = cgi->mainBuilder.CreateLoad(self);
		}
	}






	llvm::StructType* st = llvm::cast<llvm::StructType>(type);

	TypePair_t* pair = cgi->getType(type);
	if(!pair && (!st || (st && !st->isLiteral())))
	{
		error("(%s:%d) -> Internal check failed: failed to retrieve type (%s)", __FILE__, __LINE__, cgi->getReadableType(type).c_str());
	}
	else if(st && st->isLiteral())
	{
		type = st;
	}


	if((st && st->isLiteral()) || (pair->second.second == TypeKind::Tuple))
	{
		// todo: maybe move this to another file?
		// like tuplecodegen.cpp

		// quite simple, just get the number (make sure it's a Ast::Number)
		// and do a structgep.

		Number* n = dynamic_cast<Number*>(this->member);
		iceAssert(n);

		if(n->ival >= type->getStructNumElements())
			error(cgi, this, "Tuple does not have %d elements, only %d", (int) n->ival + 1, type->getStructNumElements());

		llvm::Value* gep = cgi->mainBuilder.CreateStructGEP(selfPtr, n->ival);

		// if the lhs is immutable, don't give a pointer.
		bool immut = false;
		if(VarRef* vr = dynamic_cast<VarRef*>(this->target))
		{
			VarDecl* vd = cgi->getSymDecl(this, vr->name);
			iceAssert(vd);

			immut = vd->immutable;
		}

		return Result_t(cgi->mainBuilder.CreateLoad(gep), immut ? 0 : gep);
	}
	else if(pair->second.second == TypeKind::Struct)
	{
		Struct* str = dynamic_cast<Struct*>(pair->second.first);

		iceAssert(str);
		iceAssert(self);

		// transform
		Expr* rhs = this->member;


		// get the index for the member
		// Expr* rhs = this->member;
		int i = -1;

		VarRef* var = dynamic_cast<VarRef*>(rhs);
		FuncCall* fc = dynamic_cast<FuncCall*>(rhs);


		if(var)
		{
			if(str->nameMap.find(var->name) != str->nameMap.end())
			{
				i = str->nameMap[var->name];
			}
			else
			{
				bool found = false;
				for(auto c : str->cprops)
				{
					if(c->name == var->name)
					{
						found = true;
						break;
					}
				}

				if(!found)
				{
					error(cgi, this, "Type '%s' does not have a member '%s'", str->name.c_str(), var->name.c_str());
				}
			}
		}
		else if(!var && !fc)
		{
			if(dynamic_cast<Number*>(rhs))
			{
				error(cgi, this, "Type '%s' is not a tuple", str->name.c_str());
			}
			else
			{
				error(cgi, this, "(%s:%d) -> Internal check failed: no comprehendo (%s)", __FILE__, __LINE__, typeid(*rhs).name());
			}
		}

		if(fc)
		{
			return doFunctionCall(cgi, fc, self, selfPtr, isPtr, str, false);
		}
		else if(var)
		{
			return doVariable(cgi, var, _rhs, self, selfPtr, isPtr, str, i);
		}
		else
		{
			iceAssert(!"Not var or function?!");
		}
	}

	iceAssert(!"Encountered invalid expression");
}






















namespace Codegen
{
	Func* CodegenInstance::getFunctionFromStructFuncCall(StructBase* str, FuncCall* fc)
	{
		// now we need to determine if it exists, and its params.
		Func* callee = nullptr;
		for(Func* f : str->funcs)
		{
			std::string match = this->mangleMemberFunction(str, fc->name, fc->params, str->scope);
			std::string funcN = this->mangleMemberFunction(str, f->decl->name, f->decl->params, str->scope, f->decl->isStatic);

			#if 0
			printf("func %s vs %s, orig %s\n", match.c_str(), funcN.c_str(), f->decl->name.c_str());
			#endif

			if(funcN == match)
			{
				callee = f;
				break;
			}
		}

		if(!callee)
			error(this, fc, "Function '%s' is not a member of struct '%s'", fc->name.c_str(), str->name.c_str());

		return callee;
	}


	Struct* CodegenInstance::getNestedStructFromScopes(Expr* user, std::deque<std::string> scopes)
	{
		iceAssert(scopes.size() > 0);

		std::string last = scopes.back();
		scopes.pop_back();

		TypePair_t* tp = this->getType(this->mangleWithNamespace(last, scopes.size() > 0 ? scopes : this->namespaceStack, false));
		if(!tp)
			GenError::unknownSymbol(this, user, last, SymbolType::Type);

		Struct* str = dynamic_cast<Struct*>(tp->second.first);
		iceAssert(str);

		return str;
	}

	static Expr* _recursivelyResolveNested(MemberAccess* base, std::deque<std::string>& scopes)
	{
		VarRef* left = dynamic_cast<VarRef*>(base->target);
		iceAssert(left);

		scopes.push_back(left->name);

		MemberAccess* maR = dynamic_cast<MemberAccess*>(base->member);
		// FuncCall* fcR = dynamic_cast<FuncCall*>(base->member);

		if(maR)
		{
			return _recursivelyResolveNested(maR, scopes);
		}
		else
		{
			return base->member;
		}
	}

	Expr* CodegenInstance::recursivelyResolveNested(MemberAccess* base, std::deque<std::string>* __scopes)
	{
		VarRef* left = dynamic_cast<VarRef*>(base->target);
		iceAssert(left);

		std::deque<std::string> tmpscopes;
		std::deque<std::string>* _scopes = nullptr;

		// fuck this shit. we need to know if we were originally passed a non-null.
		if(!__scopes)
			_scopes = &tmpscopes;

		else
			_scopes = __scopes;


		std::deque<std::string>& scopes = *_scopes;
		MemberAccess* maR = dynamic_cast<MemberAccess*>(base->member);

		scopes.push_back(left->name);
		if(maR)
		{
			// kinda hacky behaviour.
			// if we call with _scopes != 0, that means
			// we're interested in the function call.

			// if not, then we're only interested in the type.


			Expr* ret = _recursivelyResolveNested(maR, scopes);

			// todo: static vars
			// iceAssert(fc);

			if(__scopes != nullptr)
			{
				return ret;
			}
			else
			{
				if(FuncCall* fc = dynamic_cast<FuncCall*>(ret))
				{
					Struct* str = this->getNestedStructFromScopes(base, scopes);
					return this->getFunctionFromStructFuncCall(str, fc);
				}
				else
				{
					return ret;
				}
			}
		}
		else
		{
			return base->member;
		}
	}
}







static Result_t doFunctionCall(CodegenInstance* cgi, FuncCall* fc, llvm::Value* self, llvm::Value* selfPtr, bool isPtr, Struct* str, bool isStaticFunctionCall)
{
	// make the args first.
	// since getting the llvm type of a MemberAccess can't be done without codegening the Ast itself,
	// we codegen first, then use the llvm version.
	std::vector<llvm::Value*> args;

	args.push_back(isPtr ? self : selfPtr);
	for(Expr* e : fc->params)
		args.push_back(e->codegen(cgi).result.first);


	// now we need to determine if it exists, and its params.
	Func* callee = cgi->getFunctionFromStructFuncCall(str, fc);
	iceAssert(callee);

	if(callee->decl->isStatic)
	{
		// remove the 'self' parameter
		args.erase(args.begin());
	}


	if(callee->decl->isStatic != isStaticFunctionCall)
	{
		error(cgi, fc, "Cannot call instance method '%s' without an instance", callee->decl->name.c_str());
	}




	llvm::Function* lcallee = 0;
	for(llvm::Function* lf : str->lfuncs)
	{
		if(lf->getName() == callee->decl->mangledName)
		{
			lcallee = lf;
			break;
		}
	}

	if(!lcallee)
		error(fc, "(%s:%d) -> Internal check failed: failed to find function %s", __FILE__, __LINE__, fc->name.c_str());

	lcallee = cgi->mainModule->getFunction(lcallee->getName());
	iceAssert(lcallee);

	return Result_t(cgi->mainBuilder.CreateCall(lcallee, args), 0);
}


static Result_t doComputedProperty(CodegenInstance* cgi, VarRef* var, ComputedProperty* cprop, llvm::Value* _rhs, llvm::Value* self, llvm::Value* selfPtr, bool isPtr, Struct* str)
{
	if(_rhs)
	{
		if(!cprop->setter)
		{
			error(var, "Property '%s' of type has no setter and is readonly", cprop->name.c_str());
		}

		llvm::Function* lcallee = 0;
		for(llvm::Function* lf : str->lfuncs)
		{
			if(lf->getName() == cprop->generatedFunc->mangledName)
			{
				lcallee = lf;
				break;
			}
		}

		if(!lcallee)
			error(var, "?!??!!");


		std::vector<llvm::Value*> args { isPtr ? self : selfPtr, _rhs };

		// todo: rather large hack. since the nature of computed properties
		// is that they don't have a backing storage in the struct itself, we need
		// to return something. We're still used in a binOp though, so...

		// create a fake alloca to return to them.
		lcallee = cgi->mainModule->getFunction(lcallee->getName());
		return Result_t(cgi->mainBuilder.CreateCall(lcallee, args), cgi->allocateInstanceInBlock(_rhs->getType()));
	}
	else
	{
		llvm::Function* lcallee = 0;
		for(llvm::Function* lf : str->lfuncs)
		{
			if(lf->getName() == cprop->generatedFunc->mangledName)
			{
				lcallee = lf;
				break;
			}
		}

		if(!lcallee)
			error(var, "?!??!!");

		lcallee = cgi->mainModule->getFunction(lcallee->getName());
		std::vector<llvm::Value*> args { isPtr ? self : selfPtr };
		return Result_t(cgi->mainBuilder.CreateCall(lcallee, args), 0);
	}
}

static Result_t doVariable(CodegenInstance* cgi, VarRef* var, llvm::Value* _rhs, llvm::Value* self, llvm::Value* selfPtr, bool isPtr, Struct* str, int i)
{
	ComputedProperty* cprop = nullptr;
	for(ComputedProperty* c : str->cprops)
	{
		if(c->name == var->name)
		{
			cprop = c;
			break;
		}
	}

	if(cprop)
	{
		return doComputedProperty(cgi, var, cprop, _rhs, self, selfPtr, isPtr, str);
	}
	else
	{
		iceAssert(i >= 0);

		// if we are a Struct* instead of just a Struct, we can just use pair.first since it's already a pointer.
		iceAssert(self || selfPtr);

		// printf("*** self: %s\n*** selfptr: %s\n*** isPtr: %d\n", cgi->getReadableType(self).c_str(), cgi->getReadableType(selfPtr).c_str(), isPtr);
		llvm::Value* ptr = cgi->mainBuilder.CreateStructGEP(isPtr ? self : selfPtr, i, "memberPtr_" + var->name);
		llvm::Value* val = cgi->mainBuilder.CreateLoad(ptr);

		if(str->members[i]->immutable)
			ptr = 0;

		return Result_t(val, ptr);
	}
}



static Result_t doStaticAccess(CodegenInstance* cgi, MemberAccess* ma)
{
	std::deque<std::string> scopes;
	Expr* rightmost = cgi->recursivelyResolveNested(ma, &scopes);
	iceAssert(rightmost);

	Struct* str = cgi->getNestedStructFromScopes(ma, scopes);


	if(FuncCall* fc = dynamic_cast<FuncCall*>(rightmost))
	{
		return doFunctionCall(cgi, fc, 0, 0, false, str, true);
	}
	else if(VarRef* vr = dynamic_cast<VarRef*>(rightmost))
	{
		for(auto mem : str->members)
		{
			if(mem->isStatic && mem->name == vr->name)
			{
				std::string mangledName = cgi->mangleMemberFunction(str, mem->name, std::deque<Ast::Expr*>());
				if(llvm::GlobalVariable* gv = cgi->mainModule->getGlobalVariable(mangledName))
				{
					// todo: another kinda hacky thing.
					// this is present in some parts of the code, i don't know how many.
					// basically, if the thing is supposed to be immutable, we're not going to return
					// the ptr/ref value.

					return Result_t(cgi->mainBuilder.CreateLoad(gv), gv->isConstant() ? 0 : gv);
				}
			}
		}

		error(cgi, ma, "Struct '%s' has no such static member '%s'", str->name.c_str(), vr->name.c_str());
	}

	error(cgi, ma, "Invalid static access (%s)", typeid(*rightmost).name());
}




/*

	I hate this stuff: A list of ideas on how to fix this mess.

		  (1)				 (2)
	A.(B.(C.(D.E))) vs (((A.B).C).D).E

	1.	The parser at this point of time (#a869a94 on develop) gives us A.(B.(C.D))
		This is useful for parsing static access. We can start at A, then progressively
		unwrap (recursively) B, C and D to form the final static access chain.

	2.	However, for actual codegeneration, it's bogus. The 'root' of the entire expression would
		have A on the LHS and a bunch of stuff on the RHS, which is inconvenient and impractical
		from a codegen perspective. The benefit of using (2), is that the root has E on the right and
		stuff on the left. Recursively code-genning the LHS would result in an llvm::Value* at the end,
		which can be used to access E on.

	3.	For (1), there's no practical way to codegen it -- generating the RHS generally requires the LHS
		to have been generated (to get the llvm::Value* pointer and do a StructGEP).



	Solutions:

	1.	The parser should just return form (2).

	2.	Normal codegen is as-is. Static codegen needs to be changed.
		Current possibilities include:

		(a):	Recursively resolve, like normal codegen -- except exprs are pushed
				onto a stack in reverse order: (((A.B).C).D).E -> [ A, B, C, D, E ]

				Recursion stops when the LHS is not a Ast::MemberAccess.
				Resolution then occurs iteratively, starting from A and ending at E.

		(b):	Work-in-progress.


	3.	Type resolution is somewhat problematic, and there's probably a bunch of duplicate code
		lying around.

		The good thing is, type resolution often only depends on the rightmost expression.
		We can't limit everything around this though, or shit *will* break later on. Resolution would
		happen similarly to (2a) above -- that function should probably take a Ast::MemberAccess* and return
		A std::vector<Ast::MemberAccess*> in order.

		A good thing is that typechecking does not need to involve itself with the static-ness of
		members. All that needs to happen now, is to iteratively recurse / recursively iteraote (?!?!?)
		through the list. Using [ A, B, C, D, E ] as an example:



		(0):	ƒ(x, y) -> returns llvm::Type*
				x -> lhs
				y -> rhs

				let U = x
				let V = y

					{ tree }					{ arr }
				(((A.B).C).D).E		->		[ A, B, C, D, E ]

				for statements below.

		BEGIN
			(a):	Resolve the type of U. If U is a static reference (ie. the VarRef is a typename), goto (e).
					If U is a MemberAccess, goto (d). Else, continue to (b).

			(b):	Retrieve the Ast::Struct* from U. For a function call, use the return type. Then,
					Ensure that there exists a member V in U.

			(c):	Return the type of V.

			(d):	let type = ƒ(U.left, U.right)
					Goto (b), using 'type' to obtain an Ast::Struct*.

			(e):	Obtain the Ast::Struct* from the typename, and goto (b).
		END



		Sample call stack:



		(0):	{ tree } -> (((A().B).C()).D).E -> ma
				ƒ(ma.left, ma.right)		# ma.left = ((A().B).C()).D, ma.right = E

				U <- ((A().B).C()).D
				V <- E

				(a):	U is a member access.
				(d):	let ut = ƒ(U.left, U.right)			# U.left = (A().B).C(), U.right = D

						(1):	U1 <- (A().B).C()
								V1 <- D

							(a):	U1 is a member access.
							(d):	let ut1 = ƒ(U1.left, U1.right)		# U1.left = A().B, U1.right = C()

									(2):	U2 <- A().B
											V2 <- C()

										(a):	U2 is a member access
										(d):	let ut2 = ƒ(U2.left, U2.right)		# U2.left = A(), U2.right = B

												(3):	U3 <- A()
														V3 <- B

													(a):	U3 is a function.
													(b):	Ensure that the return type of U3 (# U3 = A()) is a struct,
															and has a member V3.	# V3 = B

													(c):	Return the type of V3.	# V3 = B

										(b):	Ensure 'ut2' has a function V2. # V2 = C()
										(c):	Return the return type of V2. # V2 = C()


							(b):	Ensure 'ut1' has a member V1. # V1 = D
							(c):	Return the type of V1. # V1 = D

				(b):	Ensure 'ut' has a member V. # V = E
				(c):	Return the type of V. # V = E

				(FIN):	Type resolution complete.





*/







/*
	(0):	ƒ(x, y) -> returns llvm::Type*
			x -> lhs
			y -> rhs

			let U = x
			let V = y

				{ tree }					{ arr }
			(((A.B).C).D).E		->		[ A, B, C, D, E ]

			for statements below.

	BEGIN
		(a):	Resolve the type of U. If U is a static reference (ie. the VarRef is a typename), goto (e).
				If U is a MemberAccess, goto (d). Else, continue to (b).

		(b):	Retrieve the Ast::Struct* from U. For a function call, use the return type. Then,
				Ensure that there exists a member V in U.

		(c):	Return the type of V.

		(d):	let type = ƒ(U.left, U.right)
				Goto (b), using 'type' to obtain an Ast::Struct*.

		(e):	Obtain the Ast::Struct* from the typename, and goto (b).
	END
*/


std::pair<llvm::Type*, llvm::Value*>
CodegenInstance::resolveDotOperator(Expr* lhs, Expr* rhs, bool doAccess, std::deque<std::string>* _scp)
{
	TypePair_t* tp = 0;
	StructBase* sb = 0;

	std::deque<std::string>* scp = 0;
	if(_scp == 0)
		scp = new std::deque<std::string>();		// todo: this will leak.

	else
		scp = _scp;


	iceAssert(scp);
	if(MemberAccess* ma = dynamic_cast<MemberAccess*>(lhs))
	{
		// (d)
		auto ret = this->resolveDotOperator(ma->target, ma->member, false, scp);
		tp = this->getType(ret.first);

		iceAssert(tp);
	}
	else if(VarRef* vr = dynamic_cast<VarRef*>(lhs))
	{
		// (e)

		std::string mname;
		if(scp != 0)
			mname = this->mangleWithNamespace(vr->name, *scp, false);

		else
			mname = this->mangleWithNamespace(vr->name, false);


		tp = this->getType(mname);

		if(!tp)
		{
			// (b)
			llvm::Type* lt = this->getLlvmType(vr);
			iceAssert(lt);

			tp = this->getType(lt);
			iceAssert(tp);
		}
	}
	else if(FuncCall* fc = dynamic_cast<FuncCall*>(lhs))
	{
		llvm::Type* lt = this->getLlvmType(fc);
		iceAssert(lt);

		tp = this->getType(lt);
		iceAssert(tp);
	}

	sb = dynamic_cast<StructBase*>(tp->second.first);
	iceAssert(sb);


	// (b)
	scp->push_back(sb->name);
	int i = -1;

	VarRef* var = dynamic_cast<VarRef*>(rhs);
	FuncCall* fc = dynamic_cast<FuncCall*>(rhs);

	if(var)
	{
		if(sb->nameMap.find(var->name) != sb->nameMap.end())
		{
			i = sb->nameMap[var->name];
		}
		else
		{
			bool found = false;
			for(auto c : sb->cprops)
			{
				if(c->name == var->name)
				{
					found = true;
					break;
				}
			}

			if(!found)
			{
				error(this, rhs, "Type '%s' does not have a member '%s'", sb->name.c_str(), var->name.c_str());
			}
		}
	}
	else if(fc)
	{
		iceAssert(getFunctionFromStructFuncCall(sb, fc));
	}
	else
	{
		if(dynamic_cast<Number*>(rhs))
		{
			error(this, rhs, "Type '%s' is not a tuple", sb->name.c_str());
		}
		else
		{
			error(this, rhs, "(%s:%d) -> Internal check failed: no comprehendo (%s)", __FILE__, __LINE__, typeid(*rhs).name());
		}
	}


	llvm::Type* type = this->getLlvmType(rhs);
	return std::make_pair(type, (llvm::Value*) 0);
}


















