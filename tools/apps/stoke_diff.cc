// Copyright 2013-2016 Stanford University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>
#include <set>

#include "src/ext/cpputil/include/command_line/command_line.h"
#include "src/ext/cpputil/include/io/console.h"
#include "src/ext/cpputil/include/signal/debug_handler.h"

#include "src/disassembler/disassembler.h"
#include "src/disassembler/function_callback.h"

using namespace cpputil;
using namespace stoke;
using namespace std;

auto& io = Heading::create("I/O options:");
auto& in = ValueArg<string>::create("i")
           .alternate("in")
           .usage("<path/to/bin>")
           .description("Binary file to extract code from")
           .default_val("./a.out");
//auto& out = ValueArg<string>::create("o")
            //.alternate("out")
            //.usage("<path/to/dir>")
            //.description("Directory to write results to")
            //.default_val("out");

//auto& io_bug = Heading::create("I/O options:");
auto& in_bug = ValueArg<string>::create("i_bug")
              .alternate("in_bug")
              .usage("<path/to/bin_bug>")
              .description("Binary buggy file to extract code from")
              .default_val("./bug");
//auto& out_bug = ValueArg<string>::create("o_bug")
                //.alternate("out_bug")
                //.usage("<path/to/dir_bug>")
                //.description("Buggy directory to write results to")
                //.default_val("out_bug");

auto& buggy_func = ValueArg<string>::create("bug_func");
auto& flat_binary = FlagArg::create("flat_binary");

bool make_dir(ValueArg<string>& va) {
  /* The permission is guarded by user's umask, which is why
     we set the mode to 0777.  We ignore the result, because mkdir will fail if
     the directory already exists.  We check for success later. */
  mkdir(va.value().c_str(), 0777);

  /* All said and done, check if the directory exists. */
  struct stat st;
  int result = stat(va.value().c_str(), &st);

  if (result) {
    // We couldn't even stat the file/directory.
    return false;
  }

  return S_ISDIR(st.st_mode);
}

void callback(const FunctionCallbackData& data, void* arg) {
  if (!data.parse_error) {
    //ofstream ofs(out.value() + "/" + data.tunit.get_name() + ".s");
    //ofs << data.tunit << endl;
  } else {
    Console::warn() << "Could not disassemble " << data.name << " (parse error)." << endl;
  }
}

//void callback_pb(const FunctionCallbackData& data, void* arg) {
  //if (!data.parse_error) {
    ////ofstream ofs(out_bug.value() + "/" + data.tunit.get_name() + ".s");
    ////ofs << data.tunit << endl;
  //} else {
    //Console::warn() << "Could not disassemble " << data.name << " (parse error)." << endl;
  //}
//}

int main(int argc, char** argv) {
  CommandLineConfig::strict_with_convenience(argc, argv);
  DebugHandler::install_sigsegv();
  DebugHandler::install_sigill();

  //if (!make_dir(out)) {
    //Console::error(1) << "Unable to create output directory " << out.value() << "!" << endl;
  //}

  Disassembler d;
  d.set_function_callback(callback, nullptr);
  //d.set_function_callback(callback_pb, nullptr);
  d.set_flat_binary(flat_binary);
  //d.disassemble(in.value());
  d.diff(in.value(), in_bug.value(), buggy_func.value());

  if (d.has_error()) {
    Console::error(1) << "Error: " << d.get_error() << endl;
  }

  return 0;
}

