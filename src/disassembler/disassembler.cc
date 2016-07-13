// Copyright 2013-2016 Stanford University
//
// Licensed under the Apache License, Version 2.0 (the License);
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <sstream>
#include <fstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <set>
#include <vector>

#include "src/ext/cpputil/include/io/fail.h"
#include "src/ext/x64asm/include/x64asm.h"
#include "src/disassembler/disassembler.h"

using namespace cpputil;
using namespace redi;
using namespace std;
using namespace x64asm;

namespace {

// trim from start
static inline string ltrim(string str) {
  str.erase(str.begin(), find_if(str.begin(), str.end(), not1(ptr_fun<int, int>(isspace))));
  return str;
}

// trim from end
static inline string rtrim(string str) {
  str.erase(find_if(str.rbegin(), str.rend(), not1(ptr_fun<int, int>(isspace))).base(), str.end());
  return str;
}

// trim from both ends
static inline string trim(string str) {
  return ltrim(rtrim(str));
}

/** Returns true if the first n characters of a string match a prefix */
bool is_prefix(const std::string& s, const char* prefix, size_t len) {
  return s.length() >= len && s.substr(0,len) == prefix;
}

/** Strips n lines from an ipstream */
void strip_lines(ipstream& ips, size_t lines) {
  string line;
  for (size_t i = 0; i < lines; ++i) {
    getline(ips, line);
  }
}

/** Is this character sequence a hex string? */
bool is_hex_string(const string& s) {
  for (auto c : s) {
    if (!isxdigit(c)) {
      return false;
    }
  }
  return true;
}

// Convert a hex string to an int */
uint64_t hex_to_int(const string& s) {
  uint64_t val;
  istringstream iss(s);
  iss >> hex >> val;
  return val;
}

/** Mangle @s and .s into _s (this is a hack around dealing with @plt functions) */
string mangle_lable(string label) {
  for (auto& c : label) {
    c = (c == '@' || c == '.') ? '_' : c;
  }
  return label;
}

} // namespace

namespace stoke {

bool Disassembler::check_filename(const string& s) {
  /* Prevent shell injection */
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];

    if (c >= 'a' && c <= 'z') {
      continue;
    } else if (c >= 'A' && c <= 'Z') {
      continue;
    } else if (c >= '0' && c <= '9') {
      continue;
    } else if (c == '.' ||
               c == '/' ||
               c == '_' ||
               c == '-' ||
               c == '~' ||
               c == '@' ||
               c == '+') {
      continue;
    }

    char buf[2];
    buf[0] = c;
    buf[1] = '\0';

    set_error(string("Character '") + string(buf) + "' not allowed in filename for security.");
    return false;
  }

  /* Check that we can open the file */
  fstream filestr;
  filestr.open(s);
  if (filestr.is_open()) {
    filestr.close();
    return true;
  }

  set_error("Error opening file.");
  return false;
}

ipstream* Disassembler::run_objdump(const string& filename, bool only_header) {
  if (!check_filename(filename)) {
    return NULL;
  }

  string target = "";
  if (only_header) {
    target = "/usr/bin/objdump -h " + filename;
  } else if (flat_binary_) {
    target = "/usr/bin/objdump -D -Msuffix -b binary -m i386:x86-64 " + filename;
  } else {
    target = "/usr/bin/objdump -j .text -Msuffix -d " + filename;
  }

  auto stream = new ipstream(target, pstreams::pstdout);
  if (!stream) {
    set_error("Unknown error spawning objdump: no memory allocated.");
    return NULL;
  }
  if (!stream->is_open()) {
    set_error("Unknown error spawning objdump.");
    delete stream;
    return NULL;
  }

  return stream;
}

