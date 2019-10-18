// llvm.h
// Copyright (c) 2017, zhiayang
// Licensed under the Apache License Version 2.0.

#pragma once

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#ifdef _MSC_VER
	#pragma warning(push, 0)
#else
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif


#include "llvm/IR/Mangler.h"
#include "llvm/IR/DataLayout.h"

#include "llvm/ADT/SmallVector.h"

#include "llvm/Target/TargetMachine.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/DynamicLibrary.h"

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"

#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/IRTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"

#ifdef _MSC_VER
	#pragma warning(pop)
#else
	#pragma GCC diagnostic pop
#endif

#include "backend.h"


namespace llvm
{
	class Module;
	class TargetMachine;
	class ExecutionEngine;
	class LLVMContext;
	class Function;
}


namespace backend
{
	using EntryPoint_t = int (*)(int, const char**);

	struct LLVMJit
	{
		using ModuleHandle_t = llvm::orc::VModuleKey;
		using OptimiseFunction = std::function<std::unique_ptr<llvm::Module>(std::unique_ptr<llvm::Module>)>;

		LLVMJit();

		void removeModule(ModuleHandle_t mod);
		ModuleHandle_t addModule(std::unique_ptr<llvm::Module> mod);

		llvm::JITSymbol findSymbol(const std::string& name);
		llvm::JITTargetAddress getSymbolAddress(const std::string& name);

		private:

		std::unique_ptr<llvm::Module> optimiseModule(std::unique_ptr<llvm::Module> M);

		llvm::orc::ExecutionSession ES;
		std::map<llvm::orc::VModuleKey, std::shared_ptr<llvm::orc::SymbolResolver>> Resolvers;
		std::unique_ptr<llvm::TargetMachine> TM;
		const llvm::DataLayout DL;
		llvm::orc::LegacyRTDyldObjectLinkingLayer ObjectLayer;
		llvm::orc::LegacyIRCompileLayer<decltype(ObjectLayer), llvm::orc::SimpleCompiler> CompileLayer;

		llvm::orc::LegacyIRTransformLayer<decltype(CompileLayer), OptimiseFunction> OptimiseLayer;

		std::unique_ptr<llvm::orc::JITCompileCallbackManager> CompileCallbackManager;
		llvm::orc::LegacyCompileOnDemandLayer<decltype(OptimiseLayer)> CODLayer;


		// llvm::orc::ExecutionSession execSession;
		// std::unique_ptr<llvm::TargetMachine> targetMachine;
		// std::shared_ptr<llvm::orc::SymbolResolver> symbolResolver;

		// llvm::DataLayout dataLayout;
		// llvm::orc::RTDyldObjectLinkingLayer objectLayer;
		// llvm::orc::IRCompileLayer<llvm::orc::RTDyldObjectLinkingLayer, llvm::orc::SimpleCompiler> compileLayer;
	};

	struct LLVMBackend : Backend
	{
		LLVMBackend(CompiledData& dat, std::vector<std::string> inputs, std::string output);
		virtual ~LLVMBackend() { }

		virtual void performCompilation() override;
		virtual void optimiseProgram() override;
		virtual void writeOutput() override;

		virtual std::string str() override;

		static llvm::LLVMContext& getLLVMContext();

		static llvm::Module* translateFIRtoLLVM(fir::Module* mod);

		private:
		void setupTargetMachine();
		EntryPoint_t getEntryFunctionFromJIT();

		llvm::Function* entryFunction = 0;
		llvm::TargetMachine* targetMachine = 0;
		std::unique_ptr<llvm::Module> linkedModule;

		LLVMJit* jitInstance = 0;
	};
}












