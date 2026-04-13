#include <clang/AST/ASTContext.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/TextDiagnosticBuffer.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/Tooling.h>
#include <gtest/gtest.h>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

class RefactorHandlerTest : public ::testing::Test {
protected:
    Rewriter Rewrite;
    RefactorHandler Handler;

    RefactorHandlerTest() : Handler(Rewrite) {}

    std::unique_ptr<ASTUnit> parseCode(const std::string &code) {
        std::vector<std::string> args = {"-std=c++17", "-fsyntax-only"};
        return buildASTFromCodeWithArgs(code, args);
    }

    const CXXRecordDecl *findDerivedClass(ASTUnit &AST, const std::string &name) {
        ASTContext &Ctx = AST.getASTContext();

        class Callback : public MatchFinder::MatchCallback {
        public:
            const CXXRecordDecl *Result = nullptr;
            void run(const MatchFinder::MatchResult &Result) override {
                this->Result = Result.Nodes.getNodeAs<CXXRecordDecl>("derivedClass");
            }
        } Callback;

        MatchFinder Finder;
        Finder.addMatcher(cxxRecordDecl(hasName(name)).bind("derivedClass"), &Callback);
        Finder.matchAST(Ctx);

        return Callback.Result;
    }

    const CXXMethodDecl *findMethod(ASTUnit &AST, const std::string &name) {
        ASTContext &Ctx = AST.getASTContext();

        class Callback : public MatchFinder::MatchCallback {
        public:
            const CXXMethodDecl *Result = nullptr;
            void run(const MatchFinder::MatchResult &Result) override {
                this->Result = Result.Nodes.getNodeAs<CXXMethodDecl>("methodDecl");
            }
        } Callback;

        MatchFinder Finder;
        Finder.addMatcher(cxxMethodDecl(hasName(name)).bind("methodDecl"), &Callback);
        Finder.matchAST(Ctx);

        return Callback.Result;
    }

    const CXXForRangeStmt *findForRange(ASTUnit &AST) {
        ASTContext &Ctx = AST.getASTContext();

        class Callback : public MatchFinder::MatchCallback {
        public:
            const CXXForRangeStmt *Result = nullptr;
            void run(const MatchFinder::MatchResult &Result) override {
                this->Result = Result.Nodes.getNodeAs<CXXForRangeStmt>("forRange");
            }
        } Callback;

        MatchFinder Finder;
        Finder.addMatcher(cxxForRangeStmt().bind("forRange"), &Callback);
        Finder.matchAST(Ctx);

        return Callback.Result;
    }
};

TEST_F(RefactorHandlerTest, HandleDerivedClass_NonVirtualDtor) {
    auto AST = parseCode(R"(
        class Base {
        public:
            ~Base() {}
        };
        class Derived : public Base {
        };
    )");
    ASSERT_NE(AST, nullptr);

    const CXXRecordDecl *Derived = findDerivedClass(*AST, "Derived");
    ASSERT_NE(Derived, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Handler.handle_derived_class(Derived, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleDerivedClass_VirtualDtorAlreadyPresent) {
    auto AST = parseCode(R"(
        class Base {
        public:
            virtual ~Base() {}
        };
        class Derived : public Base {
        };
    )");
    ASSERT_NE(AST, nullptr);

    const CXXRecordDecl *Derived = findDerivedClass(*AST, "Derived");
    ASSERT_NE(Derived, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Handler.handle_derived_class(Derived, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleDerivedClass_FinalAttrPresent) {
    auto AST = parseCode(R"(
        class Base {
        public:
            virtual ~Base() {}
        };
        class Derived : public Base {
        };
    )");
    ASSERT_NE(AST, nullptr);

    const CXXRecordDecl *Derived = findDerivedClass(*AST, "Derived");
    ASSERT_NE(Derived, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Handler.handle_derived_class(Derived, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleMissOverride_MissingOverride) {
    auto AST = parseCode(R"(
        class Base {
        public:
            virtual void foo() {}
        };
        class Derived : public Base {
            void foo() {}
        };
    )");
    ASSERT_NE(AST, nullptr);

    const CXXMethodDecl *Method = findMethod(*AST, "foo");
    ASSERT_NE(Method, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Diag.setClient(new TextDiagnosticBuffer());

    Handler.handle_miss_override(Method, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleMissOverride_OverrideAlreadyPresent) {
    auto AST = parseCode(R"(
        class Base {
        public:
            virtual void foo() {}
        };
        class Derived : public Base {
            void foo() override {}
        };
    )");
    ASSERT_NE(AST, nullptr);

    const CXXMethodDecl *Method = findMethod(*AST, "foo");
    ASSERT_NE(Method, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Diag.setClient(new TextDiagnosticBuffer());

    Handler.handle_miss_override(Method, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleMissOverride_NonVirtualNoOverride) {
    auto AST = parseCode(R"(
        class Base {
        public:
            void foo() {}
        };
    )");
    ASSERT_NE(AST, nullptr);

    const CXXMethodDecl *Method = findMethod(*AST, "foo");
    ASSERT_NE(Method, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Diag.setClient(new TextDiagnosticBuffer());

    Handler.handle_miss_override(Method, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleCRangeFor_NonConstRef) {
    auto AST = parseCode(R"(
        #include <vector>
        void test() {
            std::vector<int> v;
            for (int x : v) {}
        }
    )");
    ASSERT_NE(AST, nullptr);

    const CXXForRangeStmt *ForRange = findForRange(*AST);
    ASSERT_NE(ForRange, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Diag.setClient(new TextDiagnosticBuffer());

    Handler.handle_crange_for(ForRange, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleCRangeFor_AlreadyConstRef) {
    auto AST = parseCode(R"(
        #include <vector>
        void test() {
            std::vector<int> v;
            for (const int& x : v) {}
        }
    )");
    ASSERT_NE(AST, nullptr);

    const CXXForRangeStmt *ForRange = findForRange(*AST);
    ASSERT_NE(ForRange, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Diag.setClient(new TextDiagnosticBuffer());

    Handler.handle_crange_for(ForRange, Diag, SM);
}

TEST_F(RefactorHandlerTest, HandleCRangeFor_FundamentalType) {
    auto AST = parseCode(R"(
        void test() {
            int arr[5] = {1,2,3,4,5};
            for (int x : arr) {}
        }
    )");
    ASSERT_NE(AST, nullptr);

    const CXXForRangeStmt *ForRange = findForRange(*AST);
    ASSERT_NE(ForRange, nullptr);

    ASTContext &Ctx = AST->getASTContext();
    DiagnosticsEngine &Diag = Ctx.getDiagnostics();
    SourceManager &SM = Ctx.getSourceManager();

    Rewrite.setSourceMgr(SM, AST->getLangOpts());

    Diag.setClient(new TextDiagnosticBuffer());

    Handler.handle_crange_for(ForRange, Diag, SM);
}