map<string, uint64_t> Disassembler::parse_section_offsets(ipstream& ips) {
  map<string, uint64_t> section_offsets;

  // Skip ahead to table
  strip_lines(ips, 5);

  // Read entries one at a time
  string line;
  while (getline(ips, line)) {
    istringstream iss(line);
    string section, temp;
    uint64_t lma, offset;

    iss >> temp >> section >> temp >> temp >> hex >> lma >> offset;
    section_offsets[section] = lma - offset;

    // Trailing second line
    getline(ips, line);
  }

  return section_offsets;
}

string Disassembler::fix_instruction(const string& line) {
  // Add implicit trailing ones
  constexpr array<const char*, 8> rots {{"shl", "shr", "sal", "sar", "rcl", "rcr", "rol", "ror"}};
  if (line.length() >= 3) {
    const auto in_list = find(rots.begin(), rots.end(), line.substr(0, 3)) != rots.end();
    const auto missing = line.find_first_of(',') == string::npos;
    if (in_list && missing) {
      const auto split = line.find_first_of(' ');
      return line.substr(0, split) + " $0x1," + line.substr(split + 1);
    }
  }

  // Remove documentation arg from string instructions
  if (is_prefix(line, "stos", 4)) {
    const auto comma = line.find_first_of(',');
    return line.substr(0, 6) + line.substr(comma + 1);
  }
  if (is_prefix(line, "rep stos", 8)) {
    const auto comma = line.find_first_of(',');
    return line.substr(0, 10) + line.substr(comma + 1);
  }
  if (is_prefix(line, "repnz scas", 10)) {
    const auto comma = line.find_first_of(',');
    return line.substr(0, comma);
  }

  // Synonyms
  if (is_prefix(line, "hlt", 3) || is_prefix(line, "repz retq", 9)) {
    return "retq";
  } else if (is_prefix(line, "nop", 3) || is_prefix(line, "data", 4)) {
    return "nop";
  } else if (is_prefix(line, "movabsq", 7)) {
    return "movq" + line.substr(7);
  }

  // Append q to the end of call and jump
  if (is_prefix(line, "call ", 5)) {
    return "callq " + line.substr(5);
  } else if (line.length() >= 4 && line.substr(0,4) == "jmp ") {
    return "jmpq " + line.substr(4);
  }

  // Make lock its own instruction
  if (is_prefix(line, "lock", 4)) {
    return "lock\n" + line.substr(4);
  }

  // The remaining cases are easier to implement with regexs
  // We do get a performance hit though, so let's at least try to be smart here
  auto ll = line;

  // The whole family of (v)cmp synonyms
  if (is_prefix(line, "cmp", 4) || is_prefix(line, "vcmp", 4)) {
    ll = regex_replace(ll, regex("(v?cmp)eq([^ ]+)"),       "$1$2 \\$0x00,$3");
    ll = regex_replace(ll, regex("(v?cmp)lt([^ ]+)"),       "$1$2 \\$0x01,$3");
    ll = regex_replace(ll, regex("(v?cmp)le([^ ]+)"),       "$1$2 \\$0x02,$3");
    ll = regex_replace(ll, regex("(v?cmp)unord([^ ]+)"),    "$1$2 \\$0x03,$3");
    ll = regex_replace(ll, regex("(v?cmp)neq([^ ]+)"),      "$1$2 \\$0x04,$3");
    ll = regex_replace(ll, regex("(v?cmp)nlt([^ ]+)"),      "$1$2 \\$0x05,$3");
    ll = regex_replace(ll, regex("(v?cmp)nle([^ ]+)"),      "$1$2 \\$0x06,$3");
    ll = regex_replace(ll, regex("(v?cmp)ord([^ ]+)"),      "$1$2 \\$0x07,$3");
    ll = regex_replace(ll, regex("(v?cmp)eq_uq([^ ]+)"),    "$1$2 \\$0x08,$3");
    ll = regex_replace(ll, regex("(v?cmp)nge([^ ]+)"),      "$1$2 \\$0x09,$3");
    ll = regex_replace(ll, regex("(v?cmp)ngt([^ ]+)"),      "$1$2 \\$0x0a,$3");
    ll = regex_replace(ll, regex("(v?cmp)false([^ ]+)"),    "$1$2 \\$0x0b,$3");
    ll = regex_replace(ll, regex("(v?cmp)neq_oq([^ ]+)"),   "$1$2 \\$0x0c,$3");
    ll = regex_replace(ll, regex("(v?cmp)ge([^ ]+)"),       "$1$2 \\$0x0d,$3");
    ll = regex_replace(ll, regex("(v?cmp)gt([^ ]+)"),       "$1$2 \\$0x0e,$3");
    ll = regex_replace(ll, regex("(v?cmp)true([^ ]+)"),     "$1$2 \\$0x0f,$3");
    ll = regex_replace(ll, regex("(v?cmp)eq_os([^ ]+)"),    "$1$2 \\$0x10,$3");
    ll = regex_replace(ll, regex("(v?cmp)lt_oq([^ ]+)"),    "$1$2 \\$0x11,$3");
    ll = regex_replace(ll, regex("(v?cmp)le_oq([^ ]+)"),    "$1$2 \\$0x12,$3");
    ll = regex_replace(ll, regex("(v?cmp)unord_s([^ ]+)"),  "$1$2 \\$0x13,$3");
    ll = regex_replace(ll, regex("(v?cmp)neq_us([^ ]+)"),   "$1$2 \\$0x14,$3");
    ll = regex_replace(ll, regex("(v?cmp)nlt_uq([^ ]+)"),   "$1$2 \\$0x15,$3");
    ll = regex_replace(ll, regex("(v?cmp)nle_uq([^ ]+)"),   "$1$2 \\$0x16,$3");
    ll = regex_replace(ll, regex("(v?cmp)ord_s([^ ]+)"),    "$1$2 \\$0x17,$3");
    ll = regex_replace(ll, regex("(v?cmp)ueq_us([^ ]+)"),   "$1$2 \\$0x18,$3");
    ll = regex_replace(ll, regex("(v?cmp)nge_uq([^ ]+)"),   "$1$2 \\$0x19,$3");
    ll = regex_replace(ll, regex("(v?cmp)ngt_uq([^ ]+)"),   "$1$2 \\$0x1a,$3");
    ll = regex_replace(ll, regex("(v?cmp)false_os([^ ]+)"), "$1$2 \\$0x1b,$3");
    ll = regex_replace(ll, regex("(v?cmp)neq_os([^ ]+)"),   "$1$2 \\$0x1c,$3");
    ll = regex_replace(ll, regex("(v?cmp)ge_oq([^ ]+)"),    "$1$2 \\$0x1d,$3");
    ll = regex_replace(ll, regex("(v?cmp)gt_oq([^ ]+)"),    "$1$2 \\$0x1e,$3");
    ll = regex_replace(ll, regex("(v?cmp)true_us([^ ]+)"),  "$1$2 \\$0x1f,$3");
  }

  // I *think* these suffixe function as annotations and can be removed
  if (is_prefix(line, "vcvt", 4)) {
    ll = regex_replace(ll, regex("vcvtpd2psx"), "vcvtpd2ps");
    ll = regex_replace(ll, regex("vcvtpd2psy"), "vcvtpd2ps");
  } else if (is_prefix(line, "mova", 4)) {
    ll = regex_replace(ll, regex("movapd\\.s"), "movapd");
    ll = regex_replace(ll, regex("movaps\\.s"), "movaps");
  } else if (is_prefix(line, "movu", 4)) {
    ll = regex_replace(ll, regex("movupd\\.s"), "movupd");
    ll = regex_replace(ll, regex("movups\\.s"), "movups");
  } else if (is_prefix(line, "vmova", 5)) {
    ll = regex_replace(ll, regex("vmovapd\\.s"), "vmovapd");
    ll = regex_replace(ll, regex("vmovaps\\.s"), "vmovaps");
  } else if (is_prefix(line, "vmovd", 5)) {
    ll = regex_replace(ll, regex("vmovdqa\\.s"), "vmovdqa");
    ll = regex_replace(ll, regex("vmovdqu\\.s"), "vmovdqu");
  } else if (is_prefix(line, "vmovu", 5)) {
    ll = regex_replace(ll, regex("vmovupd\\.s"), "vmovupd");
    ll = regex_replace(ll, regex("vmovups\\.s"), "vmovups");
  }

  return ll;
}

