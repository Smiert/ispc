/*
  Copyright (c) 2010-2011, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.


   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  
*/

/** @file ispc.cpp
    @brief ispc global definitions
*/

#include "ispc.h"
#include "module.h"
#include "util.h"
#include <stdio.h>
#ifdef ISPC_IS_WINDOWS
#include <windows.h>
#include <direct.h>
#define strcasecmp stricmp
#endif
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#ifndef LLVM_2_8
#include <llvm/Analysis/DIBuilder.h>
#endif
#include <llvm/Analysis/DebugInfo.h>
#include <llvm/Support/Dwarf.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetData.h>
#if defined(LLVM_3_0) || defined(LLVM_3_0svn)
  #include <llvm/Support/TargetRegistry.h>
  #include <llvm/Support/TargetSelect.h>
#else
  #include <llvm/Target/TargetRegistry.h>
  #include <llvm/Target/TargetSelect.h>
  #include <llvm/Target/SubtargetFeature.h>
#endif
#include <llvm/Support/Host.h>

Globals *g;
Module *m;

///////////////////////////////////////////////////////////////////////////
// Target

bool
Target::GetTarget(const char *arch, const char *cpu, const char *isa,
                  bool pic, Target *t) {
    if (cpu == NULL) {
        std::string hostCPU = llvm::sys::getHostCPUName();
        if (hostCPU.size() > 0)
            cpu = hostCPU.c_str();
        else {
            fprintf(stderr, "Warning: unable to determine host CPU!\n");
            cpu = "generic";
        }
    }
    t->cpu = cpu;

    if (isa == NULL) {
        if (!strcasecmp(cpu, "atom"))
            isa = "sse2";
#if defined(LLVM_3_0) || defined(LLVM_3_0_svn)
        else if (!strcasecmp(cpu, "sandybridge") ||
                 !strcasecmp(cpu, "corei7-avx"))
            isa = "avx";
#endif // LLVM_3_0
        else
            isa = "sse4";
    }
    if (arch == NULL)
        arch = "x86-64";

    bool error = false;

    t->generatePIC = pic;

    // Make sure the target architecture is a known one; print an error
    // with the valid ones otherwise.
    t->target = NULL;
    for (llvm::TargetRegistry::iterator iter = llvm::TargetRegistry::begin();
         iter != llvm::TargetRegistry::end(); ++iter) {
        if (std::string(arch) == iter->getName()) {
            t->target = &*iter;
            break;
        }
    }
    if (t->target == NULL) {
        fprintf(stderr, "Invalid architecture \"%s\"\nOptions: ", arch);
        llvm::TargetRegistry::iterator iter;
        for (iter = llvm::TargetRegistry::begin();
             iter != llvm::TargetRegistry::end(); ++iter)
            fprintf(stderr, "%s ", iter->getName());
        fprintf(stderr, "\n");
        error = true;
    }
    else {
        t->arch = arch;
    }

    if (!strcasecmp(isa, "sse2")) {
        t->isa = Target::SSE2;
        t->nativeVectorWidth = 4;
        t->vectorWidth = 4;
        t->attributes = "+sse,+sse2,-sse3,-sse41,-sse42,-sse4a,-ssse3,-popcnt";
    }
    else if (!strcasecmp(isa, "sse4")) {
        t->isa = Target::SSE4;
        t->nativeVectorWidth = 4;
        t->vectorWidth = 4;
        t->attributes = "+sse,+sse2,+sse3,+sse41,-sse42,-sse4a,+ssse3,-popcnt,+cmov";
    }
    else if (!strcasecmp(isa, "sse4x2")) {
        t->isa = Target::SSE4;
        t->nativeVectorWidth = 4;
        t->vectorWidth = 8;
        t->attributes = "+sse,+sse2,+sse3,+sse41,-sse42,-sse4a,+ssse3,-popcnt,+cmov";
    }
#if defined(LLVM_3_0) || defined(LLVM_3_0svn)
    else if (!strcasecmp(isa, "avx")) {
        t->isa = Target::AVX;
        t->nativeVectorWidth = 8;
        t->vectorWidth = 8;
        t->attributes = "+avx,+popcnt,+cmov";
    }
    else if (!strcasecmp(isa, "avx-x2")) {
        t->isa = Target::AVX;
        t->nativeVectorWidth = 8;
        t->vectorWidth = 16;
        t->attributes = "+avx,+popcnt,+cmov";
    }
#endif // LLVM 3.0
    else {
        fprintf(stderr, "Target ISA \"%s\" is unknown.  Choices are: %s\n", 
                isa, SupportedTargetISAs());
        error = true;
    }

    if (!error) {
        llvm::TargetMachine *targetMachine = t->GetTargetMachine();
        const llvm::TargetData *targetData = targetMachine->getTargetData();
        t->is32bit = (targetData->getPointerSize() == 4);
    }

    return !error;
}


