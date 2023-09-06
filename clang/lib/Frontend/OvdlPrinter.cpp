#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/OvdlFilter.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/OverloadCallback.h"
#include "clang/Sema/Sema.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/YAMLTraits.h"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
using namespace clang;

namespace {
CodeCompleteConsumer *GetCodeCompletionConsumer_(CompilerInstance &CI) {
  return CI.hasCodeCompletionConsumer() ? &CI.getCodeCompletionConsumer()
                                        : nullptr;
}

void EnsureSemaIsCreated_(CompilerInstance &CI, FrontendAction &Action) {
  if (Action.hasCodeCompletionSupport() &&
      !CI.getFrontendOpts().CodeCompletionAt.FileName.empty())
    CI.createCodeCompletionConsumer();

  if (!CI.hasSema())
    CI.createSema(Action.getTranslationUnitKind(),
                  GetCodeCompletionConsumer_(CI));
}
} // namespace

namespace {
struct OvdlCandEntry{
  std::string declLocation;
  std::string nameSignature;
  std::optional<std::string> templateSource;
  //std::string Concepts; ???
  bool builtIn=0;
  bool Viable;
  static bool extraInfoHidden;
};
bool OvdlCandEntry::extraInfoHidden=true;
struct OvdlCompareEntry {
  OvdlCandEntry C1,C2;
  std::string reason;
  bool C1Better;
  // enum Better{C1,C2,undefined}; ???
  // Better better;
};

struct OvdlResEntry{
  std::vector<OvdlCandEntry> viableCandidates,nonViableCandidates;
  std::optional<OvdlCandEntry> best;
  std::optional<OvdlCandEntry> problem;
  std::vector<OvdlCompareEntry> compares;
  std::string callLocation;
  std::string callSignature;
  std::deque<std::string> callTypes;
  //static bool extraInfoHidden;
  static bool showCompares;
  static bool showNonViable;
};
//bool OvdlResEntry::extraInfoHidden=1;
bool OvdlResEntry::showCompares=true;
bool OvdlResEntry::showNonViable=true;
struct OvdlResNode{
  OvdlResEntry Entry;
  size_t line;
  std::string Fname;
};
class OvdlResCont{
  std::vector<OvdlResNode> data;
public:
  OvdlFilterSettings filterSettings;
  using iterator=std::vector<OvdlResNode>::iterator;
  iterator begin() { return data.begin();}
  iterator end() { return data.end();}
  OvdlResCont filterByLine(int lineBeg=-1, int lineEnd=-1) const{
    if (lineEnd==-1) lineEnd=lineBeg+1;
    if (lineBeg==-1) return *this; 
    return filterByFun([lineBeg,lineEnd](OvdlResNode& x){
      return !((unsigned)lineBeg<= x.line && (unsigned)lineEnd> x.line);
    });
  }
  OvdlResCont filter() const{
    return filterByFun([this](const OvdlResNode& x){
      return !(x.line>=filterSettings.lineFrom && x.line<filterSettings.lineTo &&
        (filterSettings.Fname=="" || filterSettings.Fname==x.Fname) && 
        (filterSettings.printEmpty || !x.Entry.viableCandidates.empty()));
    });
  }
  template<class Callable>
  OvdlResCont filterByFun(Callable Fun) const{
    OvdlResCont res(*this);
    res.data.erase(std::remove_if(res.data.begin(),res.data.end(),Fun),res.data.end());
    return res;
  }