bool Disassembler::parse_line(const string& s, LineInfo& line) {
  // Some character landmarks
  const auto tab1 = s.find_first_of('\t');
  const auto tab2 = s.find_first_of('\t', tab1 + 1);
  const auto colon = s.find_first_of(':');

  // Record line offsets
  line.offset = hex_to_int(s.substr(2, colon-2));
  // Count hex bytes
  line.hex_bytes = 0;
  for (auto i = tab1+1, ie = tab2 == string::npos ? s.length() : tab2; i < ie; i+=3) {
    line.hex_bytes += isxdigit(s[i]) ? 1 : 0;
  }

  // If this is a hex only line, we're done
  if (tab2 == string::npos) {
    return false;
  }
  // Otherwise, instruction are terminated by eol, # or <
  const auto begin = tab2 + 1;
  auto comment = s.find_last_of('#');
  comment = comment == string::npos ? s.length() : comment;
  auto annot = s.find_last_of('<');
  annot = annot == string::npos ? s.length() : annot;
  const auto len = min(comment, annot) - begin;

  line.instr = s.substr(begin, len);

  // dungnm: Find the opcode of this line
  const auto space = line.instr.find_first_of(' ');
  string opcode_str = line.instr.substr(0, space);
  //line.opcode = line.instr.substr(0, space);
  for (size_t i=0; i < X64ASM_NUM_OPCODES; ++i) {
    const Opcode opc = (Opcode)i;
    if (opcode_write_att(opc) == opcode_str) {
      //cout << "OPCODE: " << i << endl;
      //cout << opc << endl;
      line.opc = opc;
      break;
    }
  }
  //Opcode op = (Opcode)1;
  //cout << "Opcode: " << opcode_write_att(op) << endl;
  //cout << "Opcode: " << opcode_write_intel(op) << endl;

  // Find memory address if the opcode is a jump instruction or a call instruction
  const auto percent = line.instr.find('%');
  if (percent == string::npos &&
        (opcode_str == "call" || opcode_str == "callq" ||
         opcode_str == "je" || opcode_str == "jne" ||
         opcode_str == "jb" || opcode_str == "jbe" ||
         opcode_str == "jmp" || opcode_str == "jmpq")) {
      //line.mem = Imm64(hex_to_int(trim(line.instr.substr(space, line.instr.size()-space))));
      line.mem = hex_to_int(trim(line.instr.substr(space, line.instr.size()-space)));
  } else {
    //line.mem = Imm64(0);
    line.mem = 0;
  }

  // Find immediate of this line
  const auto dollar = line.instr.find_first_of('$');
  if (dollar != string::npos &&
          (opcode_str != "lea" || opcode_str != "leaq")) {
    const auto comma = line.instr.find_first_of(',');
    //line.imm = Imm64(hex_to_int(line.instr.substr(dollar+3, comma-dollar-3)));
    line.imm = hex_to_int(line.instr.substr(dollar+3, comma-dollar-3));
  } else {
    //line.imm = Imm64(0);
    line.imm = 0;
  }
  return true;
}

