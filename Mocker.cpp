#include "Mocker.h"
#include "clang/AST/PrettyPrinter.h"
#include "llvm/Pass.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/Type.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include <functional>

namespace {

    void printParamList(llvm::raw_ostream& OS, const clang::FunctionDecl* function, const std::function<void(llvm::raw_ostream&, const clang::ParmVarDecl*)>& processParam)
{
    for (unsigned i = 0; i < function->getNumParams(); ++i) {
        processParam(OS, function->getParamDecl(i));
        if (i != function->getNumParams() - 1) {
            OS << ", ";
        }
    }
}

void printParamList(llvm::raw_ostream& OS, const clang::FunctionDecl* function)
{
    printParamList(OS, function, [](llvm::raw_ostream& OS, const clang::ParmVarDecl* param) { param->print(OS); });
}

void printParamTypeList(llvm::raw_ostream& OS, const clang::FunctionDecl* function)
{
    printParamList(OS, function, [](llvm::raw_ostream& OS, const clang::ParmVarDecl* param) { OS << param->getOriginalType().getAsString(); });
}

void printParamNameList(llvm::raw_ostream& OS, const clang::FunctionDecl* function)
{
    printParamList(OS, function, [](llvm::raw_ostream& OS, const clang::ParmVarDecl* param) { OS << param->getName(); });
}

void printFunctionSignature(llvm::raw_ostream& OS, const clang::FunctionDecl* function)
{
    OS << function->getReturnType().getAsString() << " ";
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function)) {
        OS << method->getParent()->getName() << "::";
    }
    OS << function->getName() << "(";
    printParamList(OS, function);
    OS << ")";
}

void printMockFunctionCall(llvm::raw_ostream& OS, const std::string& mockClassName, const clang::FunctionDecl* function)
{
    if (!function->getReturnType()->isVoidType()) {
        OS << "return ";
    }
    OS << mockClassName << "::instance()." << function->getName() << "(";
    printParamNameList(OS, function);
    OS << ");";
}

void printFunctionDefinition(llvm::raw_ostream& OS, const std::string& mockClassName, const clang::FunctionDecl* function)
{
    printFunctionSignature(OS, function);
    OS << "\n{\n    ";
    printMockFunctionCall(OS, mockClassName, function);
    OS << "\n}\n\n";
}

void oldMockFunction(llvm::raw_ostream& OS, const clang::FunctionDecl* function)
{
    const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
    if (method && method->isConst()) {
        OS << "MOCK_CONST_METHOD";
    } else {
        OS << "MOCK_METHOD";
    }
    OS << function->getNumParams() << "(" << function->getName() << ", "
       << function->getReturnType().getAsString() << "(";
    printParamTypeList(OS, function);
    OS << "));\n";
}

void newMockFunction(llvm::raw_ostream& OS, const clang::FunctionDecl* function)
{
    const clang::CXXMethodDecl* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
    OS << "MOCK_METHOD(" << function->getReturnType().getAsString() << ", " << function->getName() << ", (";
    printParamTypeList(OS, function);
    OS << ")";
    if (method) {
        if (method->isConst()) {
            OS << ", (const";
            if (method->isVirtual()) {
                OS << ", override";
            }
            OS << ")";
        }
        if (method->isVirtual()) {
            OS << ", (override)";
        }
    }
    OS << ");\n";
}

std::string withoutI(const std::string &className) {
  if (!className.empty() && 'I' == className[0]) {
    return className.substr(1);
  }
  return className;
}

std::string extractClassName(const std::string& path)
{
    auto slashPos = path.find_last_of('/');
    if (slashPos == path.npos) {
        slashPos = 0;
    } else {
        slashPos++;
    }
    auto dotPos = path.find('.');
    auto className = path.substr(slashPos, dotPos - slashPos);
    className[0] = toupper(className[0]);
    return className;
}

std::string getRelPath(const std::string& path)
{
    const auto lastIncludePos = path.rfind("include/");
    if (std::string::npos == lastIncludePos) {
        return llvm::sys::path::filename(path).str();
    }
    return path.substr(lastIncludePos + 8);
}

std::string getGuardToken(const std::string& relPath)
{
    size_t ubarPos = 0;
    std::string token = relPath;
    for (size_t i = 0; i < token.size(); ++i) {
        if ('/' == token[i] || '.' == token[i]) {
            token[i] = '_';
            ubarPos = i;
        } else {
            token[i] = std::toupper(token[i]);
        }
    }
    token.insert(ubarPos, "_MOCK");
    return token;
}

} // namespace

