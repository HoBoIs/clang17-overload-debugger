#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/OvInsEnumPrints.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/OverloadCallback.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <iterator>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
using namespace clang;

namespace {
CodeCompleteConsumer *GetCodeCompletionConsumer(CompilerInstance &CI) {
  return CI.hasCodeCompletionConsumer() ? &CI.getCodeCompletionConsumer()
                                        : nullptr;
}

void EnsureSemaIsCreated(CompilerInstance &CI, FrontendAction &Action) {
  if (Action.hasCodeCompletionSupport() &&
      !CI.getFrontendOpts().CodeCompletionAt.FileName.empty())
    CI.createCodeCompletionConsumer();

  if (!CI.hasSema())
    CI.createSema(Action.getTranslationUnitKind(),
                  GetCodeCompletionConsumer(CI));
}
} // namespace

namespace {
struct OvInsSource {
  SourceRange range;
  SourceLocation Loc;
  std::string source;
  std::string sourceLoc;
  std::pair<std::string, std::string> rangeBegEnd;
  bool operator==(const OvInsSource &o) const {
    return range == o.range && source == o.source && Loc == o.Loc &&
           sourceLoc == o.sourceLoc;
  };
};
struct OvInsConvEntry {
  std::string convPath, convInfo;
  std::string kind;
  OvInsSource src; // Add to yaml?
  bool operator==(const OvInsConvEntry &o) const {
    return convPath == o.convPath && convInfo == o.convInfo && kind == o.kind;
  }
};
struct OvInsTemplateSpec {
  OvInsSource src;
  bool isExact;
};
struct OvInsCandEntry {
  // std::string declLocation;
  // clang::SourceLocation declLoc;
  std::string name;
  std::string usingLocation;
  clang::SourceLocation usingLoc;
  OverloadCandidateRewriteKind rewriteKind;
  std::deque<std::string> paramTypes;
  OvInsSource src;
  std::vector<OvInsTemplateSpec> templateSpecs;
  std::optional<OverloadFailureKind> failKind;
  std::optional<std::string> extraFailInfo;
  std::vector<OvInsConvEntry> Conversions;
  std::deque<std::string> templateParams;
  bool isSurrogate = false;
  bool isBestOrProblem;
  bool operator==(const OvInsCandEntry &o) const {
    return name == o.name && src == o.src && paramTypes == o.paramTypes &&
           src == o.src && failKind == o.failKind &&
           extraFailInfo == o.extraFailInfo && Conversions == o.Conversions;
  }
};
struct OvInsConvCmpEntry {
  std::string P1, cmpRes, P2;
  std::optional<int> place;
  void print(llvm::raw_string_ostream &os) const {
    os << P1 << '\t' << cmpRes << '\t' << P2;
    if (place)
      os << "\tPlace:" << *place;
  }
  std::string printToString() const {
    std::string res;
    llvm::raw_string_ostream os(res);
    print(os);
    return res;
  }
  bool operator==(const OvInsConvCmpEntry &o) const {
    return P1 == o.P1 && P2 == o.P2 && cmpRes == o.cmpRes;
  }
};
struct OvInsCompareEntry {
  OvInsCandEntry C1, C2;
  std::optional<OvInsConvCmpEntry> deciderConversion;
  std::optional<OvInsConvCmpEntry> opposerConversion;
  std::vector<OvInsConvCmpEntry> conversionCompares;
  long passedTime;
  BetterOverloadCandidateReason reason;
  bool C1Better;
  bool operator==(const OvInsCompareEntry &o) const {
    return C1 == o.C1 && C2 == o.C2 && reason == o.reason &&
           C1Better == o.C1Better && deciderConversion == o.deciderConversion &&
           conversionCompares == o.conversionCompares &&
           opposerConversion == o.opposerConversion;
  }
};

struct OvInsResEntry {
  std::vector<OvInsCandEntry> viableCandidates, nonViableCandidates;
  std::optional<OvInsCandEntry> best;
  std::vector<OvInsCandEntry> problems;
  std::vector<OvInsCompareEntry> compares;
  OvInsSource callSrc;
  std::deque<std::string> callTypes;
  std::string note;
  clang::OverloadingResult ovRes;
  bool isImplicit = false;
  long passedTime;
  //std::vector<std::pair<std::string,long>> TimePoints;
  //std::chrono::time_point<std::chrono::steady_clock> lastTimePoint;
};
struct OvInsResNode {
  OvInsResEntry Entry;
  size_t line;
  std::string Fname;
};
class OvInsResCont {
  std::vector<OvInsResNode> data;

public:
  using iterator = std::vector<OvInsResNode>::iterator;
  using const_iterator = std::vector<OvInsResNode>::const_iterator;
  iterator begin() { return data.begin(); }
  iterator end() { return data.end(); }
  const_iterator begin() const { return data.begin(); }
  const_iterator end() const { return data.end(); }
  void add(const OvInsResNode &newnode) { data.push_back(newnode); }
};
} // namespace
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(OvInsCandEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvInsCompareEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvInsConvEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvInsConvCmpEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvInsTemplateSpec);
namespace llvm {
namespace yaml {

template <> struct MappingTraits<OvInsConvCmpEntry> {
  static void mapping(IO &io, OvInsConvCmpEntry &fields) {
    io.mapRequired("Conv1", fields.P1);
    io.mapRequired("CmpRes", fields.cmpRes);
    io.mapRequired("Conv2", fields.P2);
    io.mapOptional("Place", fields.place);
  }
};
template <> struct MappingTraits<OvInsConvEntry> {
  static void mapping(IO &io, OvInsConvEntry &fields) {
    io.mapRequired("kind", fields.kind);
    io.mapOptional("convPath", fields.convPath, "");
    io.mapOptional("convInfo", fields.convInfo, "");
  }
};
template <> struct MappingTraits<OvInsSource> {
  static void mapping(IO &io, OvInsSource &fields) {
    io.mapOptional("Begin", fields.rangeBegEnd.first, "");
    io.mapOptional("End", fields.rangeBegEnd.second, "");
    io.mapOptional("Code", fields.source, "");
    io.mapOptional("Location", fields.sourceLoc, fields.rangeBegEnd.first);
  }
};
template <> struct MappingTraits<OvInsTemplateSpec> {
  static void mapping(IO &io, OvInsTemplateSpec &fields) {
    io.mapRequired("decl", fields.src);
    io.mapRequired("IsExact", fields.isExact);
  }
};
template <> struct MappingTraits<OvInsCandEntry> {
  static void mapping(IO &io, OvInsCandEntry &fields) {
    io.mapRequired("Name", fields.name);
    io.mapOptional("RewriteInfo", fields.rewriteKind, CRK_None);
    io.mapOptional("TemplateParams", fields.templateParams);
    io.mapRequired("ParamTypes", fields.paramTypes);
    io.mapRequired("declaration", fields.src);
    io.mapOptional("usingLocation", fields.usingLocation, "");
    io.mapOptional("isSurrogate", fields.isSurrogate, false);
    io.mapOptional("templateSpecs", fields.templateSpecs);
    // io.mapOptional("templateSpecs (overloading is decided before
    // specialisation)", fields.templateSpecs);
    io.mapOptional("Conversions", fields.Conversions);
    io.mapOptional("FailureKind", fields.failKind);
    io.mapOptional("extraFailInfo", fields.extraFailInfo);
  }
};
template <> struct SequenceTraits<std::deque<std::string>> {
  static size_t size(IO &io, std::deque<std::string> &list) {
    return list.size();
  }
  static std::string &element(IO &io, std::deque<std::string> &list,
                              size_t index) {
    return list[index];
  }
  static const bool flow = true;
};
template <> struct MappingTraits<OvInsCompareEntry> {
  static void mapping(IO &io, OvInsCompareEntry &fields) {
    io.mapRequired("C1", fields.C1);
    io.mapRequired("C2", fields.C2);
    io.mapRequired("C1Better", fields.C1Better);
    io.mapRequired("reason", fields.reason);
    io.mapOptional("Conversions", fields.conversionCompares);
    io.mapOptional("Decider", fields.deciderConversion);
    io.mapOptional("Decider2", fields.opposerConversion);
    io.mapRequired("nanoseconds", fields.passedTime);
  }
};
template <> struct MappingTraits<OvInsResEntry> {
  static void mapping(IO &io, OvInsResEntry &fields) {
    io.mapRequired("the call", fields.callSrc);
    io.mapRequired("callTypes", fields.callTypes);
    io.mapRequired("overloading result", fields.ovRes);
    io.mapRequired("viable candidates", fields.viableCandidates);
    io.mapOptional("non viable candidates", fields.nonViableCandidates);
    io.mapOptional("best", fields.best);
    io.mapOptional("problems", fields.problems);
    io.mapOptional("compares", fields.compares);
    io.mapOptional("Implicit", fields.isImplicit, false);
    io.mapOptional("note", fields.note, "");
    io.mapOptional("nanoseconds", fields.passedTime, 0);
  }
};

} // namespace yaml
} // namespace llvm