bool Disassembler::parse_ptr(const string& s, map<string, string>& ptrs) {
  // get the function name
  auto start = s.find_last_of('<');
  auto end = s.find_last_of('>');
  if (start == string::npos || end == string::npos) {
    return false;
  }

  //skip labels that point inside the same function
  auto function_name = s.substr(start + 1, end - start - 1);
  if (function_name.find_last_of("+") != string::npos) {
    return false;
  }

  // Mangle away tokens we don't support
  function_name = mangle_lable(function_name);

  // Read the address
  auto end_addr   = s.find_first_of(' ', start - 3);
  auto start_addr = s.find_last_of(' ', end_addr - 1);
  if (start_addr == string::npos || end_addr == string::npos) {
    return false;
  }
  auto address = s.substr(start_addr + 1, end_addr - start_addr - 1);

  // We got a result
  ptrs[address] = function_name;
  return true;
}

vector<Disassembler::LineInfo> Disassembler::parse_lines(ipstream& ips, const string& name) {
  vector<LineInfo> lines;
  map<string, string> ptrs;
  string s;

  while (getline(ips, s)) {
    // Functions are terminated by empty lines
    if (s.empty()) {
      break;
    }

    // parse_line() returns false for line continuations
    // When that happens only add hex bytes to previous result
    LineInfo line;
    if (parse_line(s, line)) {
      //cout << "offset: " << line.offset << " hex: " << line.hex_bytes << " instr: " << line.instr << endl;
      //cout << "opcode: " << line.opcode << endl;
      //if (line.imm != 0) cout << "immediate: " << line.imm << endl;
      //if (line.mem != 0) cout << "mem: " << line.mem << endl;
      lines.push_back(line);
      parse_ptr(s, ptrs);
    } else {
      lines.back().hex_bytes += line.hex_bytes;
    }
  }

  // Update non-funtion label references and record targets
  set<uint64_t> label_refs;
  for (auto& l : lines) {
    // Opcodes are followed by at least one space; ignore instructions with no operands
    auto ops_begin = l.instr.find_first_of(' ');
    for (; ops_begin != string::npos && isspace(l.instr[ops_begin]); ops_begin++);
    if (ops_begin == l.instr.length() || ops_begin == string::npos) {
      continue;
    }

    // Operands are terminated by whitespace
    const auto ops_end = l.instr.find_first_of(' ', ops_begin + 1);
    const auto ops_len = (ops_end == string::npos ? l.instr.length() : ops_end) - ops_begin;
    const auto ops = l.instr.substr(ops_begin, ops_len);

    // Arguments that are strictly hex digits become labels
    if (is_hex_string(ops)) {
      const auto itr = ptrs.find(ops);
      if (itr != ptrs.end()) {
        l.instr = l.instr.substr(0, ops_begin) + "." + itr->second;
      } else {
        label_refs.insert(hex_to_int(ops));
        l.instr = l.instr.substr(0, ops_begin) + ".L_" + ops;
      }
    }
  }

  // Insert label definitions where necessary and fix instruction text
  // @todo The fact that we split lock into two instructions is going to bite us here
  // dungnm: Modify to initialize the additional field of the structure LineInfo
  vector<LineInfo> result;
  result.push_back({lines[0].offset, 0, string(".") + name + string(":"), LABEL_DEFN, 0, 0});
  for (const auto& l : lines) {
    if (label_refs.find(l.offset) != label_refs.end()) {
      ostringstream oss;
      oss << ".L_" << hex << l.offset << ":";
      result.push_back({l.offset, 0, oss.str(), LABEL_DEFN, 0, 0});
    }
    result.push_back({l.offset, l.hex_bytes, fix_instruction(l.instr), l.opc, l.mem, l.imm});
  }

  return result;
}