Mocker::Config::Config(const nlohmann::json& cfg)
{
    const int tabLength = cfg["tab_length"];
    tab = std::string(tabLength, ' ');
    singletonPath = cfg["singleton_path"];
    singletonClass = cfg["singleton_class"];
    if (cfg["style"] == "old") {
        gmockStyle = GmockStyle::Old;
    } else if (cfg["style"] == "new") {
        gmockStyle = GmockStyle::New;
    }
}

Mocker::Mocker(Config config,
               std::string inputFilePath, std::string outputHeaderPath,
               std::string outputSourcePath)
    : clang::RecursiveASTVisitor<Mocker>(),
      config_(std::move(config)),
      inputFilePath_(std::move(inputFilePath)),
      relativeInputPath_(llvm::sys::path::filename(inputFilePath_).str()),
      outputHeaderPath_(std::move(outputHeaderPath)),
      outputSourcePath_(std::move(outputSourcePath))
{
    if (config_.gmockStyle == GmockStyle::Old) {
        printMockFunction_ = &oldMockFunction;
    } else {
        printMockFunction_ = &newMockFunction;
    }

}

bool Mocker::TraverseDecl(clang::Decl* D)
{
    const auto location = D->getLocation();
    if (location.isValid()) {
        const auto filename = llvm::sys::path::filename(
            D->getASTContext().getSourceManager().getFilename(location)); // relative path
        if (filename != relativeInputPath_) {
            // use for debug purposes
            // llvm::outs() << "Ignore the decl from " << filename << " while parsing " << relativeInputPath_ << "\n";
            return true;
        }
    }
    return RecursiveASTVisitor::TraverseDecl(D);
}

bool Mocker::TraverseTranslationUnitDecl(clang::TranslationUnitDecl *tu)
{
    if (!llvm::sys::path::is_absolute(outputHeaderPath_)) {
        llvm::errs() << "Output header path is not absolute: " << outputHeaderPath_ << "\n";
        return false;
    }
    const auto headerDir = llvm::sys::path::parent_path(outputHeaderPath_);
    std::error_code EC = llvm::sys::fs::create_directories(headerDir);
    if (EC) {
        llvm::errs() << "Can't create the directory for the output header file " << outputHeaderPath_
                     << ": " << EC.message() << "\n";
        return false;
    }
    header_ = std::make_unique<llvm::raw_fd_ostream>(outputHeaderPath_.c_str(), EC, llvm::sys::fs::OpenFlags::OF_None);
    if (EC) {
        llvm::errs() << "Can't open the output header file " << outputHeaderPath_
                     << ": " << EC.message() << "\n";
        return false;
    }

    if (!outputSourcePath_.empty()) {
        if (!llvm::sys::path::is_absolute(outputSourcePath_)) {
            llvm::errs() << "Output source path is not absolute: " << outputSourcePath_ << "\n";
            return false;
        }
        const auto sourceDir = llvm::sys::path::parent_path(outputSourcePath_);
        EC = llvm::sys::fs::create_directories(sourceDir);
        if (EC) {
            llvm::errs() << "Can't create the directory for the output source file " << sourceDir
                         << ": " << EC.message() << "\n";
            return false;
        }
        src_ = std::make_unique<llvm::raw_fd_ostream>(outputSourcePath_.c_str(), EC, llvm::sys::fs::OpenFlags::OF_None);
        if (EC) {
            llvm::errs() << "Can't open the output src file " << outputSourcePath_
                         <<": " << EC.message() << "\n";
            return false;
        }
    }

  const auto relInputPath = getRelPath(inputFilePath_);
  const auto guardToken = getGuardToken(relInputPath);
  *header_ << "#ifndef " << guardToken << "\n"
       << "#define " << guardToken << "\n\n"
           << "#include \"" << relInputPath << "\"\n";
  if (src_) {
      *header_ << "#include <estd/singleton.h>\n";
      const auto relOutputHeaderPath = getRelPath(outputHeaderPath_);
      *src_ << "#include \"" << relInputPath << "\"\n#include \"" << relOutputHeaderPath << "\"\n";
  }
  *header_ << "#include <gmock/gmock.h>\n";

  const auto result = RecursiveASTVisitor::TraverseTranslationUnitDecl(tu);

  *header_ << "#endif // " << guardToken << "\n";

  return result;
}

