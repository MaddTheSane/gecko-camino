// Copyright (c) 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Original author: Jim Blandy <jimb@mozilla.com> <jimb@red-bean.com>

// dump_stabs.cc --- implement the DumpStabsHandler class.

#include <cstdarg>
#include <cxxabi.h>

#include <algorithm>
#include <cassert>

#include "common/linux/dump_stabs.h"

namespace google_breakpad {

using std::string;

// Demangle using abi call.
// Older GCC may not support it.
static string Demangle(const string &mangled) {
  int status = 0;
  char *demangled = abi::__cxa_demangle(mangled.c_str(), NULL, NULL, &status);
  if (status == 0 && demangled != NULL) {
    string str(demangled);
    free(demangled);
    return str;
  }
  return string(mangled);
}

bool DumpStabsHandler::StartCompilationUnit(const char *name, uint64_t address,
                                            const char *build_directory) {
  assert(!in_compilation_unit_);
  in_compilation_unit_ = true;
  current_source_file_name_ = name;
  current_source_file_ = module_->FindFile(name);
  comp_unit_base_address_ = address;
  boundaries_.push_back(static_cast<Module::Address>(address));
  return true;
}

bool DumpStabsHandler::EndCompilationUnit(uint64_t address) {
  assert(in_compilation_unit_);
  in_compilation_unit_ = false;
  comp_unit_base_address_ = 0;
  current_source_file_ = NULL;
  current_source_file_name_ = NULL;
  if (address)
    boundaries_.push_back(static_cast<Module::Address>(address));
  return true;
}

bool DumpStabsHandler::StartFunction(const string &name,
                                     uint64_t address) {
  assert(!current_function_);
  Module::Function *f = new Module::Function;
  f->name = Demangle(name);
  f->address = address;
  f->size = 0;           // We compute this in DumpStabsHandler::Finalize().
  f->parameter_size = 0; // We don't provide this information.
  current_function_ = f;
  boundaries_.push_back(static_cast<Module::Address>(address));
  return true;
}

bool DumpStabsHandler::EndFunction(uint64_t address) {
  assert(current_function_);
  // Functions in this compilation unit should have address bigger
  // than the compilation unit's starting address.  There may be a lot
  // of duplicated entries for functions in the STABS data; only one
  // entry can meet this requirement.
  //
  // (I don't really understand the above comment; just bringing it
  // along from the previous code, and leaving the behaivor unchanged.
  // If you know the whole story, please patch this comment.  --jimb)
  if (current_function_->address >= comp_unit_base_address_)
    functions_.push_back(current_function_);
  else
    delete current_function_;
  current_function_ = NULL;
  if (address)
    boundaries_.push_back(static_cast<Module::Address>(address));
  return true;
}

bool DumpStabsHandler::Line(uint64_t address, const char *name, int number) {
  assert(current_function_);
  assert(current_source_file_);
  if (name != current_source_file_name_) {
    current_source_file_ = module_->FindFile(name);
    current_source_file_name_ = name;
  }
  Module::Line line;
  line.address = address;
  line.size = 0;  // We compute this in DumpStabsHandler::Finalize().
  line.file = current_source_file_;
  line.number = number;
  current_function_->lines.push_back(line);
  return true;
}

void DumpStabsHandler::Warning(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void DumpStabsHandler::Finalize() {
  // Sort our boundary list, so we can search it quickly.
  sort(boundaries_.begin(), boundaries_.end());
  // Sort all functions by address, just for neatness.
  sort(functions_.begin(), functions_.end(),
       Module::Function::CompareByAddress);
  for (vector<Module::Function *>::iterator func_it = functions_.begin();
       func_it != functions_.end();
       func_it++) {
    Module::Function *f = *func_it;
    // Compute the function f's size.
    vector<Module::Address>::iterator boundary
        = std::upper_bound(boundaries_.begin(), boundaries_.end(), f->address);
    if (boundary != boundaries_.end())
      f->size = *boundary - f->address;
    else
      // If this is the last function in the module, and the STABS
      // reader was unable to give us its ending address, then assign
      // it a bogus, very large value.  This will happen at most once
      // per module: since we've added all functions' addresses to the
      // boundary table, only one can be the last.
      f->size = kFallbackSize;

    // Compute sizes for each of the function f's lines --- if it has any.
    if (!f->lines.empty()) {
      stable_sort(f->lines.begin(), f->lines.end(),
                  Module::Line::CompareByAddress);
      vector<Module::Line>::iterator last_line = f->lines.end() - 1;
      for (vector<Module::Line>::iterator line_it = f->lines.begin();
           line_it != last_line; line_it++)
        line_it[0].size = line_it[1].address - line_it[0].address;
      // Compute the size of the last line from f's end address.
      last_line->size = (f->address + f->size) - last_line->address;
    }
  }
  // Now that everything has a size, add our functions to the module, and
  // dispose of our private list.
  module_->AddFunctions(functions_.begin(), functions_.end());
  functions_.clear();
}

} // namespace google_breakpad