bool Disassembler::parse_function(ipstream& ips, FunctionCallbackData& data, uint64_t text_offset) {
  if (ips.eof()) {
    return false;
  }

  // Get the name of the function
  string name;
  getline(ips, name);
  const auto begin = name.find_first_of('<') + 1;
  const auto len = name.find_last_of('>') - begin;
  name = mangle_lable(name.substr(begin, len));

  // Parse the contents of this function
  // This function inserts missing lines such as labels and splits lock into two instructions
  const auto lines = parse_lines(ips, name);
  stringstream ss;

  for (const auto& l : lines) {

    // For each line, try encoding it in different sizes, starting with the size that's
    // found in the disassembly.  If we have to go down, then insert nops to pad it out.
    // If we have to go up, then we fail.

    bool success = false;
    for (int attempt = l.hex_bytes; attempt >= 0; attempt--) {
      stringstream tmp;
      tmp << l.instr << " # SIZE=" << attempt << endl;
      Code c;
      tmp >> c;
      if (failed(tmp))
        continue;

      // we've found a match
      //cout << "Size " << attempt << " worked for " << l.instr << endl;
      ss << l.instr << " # SIZE=" << attempt << endl;
      for (size_t i = 0; i < l.hex_bytes - attempt; ++i) {
        ss << "nop # SIZE=1" << endl;
      }
      success = true;
      break;
    }

    if (!success) {
      //cout << "Failing on " << l.instr << " in " << l.hex_bytes << " bytes." << endl;
      fail(ss) << "Could not encode " << l.instr << " within " << l.hex_bytes << " bytes." << endl;
    }
  }

  // Read code
  Code code;
  ss >> code;

  // Record hex metadata
  size_t capacity = 0;
  for (const auto& l : lines) {
    capacity += l.hex_bytes;
  }
  const auto rip_offset = lines[0].offset;
  const auto file_offset = rip_offset - text_offset;

  // All done; back to the user
  data.parse_error = failed(ss);
  data.parse_error_msg = (failed(ss) ? fail_msg(ss) : "");
  data.name = name;
  data.tunit = {code, file_offset, rip_offset, capacity};
  // dungnm: Update set of opcodes, memory addresses and immediates of this callback function
  for (auto li : lines) {
    data.opcodes.insert(li.opc);
    if (li.mem != 0) data.mems.insert(li.mem);
    if (li.imm != 0) data.immediates.insert(li.imm);
  }
  return true;
}

