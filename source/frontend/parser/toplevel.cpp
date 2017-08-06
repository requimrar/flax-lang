// toplevel.cpp
// Copyright (c) 2014 - 2017, zhiayang@gmail.com
// Licensed under the Apache License Version 2.0.

#include "pts.h"
#include "errors.h"
#include "parser.h"
#include "frontend.h"

#include "parser_internal.h"

using namespace lexer;
using namespace ast;

namespace parser
{
	TopLevelBlock* parseTopLevel(State& st, std::string name)
	{
		using TT = TokenType;
		TopLevelBlock* root = new TopLevelBlock(st.loc(), name);

		// if it's not empty, then it's an actual user-defined namespace
		if(name != "")
		{
			// expect "namespace FOO { ... }"
			iceAssert(st.eat() == TT::Identifier);
			if(st.eat() != TT::LBrace)
				error(st.ploc(), "Expected '{' to start namespace declaration, found '%s'", st.lookahead(-1).str().c_str());
		}

		while(st.hasTokens() && st.front() != TT::EndOfFile)
		{
			switch(st.front())
			{
				case TT::Import: {

					root->statements.push_back(parseImport(st));

				} break;

				case TT::Namespace: {
					st.eat();
					Token tok;
					if((tok = st.front()) != TT::Identifier)
						error(st, "Expected identifier after 'namespace'");

					root->statements.push_back(parseTopLevel(st, tok.str()));

				} break;

				case TT::Func: {

				} break;

				case TT::Var:
				case TT::Val: {
					root->statements.push_back(parseVariable(st));
				} break;

				case TT::Comment:
				case TT::NewLine:
					break;

				case TT::RBrace:
					goto out;

				default: {
					error(st, "Unexpected token '%s' / %d", st.front().str().c_str(), st.front().type);
				}
			}

			st.skipWS();
		}

		out:
		if(name != "")
		{
			if(st.front() != TT::RBrace)
				error(st, "Expected '}' to close namespace declaration, found '%d' instead", st.front().type);

			st.eat();
		}

		debuglog("parsed namespace '%s'\n", name.c_str());
		return root;
	}

	ParsedFile parseFile(std::string filename)
	{
		const TokenList& tokens = frontend::getFileTokens(filename);
		auto state = State(tokens);

		auto toplevel = parseTopLevel(state, "");

		auto parsedFile = ParsedFile();
		parsedFile.name = filename;
		parsedFile.root = toplevel;

		return parsedFile;
	}







}
