bool Mocker::TraverseLinkageSpecDecl(clang::LinkageSpecDecl *linkageSpec)
{
    if (Stage::INCLUDES == stage_ && src_)
    {
        *src_ << "\n";
    }
    stage_ = Stage::NAMESPACES;

    mockClassName_ = extractClassName(inputFilePath_) + "Mock";
    *header_ << "class " << mockClassName_ << " : public "
             << config_.singletonClass << "<" << mockClassName_ << ">\n{\npublic:\n";

    std::string language;
    if (src_) {
        language = linkageSpec->getLanguage() == clang::LinkageSpecLanguageIDs::C ? "C" : "CXX";
        *src_ << "extern \"" << language << "\" {\n";
    }
    const auto result = RecursiveASTVisitor::TraverseLinkageSpecDecl(linkageSpec);

    if (src_) {
        *src_ << "} // extern \"" << language << "\"\n";
    }
    *header_ << "}; // class " << mockClassName_ << "\n\n";
    return result;
}

bool Mocker::TraverseNamespaceDecl(clang::NamespaceDecl* ns)
{
    if (Stage::INCLUDES == stage_)
    {
        *header_ << "\n";
        if (src_) {
            *src_ << "\n";
        }
    }
    stage_ = Stage::NAMESPACES;

    *header_ << "namespace " << ns->getName() << " {\n";
    if (src_) {
        *src_ << "namespace " << ns->getName() << " {\n";
    }

    const auto result = RecursiveASTVisitor::TraverseNamespaceDecl(ns);

    *header_ << "} // namespace " << ns->getName() << "\n";
    if (src_) {
        *src_ << "} // namespace " << ns->getName() << "\n";
    }

    return result;
}

bool Mocker::TraverseCXXRecordDecl(clang::CXXRecordDecl* cl)
{
    // TODO: go only if there are virtual methods
    if (!cl->hasDefinition()) {
        return true;
    }

    // TODO: change to the explicit parameter
    bool noStaticMethods = true;
    if (src_) {
      for (const auto* method : cl->methods()) {
        if (method->isStatic()) {
          noStaticMethods = false;
          break;
        }
      }
      if (noStaticMethods) {
        return true;
      }
    }

    if (Stage::INCLUDES == stage_ || Stage::NAMESPACES == stage_)
    {
        *header_ << "\n";
        if (src_) {
            *src_ << "\n";
        }
    }
    stage_ = Stage::CLASS;

    className_ = cl->getName().str();
    mockClassName_ = withoutI(className_) + "Mock";

    if (src_) {
        *header_ << "class " << mockClassName_ << " : public "
                 << config_.singletonClass << "<" << mockClassName_ << ">\n{\npublic:\n"
                 << config_.tab << mockClassName_ << "() : " << config_.singletonClass << "<" << mockClassName_ << ">(*this) {}\n\n";
    } else {
        *header_ << "class " << mockClassName_ << " : public "
                 << className_ << "\n{\npublic:\n";
    }

    const auto result = RecursiveASTVisitor::TraverseCXXRecordDecl(cl);

    *header_ << "}; // class " << mockClassName_ << "\n\n";

    return result;
}

bool Mocker::WalkUpFromCXXMethodDecl(clang::CXXMethodDecl* method)
{
    return VisitCXXMethodDecl(method);
}

bool Mocker::VisitCXXMethodDecl(clang::CXXMethodDecl* method)
{
    stage_ = Stage::FUNCTIONS;
    if (src_ && method->isStatic()) {
        *header_ << config_.tab;
        printMockFunction_(*header_, method);
        printFunctionDefinition(*src_, mockClassName_, method);
    } else {
        if (method->isVirtual() && !clang::CXXDestructorDecl::classof(method)) {
            *header_ << config_.tab;
            printMockFunction_(*header_, method);
        }
    }
    return true;
}

bool Mocker::VisitFunctionDecl(clang::FunctionDecl *function)
{
    if (!src_) {
        return true;
    }

    if (Stage::INCLUDES == stage_ || Stage::NAMESPACES == stage_)
    {
        *header_ << "\n";
        *src_ << "\n";
    }
    stage_ = Stage::FUNCTIONS;

    *header_ << config_.tab;
    printMockFunction_(*header_, function);
    printFunctionDefinition(*src_, mockClassName_, function);
    return true;
}