void Disassembler::disassemble(const std::string& filename) {
  // We're starting out fresh, so reset the error tracker
  clear_error();

  auto text_offset = 0;
  if (!flat_binary_) {

    // Get the headers from the objdump
    auto headers = run_objdump(filename, true);
    if (has_error()) {
      return;
    }

    // Parse the headers
    const auto section_offsets = parse_section_offsets(*headers);
    const auto text_itr = section_offsets.find(".text");
    if (text_itr == section_offsets.end()) {
      set_error("Unable to find value for text section offset");
      return;
    }
    text_offset = text_itr->second;

  }

  // Get the disassembly from objdump
  auto body = run_objdump(filename, false);
  if (has_error()) {
    return;
  }
  // Skip the first four lines of output and lines starting with 'D'
  strip_lines(*body, 4);
  for (string line; getline(*body, line) && line[0] == 'D';) {
    // Does nothing
  }
  // Read the functions and invoke the callback.
  FunctionCallbackData data;
  while (true) {
    //cout << "####################### " << data.name << endl;
    bool b = parse_function(*body, data, text_offset);
    if (!b) return;
    //for (auto op : data.opcodes) cout << op << endl;
    //for (auto mem : data.mems) cout << mem << endl;
    //for (auto im : data.immediates) cout << im << endl;
    if (!callback_closure_) {
      fxn_cb_(data, fxn_cb_arg_);
    } else {
      (*callback_closure_)(data);
    }
  }
}