const char *
Target::SupportedTargetCPUs() {
    return "atom, barcelona, core2, corei7, "
#if defined(LLVM_3_0) || defined(LLVM_3_0_svn)
        "corei7-avx, "
#endif
        "istanbul, nocona, penryn, "
#ifdef LLVM_2_9
        "sandybridge, "
#endif
        "westmere";
}


const char *
Target::SupportedTargetArchs() {
    return "x86, x86-64";
}


const char *
Target::SupportedTargetISAs() {
    return "sse2, sse4, sse4x2"
#if defined(LLVM_3_0) || defined(LLVM_3_0_svn)
        ", avx, avx-x2"
#endif
        ;
}


std::string
Target::GetTripleString() const {
    llvm::Triple triple;
    // Start with the host triple as the default
    triple.setTriple(llvm::sys::getHostTriple());

    // And override the arch in the host triple based on what the user
    // specified.  Here we need to deal with the fact that LLVM uses one
    // naming convention for targets TargetRegistry, but wants some
    // slightly different ones for the triple.  TODO: is there a way to
    // have it do this remapping, which would presumably be a bit less
    // error prone?
    if (arch == "x86")
        triple.setArchName("i386");
    else if (arch == "x86-64")
        triple.setArchName("x86_64");
    else
        triple.setArchName(arch);

    return triple.str();
}


llvm::TargetMachine *
Target::GetTargetMachine() const {
    std::string triple = GetTripleString();

    llvm::Reloc::Model relocModel = generatePIC ? llvm::Reloc::PIC_ : 
                                                  llvm::Reloc::Default;
#if defined(LLVM_3_0svn) || defined(LLVM_3_0)
    std::string featuresString = attributes;
    llvm::TargetMachine *targetMachine = 
        target->createTargetMachine(triple, cpu, featuresString, relocModel);
#else
    std::string featuresString = cpu + std::string(",") + attributes;
    llvm::TargetMachine *targetMachine = 
        target->createTargetMachine(triple, featuresString);
    targetMachine->setRelocationModel(relocModel);
#endif
    assert(targetMachine != NULL);

    targetMachine->setAsmVerbosityDefault(true);
    return targetMachine;
}


///////////////////////////////////////////////////////////////////////////
// Opt

Opt::Opt() {
    level = 1;
    fastMath = false;
    fastMaskedVload = false;
    disableBlendedMaskedStores = false;
    disableCoherentControlFlow = false;
    disableUniformControlFlow = false;
    disableGatherScatterOptimizations = false;
    disableMaskedStoreToStore = false;
    disableGatherScatterFlattening = false;
    disableUniformMemoryOptimizations = false;
    disableMaskedStoreOptimizations = false;
}

///////////////////////////////////////////////////////////////////////////
// Globals

Globals::Globals() {
    mathLib = Globals::Math_ISPC;

    includeStdlib = true;
    runCPP = true;
    debugPrint = false;
    disableWarnings = false;
    emitPerfWarnings = true;
    emitInstrumentation = false;
    generateDebuggingSymbols = false;

    ctx = new llvm::LLVMContext;

#ifdef ISPC_IS_WINDOWS
    _getcwd(currentDirectory, sizeof(currentDirectory));
#else
    getcwd(currentDirectory, sizeof(currentDirectory));
#endif
}

///////////////////////////////////////////////////////////////////////////
// ASTNode

ASTNode::~ASTNode() {
}

///////////////////////////////////////////////////////////////////////////
// SourcePos

SourcePos::SourcePos(const char *n, int l, int c) {
    name = n ? n : m->module->getModuleIdentifier().c_str();
    first_line = last_line = l;
    first_column = last_column = c;
}

llvm::DIFile SourcePos::GetDIFile() const {
#ifdef LLVM_2_8
    return llvm::DIFile();
#else
    std::string directory, filename;
    GetDirectoryAndFileName(g->currentDirectory, name, &directory, &filename);
    return m->diBuilder->createFile(filename, directory);
#endif // LLVM_2_8
}


void
SourcePos::Print() const { 
    printf(" @ [%s:%d.%d - %d.%d] ", name, first_line, first_column,
           last_line, last_column); 
}


bool
SourcePos::operator==(const SourcePos &p2) const {
    return (!strcmp(name, p2.name) && 
            first_line == p2.first_line &&
            first_column == p2.first_column &&
            last_line == p2.last_line &&
            last_column == p2.last_column);
}

