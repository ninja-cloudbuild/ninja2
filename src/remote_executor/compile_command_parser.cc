/****************************************************************************
 * Copyright (c) CloudBuild Team. 2023. All rights reserved.
 * Licensed under GNU Affero General Public License v3 (AGPL-3.0) .
 * You can use this software according to the terms and conditions of the
 * AGPL-3.0.
 * You may obtain a copy of AGPL-3.0 at:
 *     https://www.gnu.org/licenses/agpl-3.0.txt
 * 
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the AGPL-3.0 for more details.
 ****************************************************************************/

#include "compile_command_parser.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>

#include "../util.h"

namespace RemoteExecutor {

using StringSet = std::set<std::string>;
using StringVector = std::vector<std::string>;

struct SupportedCompilers {
  static const StringSet GccCompilers;
  static const StringSet GccPreprocessors;
  static const StringSet SunCPPCompilers;
  static const StringSet AIXCompilers;
  static const StringSet CCompilers;
  static const StringSet JavaCompilers;

  static const StringVector GccDefaultDeps;
  static const StringVector SunCPPDefaultDeps;
  static const StringVector AIXDefaultDeps;

  static const StringSet GccSupportedLanguages;
  static const StringSet SupportedRemoteExecuteCommands;
};

const StringSet SupportedCompilers::GccCompilers
  = { "gcc", "g++", "c++", "clang", "clang++" };
const StringSet SupportedCompilers::GccPreprocessors
  = { "gcc-preprocessor" };
const StringSet SupportedCompilers::SunCPPCompilers = { "CC" };
const StringSet SupportedCompilers::AIXCompilers
  = { "xlc", "xlc++", "xlC", "xlCcore", "xlc++core" };
const StringSet SupportedCompilers::CCompilers = { "cc", "c89", "c99" };
const StringSet SupportedCompilers::JavaCompilers = { "javac", "java" };

const StringVector SupportedCompilers::GccDefaultDeps = { "-M" };
const StringVector SupportedCompilers::SunCPPDefaultDeps = { "-xM" };
const StringVector SupportedCompilers::AIXDefaultDeps
  = { "-qsyntaxonly", "-M", "-MF" };

const StringSet SupportedCompilers::GccSupportedLanguages
  = { "c", "c++", "c-header", "c++-header", "c++-system-header",
      "c++-user-header" };
const StringSet SupportedCompilers::SupportedRemoteExecuteCommands
  = { "gcc ", "g++ ", "c++ ", "clang ", "clang++ ", "javac " };

struct CompilerListHasher {
  size_t operator()(const StringSet& compiler_list) const {
    size_t seed = 0;
    for (auto& val : compiler_list) {
      seed ^= std::hash<std::string>{}(val) + 0x9e3779b9
              + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

using ParseFunc =
   std::function<void(CompileCommandParser::ParseResult*, const std::string&)>;
using ParseRulesMap = //TODO: must be greater?
    std::map<std::string, ParseFunc, std::greater<std::string>>;
using ParseCommandMap = std::unordered_map<
    StringSet/* compiler list */, ParseRulesMap, CompilerListHasher>;

struct ParseRule {
  static void ParseInterfersWithDepsOption(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseIsInputPathOption(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseIsEqualInputPathOption(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseIsCompileOption(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseOptionRedirectsOutput(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseOptionRedirectsDepsOutput(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseIsPreprocessorArgOption(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseIsMacro(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseOptionSetsGccLanguage(
      CompileCommandParser::ParseResult* result, const std::string& option);
  static void ParseOptionIsUnsupported(
      CompileCommandParser::ParseResult* result, const std::string& option);
};

struct ParseRuleHelper {
  static std::pair<std::string, ParseFunc> MatchCompilerOptions(
      const std::string& option, const ParseRulesMap& options);
  static void ParseGccOption(CompileCommandParser::ParseResult* result,
                             const std::string& option, bool to_deps = true,
                             bool is_output = false, bool deps_output = false);
  static void AppendAndRemoveOption(CompileCommandParser::ParseResult* result,
                                    bool is_path, bool to_deps,
                                    bool is_output = false,
                                    bool deps_output = false);
  static void ParseStageOptionList(const std::string& option,
                                   StringVector* result);
};

static const ParseRulesMap GccRules = {
  // Interferes with dependencies
  { "-MD", ParseRule::ParseInterfersWithDepsOption },
  { "-MMD", ParseRule::ParseInterfersWithDepsOption },
  { "-M", ParseRule::ParseInterfersWithDepsOption },
  { "-MM", ParseRule::ParseInterfersWithDepsOption },
  { "-MG", ParseRule::ParseInterfersWithDepsOption },
  { "-MP", ParseRule::ParseInterfersWithDepsOption },
  { "-MV", ParseRule::ParseInterfersWithDepsOption },
  { "-Wmissing-include-dirs", ParseRule::ParseInterfersWithDepsOption },
  { "-Werror=missing-include-dirs", ParseRule::ParseInterfersWithDepsOption },
  // Compile options
  { "-c", ParseRule::ParseIsCompileOption },
  // Macros
  { "-D", ParseRule::ParseIsMacro },
  // Redirects output
  { "-o", ParseRule::ParseOptionRedirectsOutput },
  { "-MF", ParseRule::ParseOptionRedirectsDepsOutput },
  { "-MT", ParseRule::ParseOptionRedirectsDepsOutput },
  { "-MQ", ParseRule::ParseOptionRedirectsDepsOutput },
  // Input paths
  { "-include", ParseRule::ParseIsInputPathOption },
  { "-imacros", ParseRule::ParseIsInputPathOption },
  { "-I", ParseRule::ParseIsInputPathOption },
  { "-iquote", ParseRule::ParseIsInputPathOption },
  { "-isystem", ParseRule::ParseIsInputPathOption },
  { "-idirafter", ParseRule::ParseIsInputPathOption },
  { "-iprefix", ParseRule::ParseIsInputPathOption },
  { "-isysroot", ParseRule::ParseIsInputPathOption },
  { "--sysroot", ParseRule::ParseIsEqualInputPathOption },
  // Preprocessor arguments
  { "-Wp,", ParseRule::ParseIsPreprocessorArgOption },
  { "-Xpreprocessor", ParseRule::ParseIsPreprocessorArgOption },
  // Sets language
  { "-x", ParseRule::ParseOptionSetsGccLanguage },
};

static const ParseRulesMap GccPreprocessorRules = {
  // Interferes with dependencies
  { "-MD", ParseRule::ParseInterfersWithDepsOption },
  { "-MMD", ParseRule::ParseInterfersWithDepsOption },
  { "-M", ParseRule::ParseInterfersWithDepsOption },
  { "-MM", ParseRule::ParseInterfersWithDepsOption },
  { "-MG", ParseRule::ParseInterfersWithDepsOption },
  { "-MP", ParseRule::ParseInterfersWithDepsOption },
  { "-MV", ParseRule::ParseInterfersWithDepsOption },
  // Redirects output
  { "-o", ParseRule::ParseOptionRedirectsOutput },
  { "-MF", ParseRule::ParseOptionRedirectsDepsOutput },
  { "-MT", ParseRule::ParseOptionRedirectsDepsOutput },
  { "-MQ", ParseRule::ParseOptionRedirectsDepsOutput },
  // Input paths
  { "-include", ParseRule::ParseIsInputPathOption },
  { "-imacros", ParseRule::ParseIsInputPathOption },
  { "-I", ParseRule::ParseIsInputPathOption },
  { "-iquote", ParseRule::ParseIsInputPathOption },
  { "-isystem", ParseRule::ParseIsInputPathOption },
  { "-idirafter", ParseRule::ParseIsInputPathOption },
  { "-iprefix", ParseRule::ParseIsInputPathOption },
  { "-isysroot", ParseRule::ParseIsInputPathOption },
  { "--sysroot", ParseRule::ParseIsEqualInputPathOption },
};

static const ParseRulesMap SunCPPRules = {
  // Interferes with dependencies
  { "-xM", ParseRule::ParseInterfersWithDepsOption },
  { "-xM1", ParseRule::ParseInterfersWithDepsOption },
  { "-xMD", ParseRule::ParseInterfersWithDepsOption },
  { "-xMMD", ParseRule::ParseInterfersWithDepsOption },
  // Macros
  { "-D", ParseRule::ParseIsMacro },
  // Redirects output
  { "-o", ParseRule::ParseOptionRedirectsOutput },
  { "-xMF", ParseRule::ParseOptionRedirectsOutput },
  // Input paths
  { "-I", ParseRule::ParseIsInputPathOption },
  { "-include", ParseRule::ParseIsInputPathOption },
  // Compile options
  { "-c", ParseRule::ParseIsCompileOption },
  // Options not supported
  { "-xpch", ParseRule::ParseOptionIsUnsupported },
  { "-xprofile", ParseRule::ParseOptionIsUnsupported },
  { "-###", ParseRule::ParseOptionIsUnsupported },
};

static const ParseRulesMap AIXRules = {
  // Interferes with dependencies
  { "-qmakedep", ParseRule::ParseInterfersWithDepsOption },
  { "-qmakedep=gcc", ParseRule::ParseInterfersWithDepsOption },
  { "-M", ParseRule::ParseInterfersWithDepsOption },
  { "-qsyntaxonly", ParseRule::ParseInterfersWithDepsOption },
  // Macros
  { "-D", ParseRule::ParseIsMacro },
  // Redirects output
  { "-o", ParseRule::ParseOptionRedirectsOutput },
  { "-MF", ParseRule::ParseOptionRedirectsOutput },
  { "-qexpfile", ParseRule::ParseOptionRedirectsOutput },
  // Input paths
  { "-qinclude", ParseRule::ParseIsInputPathOption },
  { "-I", ParseRule::ParseIsInputPathOption },
  { "-qcinc", ParseRule::ParseIsInputPathOption },
  // Compile options
  { "-c", ParseRule::ParseIsCompileOption },
  // Options not supported
  { "-#", ParseRule::ParseOptionIsUnsupported },
  { "-qshowpdf", ParseRule::ParseOptionIsUnsupported },
  { "-qdump_class_hierachy", ParseRule::ParseOptionIsUnsupported },
};

static const ParseCommandMap DefaultParseCommandMap = {
  { SupportedCompilers::GccCompilers, GccRules },
  { SupportedCompilers::GccPreprocessors, GccPreprocessorRules },
  { SupportedCompilers::SunCPPCompilers, SunCPPRules },
  { SupportedCompilers::AIXCompilers, AIXRules },
};

/// Converts a command path ("/usr/bin/gcc-4.7") to a command name ("gcc")
std::string CommandBaseName(const std::string& path) {
  const auto lastSlash = path.rfind('/');
  const auto basename =
      (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
  auto length = basename.length();
  // We get rid of "_r" suffixes in, for example, "./xlc++_r":
  const std::string rSuffix = "_r";
  if (length > 2 && basename.substr(length - rSuffix.length()) == rSuffix) {
    length -= 2;
  } else if (length > 3 &&
             basename.substr(length - 3, rSuffix.length()) == rSuffix) {
    length -= 3;
  }
  const auto is_version_character = [](const char character) {
    return (isdigit(character)) || character == '.' || character == '-';
  };
  while (length > 0 && is_version_character(basename[length - 1])) {
    --length;
  }
  return basename.substr(0, length);
}

CompileCommandParser::ParseResult MakeResult(const StringVector& command) {
  CompileCommandParser::ParseResult result;
  if (command.empty())
    return result;
  const std::string& compiler = command[0];
  if (compiler.empty())
    return result;
  result.compiler = CommandBaseName(compiler);

  if (SupportedCompilers::GccCompilers.count(result.compiler)) {
    result.default_deps_command = SupportedCompilers::GccDefaultDeps;
  } else if (SupportedCompilers::SunCPPCompilers.count(result.compiler)) {
    result.default_deps_command = SupportedCompilers::SunCPPDefaultDeps;
    result.produces_sun_make_rules = true;
  } else if (SupportedCompilers::AIXCompilers.count(result.compiler)) {
    result.default_deps_command = SupportedCompilers::AIXDefaultDeps;
    result.produces_sun_make_rules = true;
    result.aix_deps_file = StaticFileUtils::CreateTempFile();
    result.default_deps_command.push_back(result.aix_deps_file.second);
  }

  result.deps_command.push_back(compiler);
  std::copy(std::next(command.begin()), command.end(),
    std::back_inserter(result.original_command));
  return result;
}

void InternalParseCommand(CompileCommandParser::ParseResult* result,
                          const ParseRulesMap& parseRules) {
  while (!result->original_command.empty()) {
    const auto& currToken = result->original_command.front();
    const auto& optionModifier =
        ParseRuleHelper::MatchCompilerOptions(currToken, parseRules);
    if (optionModifier.second) {
      optionModifier.second(result, optionModifier.first);
    } else {
      result->deps_command.push_back(currToken);
      result->original_command.pop_front();
    }
  }
}

CompileCommandParser::ParseResult CompileCommandParser::ParseCommand(
    const StringVector& command) {
  if (command.empty())
    return {};
  auto result = MakeResult(command);

  ParseRulesMap rulesToUse;
  for (const auto& val : DefaultParseCommandMap) {
    auto it = val.first.find(result.compiler);
    if (it != val.first.end()) {
      rulesToUse = val.second;
      break;
    }
  }
  InternalParseCommand(&result, rulesToUse);

  if (result.contains_unsupported_options) {
    result.is_compiler_command = false;
    return result;
  }

  // Handle gccpreprocessor options which were populated during the original
  // parsing of the command.
  // These options require special flags, before each option.
  if (result.pre_processor_options.size() > 0) {
    CompileCommandParser::ParseResult preprocess_result;
    std::copy(result.pre_processor_options.begin(),
              result.pre_processor_options.end(),
              std::back_inserter(preprocess_result.original_command));
    InternalParseCommand(&preprocess_result, GccPreprocessorRules);

    for (const auto& preproArg : preprocess_result.deps_command) {
      result.deps_command.push_back("-Xpreprocessor");
      result.deps_command.push_back(preproArg);
    }
    for (const auto& preproArg : preprocess_result.command_products) {
      result.command_products.insert(preproArg);
    }
    for (const auto& preproArg : preprocess_result.deps_command_products) {
      result.deps_command_products.insert(preproArg);
    }
    result.is_md_options =
        preprocess_result.is_md_options || result.is_md_options;
  }

  std::copy(result.default_deps_command.begin(),
            result.default_deps_command.end(),
            std::back_inserter(result.deps_command));
  std::copy(command.begin(), command.end(),
            std::back_inserter(result.original_command));
  return result;
}

struct ExecResult {
  int exit_code;
  std::string std_out;
};

ExecResult ExecuteSubProcess(const std::vector<std::string>& command) {
  ExecResult result;
  std::string cmd = MergeStrings(command);
  char szBuf[409600] = {0};
  FILE* pResultStr = popen(cmd.c_str(), "r");
  if (NULL == pResultStr) {
    printf("popen failed. (%d, %s)\n",errno, strerror(errno));
    return result;
  }
  auto len = fread(szBuf, 1, sizeof(szBuf), pResultStr);
  result.std_out.append(szBuf, len);
  result.exit_code = 0;
  pclose(pResultStr);
  return result;
}

StringSet DepsFromMakeRules(const std::string& rules, bool is_sun_format) {
  StringSet result;
  bool saw_colon_on_line = false;
  bool saw_backslash = false;
  std::string current_filename;
  for (const char& character : rules) {
    if (saw_backslash) {
      saw_backslash = false;
      if (character != '\n' && saw_colon_on_line) {
        current_filename += character;
      }
    } else if (character == '\\') {
      saw_backslash = true;
    } else if (character == ':' && !saw_colon_on_line) {
      saw_colon_on_line = true;
    } else if (character == '\n') {
      saw_colon_on_line = false;
      if (!current_filename.empty())
        result.insert(current_filename);
      current_filename.clear();
    } else if (character == ' ') {
      if (is_sun_format) {
        if (!current_filename.empty() && saw_colon_on_line)
          current_filename += character;
      } else {
        if (!current_filename.empty())
          result.insert(current_filename);
        current_filename.clear();
      }
    } else if (saw_colon_on_line) {
      current_filename += character;
    }
  }
  if (!current_filename.empty())
    result.insert(current_filename);
  return result;
}

StringSet CompileCommandParser::ParseHeaders(const ParseResult& result) {
  auto exec_result = ExecuteSubProcess(result.deps_command);
  if (exec_result.exit_code != 0) {
    std::string errorMsg = "Failed to execute get dependencies command: ";
    for (auto& token : result.deps_command) {
      errorMsg += (token + " ");
    }
    Error("Exit status: %d, message: \"%s\"", errorMsg, exec_result.exit_code);
    Info("  stdout: \"%s\"", exec_result.std_out);
    return {}; //TODO test
  }
  std::string dependencies = exec_result.std_out;
  if (result.aix_deps_file.first >= 0) {
    dependencies = StaticFileUtils::GetFileContents(
      result.aix_deps_file.second.c_str());
  }
  return DepsFromMakeRules(dependencies, result.produces_sun_make_rules);
}

const StringSet& CompileCommandParser::SupportedRemoteExecuteCommands() {
  return SupportedCompilers::SupportedRemoteExecuteCommands;
}

std::pair<std::string, ParseFunc> ParseRuleHelper::MatchCompilerOptions(
    const std::string& option, const ParseRulesMap& options) {
  auto opt = option;
  if (!opt.empty() && opt.front() == '-') {
    // Check for an equal sign, if any, return left side.
    opt = opt.substr(0, opt.find("="));
    // First try finding an exact match, removing and parsing until an
    // equal sign. Remove any spaces from the option.
    opt.erase(remove_if(opt.begin(), opt.end(), ::isspace), opt.end());
    if (options.count(opt) > 0)
      return std::make_pair(opt, options.at(opt));
    // Second, try a substring search, iterating through all the
    // options in the map.
    for (const auto& option_map_val : options) {
      const auto val = option.substr(0, option_map_val.first.length());
      if (val == option_map_val.first)
        return std::make_pair(option_map_val.first, option_map_val.second);
    }
  }
  return std::make_pair("", nullptr);
}

void ParseRule::ParseInterfersWithDepsOption(
    CompileCommandParser::ParseResult* result, const std::string&) {
  if (result->original_command.front() == "-MMD" ||
      result->original_command.front() == "-MD")
    result->is_md_options = true;
  result->original_command.pop_front();
}

void ParseRule::ParseIsInputPathOption(
    CompileCommandParser::ParseResult* result, const std::string& option) {
  ParseRuleHelper::ParseGccOption(result, option);
}

void ParseRule::ParseIsEqualInputPathOption(
    CompileCommandParser::ParseResult* result, const std::string& option) {
  ParseRuleHelper::ParseGccOption(result, option);
}

void ParseRule::ParseIsCompileOption(
    CompileCommandParser::ParseResult* result, const std::string&) {
  result->is_compiler_command = true;
  // Push back option (e.g "-c")
  ParseRuleHelper::AppendAndRemoveOption(result, false, true);
}

void ParseRule::ParseOptionIsUnsupported(
    CompileCommandParser::ParseResult* result, const std::string&) {
  result->contains_unsupported_options = true;
  std::copy(result->original_command.begin(), result->original_command.end(),
            std::back_inserter(result->deps_command));
  // Clear the original command so parsing stops.
  result->original_command.clear();
}

void ParseRule::ParseOptionRedirectsOutput(
    CompileCommandParser::ParseResult* result, const std::string& option) {
  ParseRuleHelper::ParseGccOption(result, option, false, true);
}

void ParseRule::ParseOptionRedirectsDepsOutput(
    CompileCommandParser::ParseResult* result, const std::string& option) {
  ParseRuleHelper::ParseGccOption(result, option, false, true, true);
}

void ParseRule::ParseIsMacro(
    CompileCommandParser::ParseResult* result, const std::string& option) {
  // This can come in four forms:
  // 1. -Dname
  // 2. -Dname=definition
  // 3. -D name
  // 4. -D name=definition
  // We just need to make sure we handle the cases where there's a
  // space between the -D flag and the rest of the arguments.
  const std::string token = result->original_command.front();
  result->deps_command.push_back(token);
  if (token == option) {
    result->original_command.pop_front();
    const std::string arg = result->original_command.front();
    result->deps_command.push_back(arg);
  }
  result->original_command.pop_front();
}

void ParseRule::ParseOptionSetsGccLanguage(
    CompileCommandParser::ParseResult* result, const std::string& option) {
  const std::string original_cmd_opt = result->original_command.front();
  result->original_command.pop_front();
  std::string language = "";

  if (original_cmd_opt == option) {
    // Space between -x and argument, e.g. "-x assembler"
    if (result->original_command.empty()) {
      // The -x was at the end of the command with no argument
      Warning("gcc's \"-x\" flag requires an argument");
      result->contains_unsupported_options = true;
      return;
    }
    language = result->original_command.front();
  } else {
    // No space, e.g. "-xassembler"
    // Note that gcc -x does not understand an equals sign. If "-x=c++"
    // is provided, the language is treated as "=c++"
    language = original_cmd_opt.substr(option.length());
  }

  result->original_command.push_front(original_cmd_opt);
  if (!SupportedCompilers::GccSupportedLanguages.count(language)) {
    Warning("Ninja[remote] does not support the language [%s].",
            language.c_str());
    result->contains_unsupported_options = true;
  }
  ParseRuleHelper::ParseGccOption(result, option);
}

void ParseRule::ParseIsPreprocessorArgOption(
    CompileCommandParser::ParseResult* result, const std::string& option) {
  auto val = result->original_command.front();
  if (option == "-Wp,") {
    // parse comma separated list of args, and store in preprocessor vector.
    auto optionList = val.substr(option.size());
    ParseRuleHelper::ParseStageOptionList(optionList,
                                          &result->pre_processor_options);
  } else if (option == "-Xpreprocessor") {
    // push back next arg
    result->original_command.pop_front();
    result->pre_processor_options.push_back(result->original_command.front());
  }
  result->original_command.pop_front();
}

void ParseRuleHelper::ParseGccOption(CompileCommandParser::ParseResult* result,
                                     const std::string& option, bool to_deps,
                                     bool is_output, bool deps_output) {
  auto val = result->original_command.front();
  // Space between option and input path (-I /usr/bin/include)
  if (val == option) {
    AppendAndRemoveOption(result, false, to_deps);
    // Push back corresponding path, but not into deps command
    AppendAndRemoveOption(result, true, to_deps, is_output, deps_output);
  } else {
    // No space between option and path (-I/usr/bin/include)
    // Or if "=" sign between option and path. (-I=/usr/bin/include)
    const auto equalPos = val.find('=');
    auto optionPath = val.substr(option.size());
    auto modifiedOption = option;
    if (equalPos != std::string::npos) {
      modifiedOption += "=";
      optionPath = val.substr(equalPos + 1);
    }
    if (is_output && !deps_output) {
      result->command_products.insert(optionPath);
    } else if (is_output) {
      result->deps_command_products.insert(optionPath);
    } else if (to_deps) {
      result->deps_command.push_back(modifiedOption + optionPath);
    }
    result->original_command.pop_front();
  }
}

void ParseRuleHelper::AppendAndRemoveOption(
    CompileCommandParser::ParseResult* result,
    bool is_path, bool to_deps, bool is_output, bool deps_output) {
  auto option = result->original_command.front();
  if (is_path) {
    if (to_deps) {
      result->deps_command.push_back(option);
    }
    if (is_output && !deps_output) {
      result->command_products.insert(option);
    } else if (is_output) {
      result->deps_command_products.insert(option);
    }
  } else {
    if (to_deps) {
      result->deps_command.push_back(option);
    }
  }
  result->original_command.pop_front();
}

void ParseRuleHelper::ParseStageOptionList(const std::string& option,
                                           StringVector* result) {
  bool quoted = false;
  std::string current;
  for (const char& character : option) {
    if (character == '\'') {
      quoted = !quoted;
    } else if (character == ',' && !quoted) {
      result->push_back(current);
      current = std::string();
    } else {
      current += character;
    }
  }
  result->push_back(current);
}

}  // namespace RemoteExecutor
