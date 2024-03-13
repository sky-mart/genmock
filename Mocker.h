#ifndef MOCKER_H
#define MOCKER_H

#include "clang/AST/RecursiveASTVisitor.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace clang {

class QualType;
class FunctionDecl;

} // namespace clang

namespace llvm {

class raw_ostream;
class raw_fd_ostream;

} // namespace llvm

class Mocker : public clang::RecursiveASTVisitor<Mocker>
{
public:
    enum class GmockStyle {
        Old,
        New
    };

    struct Config {
        explicit Config(const nlohmann::json& cfg);

        std::string tab;
        std::string singletonPath;
        std::string singletonClass;
        GmockStyle gmockStyle;
    };

    Mocker(Config config, std::string inputFilePath, std::string outputHeaderPath, std::string outputSourcePath="");

    bool TraverseDecl(clang::Decl* D);
    bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl *tu);
    bool TraverseLinkageSpecDecl(clang::LinkageSpecDecl *linkageSpec);
    bool TraverseNamespaceDecl(clang::NamespaceDecl* ns);
    bool TraverseCXXRecordDecl(clang::CXXRecordDecl* cl);

    bool WalkUpFromCXXMethodDecl(clang::CXXMethodDecl* method);
    bool VisitCXXMethodDecl(clang::CXXMethodDecl* method);
    bool VisitFunctionDecl(clang::FunctionDecl* function);

private:
    enum class Stage {
        INCLUDES,
        NAMESPACES,
        CLASS,
        FUNCTIONS
    };

    using PrintMockFunction = std::function<void(llvm::raw_ostream&, const clang::FunctionDecl*)>;

    const Config config_;
    const std::string inputFilePath_;
    const std::string relativeInputPath_;
    const std::string outputHeaderPath_;
    const std::string outputSourcePath_;

    std::unique_ptr<llvm::raw_fd_ostream> header_;
    std::unique_ptr<llvm::raw_fd_ostream> src_;
    Stage stage_{Stage::INCLUDES};
    std::string className_;
    std::string mockClassName_;
    PrintMockFunction printMockFunction_;
};

#endif /* MOCKER_H */