  void add(const OvdlResNode& newnode){
    data.push_back(newnode);
  }
};
} // namespace
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(OvdlCandEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvdlCompareEntry);
namespace llvm{
namespace yaml{
template <> struct MappingTraits<BetterOverloadCandidateReason>{
  static void ScalarEnumerationTraits(IO& io, BetterOverloadCandidateReason& val){
      io.enumCase(val,"viability",viability);
      io.enumCase(val,"betterConversion",betterConversion);
      io.enumCase(val,"betterImplicitConversion",betterImplicitConversion);
      io.enumCase(val,"constructor",constructor);
      io.enumCase(val,"isSpecialization",isSpecialization);
      io.enumCase(val,"moreSpecialized",moreSpecialized);
      io.enumCase(val,"isInherited",isInherited);
      io.enumCase(val,"derivedFromOther",derivedFromOther);
      io.enumCase(val,"RewriteKind",RewriteKind);
      io.enumCase(val,"guideImplicit",guideImplicit);
      io.enumCase(val,"guideCopy",guideCopy);
      io.enumCase(val,"enableIf",enableIf);
      io.enumCase(val,"parameterObjectSize",parameterObjectSize);
      io.enumCase(val,"multiversion",multiversion);
      io.enumCase(val,"CUDApreference",CUDApreference);
      io.enumCase(val,"addressSpace",addressSpace);
      io.enumCase(val,"inconclusive",inconclusive);
  }
};
template <> struct MappingTraits<OvdlCandEntry>{
  static void mapping(IO& io, OvdlCandEntry& fields){
    io.mapRequired("Name and signature",fields.nameSignature);
    io.mapRequired("declLocation",fields.declLocation);
    if (fields.templateSource){
      io.mapOptional("templateSource", *fields.templateSource);
    }
    if (!OvdlCandEntry::extraInfoHidden){
      io.mapOptional("Viable", fields.Viable);
      io.mapOptional("Builtin", fields.builtIn);
    }
  }
};
template <>
struct SequenceTraits<std::deque<std::string>> {
    static size_t size(IO &io, std::deque<std::string> &list) { return list.size(); }
      static std::string &element(IO &io, std::deque<std::string> &list, size_t index) { return list[index]; }
  static const bool flow = true;
};
template <> struct MappingTraits<OvdlCompareEntry>{
  static void mapping(IO& io, OvdlCompareEntry& fields){
    io.mapRequired("C1",fields.C1);
    io.mapRequired("C2",fields.C2);
    io.mapRequired("C1Better",fields.C1Better);
    io.mapRequired("reason",fields.reason);
  }
};
template <> struct MappingTraits<OvdlResEntry>{
  static void mapping(IO& io, OvdlResEntry& fields){
    //std::string strcands;
    //for (const auto& c:fields.candidates){}
    io.mapRequired("callLocation",fields.callLocation);
    io.mapRequired("callSignature",fields.callSignature);
    io.mapRequired("callTypes",fields.callTypes);
    io.mapRequired("viable candidates",fields.viableCandidates);
    if (fields.showNonViable)
      io.mapRequired("non viable candidates",fields.nonViableCandidates);
    if (fields.best)
      io.mapOptional("best",*fields.best);
    if (fields.problem)
      io.mapOptional("problem",*fields.problem);
    if (OvdlResEntry::showCompares)
      io.mapOptional("compares",fields.compares);
  }
};

}//namespace yaml
}//namespace llvm