namespace {
std::string getConversionSeq(const StandardConversionSequence &cs) {
  std::string res;
  llvm::raw_string_ostream os(res);
  cs.writeToStream(os);
  return res;
}
std::string getConversionSeq(const UserDefinedConversionSequence &cs) {
  std::string res;
  llvm::raw_string_ostream os(res);
  cs.writeToStream(os);
  return res;
}

const QualType getFromType(const ImplicitConversionSequence &C) {
  if (!C.isInitialized())
    return {};
  switch (C.getKind()) {
  case ImplicitConversionSequence::StandardConversion:
    return C.Standard.getFromType();
  case ImplicitConversionSequence::StaticObjectArgumentConversion:
    return {};
  case ImplicitConversionSequence::UserDefinedConversion:
    return C.UserDefined.Before.getFromType();
  case ImplicitConversionSequence::AmbiguousConversion:
    return C.Ambiguous.getFromType();
  case ImplicitConversionSequence::EllipsisConversion:
    return {};
  case ImplicitConversionSequence::BadConversion:
    return C.Bad.getFromType();
  }
  llvm_unreachable("Unknown ImplicitConversionSequence kind");
}

const QualType getToType(const ImplicitConversionSequence &C) {
  if (!C.isInitialized())
    return {};
  switch (C.getKind()) {
  case ImplicitConversionSequence::StandardConversion:
    return C.Standard.getToType(2);
  case ImplicitConversionSequence::StaticObjectArgumentConversion:
    return {};
  case ImplicitConversionSequence::UserDefinedConversion:
    return C.UserDefined.After.getToType(2);
  case ImplicitConversionSequence::AmbiguousConversion:
    return C.Ambiguous.getToType();
  case ImplicitConversionSequence::EllipsisConversion:
    return {};
  case ImplicitConversionSequence::BadConversion:
    return C.Bad.getToType();
  }
  llvm_unreachable("Unknown ImplicitConversionSequence kind");
}
QualType getToType(const OverloadCandidate &C, int idx) {
  if (C.isReversed())
    idx = 1 - idx;
  if (C.IsSurrogate && idx == 0)
    return C.Conversions[0].UserDefined.ConversionFunction->getReturnType();
  if (C.Function == nullptr)
    return getToType(C.Conversions[idx]);
  if (C.Conversions[idx].getKind() ==
      ImplicitConversionSequence::Kind::EllipsisConversion)
    return QualType{};
  // if (0&&isa<CXXConversionDecl>(C.Function))
  //   return C.FinalConversion.getToType(2);
  if (isa<CXXMethodDecl>(C.Function) && !isa<CXXConstructorDecl>(C.Function))
    --idx;
  // dyn_cast<CXXMethodDecl>(C.Function);
  if (idx == -1) {
    return getToType(C.Conversions[0]); // TODO:thistype
    // return llvm::dyn_cast<CXXMethodDecl>(C.Function)->getThisType();
  }
  return C.Function->parameters()[idx]->getType();
}
void displayOvInsResEntry(llvm::raw_ostream &Out, OvInsResEntry &Entry) {
  std::string YAML;
  {
    llvm::raw_string_ostream OS(YAML);
    llvm::yaml::Output YO(OS);
    llvm::yaml::EmptyContext Context;
    llvm::yaml::yamlize(YO, Entry, true, Context);
  }
  Out << "---" << YAML << "\n";
}
class DefaultOverloadInstCallback : public OverloadCallback {
  struct cmpInfo {
    const Sema *TheSema;
    SourceLocation Loc;
    BetterOverloadCandidateReason reason;
    const OverloadCandidate *Cand1;
    const OverloadCandidate *Cand2;
    bool C1Better;
    int infoIdx;
    long duration;
    bool sameCands(const cmpInfo &o) const {
      return Cand1 == o.Cand1 && Cand2 == o.Cand2;
    }
    bool mirroredCands(const cmpInfo &o) const {
      return Cand2 == o.Cand1 && Cand1 == o.Cand2;
    }
  };
  using CompareKind = clang::ImplicitConversionSequence::CompareKind;
  static constexpr llvm::StringLiteral Kinds[] = {"[temporary]", "", "&&"};
  const Sema *S = nullptr;
  bool inBestOC = false;
  bool inCompare = false;
  const OverloadCandidateSet *Set = nullptr;
  SourceLocation Loc = {};
  // std::vector<OvInsCompareEntry> compares;
  llvm::SmallVector<cmpInfo> compares;
  OvInsResCont cont;
  clang::FrontendOptions::OvInsSettingsType settings;
  std::vector<CompareKind> compareResults;
  struct SetArgs {
    const OverloadCandidateSet *Set;
    llvm::SmallVector<Expr *, 4> inArgs = {};
    const Expr *ObjectExpr = nullptr;
    SourceLocation EndLoc = {};
    SourceLocation Loc = {};
    bool valid = false;
    bool isImplicit = false;
  };
  std::chrono::time_point<std::chrono::steady_clock> ovStartTime;
  std::chrono::time_point<std::chrono::steady_clock> cmpStartTime;
  std::unordered_map<const OverloadCandidateSet *, SetArgs> SetArgMap;

public:
  virtual void addSetInfo(const OverloadCandidateSet &Set,
                          const SetInfo &S) override {
    SetArgMap[&Set].Set = &Set;
    if (SetArgMap[&Set].valid && SetArgMap[&Set].Loc != Set.getLocation()) {
      SetArgMap[&Set] = {}; // Reset because the info is outdated
      SetArgMap[&Set].Set = &Set;
    }
    SetArgMap[&Set].valid = true;
    if (S.Args)
      SetArgMap[&Set].inArgs = llvm::SmallVector<Expr *>(*S.Args);
    if (S.Args->size() == 1 && (*S.Args)[0] == nullptr) {
      // NEVER any more?
      // assert(0);
      SetArgMap[&Set].inArgs = llvm::SmallVector<Expr *>();
    }
    if (S.ObjectExpr)
      SetArgMap[&Set].ObjectExpr = *S.ObjectExpr;
    if (S.EndLoc)
      SetArgMap[&Set].EndLoc = *S.EndLoc;
    SetArgMap[&Set].Loc = Set.getLocation();
    if (S.isImplicit)
      SetArgMap[&Set].isImplicit = *S.isImplicit;
  };
  virtual bool needAllCompareInfo() const override {
    return settings.ShowConversions == clang::FrontendOptions::SC_Verbose &&
           inBestOC && settings.ShowCompares;
  };
  virtual void setCompareInfo(const std::vector<CompareKind> &c) override {
    if (needAllCompareInfo())
      compareResults = c;
  };
  void setSettings(const clang::FrontendOptions::OvInsSettingsType &s) {
    settings = s;
  }
  virtual void initialize(const Sema &) override{};
  virtual void finalize(const Sema &) override{};
  virtual void atEnd() override {
    if (settings.PrintYAML) {
      for (auto &x : cont)
        displayOvInsResEntry(llvm::outs(), x.Entry);
      llvm::outs() << "...\n";
    } else
      printHumanReadable();
    cont = {};
  }
  virtual void atOverloadBegin(const Sema &s, const SourceLocation &loc,
                               const OverloadCandidateSet &set) override {
    S = &s;
    Set = &set;
    Loc = loc;

    if (!SetArgMap[&set].valid || SetArgMap[&set].Loc != Set->getLocation())
      return;
    PresumedLoc L = S->getSourceManager().getPresumedLoc(loc);
    unsigned line = L.getLine();
    if ((L.getIncludeLoc().isValid() && !settings.ShowIncludes) ||
        !inSetInterval(line) || (!settings.ShowEmptyOverloads && set.empty()))
      return;
    if (SetArgMap.find(&set) == SetArgMap.end())
      return;
    if (getSetArgs().isImplicit && !settings.ShowImplicitConversions)
      return;
    // if (getSetArgs().inArgs.size()==0 &&
    // getSetArgs().ObjectExpr==nullptr)return; if (settings.FunName!="" &&
    // !set.empty() && nameEq(settings.FunName,set))
    //   return;
    compares.clear();
    inBestOC = true;
    /*llvm::sys::TimePoint<> now;
    std::chrono::nanoseconds user, sys;
    llvm::sys::Process::GetTimeUsage(now, user, sys);
    if (user != std::chrono::nanoseconds::zero())
      now = llvm::sys::TimePoint<>(user);

    using Seconds = std::chrono::duration<double, std::ratio<1>>;
    double startTime = Seconds(now.time_since_epoch()).count();*/
    if (settings.measureTime)
      ovStartTime = std::chrono::steady_clock().now();
  }
  virtual void atOverloadEnd(const Sema &s, const SourceLocation &loc,
                             const OverloadCandidateSet &set,
                             OverloadingResult ovRes,
                             const OverloadCandidate *BestOrProblem) override {
    if (!inBestOC)
      return;
    std::chrono::time_point<std::chrono::steady_clock> ovEndTime;
    if (settings.measureTime)
      ovEndTime = std::chrono::steady_clock().now();
    OvInsResNode node;
    PresumedLoc L = S->getSourceManager().getPresumedLoc(loc);
    node.line = L.getLine();
    node.Fname = L.getFilename();
    node.Entry = getResEntry(ovRes, BestOrProblem);
    node.Entry.passedTime = (ovEndTime - ovStartTime).count();
    node.Entry.compares = transformCompares();
    compares.clear();
    if (settings.ShowCompares != FrontendOptions::SC_Verbose)
      filterForRelevant(node.Entry);
    if (settings.SummarizeBuiltInBinOps)
      summarizeBuiltInBinOps(node.Entry);
    if (!node.Entry.isImplicit || settings.ShowImplicitConversions)
      if (nameOk())
        cont.add(node);

    inBestOC = false;
  }
  virtual void atCompareOverloadBegin(const Sema &S, const SourceLocation &Loc,
                                      const OverloadCandidate &C1,
                                      const OverloadCandidate &C2) override {
    if (!inBestOC || !settings.ShowCompares)
      return;
    inCompare = true;
    if (settings.measureTime)
      cmpStartTime = std::chrono::steady_clock().now();
  }
  virtual void atCompareOverloadEnd(const Sema &TheSema,
                                    const SourceLocation &Loc,
                                    const OverloadCandidate &Cand1,
                                    const OverloadCandidate &Cand2, bool res,
                                    BetterOverloadCandidateReason reason,
                                    int infoIdx) override {
    if (!inCompare)
      return;
    long time = 0;
    if (settings.measureTime)
      time = (std::chrono::steady_clock().now() - cmpStartTime).count();
    compares.push_back(
        cmpInfo{&TheSema, Loc, reason, &Cand1, &Cand2, res, infoIdx, time});
    inCompare = false;
  }

private:
  std::vector<OvInsCompareEntry> transformCompares() {
    std::vector<OvInsCompareEntry> res;
    std::vector<bool> shouldSkip(compares.size());
    for (size_t i = 0; i < compares.size(); ++i) {
      if (shouldSkip[i])
        continue;
      auto &cmp = compares[i];
      res.emplace_back();
      auto callKinds = getCallKinds();
      auto &Entry = res.back();
      for (size_t j = i + 1; j < compares.size(); ++j) {
        if (cmp.sameCands(compares[j])) {
          shouldSkip[j] = true;
          cmp.duration += compares[j].duration;
        } else if (cmp.mirroredCands(compares[j])) {
          shouldSkip[j] = true;
          if (!cmp.C1Better && compares[j].C1Better)
            cmp = compares[j];
          if (!cmp.C1Better && !compares[j].C1Better)
            if (cmp.reason == badConversion || cmp.reason == betterConversion)
              Entry.opposerConversion =
                  ConversionCompare(*cmp.Cand1, *cmp.Cand2, compares[j].infoIdx,
                                    CompareKind::Better, callKinds);
          cmp.duration += compares[j].duration;
        }
      }
      Entry.passedTime = cmp.duration;
      Entry.C1Better = cmp.C1Better;
      Entry.C1 = getCandEntry(*cmp.Cand1);
      Entry.C2 = getCandEntry(*cmp.Cand2);
      Entry.reason = cmp.reason;
      if (cmp.reason == clang::betterConversion) {
        Entry.deciderConversion = ConversionCompare(
            *cmp.Cand1, *cmp.Cand2, cmp.infoIdx,
            cmp.C1Better ? CompareKind::Better : CompareKind::Worse, callKinds);
      } else if (cmp.reason == clang::badConversion) {
        Entry.deciderConversion = ConversionCompare(
            *cmp.Cand1, *cmp.Cand2, cmp.infoIdx,
            cmp.C1Better ? CompareKind::Better : CompareKind::Worse, callKinds);
        // Entry.deciderConversion =
        //   getFromType(cmp.Cand1.Conversions[cmp.infoIdx]).getAsString()+" ->
        //   "+
        //   getToType((cmp.res?cmp.Cand2:cmp.Cand1),cmp.infoIdx).getAsString()+"
        //   is ill formated";
      }
      for (size_t i = 0; i != compareResults.size(); ++i) {
        bool isStaticCall = SetArgMap[Set].ObjectExpr == nullptr &&
                            (cmp.Cand1->IgnoreObjectArgument ||
                             cmp.Cand2->IgnoreObjectArgument);
        Entry.conversionCompares.push_back(
            ConversionCompare(*cmp.Cand1, *cmp.Cand2, i + isStaticCall,
                              compareResults[i], callKinds));
        Entry.conversionCompares.back().place = {};
      }
    }
    compares.clear();
    return res;
  };
  bool checkPlace(SourceLocation loc) const {
    PresumedLoc L = S->getSourceManager().getPresumedLoc(loc);
    unsigned line = L.getLine();
    if ((L.getIncludeLoc().isValid() && !settings.ShowIncludes) ||
        (!inSetInterval(line) && true)) {
      return false;
    }
    return true;
  }
  bool nameOk() const {
    bool qual = (settings.CandFunName.find("::") != std::string::npos);
    if (settings.CandFunName.empty())
      return true;
    for (const auto &cand : *Set) {
      if (cand.Function && !cand.IsSurrogate &&
          checkName(settings.CandFunName,
                    qual ? cand.Function->getQualifiedNameAsString()
                         : cand.Function->getNameAsString()))
        return true;
      if (!cand.Function && !cand.IsSurrogate) {
        std::string s;
        llvm::raw_string_ostream os(s);
        os << "operator";
        addBuiltinOpSpelling(os, cand);
        if (checkName(settings.CandFunName, s))
          return true;
      }
      if (cand.IsSurrogate) {
        if (checkName(settings.CandFunName,
                      qual ? cand.Surrogate->getQualifiedNameAsString()
                           : cand.Surrogate->getNameAsString()))
          return true;
      }
    }
    return false;
  }
  bool checkName(const std::string &FunName,
                 const std::string &candName) const {
    size_t i = 0, j = 0;
    while (i != FunName.size() && j != candName.size()) {
      if (FunName[i] == candName[j]) {
        ++i;
        ++j;
      } else if (FunName[i] == '~' && candName[j] == ' ') {
        ++i;
        ++j;
      } else if (candName[j] == ' ')
        ++j; // for operator T
      else if (FunName[i] == '\'' && candName[j] == ',') {
        ++i;
        ++j;
      } // for operator,
      else if (FunName.size() == i + 5 && FunName.substr(i) == "Comma" &&
               candName[j] == ',') {
        i += 5;
        ++j;
      } // for operator,
      else
        return false;
    }
    return i == FunName.size() && j == candName.size();
  }
  SourceRange makeSourceRangeFromPossibleEndpoints(
      const std::vector<SourceLocation> &begs,
      const std::vector<SourceLocation> &ends) const {
    SourceRange res;
    for (const auto &x : begs)
      if (x.isValid() && (res.getBegin().isInvalid() || x < res.getBegin()))
        res.setBegin(x);
    for (const auto &x : ends)
      if (x.isValid() && (res.getEnd().isInvalid() || x < res.getEnd()))
        res.setEnd(x);
    return res;
  }
  void printConvEntry(const OvInsConvEntry &Entry) const {
    unsigned ID1 = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note, "%0 conversion: \n%1\n%2");
    unsigned ID1NoConvInfo = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note, "%0 conversion: \n%1");
    unsigned ID1uninited = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note, "%0 conversion");
    if (Entry.convPath != "")
      if (Entry.convInfo != "")
        S->Diags.Report(Entry.src.range.getBegin(), ID1)
            << Entry.kind << Entry.convPath << Entry.convInfo
            << Entry.src.range;
      else
        S->Diags.Report(Entry.src.range.getBegin(), ID1NoConvInfo)
            << Entry.kind << Entry.convPath << Entry.src.range;
    else
      S->Diags.Report(Entry.src.range.getBegin(), ID1uninited)
          << Entry.kind << Entry.src.range;
  }
  template <class T, class Tr = const std::string &(*)(const std::string &)>
  std::string concat(
      const T &in, const std::string &sep, char begin = '[', char end = ']',
      Tr transform = [](const std::string &a) -> const std::string & {
        return a;
      }) const {
    std::string res;
    if (in.empty())
      return res;
    llvm::raw_string_ostream os(res);
    os << begin;
    for (size_t i = 0; i < in.size() - 1; ++i)
      os << transform(in[i]) << sep;
    os << transform(in.back()) << end;
    return res;
  }
  void printCompareEntry(const OvInsCompareEntry &Entry,
                         SourceLocation loc) const {
    unsigned ID1 = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note,
        "Compearing candidates resulted in %0 (reason: %1) %2 %3%4%5");
    S->Diags.Report(loc, ID1)
        << (Entry.C1Better ? "The first is better" : "The first is not better")
        << str::toString(Entry.reason)
        << (Entry.conversionCompares.empty()
                ? ""
                : "\n\tConversions:" + concat(Entry.conversionCompares, "\n\t",
                                              '[', ']',
                                              [](const OvInsConvCmpEntry &a) {
                                                return a.printToString();
                                              }))
        << (Entry.deciderConversion
                ? "\n\tDecider: " + Entry.deciderConversion->printToString()
                : "")
        << (Entry.opposerConversion
                ? "\n\tBut: " + Entry.opposerConversion->printToString()
                : "")
        << (Entry.passedTime
                ? "\n\tTime:" + std::to_string(Entry.passedTime) + "ns"
                : "");
    printCandEntry(Entry.C1, Entry.C1Better ? "Better " : "Not Better ");
    printCandEntry(Entry.C2, Entry.C1Better ? "Worse " : "Not Worse ");
  }
  void printCandEntry(const OvInsCandEntry &Entry, std::string nm = "") const {
    unsigned ID1 = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note, "%0candidate: %1%2 %3%4 %5 %6");
    std::string temp = concat(Entry.templateParams, ", ");
    std::string paramTypes = concat(Entry.paramTypes, ", ", '(', ')');
    if (paramTypes.empty())
      paramTypes = "()";
    S->Diags.Report(Entry.src.Loc, ID1)
        << nm << str::toString(Entry.rewriteKind) << Entry.name << paramTypes
        << (temp == "" ? "" : "\n\tTemplate params: " + temp)
        << (Entry.failKind
                ? "\n\tFailure reason: " + str::toString(*Entry.failKind)
                : "")
        << (Entry.extraFailInfo ? *Entry.extraFailInfo : "") << Entry.src.range;
    unsigned IDusingNote = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note, "using from here");
    if (Entry.usingLoc != SourceLocation{})
      S->Diags.Report(Entry.usingLoc, IDusingNote);
    if (0) //! SUMARIZE convs
      for (const auto &x : Entry.Conversions)
        printConvEntry(x);
    else if (!Entry.Conversions.empty()) {
      unsigned ID0 = S->Diags.getDiagnosticIDs()->getCustomDiagID(
          DiagnosticIDs::Note, "Conversions: %0");
      std::string message;
      llvm::raw_string_ostream os(message);
      for (const auto &x : Entry.Conversions) {
        os << "\n\t" << x.kind << " conversion";
        if (x.convPath != "") {
          os << ":\n\t\t" << x.convPath;
          if (x.convInfo != "")
            os << "\n\t\t" << x.convInfo;
        }
      }
      auto diag = S->Diags.Report(Entry.Conversions[0].src.Loc, ID0);
      diag << message;
      for (const auto &x : Entry.Conversions)
        diag << x.src.range;
    }
    if (Entry.templateSpecs.size()) {
      unsigned IDTempSpecCnt = S->Diags.getDiagnosticIDs()->getCustomDiagID(
          DiagnosticIDs::Note, "%0 explicit template specialivations found");
      S->Diags.Report(SourceLocation{}, IDTempSpecCnt)
          << Entry.templateSpecs.size();
      /*unsigned IDTempSpecDecl = S->Diags.getDiagnosticIDs()->getCustomDiagID(
          DiagnosticIDs::Note, "explicit template specialisation");
      unsigned IDTempSpecDeclExact =
          S->Diags.getDiagnosticIDs()->getCustomDiagID(
              DiagnosticIDs::Note,
              "exactly matched explicit template specialisation");
      for (const auto &ts : Entry.templateSpecs)
        if (ts.isExact)
          S->Diags.Report(ts.src.Loc, IDTempSpecDeclExact) << ts.src.range;
        else
          S->Diags.Report(ts.src.Loc, IDTempSpecDecl) << ts.src.range;*/
    }
  }
  void printResEntry(const OvInsResEntry &Entry) const {
    long timeDiff = Entry.passedTime;
    if (Entry.passedTime)
      for (const auto &cmp : Entry.compares)
        timeDiff -= cmp.passedTime;
    std::string types = concat(Entry.callTypes, ", ");
    auto ID0 = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Remark, "Overload resulted with %0 With types %1 %2");
    S->Diags.Report(Entry.callSrc.Loc, ID0)
        << str::toString(Entry.ovRes) << types << Entry.note
        << Entry.callSrc.range;
    auto IDTime = S->Diags.getDiagnosticIDs()->getCustomDiagID(
        DiagnosticIDs::Note, "Time: %0ns Not in compare: %1ns");
    // S->Diags.Report(SourceLocation{}, IDTime)<< Entry.passedTime<<timeDiff;
    if (Entry.best)
      printCandEntry(*Entry.best, "Best ");
    for (const auto &x : Entry.problems)
      printCandEntry(x, Entry.ovRes == clang::OR_Ambiguous ? "Ambigius "
                                                           : "Unresolvable ");
    // OR unresolvable concept
    for (const auto &x : Entry.viableCandidates)
      if (!x.isBestOrProblem)
        printCandEntry(x, "Viable ");
    for (const auto &x : Entry.nonViableCandidates)
      if (!x.isBestOrProblem)
        printCandEntry(x, "Non viable ");
    for (const auto &x : Entry.compares)
      printCompareEntry(x, Entry.callSrc.Loc);
    // Entry.note;;
    if (Entry.passedTime)
      S->Diags.Report(SourceLocation{}, IDTime) << Entry.passedTime << timeDiff;
  }
  void printHumanReadable() const {
    for (const auto &x : cont)
      printResEntry(x.Entry);
  }
  const SetArgs &getSetArgs() const {
    auto it = SetArgMap.find(Set);
    assert(it != SetArgMap.end() && "Set must be stored");
    return it->second;
  }
  bool inSetInterval(unsigned x) const {
    if (settings.Intervals.size() == 0)
      return true;
    for (const auto &[from, to] : settings.Intervals)
      if (from <= x && x <= to)
        return true;
    return false;
  }
  std::optional<std::pair<std::string, std::string>>
  writeBuiltInsBinOpSumm(const OvInsResEntry &E) const {
    std::pair<std::string, std::string> res;
    std::set<std::string> types[3];
    if (Set->getKind() == Set->CandidateSetKind::CSK_Operator) {
      for (const auto &c : E.viableCandidates) {
        if (!shouldSumm(c))
          continue;
        for (size_t i = 0; i != c.paramTypes.size(); i++)
          types[i].emplace(c.paramTypes[i]);
      }
      if (types[0].empty() || types[1].empty() || !types[2].empty())
        return {}; // Non bin op
      llvm::raw_string_ostream os(res.first);
      for (auto &x : types[0])
        os << x << '/';
      res.first.pop_back();
      llvm::raw_string_ostream os2(res.second);
      for (auto &x : types[1])
        os2 << x << '/';
      res.second.pop_back();
      return res;
    }
    return {};
  }
  std::string getTokenFromLoc(const SourceLocation &loc) const {
    if (loc == SourceLocation{})
      return "";
    SourceLocation endloc(Lexer::getLocForEndOfToken(
        loc, 0, S->getSourceManager(), S->getLangOpts()));
    CharSourceRange range = CharSourceRange::getCharRange(loc, endloc);
    return std::string(
        Lexer::getSourceText(range, S->getSourceManager(), S->getLangOpts()));
  }
  /*void addBuiltinOpTypes(llvm::raw_string_ostream& os, const
  OverloadCandidate& C)const{ assert(Set->CSK_Operator == Set->getKind() && "Not
  operator"); os<<" ("; if (C.BuiltinParamTypes[0]!=QualType{})
      os<<C.BuiltinParamTypes[0].getAsString();
    if (C.BuiltinParamTypes[1]!=QualType{})
      os<<", "<<C.BuiltinParamTypes[1].getAsString();
    if (C.BuiltinParamTypes[2]!=QualType{})
      os<<", "<<C.BuiltinParamTypes[2].getAsString();
    os<<")";
  }*/
  void addBuiltinOpSpelling(llvm::raw_string_ostream &os,
                            const OverloadCandidate &C) const {
    assert(Set->CSK_Operator == Set->getKind() && "Not operator");
    const char *cc =
        getOperatorSpelling(Set->getRewriteInfo().OriginalOperator);
    // Not handles all operators
    if (cc)
      os << cc;
    else {
      std::string s1 = getTokenFromLoc(Set->getLocation());
      // Note:there are no builtin operator ->
      if (s1 == "(")
        os << "()";
      else if (s1 == "[")
        os << "[]";
      else if (s1 == "?")
        os << "?:";
      else
        os << s1;
    }
  }
  std::string getBuiltInOperatorName(const OverloadCandidate &C) const {
    assert(Set->CSK_Operator == Set->getKind() && "Not operator");
    std::string res;
    llvm::raw_string_ostream os(res);
    os << "Built-in operator";
    addBuiltinOpSpelling(os, C);
    // addBuiltinOpTypes(os, C);
    return res;
  }
  bool shouldSumm(const OvInsCandEntry &cand) const {
    if (cand.src.sourceLoc == "Built-in" && cand.paramTypes.size() == 2 &&
        cand.paramTypes[0] != cand.paramTypes[1]) {
      std::string_view name(cand.name);
      // sizeof("Built-in operator "\0)=19
      name = name.substr(sizeof("Built-in operator ") - 1, 2);
      return name != "()" && name != ", " && name != "[]" && name != "++" &&
             name != "--";
    }
    return false;
  }
  void summarizeBuiltInBinOps(OvInsResEntry &E) {
    if (const auto &c = writeBuiltInsBinOpSumm(E)) {
      for (int i = 0; i < (int)E.viableCandidates.size(); ++i) {
        const auto &cand = E.viableCandidates[i];
        if (shouldSumm(cand)) {
          std::swap(E.viableCandidates[i], E.viableCandidates.back());
          --i;
          E.viableCandidates.pop_back();
        }
      }
      // E.viableCandidates.push_back(*c);
      auto relevants = getRelevantCands(E);
      for (int i = 0; i < (int)E.compares.size(); ++i) {
        const auto &comp = E.compares[i];
        if ((!isIn(relevants, comp.C1) && shouldSumm(comp.C1)) ||
            (!isIn(relevants, comp.C2) && shouldSumm(comp.C2))) {
          std::swap(E.compares.back(), E.compares[i]);
          E.compares.pop_back();
          --i;
        }
      }
      E.note += "\nThere are more viable built in functions, with the "
                "calltypes combined from " +
                c->first + " and " + c->second +
                " you can list them with \"ShowBuiltInBinOps\"";
    }
  }
  std::string getConvAsStr(const OverloadCandidate &Cand, int idx,
                           ExprValueKind vk) const {
    std::string res;
    llvm::raw_string_ostream os(res);
    if (!Cand.Conversions[idx].isInitialized())
      os << "Uninited";
    else {
      if (!(Cand.Conversions[idx].isStandard() ||
            Cand.Conversions[idx].isUserDefined())) //?
        os << str::toString(Cand.Conversions[idx].getKind());
      if (!(Cand.Conversions[idx].isEllipsis() ||
            Cand.Conversions[idx].isStaticObjectArgument())) {
        os << " ("
           << getFromType(Cand.Conversions[idx])
                  .getCanonicalType()
                  .getAsString()
           << Kinds[vk] << " -> "
           << getToType(Cand, idx).getCanonicalType().getAsString();
        std::string temp = getTemplatedParamForConversion(Cand, idx);
        if (temp != "")
          os << " = " << temp;
        os << ')';
      }
    }
    return res;
  }
  OvInsConvCmpEntry
  ConversionCompare(const OverloadCandidate &Cand1,
                    const OverloadCandidate &Cand2, int idx, CompareKind cmpRes,
                    const std::vector<ExprValueKind> &vkarr) const {
    bool isStaticCall =
        getSetArgs().ObjectExpr == nullptr &&
        (Cand1.IgnoreObjectArgument || Cand2.IgnoreObjectArgument);
    ExprValueKind vk =
        idx >= isStaticCall ? vkarr[idx - isStaticCall] : VK_LValue;
    const static char compareSigns[]{'>', '=', '<'};
    OvInsConvCmpEntry res;
    res.P1 = getConvAsStr(Cand1, idx, vk);
    res.cmpRes = compareSigns[cmpRes + 1];
    res.P2 = getConvAsStr(Cand2, idx, vk);
    res.place = idx + 1;
    return res;
  }
  std::vector<OvInsCandEntry> getRelevantCands(OvInsResEntry &Entry) const {
    std::vector<OvInsCandEntry> relevants = Entry.problems;
    if (relevants.empty() && Entry.best)
      relevants.push_back(*Entry.best);
    return relevants;
  }
  bool isIn(const std::vector<OvInsCandEntry> &v, OvInsCandEntry c) const {
    for (const auto &e : v)
      if (e == c)
        return true;
    return false;
  }
  void filterForRelevant(OvInsResEntry &Entry) const {
    std::vector<OvInsCandEntry> relevants = getRelevantCands(Entry);
    for (size_t i = 0; i < Entry.compares.size(); ++i) {
      bool isRelevant = isIn(relevants, Entry.compares[i].C1) ||
                        isIn(relevants, Entry.compares[i].C2);
      if (!isRelevant) {
        std::swap(Entry.compares[i], Entry.compares.back());
        Entry.compares.pop_back();
        --i;
      }
    }
  }
  std::pair<int, int> NeededArgsRange(const OverloadCandidate &C) const {
    if (C.IsSurrogate) {
      // C.FoundDecl.getDecl();
      const NamedDecl *nd = C.FoundDecl.getDecl();
      while (isa<UsingShadowDecl>(nd))
        nd = llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
      const auto ty = nd->getAsFunction()->getCallResultType();
      // The type of the functionPtr
      const auto *fptr = ty->getAs<PointerType>()
                             ->getPointeeType()
                             ->getAs<FunctionProtoType>();
      int cnt = fptr->getNumParams() + 1;
      return {cnt, cnt};
    }
    if (C.Function) {
      // Check when deducing this is implemented FIXME
      bool needObj = isa<CXXMethodDecl>(C.Function) &&
                     !isa<CXXConstructorDecl>(C.Function);
      if (!C.Function->getEllipsisLoc().isInvalid())
        return {C.Function->getMinRequiredArguments() + needObj, -1};
      return {C.Function->getMinRequiredArguments() + needObj,
              C.Function->param_size() + needObj};
    }
    int res = 0;
    for (int i = 0; i != 3; i++)
      if (C.BuiltinParamTypes[i] != QualType{})
        ++res;
    return {res, res};
  }
  std::optional<std::string>
  getExtraFailInfo(const OverloadCandidate &C) const {
    if (C.Viable)
      return {};
    std::string res;
    llvm::raw_string_ostream os(res);
    switch ((OverloadFailureKind)C.FailureKind) {
    case ovl_fail_too_many_arguments:
    case ovl_fail_too_few_arguments: {
      const auto &[mn, mx] = NeededArgsRange(C);
      os << "Needed: " << mn;
      if (mn != mx) {
        if (mx == -1)
          os << "...";
        else
          os << ".." << mx;
      }
      os << " Got: " << getCallKinds().size();
      return res;
    }
    case ovl_fail_bad_conversion:
      for (size_t i = 0; i != C.Conversions.size(); i++) {
        if (!C.Conversions[i].isInitialized())
          continue;
        if (C.Conversions[i].isBad()) {
          os << str::toString(C.Conversions[i].Bad.Kind) << " Pos: " << (1 + i)
             << "    From: " << C.Conversions[i].Bad.getFromType().getAsString()
             << Kinds[getCallKinds()[i - C.IgnoreObjectArgument]]
             << "    To: " << C.Conversions[i].Bad.getToType().getAsString();
          return res;
        }
      }
      return {};
    case ovl_fail_bad_deduction:
      return str::toString(
          Sema::TemplateDeductionResult(C.DeductionFailure.Result));
    case ovl_fail_trivial_conversion:
    case ovl_fail_illegal_constructor:
    case ovl_fail_bad_final_conversion:
    case ovl_fail_final_conversion_not_exact:
    case ovl_fail_bad_target:
      return {};
    case ovl_fail_enable_if:
      return std::string(
          static_cast<EnableIfAttr *>(C.DeductionFailure.Data)->getMessage());
    case ovl_fail_explicit:
    case ovl_fail_addr_not_available:
    case ovl_fail_inhctor_slice:
    case ovl_non_default_multiversion_function:
    case ovl_fail_object_addrspace_mismatch:
    case ovl_fail_constraints_not_satisfied:
    case ovl_fail_module_mismatched:
      break;
    }
    return {};
  }

  OvInsSource getSrcFromExpr(const Expr *e) const {
    OvInsSource res = getSrcFromRange(e->getSourceRange());
    // res.range=e->getSourceRange();
    res.Loc = e->getExprLoc();
    res.sourceLoc = res.Loc.printToString(S->getSourceManager());
    // res.sourceLoc=res.range.getBegin().printToString(S->getSourceManager());
    // res.source=;
    return res;
  };
  OvInsSource getConversionSource(const SetArgs &setArg, int idx) const {
    if (idx >= 0)
      return getSrcFromExpr(setArg.inArgs[idx]);
    if (const auto *objExpr = setArg.ObjectExpr) {
      if (const auto *unresObjExpr = dyn_cast<UnresolvedMemberExpr>(objExpr)) {
        if (unresObjExpr->isImplicitAccess())
          return getSrcFromRange(unresObjExpr->getSourceRange());
        return getSrcFromRange(unresObjExpr->getBase()->getSourceRange());
      }
      return getSrcFromRange(objExpr->getSourceRange());
    }
    OvInsSource res;
    res.range = {setArg.Loc};
    return res;
  }
  std::vector<OvInsConvEntry> getConversions(const OverloadCandidate &C) const {
    std::vector<OvInsConvEntry> res(C.Conversions.size());
    const auto callKinds = getCallKinds();
    const SetArgs &setArg = getSetArgs();
    bool isStaticCall = setArg.ObjectExpr == nullptr && C.IgnoreObjectArgument;
    for (size_t i = 0; i < C.Conversions.size(); ++i) {
      const ExprValueKind fromKind =
          (callKinds.size() > i - isStaticCall)
              ? ((i >= isStaticCall) ? callKinds[i - isStaticCall] : VK_LValue)
              : VK_LValue;
      const auto &conv = C.Conversions[i];
      OvInsConvEntry &actual = res[i];
      {
        int inArgsIdx = i;
        if ((C.Function && isa<CXXMethodDecl>(C.Function) &&
             !isa<CXXConstructorDecl>(C.Function) && setArg.ObjectExpr) ||
            C.IsSurrogate)
          --inArgsIdx;
        actual.src = getConversionSource(setArg, inArgsIdx);
      }
      if (!conv.isInitialized()) {
        actual.kind = "Uninitialized";
        continue;
      }
      int idx = i;
      if (C.isReversed())
        idx = 1 - idx;
      if ((C.Function && isa<CXXMethodDecl>(C.Function) &&
           !isa<CXXConstructorDecl>(C.Function)) ||
          C.IsSurrogate)
        --idx;
      llvm::raw_string_ostream path(actual.convPath);
      actual.kind = str::toString(conv.getKind());
      switch (conv.getKind()) {
      case ImplicitConversionSequence::StandardConversion:
        path << conv.Standard.getFromType().getCanonicalType().getAsString();
        path << Kinds[fromKind];
        actual.convInfo = getConversionSeq(conv.Standard);
        if (C.Function && idx != -1 &&
            !isa<clang::InitListExpr>(setArg.inArgs[idx]))
          path << " -> "
               << C.Function->parameters()[idx]
                      ->getType()
                      .getCanonicalType()
                      .getAsString();
        else
          path << " -> "
               << conv.Standard.getToType(2).getCanonicalType().getAsString();
        break;
      case ImplicitConversionSequence::StaticObjectArgumentConversion:
        break;
      case ImplicitConversionSequence::UserDefinedConversion:
        path << conv.UserDefined.Before.getFromType()
                    .getCanonicalType()
                    .getAsString();
        path << Kinds[fromKind];
        if (conv.UserDefined.Before.First || conv.UserDefined.Before.Second ||
            conv.UserDefined.Before.Third)
          path << " -> "
               << conv.UserDefined.Before.getToType(2)
                      .getCanonicalType()
                      .getAsString();
        actual.convInfo = getConversionSeq(conv.UserDefined);
        if (conv.UserDefined.After.First || conv.UserDefined.After.Second ||
            conv.UserDefined.After.Third)
          path << " -> "
               << conv.UserDefined.After.getFromType()
                      .getCanonicalType()
                      .getAsString();
        if (C.Function && idx != -1 &&
            !isa<clang::InitListExpr>(setArg.inArgs[idx]))
          path << " -> "
               << C.Function->parameters()[idx]
                      ->getType()
                      .getCanonicalType()
                      .getAsString();
        else if (C.IsSurrogate && idx == -1)
          path << " -> "
               << conv.UserDefined.ConversionFunction->getReturnType()
                      .getCanonicalType()
                      .getAsString();
        else
          path << " -> "
               << conv.UserDefined.After.getToType(2)
                      .getCanonicalType()
                      .getAsString();
        break;
      case ImplicitConversionSequence::AmbiguousConversion:
        path << conv.Ambiguous.getFromType().getCanonicalType().getAsString();
        path << Kinds[fromKind];
        path << " -> "
             << conv.Ambiguous.getToType().getCanonicalType().getAsString();
        break;
      case ImplicitConversionSequence::EllipsisConversion:
        break;
      case ImplicitConversionSequence::BadConversion:
        path << conv.Bad.getFromType().getCanonicalType().getAsString();
        path << Kinds[fromKind];
        path << " -> " << conv.Bad.getToType().getCanonicalType().getAsString();
        actual.convInfo = str::toString(conv.Bad.Kind);
        break;
      }
      if (C.Function /*&& !C.IsSurrogate*/) {
        std::string temp = getTemplatedParamForConversion(C, i);
        if (temp != "")
          path << " = " << temp;
      }
    }

    return res;
  }
  /*OvdlCandEntry getCandEntry(const OverloadCandidate& C){
    static std::vector<std::pair<const NamedDecl *, OvdlCandEntry>> mp;
    if (const NamedDecl *p = C.FoundDecl.getDecl()) {
      for (const auto &[key, val] : mp) {
        if (key == p / *|| S->isEquivalentInternalLinkageDeclaration(key, p)* /)
          return val;
      }
      auto res = getCandEntryIner(C);
      mp.push_back({p, res});
      return res;
    }

    auto res = getCandEntryIner(C);
    return res;
  }*/
  std::deque<std::string> getSurrSignature(const OverloadCandidate &C) const {
    std::deque<std::string> res;
    const NamedDecl *nd = C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd)) {
      nd = llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    }
    const auto ty = nd->getAsFunction()->getCallResultType();
    // The type of the functionPtr
    const auto *fptr =
        ty->getAs<PointerType>()->getPointeeType()->getAs<FunctionProtoType>();
    res.emplace_back(
        getSetArgs().ObjectExpr->getType().getCanonicalType().getAsString() +
        "=*this");
    for (const auto &t : fptr->getParamTypes())
      res.emplace_back(t.getCanonicalType().getAsString());

    return res;
  }
  OvInsCandEntry getCandEntry(const OverloadCandidate &C) const {
    OvInsCandEntry res;
    res.isBestOrProblem = C.Best;
    llvm::raw_string_ostream usingLoc(res.usingLocation);
    if (settings.ShowConversions) {
      res.Conversions = getConversions(C); // FIXME
    } else
      res.Conversions = {};
    if (!C.Viable)
      res.failKind = (OverloadFailureKind)C.FailureKind;
    else
      res.failKind = {};
    res.extraFailInfo = getExtraFailInfo(C);
    //    llvm::raw_string_ostream signature(res.signature);
    if (C.IsSurrogate) {

      if (C.FoundDecl.getDecl())
        res.name = "Surrogate from " +
                   C.FoundDecl.getDecl()->getQualifiedNameAsString();
      else
        res.name = "Surrogate from " + C.Surrogate->getQualifiedNameAsString();
      res.paramTypes = getSurrSignature(C);
      res.isSurrogate = true;
      res.src = getSrcFromRange(C.Surrogate->getSourceRange());
      return res;
    }

    if (C.Function == nullptr) {
      res.src.sourceLoc = "Built-in";
      res.src.Loc = {};
      res.name = "";
      if (Set->getKind() == Set->CSK_Operator)
        res.name = getBuiltInOperatorName(C);
      for (const auto &tmp : C.BuiltinParamTypes)
        if (tmp != QualType{})
          res.paramTypes.push_back(tmp.getAsString());
      return res;
    }
    if (isa<UsingShadowDecl>(C.FoundDecl.getDecl())) {
      res.usingLoc = C.FoundDecl.getDecl()->getLocation();
      res.usingLoc.print(usingLoc, S->SourceMgr);
    }
    res.rewriteKind = C.getRewriteKind();
    if (!C.Function && !C.IsSurrogate)
      res.rewriteKind = OverloadCandidateRewriteKind::CRK_None;
    res.name = C.FoundDecl.getDecl()->getQualifiedNameAsString();
    if (C.Function) {
      res.paramTypes = getParamTypes(C);
      res.src = getSource(C);
      if (isTemplatedFun(C) && settings.ShowTemplateSpecs) {
        res.templateSpecs = getSpecializations(C);
        res.templateParams = getTemplateParams(C);
      }
    }
    return res;
  }
  std::vector<OvInsTemplateSpec>
  getSpecializations(const OverloadCandidate &C) const {
    const NamedDecl *nd = C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd))
      nd = llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    const FunctionDecl *f = nd->getAsFunction();
    std::vector<OvInsTemplateSpec> res;
    for (const auto *x : f->getDescribedFunctionTemplate()->specializations()) {
      if (x->getSourceRange() != f->getSourceRange()) {
        OvInsTemplateSpec entry;
        entry.isExact = true;
        entry.src = getSrcFromRange({x->getLocation(), x->getTypeSpecEndLoc()});
        if (x->getTemplateSpecializationArgs() &&
            C.Function->getTemplateSpecializationArgs()) {
          const auto specializedArgs =
              x->getTemplateSpecializationArgs()->asArray();
          auto genericArgs =
              C.Function->getTemplateSpecializationArgs()->asArray();
          int midx = std::min(specializedArgs.size(), genericArgs.size());
          entry.isExact = genericArgs.size() == specializedArgs.size();
          for (int i = 0; i != midx; ++i)
            entry.isExact =
                entry.isExact &&
                specializedArgs[i].structurallyEquals(genericArgs[i]);
        } else
          entry.isExact = false;
        if (entry.isExact && !C.Best && !inCompare) {
          static std::set<std::pair<SourceLocation, const FunctionDecl *>> s;
          if (!s.count(std::pair{Loc, x})) {
            s.emplace(Loc, x);
            auto ID = S->Diags.getDiagnosticIDs()->getCustomDiagID(
                DiagnosticIDs::Warning, "Explicit specialization ignored");
            S->Diags.Report(Loc, ID) << sr;
            auto ID2 = S->Diags.getDiagnosticIDs()->getCustomDiagID(
                DiagnosticIDs::Note, "The ignored specialization:");
            S->Diags.Report(x->getLocation(), ID2) << x->getSourceRange();
            auto ID3 = S->Diags.getDiagnosticIDs()->getCustomDiagID(
                DiagnosticIDs::Note, "General declaration:");
            S->Diags.Report(f->getLocation(), ID3) << f->getSourceRange();
            // if ()
          }
        }
        res.push_back(entry);
      }
    }
    return res;
  }
  OvInsSource getSrcFromRange(const SourceRange &r) const {
    OvInsSource res;
    SourceLocation endloc(Lexer::getLocForEndOfToken(
        r.getEnd(), 0, S->getSourceManager(), S->getLangOpts()));
    SourceLocation endloc2(Lexer::getLocForEndOfToken(
        r.getEnd(), 1, S->getSourceManager(), S->getLangOpts()));
    CharSourceRange crange =
        CharSourceRange::getCharRange(r.getBegin(), endloc);
    res.range = {r.getBegin(), endloc2};
    res.rangeBegEnd = {r.getBegin().printToString(S->getSourceManager()),
                       r.getEnd().printToString(S->getSourceManager())};
    res.source =
        Lexer::getSourceText(crange, S->getSourceManager(), S->getLangOpts());
    res.Loc = r.getBegin();
    res.sourceLoc = res.Loc.printToString(S->getSourceManager());
    return res;
  }

  OvInsSource getNonTemplateSrc(const FunctionDecl *f) const {
    SourceRange r = f->getSourceRange();
    r.setEnd(f->getTypeSpecEndLoc());
    return getSrcFromRange(r);
  }
  OvInsSource getTemplateSrc(const FunctionDecl *f) const {
    llvm::SmallVector<const Expr *> AC;
    SourceRange r = f->getDescribedFunctionTemplate()->getSourceRange();
    r.setEnd(f->getTypeSpecEndLoc());
    const auto *t = f->getDescribedTemplate();
    SourceLocation l0;
    if (t) {
      t->getAssociatedConstraints(AC);
      if (AC.size())
        l0 = AC.back()->getEndLoc();
    }
    if (l0 > r.getEnd())
      r.setEnd(l0);
    return getSrcFromRange(r);
  }
  bool isTemplatedFun(const OverloadCandidate &C) const {
    const NamedDecl *nd = C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd))
      nd = llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    const FunctionDecl *f = nd->getAsFunction();
    return f && f->isTemplated();
  }
  OvInsSource getSource(const OverloadCandidate &C) const {
    // if (C.IsSurrogate) return {"",{}};
    const NamedDecl *nd = C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd))
      nd = llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    const FunctionDecl *f = nd->getAsFunction();
    if (!f)
      return {};
    if (f->isTemplated())
      return getTemplateSrc(f);
    return getNonTemplateSrc(f);
    // return f->getSourceRange();
  }
  QualType getThisType(const OverloadCandidate &C) const {
    if (const auto *mp = llvm::dyn_cast_or_null<CXXMethodDecl>(C.Function))
      if (mp->isInstance() && !isa<CXXConstructorDecl>(mp))
        return mp->getThisType();
    return QualType{};
  }
  std::string getTemplatedParamForConversion(const OverloadCandidate &C,
                                             int i) const {
    if (C.IsSurrogate)
      return {}; // Not sure if needed
    if (C.Function == nullptr)
      return {};
    if (C.Function && isa<CXXMethodDecl>(C.Function) &&
        !isa<CXXConstructorDecl>(C.Function))
      --i;
    const NamedDecl *nd = C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd))
      nd = llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    const FunctionDecl *f = nd->getAsFunction();
    std::vector<std::string> res;
    if (!f || !f->isTemplated())
      return {};
    const auto params = f->parameters();
    if (i < 0)
      return {};
    if ((unsigned)i < params.size() && params[i]->isTemplated())
      return params[i]->getType().getAsString();
    return {};
  }
  std::deque<std::string> getTemplateParams(const OverloadCandidate &C) const {
    std::deque<std::string> res;
    if (C.Function == nullptr)
      return {};
    const auto *templateArgs = C.Function->getTemplateSpecializationArgs();
    if (auto *ptemplates = C.Function->getPrimaryTemplate())
      if (ptemplates->getTemplateParameters())
        for (size_t i = 0; i < templateArgs->size(); ++i) {
          res.push_back({});
          llvm::raw_string_ostream os(res.back());
          os << ptemplates->getTemplateParameters()
                    ->asArray()[i]
                    ->getNameAsString()
             << " = ";
          templateArgs->data()[i].dump(os);
        }
    return res;
  };
  std::vector<std::tuple<QualType, bool>>
  getSignatureTypes(const OverloadCandidate &C) const {
    if (C.Function == nullptr)
      return {};
    std::vector<std::tuple<QualType, bool>> res;
    QualType thisType = getThisType(C);
    if (thisType != QualType{})
      res.emplace_back(std::tuple{thisType, false});
    if (!C.Function->param_empty())
      for (size_t i = 0; i != C.Function->param_size(); ++i) {
        const auto *x = C.Function->parameters()[i];
        res.emplace_back(x->getType(), x->hasDefaultArg());
      }
    return res;
  }
  std::deque<std::string> getParamTypes(const OverloadCandidate &C) const {
    // const FunctionDecl* f=C.Function;
    std::deque<std::string> res;
    // llvm::raw_string_ostream os(res);
    const auto types = getSignatureTypes(C);
    size_t i = 0;
    if (getThisType(C) != QualType{}) {
      res.push_back(getThisType(C).getCanonicalType().getAsString() + "=this");
      ++i;
    }
    for (; i < types.size(); i++) {
      const auto &[type, isDefaulted] = types[i];
      res.push_back(type.getCanonicalType().getAsString() +
                    (isDefaulted ? "=default" : ""));
    }
    if (C.Function && C.Function->getEllipsisLoc() != SourceLocation())
      res.push_back("...");
    return res;
  }
  std::vector<ExprValueKind> getCallKinds() const {
    std::vector<ExprValueKind> res;
    if (const Expr *Oe = getSetArgs().ObjectExpr)
      res.push_back(Oe->getValueKind());
    auto Args = getSetArgs().inArgs;
    /*if (Args.size()==1&&isa<clang::InitListExpr>(Args[0])){
      Args = llvm::dyn_cast<const clang::InitListExpr>(Args[0])->inits();
    }*/
    for (const auto *Arg : Args)
      res.push_back(Arg->getValueKind());
    return res;
  }
  SourceRange sr;
  SourceRange makeSrFromArgs(ArrayRef<Expr *> Args, SourceLocation Loc) const {
    // Args can be reversed.
    return makeSourceRangeFromPossibleEndpoints(
        {Loc,
         (Args.size() && Args[0] ? Args[0]->getBeginLoc() : SourceLocation{}),
         (Args.size() && Args.back() ? Args.back()->getBeginLoc()
                                     : SourceLocation{})},
        {Loc,
         (Args.size() && Args[0] ? Args[0]->getEndLoc() : SourceLocation{}),
         (Args.size() && Args.back() ? Args.back()->getEndLoc()
                                     : SourceLocation{}),
         getSetArgs().EndLoc});
  };
  std::string getObjTypeStr(const Expr *Oe) const {
    if (const auto *UOe = llvm::dyn_cast<UnresolvedMemberExpr>(Oe))
      return UOe->getBaseType().getCanonicalType().getAsString();
    return Oe->getType().getCanonicalType().getAsString();
  }
  std::deque<std::string> getCallTypes(ArrayRef<Expr *> Args) const {
    std::deque<std::string> res;
    if (const Expr *Oe = getSetArgs().ObjectExpr)
      res.push_back("(Obj:" + getObjTypeStr(Oe) + ')');
    for (const auto &x : Args) {
      if (x == nullptr)
        res.push_back("NULL");
      else if (isa<clang::InitListExpr>(x))
        res.push_back("InitizerList");
      else
        res.push_back(x->getType().getCanonicalType().getAsString());
    }

    const auto callKinds = getCallKinds();
    for (size_t i = 0; i != callKinds.size(); ++i)
      res[i] += Kinds[callKinds[i]];
    return res;
  }
  OvInsResEntry getResEntry(OverloadingResult ovres,
                            const OverloadCandidate *BestOrProblem) {
    OvInsResEntry res;
    ArrayRef<Expr *> Args = getSetArgs().inArgs;
    sr = makeSrFromArgs(Args, Loc);
    res.isImplicit = getSetArgs().isImplicit;
    res.callSrc = getSrcFromRange(sr);
    res.callSrc.Loc = Loc;
    res.ovRes = ovres;
    if (ovres == OR_Success)
      res.best = getCandEntry(*BestOrProblem);
    else if (ovres == clang::OR_Ambiguous) {
      for (const auto &cand : *Set)
        if (cand.Best)
          res.problems.push_back(getCandEntry(cand));
    } else if (BestOrProblem)
      res.problems = {getCandEntry(*BestOrProblem)};
    res.callTypes = getCallTypes(Args);
    for (const auto &cand : *Set)
      if (cand.Viable)
        addCand(res.viableCandidates, getCandEntry(cand));
      else if (settings.ShowNonViableCands) {
        auto x = getCandEntry(cand);
        if (x.src.sourceLoc != "Built-in" || settings.ShowBuiltInNonViable)
          addCand(res.nonViableCandidates, std::move(x));
      }
    return res;
  }
  void addCand(std::vector<OvInsCandEntry> &v, OvInsCandEntry &&cand) const {
    for (const auto &c : v)
      if (c == cand)
        return;
    v.emplace_back(cand);
  }
};
} // namespace
std::unique_ptr<ASTConsumer>
OvInsDumpAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return std::make_unique<ASTConsumer>();
}
void OvInsDumpAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  EnsureSemaIsCreated(CI, *this);
  auto x = std::make_unique<DefaultOverloadInstCallback>();
  x->setSettings(CI.getFrontendOpts().OvInsSettings);
  CI.getSema().OverloadInspectionCallbacks.push_back(std::move(x));
  ASTFrontendAction::ExecuteAction();
}