void Disassembler::diff(const std::string& pp, const std::string& pb, const std::string func) {
  // We're starting out fresh, so reset the error tracker
  clear_error();

  auto text_offset_pp = 0;
  auto text_offset_pb = 0;
  if (!flat_binary_) {

    // Get the headers from the objdump
    auto headers_pp = run_objdump(pp, true);
    auto headers_pb = run_objdump(pb, true);
    if (has_error()) {
      return;
    }

    // Parse the headers
    const auto section_offsets_pp = parse_section_offsets(*headers_pp);
    const auto section_offsets_pb = parse_section_offsets(*headers_pb);
    const auto text_itr_pp = section_offsets_pp.find(".text");
    const auto text_itr_pb = section_offsets_pb.find(".text");
    if (text_itr_pp == section_offsets_pp.end() || text_itr_pb == section_offsets_pb.end()) {
      set_error("Unable to find value for text section offset");
      return;
    }
    text_offset_pp = text_itr_pp->second;
    text_offset_pb = text_itr_pb->second;

  }

  // Get the disassembly from objdump
  auto body_pp = run_objdump(pp, false);
  auto body_pb = run_objdump(pb, false);
  if (has_error()) {
    return;
  }
  // Skip the first four lines of output and lines starting with 'D'
  strip_lines(*body_pp, 4);
  strip_lines(*body_pb, 4);
  for (string line; getline(*body_pp, line) && line[0] == 'D';) {
    // Does nothing
  }
  for (string line; getline(*body_pb, line) && line[0] == 'D';) {
    // Does nothing
  }
  // Read the functions and invoke the callback.
  //FunctionCallbackData data_pp;
  //FunctionCallbackData data_pb;
  while (true) {
    //cout << "####################### " << data.name << endl;
    FunctionCallbackData data_pp;
    FunctionCallbackData data_pb;
    bool b1 = parse_function(*body_pp, data_pp, text_offset_pp);
    bool b2 = parse_function(*body_pb, data_pb, text_offset_pb);
    if (!b1 || !b2) return;
    set<x64asm::Opcode> op_pp, op_pb;
    set<x64asm::Imm64> mem_pp, mem_pb, imm_pp, imm_pb;
    if (data_pp.name == func) {
      cout << "###################PATCH#########################" << endl;
      op_pp = data_pp.opcodes;
      mem_pp = data_pp.mems;
      imm_pp = data_pp.immediates;
      for (auto op : op_pp) cout << op << endl;
      for (auto mem : mem_pp) cout << mem << endl;
      for (auto imm : imm_pp) cout << imm << endl;
    }
    if (data_pb.name == func) {
      cout << "###################BUGGY#########################" << endl;
      op_pb = data_pb.opcodes;
      mem_pb = data_pb.mems;
      imm_pb = data_pb.immediates;
      for (auto op : op_pb) cout << op << endl;
      for (auto mem : mem_pb) cout << mem << endl;
      for (auto imm : imm_pb) cout << imm << endl;

      set<x64asm::Opcode> op_diff;
      set<x64asm::Imm64> mem_diff, imm_diff;
      set_difference(op_pp.begin(), op_pp.end(), op_pb.begin(), op_pb.end(), inserter(op_diff, op_diff.begin()));
      set_difference(mem_pp.begin(), mem_pp.end(), mem_pb.begin(), mem_pb.end(), inserter(mem_diff, mem_diff.begin()));
      set_difference(imm_pp.begin(), imm_pp.end(), imm_pb.begin(), imm_pb.end(), inserter(imm_diff, imm_diff.begin()));
      cout << "*******************OPCODE**********************" << endl;
      for (auto op_d : op_diff) cout << op_d << endl;
      cout << "*******************MEMORY**********************" << endl;
      for (auto mem_d : mem_diff) cout << mem_d << endl;
      cout << "*******************IMMEDIATE**********************" << endl;
      for (auto imm_d : imm_diff) cout << imm_d << endl;
      cout << "*****************************************" << endl;
    }
    if (!callback_closure_) {
      fxn_cb_(data_pp, fxn_cb_arg_);
    } else {
      (*callback_closure_)(data_pp);
    }
    //if (!callback_closure_pb) {
      //fxn_cb_pb(data_pb, fxn_cb_arg_pb);
    //} else {
      //(*callback_closure_pb)(data_pb);
    //}
  }
}

} // namespace stoke
