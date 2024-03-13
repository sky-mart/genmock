#include "Mocker.h"

#include "clang/AST/Decl.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory GenMockCategory("GenMock Options");

enum class MockType { Interface, Singleton };

static llvm::cl::opt<MockType>
    inputType("mocktype", llvm::cl::desc("Specify a mock type"),
              llvm::cl::values(
                  clEnumValN(MockType::Interface, "interface",
                             "Mock an interface"),
                  clEnumValN(MockType::Singleton, "singleton",
                             "Mock using a singleton")),
              llvm::cl::init(MockType::Interface),
              llvm::cl::cat(GenMockCategory));

static llvm::cl::opt<std::string> outputHeader("outh", llvm::cl::desc("Specify an output header file"), llvm::cl::cat(GenMockCategory));
static llvm::cl::opt<std::string> outputSource("outsrc", llvm::cl::desc("Specify an output source file"), llvm::cl::cat(GenMockCategory));

static llvm::cl::opt<std::string> configPath("config", llvm::cl::desc("Specify a config file"), llvm::cl::cat(GenMockCategory));
// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

struct MockerArguments {
    MockerArguments(const Mocker::Config& config, const std::string& outputHeaderPath, const std::string& outputSourcePath) :
        config(config), outputHeaderPath(outputHeaderPath), outputSourcePath(outputSourcePath) {}

    const Mocker::Config& config;
    const std::string& outputHeaderPath;
    const std::string& outputSourcePath;
};


class MockingConsumer : public clang::ASTConsumer {
public:
    MockingConsumer(const MockerArguments& mockerArguments, StringRef InFile) :
        mocker_(mockerArguments.config, InFile.str(), mockerArguments.outputHeaderPath, mockerArguments.outputSourcePath) {}

    void HandleTranslationUnit(clang::ASTContext& Context) override {
        mocker_.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    Mocker mocker_;
};

class MockingAction : public clang::ASTFrontendAction {
public:
    MockingAction(const MockerArguments& mockerArguments) : mockerArguments_(mockerArguments) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &Compiler,
        llvm::StringRef InFile) override
    {
        return std::make_unique<MockingConsumer>(mockerArguments_, InFile);
    }

private:
    const MockerArguments& mockerArguments_;
};

class MockingActionFactory : public clang::tooling::FrontendActionFactory {
public:
    MockingActionFactory(const MockerArguments& mockerArguments) : mockerArguments_(mockerArguments) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<MockingAction>(mockerArguments_);
    }

private:
    const MockerArguments& mockerArguments_;
};

int main(int argc, const char **argv)
{
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, GenMockCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();

  if (0 == configPath.getNumOccurrences()) {
      const size_t maxConfigPathLength = 300;
      llvm::SmallVector<char, maxConfigPathLength> defaultConfigPath;
      llvm::sys::path::user_config_directory(defaultConfigPath);
      llvm::sys::path::append(defaultConfigPath, "genmock", "genmock.json");
      defaultConfigPath.push_back('\0');
      configPath.setValue(defaultConfigPath.data());
  }
  std::ifstream f(configPath.getValue());
  const Mocker::Config config(nlohmann::json::parse(f));
  const MockerArguments mockerArguments(config, outputHeader.getValue(), outputSource.getValue());

  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  Tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster("-xc++"));
  Tool.setDiagnosticConsumer(new IgnoringDiagConsumer());

  MockingActionFactory factory(mockerArguments);
  return Tool.run(&factory);
}