namespace{
std::string toString(BetterOverloadCandidateReason r){
  switch(r){
    case viability:
    return "viability";
  case betterConversion:
    return "betterConversion";
  case betterImplicitConversion:
    return "betterImplicitConversion";
  case constructor:
    return "constructor";
  case isSpecialization:
    return "isSpecialization";
  case moreSpecialized:
    return "moreSpecialized";
  case isInherited:
    return "isInherited";
  case derivedFromOther:
    return "derivedFromOther";
  case RewriteKind:
    return "RewriteKind";
  case guideImplicit:
    return "guideImplicit";
  case guideCopy:
    return "guideCopy";
  case enableIf:
    return "enableIf";
  case parameterObjectSize:
    return "parameterObjectSize";
  case multiversion:
    return "multiversion";
  case CUDApreference:
    return "CUDApreference";
  case addressSpace:
    return "addressSpace";
  case inconclusive:
    return "inconclusive";
  };
  return "";//UNREACHABLE
};
void displayOvdlResEntry(llvm::raw_ostream& Out,OvdlResEntry& Entry){
  std::string YAML;
  {
    llvm::raw_string_ostream OS(YAML);
    llvm::yaml::Output YO(OS);
    llvm::yaml::EmptyContext Context;
    llvm::yaml::yamlize(YO,Entry,true,Context);
  }
  Out<<"---"<<YAML<<"\n";
}


class DefaultOverloadInstCallback:public OverloadCallback{
  const Sema* S=nullptr;
  bool inBestOC=0;
  const OverloadCandidateSet* Set=nullptr;
  const SourceLocation* Loc=nullptr;
  std::vector<OvdlCompareEntry> compares;
  OvdlResCont cont;
public:
  virtual void initialize(const Sema&) override{};
  virtual void finalize(const Sema&) override{};
  virtual void atEnd() override{
    std::string message;
    bool exiting=0;
    std::string commangs="exit/q; Fname/FN string; showEmpty/sE bool; lineFrom/lF uint; lineTo/lT uint; showCompares/sC bool; showNonViable/sNV bool; list/l\n";
    do {
      llvm::outs()<<"What filter do you want to do? (help to show commands)\n";
      std::cin>>message;
      if (message=="exit"||message=="q"){
        exiting=1;
      }else if (message=="Fname"|| message=="FN"){
        std::cin>>cont.filterSettings.Fname;
      }else if (message=="showEmpty"||message=="sE"){
        std::cin>>cont.filterSettings.printEmpty;
      } else if (message=="lineFrom"||message=="lF"){
        std::cin>>cont.filterSettings.lineFrom;
      } else if (message=="lineTo"||message=="lT"){
        std::cin>>cont.filterSettings.lineTo;
      } else if (message=="showCompares"||message=="sC"){
        std::cin>>OvdlResEntry::showCompares;
      } else if (message=="showNonViable"||message=="sNV"){
        std::cin>>OvdlResEntry::showNonViable;
      }else if (message=="list"||message=="l"){
        for (auto& x:cont.filter()){
          displayOvdlResEntry(llvm::outs(),x.Entry);
        }
      }else if (message=="help"){
        llvm::outs()<<commangs;
      }else{
        llvm::outs()<<"invalid\n";
      }
    }while (!exiting);
  }
  virtual void atOverloadBegin(const Sema&s,const SourceLocation& loc,const OverloadCandidateSet& set) override{
    S=&s;
    Set=&set;
    Loc=&loc;
    inBestOC=1;
  }
  virtual void atOverloadEnd(const Sema&s,const SourceLocation& loc,
        const OverloadCandidateSet& set, OverloadingResult res, 
        const OverloadCandidate* BestOrProblem) override{
    //TOFIX
    OvdlResNode node;
    node.Entry=getResEntry(res,BestOrProblem);
    node.Entry.compares=std::move(compares);
    compares={};
    node.line=S->getSourceManager().getPresumedLoc(loc).getLine();
    node.Fname=S->getSourceManager().getPresumedLoc(loc).getFilename();
    cont.add(node);
    inBestOC=0;
  }
  virtual void atCompareOverloadBegin(const Sema& TheSema,const SourceLocation& Loc,
        const OverloadCandidate &Cand1, const OverloadCandidate &Cand2)override{
  }
  virtual void atCompareOverloadEnd(const Sema& TheSema,const SourceLocation& Loc,
        const OverloadCandidate &Cand1, const OverloadCandidate &Cand2, bool res,BetterOverloadCandidateReason reason) override {
    OvdlCompareEntry Entry;
    Entry.C1Better=res;
    Entry.C1=getCandEntry(Cand1);
    Entry.C2=getCandEntry(Cand2);
    Entry.reason=toString(reason);
    compares.push_back(Entry);
  }
  OvdlCandEntry getCandEntry(const OverloadCandidate& C){
    OvdlCandEntry res;
    res.Viable=C.Viable;
    if (C.IsSurrogate){
      if (C.FoundDecl.getDecl()!=0){
        res.nameSignature=C.FoundDecl.getDecl()->getQualifiedNameAsString();
      } 
      res.declLocation="Surrogate ";
      res.declLocation+=C.Surrogate->getLocation().printToString(S->SourceMgr);
      return res;
    }

    if (C.FoundDecl.getDecl()==0) {
      res.declLocation="Built-in";
      res.builtIn=1;
      for (const auto& tmp:C.BuiltinParamTypes){
        if (tmp.getAsString()!="NULL TYPE")
          res.nameSignature+=tmp.getAsString()+" ";
      }
      return res;
    }
    res.declLocation=C.FoundDecl.getDecl()->getLocation().printToString(S->SourceMgr);
    //res.signature=C->FoundDecl.dumpSignature(0);//.getDecl();//->getType();
    res.nameSignature=C.FoundDecl.getDecl()->getQualifiedNameAsString();
    if (C.Function){
      res.nameSignature+=" - "+getSignature(C);
      res.templateSource=getTemplate(C);
      if (res.templateSource=="") res.templateSource={};
      //res.nameSignature+=" - "+getSignature(*C.Function);
    }else{
      llvm::errs()<<"\n"<<res.nameSignature<<"";
      llvm::errs()<<"\n"<<res.declLocation<<"\n";
      /*TODO can't reach??? */
    }
    return res;
  }
  std::string getTemplate(const OverloadCandidate& C){
    const FunctionDecl* f=C.FoundDecl->getAsFunction();
    if (!f->isTemplated() ) return "";
    llvm::SmallVector<const Expr *> AC;
    SourceRange r=f->getDescribedFunctionTemplate()->getSourceRange();
    r.setEnd(f->getTypeSpecEndLoc());
    const auto* t=f->getDescribedTemplate();
    SourceLocation l0;
    if (t) {
      t->getAssociatedConstraints(AC);
      if (AC.size()){
        l0=AC.back()->getEndLoc();
      }
    }
    if (l0>r.getEnd())
      r.setEnd(l0);
    SourceLocation endloc(Lexer::getLocForEndOfToken(r.getEnd(), 0,S->getSourceManager() , S->getLangOpts()));
    CharSourceRange range=CharSourceRange::getCharRange(r.getBegin(),endloc);
    return std::string(Lexer::getSourceText(range, S->getSourceManager(), S->getLangOpts()));
  }
  std::string getSignature(const OverloadCandidate& C) const{
    const FunctionDecl* f=C.Function;
    return f->getType().getAsString();
  }
  OvdlResEntry getResEntry(OverloadingResult ovres, const OverloadCandidate* BestOrProblem){
    OvdlResEntry res;

    if (ovres==OR_Success) {
      res.best=getCandEntry(*BestOrProblem);
      res.problem={};
    }else{
      res.best={};
      if (BestOrProblem)
        res.problem=getCandEntry(*BestOrProblem);
    }
    res.callLocation=Loc->printToString(S->SourceMgr);
    SourceLocation endloc(Lexer::getLocForEndOfToken(*Loc, 0,S->getSourceManager() , S->getLangOpts()));
    CharSourceRange range;
    ArrayRef<Expr*> Args=Set->getArgs();
    SourceLocation endloc02=Set->getEndLoc();
    //SourceRange objParamRange=Set->getObjectParamRange();
    SourceLocation begloc=*Loc;
    if (!Args.empty() && Args[0]->getBeginLoc()<begloc){
      begloc=Args[0]->getBeginLoc();
    }
    /*if (objParamRange!=SourceRange() && objParamRange.getBegin()<begloc){
      begloc=objParamRange.getBegin();
    }*/
    if (!Args.empty()){
      SourceLocation endloc1(Lexer::getLocForEndOfToken(Args.back()->getEndLoc(), 0,S->getSourceManager() , S->getLangOpts()));
      if (endloc<endloc1) endloc=endloc1;
    }
    if (endloc02!=SourceLocation()){
      SourceLocation endloc2(Lexer::getLocForEndOfToken(endloc02, 0,S->getSourceManager() , S->getLangOpts()));
      if (endloc<endloc2) endloc=endloc2;
    }
    range=CharSourceRange::getCharRange(begloc,endloc);
    res.callSignature=Lexer::getSourceText(range, S->getSourceManager(), S->getLangOpts());
    /*
    if (Args.empty() && SourceRange()==objParamRange){
      res.callSignature="(FormLocation) ";
      range=CharSourceRange::getCharRange(*Loc,endloc);
      res.callSignature+=Lexer::getSourceText(range, S->getSourceManager(), S->getLangOpts());
    }else{
      res.callSignature="";
      if (objParamRange!=SourceRange()){
        res.callSignature="this=";
        SourceLocation endloc(Lexer::getLocForEndOfToken(objParamRange.getEnd(), 0,S->getSourceManager() , S->getLangOpts()));
        CharSourceRange cr=CharSourceRange::getCharRange(objParamRange.getBegin(),endloc);
        res.callSignature+=Lexer::getSourceText(cr, S->getSourceManager(), S->getLangOpts());
        res.callSignature+="; ";
      }
      if (!Args.empty()){
        SourceLocation endloc(Lexer::getLocForEndOfToken(Args.back()->getSourceRange().getEnd(), 0,S->getSourceManager() , S->getLangOpts()));
        range=CharSourceRange::getCharRange(Args[0]->getSourceRange().getBegin(),endloc);
        res.callSignature+=Lexer::getSourceText(range, S->getSourceManager(), S->getLangOpts());
      }//range=CharSourceRange::getCharRange(Set->getSourceRange().getBegin(),Set->getSourceRange().getEnd());
    }*/
    if (Set->getObjectParamType()!=QualType()) {
      res.callTypes.push_back("(ObjectParam:"+Set->getObjectParamType().getAsString()+")");
    }
    for (const auto& x:Args){
      res.callTypes.push_back(x->getType().getAsString());
    }
    S->getSourceManager();
    for (const auto& cand:*Set){
      if (cand.Viable){
        res.viableCandidates.push_back(getCandEntry(cand));
        /*if (res.callTypes.empty()){
          for (const auto&x: cand.Conversions){
            if (x.isInitialized()==false){
              if (!BestOrProblem->IgnoreObjectArgument){
                //I'm 95% sure this is unreachable
                llvm::errs()<<"This was not supposed to happen\n uninitialized concersion not for ignored object argument";
              }
              res.callTypes.push_back({"(Ignored)"});
              //llvm::outs()<<"Not inited "+res.best->nameSignature;
              continue;
            }
            switch (x.getKind()) {
            case ImplicitConversionSequence::StandardConversion:
              res.callTypes.emplace_back(x.Standard.getFromType().getAsString());
              break;
            case ImplicitConversionSequence::StaticObjectArgumentConversion:
              res.callTypes.push_back({"(Ignored)"});
              break;
            case ImplicitConversionSequence::UserDefinedConversion:
              res.callTypes.push_back(x.UserDefined.Before.getFromType().getAsString());
              break;
            case ImplicitConversionSequence::AmbiguousConversion:
              res.callTypes.push_back({"AmbigiusConversion"});
              break;
            case ImplicitConversionSequence::EllipsisConversion:
              res.callTypes.push_back({"EllipsisConversion"});
              break;
            case ImplicitConversionSequence::BadConversion:
              res.callTypes.push_back({"BadConversion"});
              break;
            }
          }
        }*/
      }
      else
        res.nonViableCandidates.push_back(getCandEntry(cand));
    }
    return res;
  }
  };
}//namespace
std::unique_ptr<ASTConsumer>
OvdlDumpAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
    return std::make_unique<ASTConsumer>();
}
void OvdlDumpAction::ExecuteAction(){

  CompilerInstance &CI = getCompilerInstance();
  EnsureSemaIsCreated_(CI, *this);
  CI.getSema().OverloadCallbacks.push_back(
      std::make_unique<DefaultOverloadInstCallback>());
  ASTFrontendAction::ExecuteAction();
}


