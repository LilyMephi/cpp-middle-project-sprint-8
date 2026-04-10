#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include "RefactorTool.h"
#include <iostream>
#include <unordered_set>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;

    if (const auto *Record = Result.Nodes.getNodeAs<CXXRecordDecl>("derivedClass")) {
        handle_derived_class(Record, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("methodDecl")) {
        if (Method->size_overridden_methods() > 0 && !Method->hasAttr<OverrideAttr>()) {
            handle_miss_override(Method, Diag, SM);
        }
    }

    if (const auto *ForRange = Result.Nodes.getNodeAs<CXXForRangeStmt>("forRange")) {
        handle_crange_for(ForRange, Diag, SM);
    }
}

void RefactorHandler::handle_derived_class(const CXXRecordDecl *Derived, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!Derived->hasDefinition())
        return;
    if (SM.isInSystemHeader(Derived->getLocation()))
        return;

    for (const auto &Base : Derived->bases()) {
        const CXXRecordDecl *BaseRD = Base.getType()->getAsCXXRecordDecl();
        if (!BaseRD)
            continue;

        const CXXDestructorDecl *BaseDtor = BaseRD->getDestructor();
        if (!BaseDtor || BaseDtor->isVirtual())
            continue;

        if (BaseDtor->hasAttr<OverrideAttr>() || BaseDtor->hasAttr<FinalAttr>())
            continue;

        SourceLocation DtorLoc = BaseDtor->getLocation();
        if (!DtorLoc.isValid() || BaseDtor->isImplicit())
            continue;

        llvm::errs() << "Found: " << BaseRD->getNameAsString() << " needs virtual dtor\n";

        const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "base class needs virtual destructor");

        FullSourceLoc FullLoc(DtorLoc, SM);

        FixItHint FixIt = FixItHint::CreateInsertion(DtorLoc, "virtual ");
        this->Rewrite.InsertText(DtorLoc, "virtual ");
        Diag.Report(FullLoc, DiagID) << FixIt;
    }
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (SM.isInSystemHeader(Method->getLocation()))
        return;
    if (!Method->getTypeSourceInfo() || Method->hasAttr<OverrideAttr>() || Method->hasAttr<FinalAttr>())
        return;

    if (!Method->isVirtual() && !Method->size_overridden_methods())
        return;

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "missing override");

    FullSourceLoc FullLoc(Method->getLocation(), SM);

    TypeSourceInfo *TypeInfo = Method->getTypeSourceInfo();
    if (!TypeInfo)
        return;

    SourceLocation NameEndLoc = TypeInfo->getTypeLoc().getEndLoc();
    FixItHint FixIt = FixItHint::CreateInsertion(NameEndLoc, "override");
    this->Rewrite.InsertTextAfterToken(NameEndLoc, " override");
    Diag.Report(FullLoc, DiagID) << FixIt;
}

void RefactorHandler::handle_crange_for(const CXXForRangeStmt *ForRange, DiagnosticsEngine &Diag, SourceManager &SM) {
    const VarDecl *LoopVar = ForRange->getLoopVariable();
    if (!LoopVar || SM.isInSystemHeader(LoopVar->getLocation()))
        return;

    QualType VarType = LoopVar->getType();
    if (VarType->isReferenceType() || VarType->isFundamentalType())
        return;

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "missing const ref in range-for");

    FullSourceLoc FullLoc(LoopVar->getLocation(), SM);

    std::string FixItStr = "&";

    TypeSourceInfo *TypeInfo = LoopVar->getTypeSourceInfo();
    if (!TypeInfo)
        return;

    SourceLocation InserLoc = ForRange->getLoopVariable()->getLocation().getLocWithOffset(-1);

    FixItHint FixIt = FixItHint::CreateInsertion(InserLoc, FixItStr);
    this->Rewrite.InsertTextBefore(InserLoc, FixItStr);
    Diag.Report(FullLoc, DiagID) << FixIt;
}

// todo: ниже необходимо реализовать матчеры для поиска узлов AST
// note: синтаксис написания матчеров точно такой же как и для использования clang-query
/*
    Пример того, как может выглядеть реализация:
    auto AllClassesMatcher()
    {
        return cxxRecordDecl().bind("classDecl");
    }
*/
auto DerivedClassMatcher() { return cxxRecordDecl().bind("derivedClass"); }

auto NoOverrideMatcher() { return cxxMethodDecl(hasParent(recordDecl())).bind("methodDecl"); }

auto NoRefConstVarInRangeLoopMatcher() {
    return cxxForRangeStmt(hasLoopVariable(varDecl(unless(hasType(referenceType()))))).bind("forRange");
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Rewrite(Rewrite), Handler(Rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    Finder.addMatcher(DerivedClassMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) { Finder.matchAST(Context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    // Инициализируем Rewriter для рефакторинга.
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    llvm::errs() << "EditBuffer size: "
                 << RewriterForCodeRefactor.getEditBuffer(RewriterForCodeRefactor.getSourceMgr().getMainFileID()).size()
                 << "\n";
    bool result = RewriterForCodeRefactor.overwriteChangedFiles();
    llvm::errs() << "overwriteChangedFiles() returned: " << result << "\n";
    if (!result) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();
    // Создаем ClangTool
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}