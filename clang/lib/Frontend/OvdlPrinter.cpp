#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/OverloadCallback.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/BTF/BTF.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <deque>
#include <iterator>
#include <numeric>
#include <optional>
#include <string>
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
struct OvdlConvEntry{
  std::string path,pathInfo;
  std::string kind;
  SourceLocation loc;
  bool operator==(const OvdlConvEntry& o) const{
    return path==o.path && pathInfo == o.pathInfo && kind==o.kind;
  }
};
struct OvdlTemplateSpec{
  std::string declLocation;
  std::string source;
  bool isExact;
  std::vector<std::string> params;
};
struct OvdlCandEntry{
  std::string declLocation;
  clang::SourceLocation declLoc;
  std::string usingLocation;
  clang::SourceLocation usingLoc;
  std::string name;
  bool isSurrogate=false;
  std::deque<std::string> signature;
  std::string templateSource;
  std::vector<OvdlTemplateSpec> templateSpecs;
  std::optional<OverloadFailureKind> failKind;
  std::optional<std::string> extraFailInfo;
  std::vector<OvdlConvEntry> Conversions;
  std::deque<std::string> templateParams;
  bool operator==(const OvdlCandEntry& o) const{
    return declLocation == o.declLocation && name == o.name &&
      signature == o.signature && templateSource == o.templateSource &&
      failKind == o.failKind && extraFailInfo == o.extraFailInfo &&
      Conversions == o.Conversions;
  }
};
struct OvdlCompareEntry {
  OvdlCandEntry C1,C2;
  std::string reason;
  bool C1Better;
  std::optional<std::string> deciderConversion;
  std::vector<std::string> conversionCompares;
  bool operator==(const OvdlCompareEntry& o)const{
    return C1 == o.C1 && C2 == o.C2 && reason == o.reason &&
      C1Better == o.C1Better && deciderConversion == o.deciderConversion &&
      conversionCompares == o.conversionCompares;
  }
};

struct OvdlResEntry{
  std::vector<OvdlCandEntry> viableCandidates,nonViableCandidates;
  std::optional<OvdlCandEntry> best;
  std::vector<OvdlCandEntry> problems;
  std::vector<OvdlCompareEntry> compares;
  std::string callLocation;
  std::string callSignature;
  clang::SourceRange callRange;
  clang::OverloadingResult ovRes;
  SourceLocation callLoc;
  std::deque<std::string> callTypes;
  std::string note;
  bool isImplicit=false;
};
struct OvdlResNode{
  OvdlResEntry Entry;
  size_t line;
  std::string Fname;
};
class OvdlResCont{
  std::vector<OvdlResNode> data;
public:
  using iterator=std::vector<OvdlResNode>::iterator;
  using const_iterator=std::vector<OvdlResNode>::const_iterator;
  iterator begin() { return data.begin();}
  iterator end() { return data.end(); }
  const_iterator begin() const { return data.begin();}
  const_iterator end() const { return data.end(); }
  void add(const OvdlResNode& newnode){
    data.push_back(newnode);
  }
};
} // namespace
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(OvdlCandEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvdlCompareEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvdlConvEntry);
LLVM_YAML_IS_SEQUENCE_VECTOR(OvdlTemplateSpec);
namespace llvm{
namespace yaml{
template <> struct ScalarEnumerationTraits<OverloadingResult>{
  static void enumeration(IO& io, OverloadingResult& val){
    io.enumCase(val,"Success" ,OR_Success);
    io.enumCase(val,"NoViableFunction" ,OR_No_Viable_Function);
    io.enumCase(val,"Deleted" ,OR_Deleted);
    io.enumCase(val,"Ambigius" ,OR_Ambiguous);
  }
};
template <> struct ScalarEnumerationTraits<BetterOverloadCandidateReason>{
  static void enumeration(IO& io, BetterOverloadCandidateReason& val){
      io.enumCase(val,"viability",viability);
      io.enumCase(val,"CUDAEmit",CUDAEmit);
      io.enumCase(val,"badConversion",badConversion);
      io.enumCase(val,"betterConversion",betterConversion);
      io.enumCase(val,"betterImplicitConversion",betterImplicitConversion);
      io.enumCase(val,"constructor",constructor);
      io.enumCase(val,"isSpecialization",isSpecialization);
      io.enumCase(val,"moreSpecialized",moreSpecialized);
      io.enumCase(val,"isInherited",isInherited);
      io.enumCase(val,"derivedFromOther",derivedFromOther);
      io.enumCase(val,"RewriteKind",RewriteKind);
      io.enumCase(val,"guideImplicit",guideImplicit);
      io.enumCase(val,"guideTemplated",guideTemplated);
      io.enumCase(val,"guideCopy",guideCopy);
      io.enumCase(val,"enableIf",enableIf);
      io.enumCase(val,"parameterObjectSize",parameterObjectSize);
      io.enumCase(val,"multiversion",multiversion);
      io.enumCase(val,"CUDApreference",CUDApreference);
      io.enumCase(val,"addressSpace",addressSpace);
      io.enumCase(val,"inconclusive",inconclusive);
  }
};
template <> struct ScalarEnumerationTraits<clang::OverloadFailureKind>{
  static void enumeration(IO& io, clang::OverloadFailureKind& val){
    io.enumCase(val, "ovl_fail_too_many_arguments",
                ovl_fail_too_many_arguments);
    io.enumCase(val, "ovl_fail_too_few_arguments", ovl_fail_too_few_arguments);
    io.enumCase(val, "ovl_fail_bad_conversion", ovl_fail_bad_conversion);
    io.enumCase(val, "ovl_fail_bad_deduction", ovl_fail_bad_deduction);
    io.enumCase(val, "ovl_fail_trivial_conversion",
                ovl_fail_trivial_conversion);
    io.enumCase(val, "ovl_fail_illegal_constructor",
                ovl_fail_illegal_constructor);
    io.enumCase(val, "ovl_fail_bad_final_conversion",
                ovl_fail_bad_final_conversion);
    io.enumCase(val, "ovl_fail_final_conversion_not_exact",
                ovl_fail_final_conversion_not_exact);
    io.enumCase(val, "ovl_fail_bad_target", ovl_fail_bad_target);
    io.enumCase(val, "ovl_fail_enable_if", ovl_fail_enable_if);
    io.enumCase(val, "ovl_fail_explicit", ovl_fail_explicit);
    io.enumCase(val, "ovl_fail_addr_not_available",
                ovl_fail_addr_not_available);
    io.enumCase(val, "ovl_fail_inhctor_slice", ovl_fail_inhctor_slice);
    io.enumCase(val, "ovl_non_default_multiversion_function",
                ovl_non_default_multiversion_function);
    io.enumCase(val, "ovl_fail_object_addrspace_mismatch",
                ovl_fail_object_addrspace_mismatch);
    io.enumCase(val, "ovl_fail_constraints_not_satisfied",
                ovl_fail_constraints_not_satisfied);
    io.enumCase(val, "ovl_fail_module_mismatched", ovl_fail_module_mismatched);
    }
};
template <> struct MappingTraits<OvdlConvEntry>{
  static void mapping(IO& io, OvdlConvEntry& fields){
    io.mapRequired("kind",fields.kind);
    io.mapOptional("path",fields.path,"");
    io.mapOptional("pathInfo",fields.pathInfo,"");
  }
};
template <> struct MappingTraits<OvdlTemplateSpec>{
  static void mapping(IO& io, OvdlTemplateSpec& fields){
    io.mapRequired("declLocation",fields.declLocation);
    io.mapRequired("source",fields.source);
    io.mapRequired("IsExact",fields.isExact);
  }
};
template <> struct MappingTraits<OvdlCandEntry>{
  static void mapping(IO& io, OvdlCandEntry& fields){
    io.mapRequired("Name",fields.name);
    io.mapOptional("TemplateParams",fields.templateParams);
    io.mapRequired("Signature",fields.signature);
    io.mapRequired("declLocation",fields.declLocation);
    io.mapOptional("usingLocation",fields.usingLocation,"");
    io.mapOptional("isSurrogate",fields.isSurrogate,false);
    io.mapOptional("templateSource", fields.templateSource,"");
    //io.mapOptional("templateSpecs (not involved in overloading (overloading is decided before specialisation))", fields.templateSpecs);
    io.mapOptional("templateSpecs (overloading is decided before specialisation)", fields.templateSpecs);
    io.mapOptional("Conversions", fields.Conversions);
    io.mapOptional("FailureKind", fields.failKind);
    io.mapOptional("extraFailInfo", fields.extraFailInfo);
  }
};
template <>
struct SequenceTraits<std::deque<std::string>> {
  static size_t size(IO &io, std::deque<std::string> &list) {
    return list.size();
  }
  static std::string &element(IO &io, std::deque<std::string> &list,
      size_t index) {
    return list[index];
  }
  static const bool flow = true;
};
template <> struct MappingTraits<OvdlCompareEntry>{
  static void mapping(IO& io, OvdlCompareEntry& fields){
    io.mapRequired("C1",fields.C1);
    io.mapRequired("C2",fields.C2);
    io.mapRequired("C1Better",fields.C1Better);
    io.mapRequired("reason",fields.reason);
    io.mapOptional("Conversions", fields.conversionCompares);
    io.mapOptional("Decider", fields.deciderConversion);
  }
};
template <> struct MappingTraits<OvdlResEntry>{
  static void mapping(IO& io, OvdlResEntry& fields){
    io.mapRequired("callLocation",fields.callLocation);
    io.mapRequired("callSignature",fields.callSignature);
    io.mapRequired("callTypes",fields.callTypes);
    io.mapRequired("overloading result",fields.ovRes);
    io.mapRequired("viable candidates",fields.viableCandidates);

    io.mapOptional("non viable candidates",fields.nonViableCandidates);
    io.mapOptional("best",fields.best);
    io.mapOptional("problems",fields.problems);
    io.mapOptional("compares",fields.compares);
    io.mapOptional("Implicit",fields.isImplicit,false);
    io.mapOptional("note",fields.note,"");
  }
};

}//namespace yaml
}//namespace llvm

namespace{
std::string toString(ImplicitConversionKind e){
  switch(e){
  case ICK_Identity: return "Identity";
  case ICK_Lvalue_To_Rvalue: return "Lvalue_To_Rvalue";
  case ICK_Array_To_Pointer: return "Array_To_Pointer";
  case ICK_Function_To_Pointer: return "Function_To_Pointer";
  case ICK_Function_Conversion: return "Function_Conversion";
  case ICK_Qualification: return "Qualification";
  case ICK_Integral_Promotion: return "Integral_Promotion";
  case ICK_Floating_Promotion: return "Floating_Promotion";
  case ICK_Complex_Promotion: return "Complex_Promotion";
  case ICK_Integral_Conversion: return "Integral_Conversion";
  case ICK_Floating_Conversion: return "Floating_Conversion";
  case ICK_Complex_Conversion: return "Complex_Conversion";
  case ICK_Floating_Integral: return "Floating_Integral";
  case ICK_Pointer_Conversion: return "Pointer_Conversion";
  case ICK_Pointer_Member: return "Pointer_Member";
  case ICK_Boolean_Conversion: return "Boolean_Conversion";
  case ICK_Compatible_Conversion: return "Compatible_Conversion";
  case ICK_Derived_To_Base: return "Derived_To_Base";
  case ICK_Vector_Conversion: return "Vector_Conversion";
  case ICK_SVE_Vector_Conversion: return "SVE_Vector_Conversion";
  case ICK_RVV_Vector_Conversion: return "RVV_Vector_Conversion";
  case ICK_Vector_Splat: return "Vector_Splat";
  case ICK_Complex_Real: return "Complex_Real";
  case ICK_Block_Pointer_Conversion: return "Block_Pointer_Conversion";
  case ICK_TransparentUnionConversion: return "TransparentUnionConversion";
  case ICK_Writeback_Conversion: return "Writeback_Conversion";
  case ICK_Zero_Event_Conversion: return "Zero_Event_Conversion";
  case ICK_Zero_Queue_Conversion: return "Zero_Queue_Conversion";
  case ICK_C_Only_Conversion: return "C_Only_Conversion";
  case ICK_Incompatible_Pointer_Conversion:
    return "Incompatible_Pointer_Conversion";
  case ICK_Num_Conversion_Kinds: return "Num_Conversion_Kinds";
  }
  llvm_unreachable("Unknown ImplicitConversionKind");
}
std::string toString(clang::OverloadFailureKind r){
  switch (r) {
  case ovl_fail_too_many_arguments: return "ovl_fail_too_many_arguments";
  case ovl_fail_too_few_arguments: return "ovl_fail_too_few_arguments";
  case ovl_fail_bad_conversion: return "ovl_fail_bad_conversion";
  case ovl_fail_bad_deduction: return "ovl_fail_bad_deduction";
  case ovl_fail_trivial_conversion: return "ovl_fail_trivial_conversion";
  case ovl_fail_illegal_constructor: return "ovl_fail_illegal_constructor";
  case ovl_fail_bad_final_conversion: return "ovl_fail_bad_final_conversion";
  case ovl_fail_final_conversion_not_exact: return "ovl_fail_final_conversion_not_exact";
  case ovl_fail_bad_target: return "ovl_fail_bad_target";
  case ovl_fail_enable_if: return "ovl_fail_enable_if";
  case ovl_fail_explicit: return "ovl_fail_explicit";
  case ovl_fail_addr_not_available: return "ovl_fail_addr_not_available";
  case ovl_fail_inhctor_slice: return "ovl_fail_inhctor_slice";
  case ovl_non_default_multiversion_function: return "ovl_non_default_multiversion_function";
  case ovl_fail_object_addrspace_mismatch: return "ovl_fail_object_addrspace_mismatch";
  case ovl_fail_constraints_not_satisfied: return "ovl_fail_constraints_not_satisfied";
  case ovl_fail_module_mismatched: return "ovl_fail_module_mismatched";
  }
  llvm_unreachable("Unknown OFK");
}
std::string toString(clang::OverloadingResult r){
  switch(r){
  case OR_Success:
    return "OR_Success";
  case OR_No_Viable_Function:
    return "OR_No_Viable_Function";
  case OR_Ambiguous:
    return "OR_Ambiguous";
  case OR_Deleted:
    return "OR_Deleted";
  }
  llvm_unreachable("Unknown OR");
}
std::string toString(Sema::TemplateDeductionResult r){
  switch (r) {
  case Sema::TDK_Success: return "TDK_Success";
  case Sema::TDK_Invalid: return "TDK_Invalid";
  case Sema::TDK_InstantiationDepth: return "TDK_InstantiationDepth";
  case Sema::TDK_Incomplete: return "TDK_Incomplete";
  case Sema::TDK_IncompletePack: return "TDK_IncompletePack";
  case Sema::TDK_Inconsistent: return "TDK_Inconsistent";
  case Sema::TDK_Underqualified: return "TDK_Underqualified";
  case Sema::TDK_SubstitutionFailure: return "TDK_SubstitutionFailure";
  case Sema::TDK_DeducedMismatch: return "TDK_DeducedMismatch";
  case Sema::TDK_DeducedMismatchNested: return "TDK_DeducedMismatchNested";
  case Sema::TDK_NonDeducedMismatch: return "TDK_NonDeducedMismatch";
  case Sema::TDK_TooManyArguments: return "TDK_TooManyArguments";
  case Sema::TDK_TooFewArguments: return "TDK_TooFewArguments";
  case Sema::TDK_InvalidExplicitArguments:
    return "TDK_InvalidExplicitArguments";
  case Sema::TDK_NonDependentConversionFailure:
    return "TDK_NonDependentConversionFailure";
  case Sema::TDK_ConstraintsNotSatisfied:
    return "TDK_ConstraintsNotSatisfied";
  case Sema::TDK_MiscellaneousDeductionFailure:
    return "TDK_MiscellaneousDeductionFailure";
  case Sema::TDK_CUDATargetMismatch: return "TDK_CUDATargetMismatch";
  case Sema::TDK_AlreadyDiagnosed: return "TDK_AlreadyDiagnosed";
  }
llvm_unreachable("unknown TemplateDeductionResult");
}
std::string toString(ImplicitConversionRank e){
  switch(e){
  case ICR_Exact_Match:
    return "Exact Match";
  case ICR_Promotion:
    return "Promotion";
  case ICR_Conversion:
    return "Conversion";
  case ICR_OCL_Scalar_Widening:
    return "ScalarWidening";
  case ICR_Complex_Real_Conversion:
    return "Complex-Real";
  case ICR_Writeback_Conversion:
    return "Writeback";
  case ICR_C_Conversion:
    return "C Only";
  case ICR_C_Conversion_Extension:
    return "Not standard";
  }
llvm_unreachable("unknown enum");
}
std::string toString(BadConversionSequence::FailureKind k){
  switch (k) {
  case BadConversionSequence::no_conversion:
    return "no_conversion";
  case BadConversionSequence::unrelated_class:
    return "unrelated_class";
  case BadConversionSequence::bad_qualifiers:
    return "bad_qualifiers";
  case BadConversionSequence::lvalue_ref_to_rvalue:
    return "lvalue_ref_to_rvalue";
  case BadConversionSequence::rvalue_ref_to_lvalue:
    return "rvalue_ref_to_lvalue";
  case BadConversionSequence::too_few_initializers:
    return "too_few_initializers";
  case BadConversionSequence::too_many_initializers:
    return "too_many_initializers";
  }
llvm_unreachable("unknown FaliureKind");
}
std::string toString(BetterOverloadCandidateReason r){
  switch(r){
    case viability:
    return "viability";
  case CUDAEmit:
    return "CUDAEmit";
  case badConversion:
    return "badConversion";
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
  case guideTemplated:
    return "guideTemplated";
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
  llvm_unreachable("Unknown BetterOverloadCandidateReason");
};
std::string getConversionSeq(const StandardConversionSequence &cs) {
  std::string res;
  llvm::raw_string_ostream os(res);
  bool PrintedSomething = false;
  if (cs.First != ICK_Identity) {
    os << toString(cs.First);
    PrintedSomething = true;
  }

  if (cs.Second != ICK_Identity) {
    if (PrintedSomething) {
      os << " -> ";
    }
    os << toString(cs.Second);

    if (cs.CopyConstructor) {
      os << " (by copy constructor)";
    } else if (cs.DirectBinding) {
      os << " (direct reference binding)";
    } else if (cs.ReferenceBinding) {
      os << " (reference binding)";
    }
    PrintedSomething = true;
  }

  if (cs.Third != ICK_Identity) {
    if (PrintedSomething) {
      os << " -> ";
    }
    os << toString(cs.Third);
    PrintedSomething = true;
  }

  if (!PrintedSomething) {
    os << "No conversions required";
  }
  return res;
}
std::string getConversionSeq(const UserDefinedConversionSequence &cs) {
  std::string res;
  llvm::raw_string_ostream os(res);
  if (cs.Before.First || cs.Before.Second || cs.Before.Third) {
    os << getConversionSeq(cs.Before);
    os << " -> ";
  }
  if (cs.ConversionFunction) {
    os <<'"'<< cs.ConversionFunction->getQualifiedNameAsString()<<'"';
  } else
    os << "aggregate initialization";
  if (cs.After.First || cs.After.Second || cs.After.Third) {
    os << " -> ";
    os << getConversionSeq(cs.After);
  }
  return res;
}

const QualType getFromType(const ImplicitConversionSequence& C){
  if (! C.isInitialized()) return {};
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

const QualType getToType(const ImplicitConversionSequence& C){
  if (! C.isInitialized()) return {};
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
QualType getToType(const OverloadCandidate& C,int idx){
  if (C.Function==nullptr)
    return getToType(C.Conversions[idx]);
  if (C.Conversions[idx].getKind()==ImplicitConversionSequence::Kind::EllipsisConversion)
    return QualType{};
  //if (0&&isa<CXXConversionDecl>(C.Function))
  //  return C.FinalConversion.getToType(2);
  if (isa<CXXMethodDecl>(C.Function) &&
      !isa<CXXConstructorDecl>(C.Function))
    --idx;
    //dyn_cast<CXXMethodDecl>(C.Function);
  if (idx==-1){
    return getToType(C.Conversions[0]);//TODO:thistype
    //return llvm::dyn_cast<CXXMethodDecl>(C.Function)->getThisType();
  }
  return C.Function->parameters()[idx]->getType();
}
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
  using CompareKind = clang::ImplicitConversionSequence::CompareKind;
  static constexpr llvm::StringLiteral Kinds[]={"[temporary]","","&&"};
  const Sema* S=nullptr;
  bool inBestOC=false;
  bool inCompare=false;
  const OverloadCandidateSet* Set=nullptr;
  SourceLocation Loc={};
  std::vector<OvdlCompareEntry> compares;
  OvdlResCont cont;
  clang::FrontendOptions::OvdlSettingsType settings;
  std::vector<CompareKind> compareResults;
  struct SetArgs{
    const OverloadCandidateSet* Set;
    llvm::SmallVector<Expr*> Args={};
    const Expr* ObjectExpr=nullptr;
    SourceLocation EndLoc={};
    SourceLocation Loc={};
    bool valid=0;
  };
  std::unordered_map<const OverloadCandidateSet*, SetArgs> SetArgMap;
public:
  virtual void addSetInfo(const OverloadCandidateSet& Set,const SetInfo& S)override{
    SetArgMap[&Set].Set=&Set;
    if (SetArgMap[&Set].valid && SetArgMap[&Set].Loc!=Set.getLocation()){
      SetArgMap[&Set]={};//Reset because the info is outdated
      SetArgMap[&Set].Set=&Set;
    }
    SetArgMap[&Set].valid=true;
    if (S.Args)
      SetArgMap[&Set].Args=llvm::SmallVector<Expr*>(*S.Args);
    if (S.Args->size()==1 && (*S.Args)[0]==nullptr){
      //NEVER any more?
      //assert(0);
      SetArgMap[&Set].Args=llvm::SmallVector<Expr*>();
    }
    if (S.ObjectExpr)
      SetArgMap[&Set].ObjectExpr=*S.ObjectExpr;
    if (S.EndLoc)
      SetArgMap[&Set].EndLoc=*S.EndLoc;
    SetArgMap[&Set].Loc=Set.getLocation();
  };
  virtual bool needAllCompareInfo() const override{
    return settings.ShowConversions==clang::FrontendOptions::SC_Verbose &&
      inBestOC && settings.ShowCompares;
  };
  virtual void setCompareInfo(const std::vector<CompareKind>& c)override{
    if (needAllCompareInfo())
      compareResults=c;
  };
  void setSettings(const clang::FrontendOptions::OvdlSettingsType &s) {
    settings = s;
  }
  virtual void initialize(const Sema&) override{};
  virtual void finalize(const Sema&) override{};
  virtual void atEnd() override{
    for (auto& x:cont){
      displayOvdlResEntry(llvm::outs(),x.Entry);
    }
    printHumanReadable();
    cont={};
  }
  virtual void atOverloadBegin(const Sema &s, const SourceLocation &loc,
                               const OverloadCandidateSet &set) override {
    S=&s;
    Set=&set;
    Loc=loc;

    if (!SetArgMap[&set].valid || SetArgMap[&set].Loc != Set->getLocation()) return;
    PresumedLoc L = S->getSourceManager().getPresumedLoc(loc);
    unsigned line=L.getLine();
    if ((L.getIncludeLoc().isValid() && !settings.ShowIncludes) ||
        !inSetInterval(line) ||
        (!settings.ShowEmptyOverloads && set.empty())) {
      return;
    }
    if (SetArgMap.find(&set)==SetArgMap.end())
      return;
    compares = {};
    inBestOC = true;
  }
  virtual void atOverloadEnd(const Sema&s,const SourceLocation& loc,
        const OverloadCandidateSet& set, OverloadingResult ovRes,
        const OverloadCandidate* BestOrProblem) override{
    if (!inBestOC)return;
    OvdlResNode node;
    PresumedLoc L=S->getSourceManager().getPresumedLoc(loc);
    node.line=L.getLine();
    node.Fname=L.getFilename();
    node.Entry=getResEntry(ovRes,BestOrProblem);
    node.Entry.compares=std::move(compares);
    compares={};
    if (settings.ShowCompares!=FrontendOptions::SC_Verbose)
      filterForRelevant(node.Entry);
    if (settings.SummarizeBuiltInBinOps)
      summarizeBuiltInBinOps(node.Entry);
    if (!node.Entry.isImplicit || settings.ShowImplicitConversions){
      cont.add(node);
      //printResEntry(node.Entry);
    }
    inBestOC=false;
  }
  virtual void atCompareOverloadBegin(const Sema &S, const SourceLocation &Loc,
                                      const OverloadCandidate &C1,
                                      const OverloadCandidate &C2) override {
    if (!inBestOC || !settings.ShowCompares) {return;}
    inCompare=true;
  }
  virtual void atCompareOverloadEnd(const Sema &TheSema,
                                    const SourceLocation &Loc,
                                    const OverloadCandidate &Cand1,
                                    const OverloadCandidate &Cand2, bool res,
                                    BetterOverloadCandidateReason reason,
                                    int infoIdx) override {
    if (!inBestOC || !settings.ShowCompares) {return;}
    PresumedLoc L=S->getSourceManager().getPresumedLoc(Loc);
    if (!L.isValid() ||
        (L.getIncludeLoc().isValid() && !settings.ShowIncludes) ||
        !inSetInterval(L.getLine())){
      inCompare=false;
      return;
    }
    OvdlCompareEntry Entry;
    Entry.C1Better=res;
    Entry.C1=getCandEntry(Cand1);
    Entry.C2=getCandEntry(Cand2);
    if (Entry.C1 == Entry.C2)
      return;                    // EquivalentInternalLinkageDeclaration
    for (const auto& E:compares){//Removeing duplicates
      if (E.C1==Entry.C1 && E.C2==Entry.C2){inCompare=false;return;}
    }
    Entry.reason=toString(reason);
    const auto& callKinds=getCallKinds();
    if (reason==clang::betterConversion){
      Entry.deciderConversion = ConversionCompareAsString(Cand1,Cand2,infoIdx,
        res?CompareKind::Better:CompareKind::Worse,callKinds) +
        "    Place: "+std::to_string(infoIdx+1);
    }else if (reason==clang::badConversion){
      Entry.deciderConversion =
        getFromType(Cand1.Conversions[infoIdx]).getAsString()+" -> "+
        getToType((res?Cand2:Cand1),infoIdx).getAsString()+" is ill formated";
    }
    for (size_t i=0; i!= compareResults.size(); ++i){
      bool isStaticCall=SetArgMap[Set].ObjectExpr==nullptr&&
        (Cand1.IgnoreObjectArgument || Cand2.IgnoreObjectArgument);
      Entry.conversionCompares.push_back(ConversionCompareAsString(
            Cand1,Cand2,i+isStaticCall,compareResults[i],callKinds));
    }
    for (auto &E : compares) { // Removeing mirrors
      if (E.C1==Entry.C2 && E.C2==Entry.C1){
        if (!E.C1Better && Entry.C1Better){
          E=Entry;//Keep the one where C1 is better
          inCompare=false;
          return;
        }else if (!E.C1Better && !Entry.C1Better){
          /*Ambigioty, keep both*/
        }else{ 
          inCompare=false;
          return;
        }
      }
    }
    compares.push_back(Entry);
          inCompare=false;
  }
private:
  SourceRange makeSR(const std::vector<SourceLocation>& begs,const std::vector<SourceLocation>& ends)const{
    SourceLocation beg={};
    for (const auto& x:begs){
      if (x.isValid() && (beg.isInvalid()||x<beg))beg=x;
    }
    SourceLocation end={};
    for (const auto& x:ends){
      if (x.isValid() && (end.isInvalid() || x>end))end=x;
    }
    return {beg,end};
  }
  void printConvEntry(const OvdlConvEntry& Entry,std::string nm="")const {
    unsigned ID1=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Note, "%0 conversion: \n%1\n%2");
    unsigned ID1uninited=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Note, "%0 conversion");
    if (Entry.path!="")
      S->Diags.Report(Entry.loc,ID1)<<Entry.kind<<Entry.path<<Entry.pathInfo;
    else
      S->Diags.Report(Entry.loc,ID1uninited)<<Entry.kind;
  }
  template<class T>
  std::string concat(const T& in, const std::string& sep)const{
    std::string res;
    if (in.empty())return res;
    llvm::raw_string_ostream os(res);
    os<<'[';
    for (size_t i=0; i<in.size()-1;++i){
      os<<in[i]<<sep;
    }
    os<<in.back()<<']';
    return res;
  }
  void printCompareEntry(const OvdlCompareEntry& Entry,SourceLocation loc) const{
    unsigned ID1=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Note, "Compearing candidates resulted in %0 reason: %1 %2 %3");
    S->Diags.Report(loc,ID1)<<(Entry.C1Better?"The first is better":"The first is not better")
                  <<(Entry.conversionCompares.empty()?"":"\n\tConversions:"+concat(Entry.conversionCompares,"\n\t"))
                  <<Entry.reason<<(Entry.deciderConversion==""?"":"\n\tDeider: "+*Entry.deciderConversion);
    printCandEntry(Entry.C1,Entry.C1Better?"Better ":"Not Better");
    printCandEntry(Entry.C2,Entry.C1Better?"Worse ":"Not Worse");
  }
  void printCandEntry(const OvdlCandEntry& Entry,std::string nm="")const {
    unsigned ID1=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Note, "%0candidate: %1 %2 %3 %4");
    std::string temp=concat(Entry.templateParams,", ");
    S->Diags.Report(Entry.declLoc, ID1)<<nm<<Entry.name
            <<(temp=="[]"?"":"\n\tTemplate params: "+temp)
            <<(Entry.failKind?"\n\tFailure reason: "+toString(*Entry.failKind):"")
            <<(Entry.extraFailInfo?*Entry.extraFailInfo:"");
    for (const auto& x:Entry.Conversions){
      printConvEntry(x);
    }
  }
  void printResEntry(const OvdlResEntry& Entry)const{
    std::string types=concat(Entry.callTypes,", ");
    auto ID0=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Remark, "Overload resulted with %0 With types %1 %2");
    S->Diags.Report(Entry.callLoc, ID0)<<toString(Entry.ovRes)<<types<<Entry.note
              <<Entry.callRange;
    if (Entry.best){
      printCandEntry(*Entry.best,"Best ");
    }
    for (const auto& x:Entry.problems){
      printCandEntry(x,Entry.ovRes==clang::OR_Ambiguous?"Ambigius ":"Unresolvable ");//OR unresolvable concept
    }
    for (const auto& x:Entry.viableCandidates){
      printCandEntry(x,"Viable ");
    }
    for (const auto& x:Entry.nonViableCandidates){
      printCandEntry(x,"Non viable ");
    }
    for (const auto& x:Entry.compares){
      printCompareEntry(x,Entry.callLoc);
    }
    //Entry.note;
  }
  void printHumanReadable() const {
    for (const auto& x:cont){
      printResEntry(x.Entry);
    }
  }
  const SetArgs& getSetArgs()const{
    auto it=SetArgMap.find(Set);
    assert(it!=SetArgMap.end() && "Set must be stored");
    return it->second;
  }
  bool inSetInterval(unsigned x)const{
    if (settings.Intervals.size()==0)
      return true;
    for (const auto& [from,to]:settings.Intervals)
      if (from<=x && x<= to)return true;
    return false;
  }
  std::optional<std::pair<std::string,std::string>> writeBuiltInsBinOpSumm(const OvdlResEntry& E)const{
    std::pair<std::string,std::string> res;
    std::set<std::string> types[3];
    if (Set->getKind()==Set->CandidateSetKind::CSK_Operator){
      for (const auto& c:E.viableCandidates){
        if (!shouldSumm(c)) continue;
        for (size_t i=0; i!=c.signature.size(); i++){
          types[i].emplace(c.signature[i]);
        }
      }
      if (types[0].empty() || types[1].empty() || !types[2].empty())
        return {};//Non bin op
      llvm::raw_string_ostream os(res.first);
      for (auto& x:types[0]) os<<x<<'/';
      res.first.pop_back();
      llvm::raw_string_ostream os2(res.second);
      for (auto& x:types[1]) os2<<x<<'/';
      res.second.pop_back();
      return res;
    }
    return {};
  }
  std::string getTokenFromLoc(const SourceLocation& loc) const{
    if (loc==SourceLocation{})
      return "";
    SourceLocation endloc(Lexer::getLocForEndOfToken(
            loc, 0, S->getSourceManager(), S->getLangOpts()));
    CharSourceRange range=CharSourceRange::getCharRange(loc,endloc);
    return std::string(Lexer::getSourceText(
          range, S->getSourceManager(), S->getLangOpts()));
  }
  std::string getBuiltInOperatorName()const{
    assert(Set->CSK_Operator == Set->getKind() && "Not operator");
    const char* cc=getOperatorSpelling(Set->getRewriteInfo().OriginalOperator);
    //Not handles all operators
    if (cc)
      return cc;
    std::string s1=getTokenFromLoc(Set->getLocation());
    //Note:there are no builtin operator ->
    if (s1=="(")
      s1="()";
    else if (s1=="[")
      s1="[]";
    else if (s1=="?")
      s1="?:";
    return s1;
  }
  bool shouldSumm(const OvdlCandEntry& cand)const{
    return cand.declLocation=="Built-in" && cand.signature.size()==2 && 
        cand.signature[0]!=cand.signature[1] && cand.name!="()" && 
        cand.name!="," && cand.name!="[]"&&cand.name!="++"&&cand.name!="--";
  }
  void summarizeBuiltInBinOps(OvdlResEntry& E){
    if (const auto& c=writeBuiltInsBinOpSumm(E)){
      for (int i=0; i<(int)E.viableCandidates.size();++i){
        const auto& cand=E.viableCandidates[i];
        if (shouldSumm(cand)){
          std::swap(E.viableCandidates[i],E.viableCandidates.back());
          --i;
          E.viableCandidates.pop_back();
        }
      }
      //E.viableCandidates.push_back(*c);
      auto relevants=getRelevantCands(E);
      for (int i=0; i<(int)E.compares.size();++i){
        const auto& comp=E.compares[i];
        if ((!isIn(relevants,comp.C1) && shouldSumm(comp.C1)) || 
            (!isIn(relevants,comp.C2) && shouldSumm(comp.C2))){
          std::swap(E.compares.back(),E.compares[i]);
          E.compares.pop_back();
          --i;
        }
      }
      E.note+="\nThere are more viable built in functions, with the calltypes combined from "+
        c->first+" and "+c->second+" you can list them with \"ShowBuiltInBinOps\"";
    }

  }
  std::string ConversionCompareAsString(const OverloadCandidate& Cand1,
      const OverloadCandidate& Cand2,int idx,CompareKind cmpRes,
      const std::vector<ExprValueKind>& vkarr)const{
    /*bool isStaticCall=false;
    if (auto *p=llvm::dyn_cast_or_null<CXXMethodDecl>(Cand1.Function)){
      isStaticCall=p->isStatic() && Set->getObjectExpr()==nullptr&&
                    !isa<CXXConstructorDecl>(Cand1.Function);
    }*/
    bool isStaticCall=getSetArgs().ObjectExpr==nullptr&&
        (Cand1.IgnoreObjectArgument || Cand2.IgnoreObjectArgument);
    ExprValueKind vk=idx>=isStaticCall?vkarr[idx-isStaticCall]:VK_LValue;
    const static char compareSigns[]{'>','=','<'};
    std::string res;
    llvm::raw_string_ostream os(res);
    if (!Cand1.Conversions[idx].isInitialized())
      os<<"Uninited";
    else if (Cand1.Conversions[idx].isEllipsis())
      os<<"Ellipsis";
    else if (Cand1.Conversions[idx].isStaticObjectArgument())
      os<<"StaticObjectArgument";
    else{
      os<< '(' << getFromType(Cand1.Conversions[idx]).getCanonicalType().getAsString() <<
        Kinds[vk] << " -> " << getToType(Cand1, idx).getCanonicalType().getAsString();
      std::string temp=getTemplatedParamForConversion(Cand1, idx);
      if (temp!="")
        os<<" = "<<temp;
      os<<')';
    }
    os<< "\t" << compareSigns[cmpRes + 1] << '\t';
    if (!Cand2.Conversions[idx].isInitialized())
      os<<"Uninited";
    else if (Cand2.Conversions[idx].isEllipsis())
      os<<"Ellipsis";
    else if (Cand2.Conversions[idx].isStaticObjectArgument())
      os<<"StaticObjectArgument";
    else{
      os<<'(' <<
        getFromType(Cand2.Conversions[idx]).getCanonicalType().getAsString() <<
        Kinds[vk] << " -> " << getToType(Cand2, idx).getCanonicalType().getAsString();
      std::string temp=getTemplatedParamForConversion(Cand2, idx);
      if (temp!="")
        os<<" = "<<temp;
      os<<')';
    }
    return res;
  }
  std::vector<OvdlCandEntry> getRelevantCands(OvdlResEntry& Entry)const{
    std::vector<OvdlCandEntry> relevants=Entry.problems;
    if (relevants.empty() && Entry.best){
      relevants.push_back(*Entry.best);
    }
    return relevants;
  }
  bool isIn(const std::vector<OvdlCandEntry>& v, OvdlCandEntry c)const{
    for (const auto& e:v){
      if (e==c) return true;
    }
    return false;
  }
  void filterForRelevant(OvdlResEntry& Entry)const{
    std::vector<OvdlCandEntry> relevants=getRelevantCands(Entry);
    for (size_t i=0; i<Entry.compares.size();++i){
      bool isRelevant=isIn(relevants,Entry.compares[i].C1) ||
                      isIn(relevants,Entry.compares[i].C2);
      if (!isRelevant){
        std::swap(Entry.compares[i],Entry.compares.back());
        Entry.compares.pop_back();
        --i;
      }
    }
  }
  std::pair<int,int> NeededArgs(const OverloadCandidate& C)const{
    if (C.IsSurrogate){
      //C.FoundDecl.getDecl();
      const NamedDecl* nd=C.FoundDecl.getDecl();
      while (isa<UsingShadowDecl>(nd)){
        nd=llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
      }
      const auto ty=nd->getAsFunction()->getCallResultType();
        //The type of the functionPtr
      const auto* fptr=ty->getAs<PointerType>()->getPointeeType()->getAs<FunctionProtoType>();
      int cnt=fptr->getNumParams()+1;
      return {cnt,cnt};
    }//TODO
    if (C.Function){
      //Check when deducing this is implemented FIXME
      bool needObj=isa<CXXMethodDecl>(C.Function) && !isa<CXXConstructorDecl>(C.Function);
      if (!C.Function->getEllipsisLoc().isInvalid())
        return {C.Function->getMinRequiredArguments()+needObj,-1};
      return {C.Function->getMinRequiredArguments()+needObj,
              C.Function->param_size()+needObj};
    }
    int res=0;
    for (int i=0; i!=3;i++)
      if (C.BuiltinParamTypes[i]!=QualType{})
        ++res;
    return {res,res};
  }
  std::optional<std::string> getExtraFailInfo(const OverloadCandidate& C)const{
    if (C.Viable) return {};
    std::string res;
    llvm::raw_string_ostream os(res);
    switch ((OverloadFailureKind)C.FailureKind) {
    case ovl_fail_too_many_arguments:
    case ovl_fail_too_few_arguments:{
      const auto &[mn,mx]=NeededArgs(C);
      os<<"Needed: "<<mn;
      if (mn!=mx){
        if(mx==-1)
          os<<"...";
        else
          os<<".."<<mx;
      }
      os<<" Got: "<<getCallKinds().size();
      return res;
    }
    case ovl_fail_bad_conversion:
      for (size_t i=0; i!=C.Conversions.size(); i++){
        if (!C.Conversions[i].isInitialized())continue;
        if (C.Conversions[i].isBad()){
          os << toString(C.Conversions[i].Bad.Kind) <<
                 " Pos: " <<  (1+i) <<  "    From: " <<
                 C.Conversions[i].Bad.getFromType().getAsString() <<
                 Kinds[getCallKinds()[i-C.IgnoreObjectArgument]] << "    To: " <<
                 C.Conversions[i].Bad.getToType().getAsString();
          return res;
        }
      }
      return {};
    case ovl_fail_bad_deduction:
      return toString(Sema::TemplateDeductionResult(C.DeductionFailure.Result));
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
  std::vector<OvdlConvEntry> getConversions(const OverloadCandidate& C)const{
    std::vector<OvdlConvEntry> res;
    const auto& callKinds=getCallKinds();

    bool isStaticCall=getSetArgs().ObjectExpr==nullptr&&C.IgnoreObjectArgument;
    for (size_t i=0; i<C.Conversions.size();++i){
      const ExprValueKind fromKind=(i>=isStaticCall)?callKinds[i-isStaticCall]:VK_LValue;
      const auto& conv=C.Conversions[i];
      int idx=i;
      if (C.Function && isa<CXXMethodDecl>(C.Function) &&
          !isa<CXXConstructorDecl>(C.Function))
        --idx;
      res.push_back({});
      if (idx>=0)
        res.back().loc=SetArgMap.find(Set)->second.Args[idx]->getExprLoc();
      else if (SetArgMap.find(Set)->second.ObjectExpr)
        res.back().loc=SetArgMap.find(Set)->second.ObjectExpr->getExprLoc();
      auto& act=res.back();
      if (!conv.isInitialized()){
        act.kind="Not initialized";
        continue;
      }
      llvm::raw_string_ostream path(act.path);
      switch (conv.getKind()) {
      case ImplicitConversionSequence::StandardConversion:
        act.kind="Standard";
        //conv.Standard.getFromType().getAsString();
        path<<conv.Standard.getFromType().getCanonicalType().getAsString();
        path<<Kinds[fromKind];
        act.pathInfo=getConversionSeq(conv.Standard);
//C.Function->parameters()[idx]->getLocation();
        if (C.Function && idx!=-1  && !isa<clang::InitListExpr>(getSetArgs().Args[idx]))
          path  << " -> " << C.Function->parameters()[idx]->getType().getCanonicalType().getAsString();
        else
          path << " -> " << conv.Standard.getToType(2).getCanonicalType().getAsString();
        break;
      case ImplicitConversionSequence::StaticObjectArgumentConversion:
        act.kind="StaticObjectConversion";
        break;
      case ImplicitConversionSequence::UserDefinedConversion:
        act.kind="UserDefined";
        path << conv.UserDefined.Before.getFromType().getCanonicalType().getAsString();
        path << Kinds[fromKind];
        if (conv.UserDefined.Before.First ||
            conv.UserDefined.Before.Second ||
            conv.UserDefined.Before.Third)
          path << " -> " << conv.UserDefined.Before.getToType(2).getCanonicalType().getAsString();
        act.pathInfo=getConversionSeq(conv.UserDefined);
        if (conv.UserDefined.After.First ||
            conv.UserDefined.After.Second ||
            conv.UserDefined.After.Third)
          path << " -> " << conv.UserDefined.After.getFromType().getCanonicalType().getAsString();
        if (C.Function && idx!=-1&& !isa<clang::InitListExpr>(getSetArgs().Args[idx]))
          path << " -> " << C.Function->parameters()[idx]->getType().getCanonicalType().getAsString();
        else
          path << " -> " << conv.UserDefined.After.getToType(2).getCanonicalType().getAsString();
        break;
      case ImplicitConversionSequence::AmbiguousConversion:
        act.kind="Ambigious";
        path << conv.Ambiguous.getFromType().getCanonicalType().getAsString();
        path << Kinds[fromKind];
        path << " -> " << conv.Ambiguous.getToType().getCanonicalType().getAsString();
        break;
      case ImplicitConversionSequence::EllipsisConversion:
        act.kind="Ellipsis";
        break;
      case ImplicitConversionSequence::BadConversion:
        act.kind="Bad: "+toString(conv.Bad.Kind);
        path << conv.Bad.getFromType().getCanonicalType().getAsString();
        path << Kinds[fromKind];
        path << " -> " << conv.Bad.getToType().getCanonicalType().getAsString();
        break;
      }
      if (C.Function){
      std::string temp=getTemplatedParamForConversion(C, i);
      if (temp!="")
        path<<" = "<<temp;
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
  std::deque<std::string> getSurrSignature(const OverloadCandidate&C) const{
    std::deque<std::string> res;
    const NamedDecl* nd=C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd)){
      nd=llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    }
    const auto ty=nd->getAsFunction()->getCallResultType();
        //The type of the functionPtr
    const auto* fptr=ty->getAs<PointerType>()->getPointeeType()->getAs<FunctionProtoType>();
    res.emplace_back(getSetArgs().ObjectExpr->getType().getCanonicalType().getAsString()+"=*this");
    for (const auto& t:fptr->getParamTypes())
      res.emplace_back(t.getCanonicalType().getAsString());

    return res;
  }
  OvdlCandEntry getCandEntry(const OverloadCandidate &C)const {
    OvdlCandEntry res;
    llvm::raw_string_ostream declLoc(res.declLocation);
    llvm::raw_string_ostream usingLoc(res.usingLocation);
    if (settings.ShowConversions){
      res.Conversions=getConversions(C);//FIXME
    }else
      res.Conversions={};
    if (!C.Viable)
      res.failKind=(OverloadFailureKind)C.FailureKind;
    else
      res.failKind={};
    res.extraFailInfo=getExtraFailInfo(C);
//    llvm::raw_string_ostream signature(res.signature);
    if (C.IsSurrogate){
      if (C.FoundDecl.getDecl()){
        res.name=C.FoundDecl.getDecl()->getQualifiedNameAsString();
      }else{
        res.name={C.Surrogate->getNameAsString()};
      }
      res.signature=getSurrSignature(C);
      res.isSurrogate=true;
      res.declLoc=C.Surrogate->getLocation();
      res.declLoc.print(declLoc,S->SourceMgr);
      return res;
    }

    if (C.Function==nullptr) {
      res.declLocation="Built-in";
      res.declLoc={};
      res.name="";
      if (Set->getKind()==Set->CSK_Operator){
        res.name=getBuiltInOperatorName();
      }
      for (const auto& tmp:C.BuiltinParamTypes){
        if (tmp != QualType{})
          res.signature.push_back(tmp.getAsString());
      }
      return res;
    }
    if (const auto *p = dyn_cast<UsingShadowDecl>(C.FoundDecl.getDecl())){
      res.declLoc=p->getTargetDecl()->getLocation();
      res.declLoc.print(declLoc,S->SourceMgr);
      res.usingLoc=C.FoundDecl.getDecl()->getLocation();
      res.usingLoc.print(usingLoc,S->SourceMgr);
    }else{
      res.declLoc=C.FoundDecl.getDecl()->getLocation();
      res.declLoc.print(declLoc,S->SourceMgr);
    }
    res.name=C.FoundDecl.getDecl()->getQualifiedNameAsString();
    if (C.Function){
      /*if (const auto* mp=dyn_cast<CXXMethodDecl>(C.Function)){
        if (mp->isInstance()&&!isa<CXXConstructorDecl>(mp))
          res.signature=mp->getThisObjectType().getAsString()+"; ";
      }*/
      res.signature=getSignature(C);
      res.templateSource=getTemplateSource(C);
      if (res.templateSource!="" && settings.ShowTemplateSpecs){
        res.templateSpecs=getSpecializations(C);
        res.templateParams=getTemplateParams(C);
      }
    }
    return res;
  }
  std::vector<OvdlTemplateSpec> getSpecializations(const OverloadCandidate& C)const{
    const NamedDecl* nd=C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd)){
      nd=llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    }
    const FunctionDecl* f=nd->getAsFunction();
    std::vector<OvdlTemplateSpec> res;
    for (const auto* x:f->getDescribedFunctionTemplate()->specializations()){
      if (x->getSourceRange()!=f->getSourceRange()){
        OvdlTemplateSpec entry;
        entry.isExact=true;

        entry.declLocation=x->getLocation().printToString(S->getSourceManager());
        SourceLocation endloc(Lexer::getLocForEndOfToken(
            x->getTypeSpecEndLoc(), 0, S->getSourceManager(), S->getLangOpts()));
        CharSourceRange range=CharSourceRange::getCharRange(x->getLocation(),endloc);
        entry.source= std::string(
            Lexer::getSourceText(range, S->getSourceManager(), S->getLangOpts()));
        if (x->getTemplateSpecializationArgs() && C.Function->getTemplateSpecializationArgs()){
        const auto specializedArgs=x->getTemplateSpecializationArgs()->asArray();
        const auto genericArgs =  C.Function->getTemplateSpecializationArgs()->asArray();
        int midx=std::min(specializedArgs.size(),genericArgs.size());
        entry.isExact= genericArgs.size()==specializedArgs.size();
        for(int i=0; i!=midx;++i){
          entry.isExact=entry.isExact && specializedArgs[i].structurallyEquals(genericArgs[i]);
        }
        }else 
          entry.isExact=false;
        if (entry.isExact && !C.Best && !inCompare){
          static std::set<std::pair<SourceLocation,const FunctionDecl*>> s;
          if (!s.count(std::pair{Loc,x})){
            s.emplace(Loc,x);
            auto ID=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Warning, "Explicit specialization ignored");
            S->Diags.Report(Loc, ID)<<sr;
            auto ID2=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Note, "The ignored specialization:");
            S->Diags.Report(x->getLocation(), ID2)<<x->getSourceRange();
            auto ID3=S->Diags.getDiagnosticIDs()->getCustomDiagID(DiagnosticIDs::Note, "General declaration:");
            S->Diags.Report(f->getLocation(), ID3)<<f->getSourceRange();
            //if ()
          }
        }
        res.push_back(entry);
      }
    }
    return res;
  }
  std::string getTemplate(const FunctionDecl* f)const{
    if (!f||!f->isTemplated() ) return "";
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
    SourceLocation endloc(Lexer::getLocForEndOfToken(
        r.getEnd(), 0, S->getSourceManager(), S->getLangOpts()));
    CharSourceRange range=CharSourceRange::getCharRange(r.getBegin(),endloc);
    return std::string(
        Lexer::getSourceText(range, S->getSourceManager(), S->getLangOpts()));
  }
  std::string getTemplateSource(const OverloadCandidate& C)const{
    const NamedDecl* nd=C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd)){
      nd=llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    }
    const FunctionDecl* f=nd->getAsFunction();
    return getTemplate(f);
  }
  std::string getSource(const OverloadCandidate& C)const{
    const NamedDecl* nd=C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd)){
      nd=llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    }
    const FunctionDecl* f=nd->getAsFunction();
    return getTemplate(f);
  }
  QualType getThisType(const OverloadCandidate& C)const{
    if (const auto* mp=llvm::dyn_cast_or_null<CXXMethodDecl>(C.Function)){
      if (mp->isInstance()&&!isa<CXXConstructorDecl>(mp))
        return mp->getThisType();
    }
    return QualType{};
  }
  std::string getTemplatedParamForConversion(const OverloadCandidate& C,int i)const{
    if (C.Function && isa<CXXMethodDecl>(C.Function) &&
        !isa<CXXConstructorDecl>(C.Function))
      --i;
    const NamedDecl* nd=C.FoundDecl.getDecl();
    while (isa<UsingShadowDecl>(nd)){
      nd=llvm::dyn_cast<UsingShadowDecl>(nd)->getTargetDecl();
    }
    const FunctionDecl* f=nd->getAsFunction();
    std::vector<std::string> res;
    if (!f||!f->isTemplated() ) return {};
    const auto params=f->parameters();
    if (i<0) return{};
    if ((unsigned)i<params.size() && params[i]->isTemplated()){
      return params[i]->getType().getAsString();
    }
    return {};
  }
  std::deque<std::string> getTemplateParams(const OverloadCandidate& C)const{
    std::deque<std::string> res;
    if (C.Function==nullptr)
      return {};
    const auto *templateArgs =  C.Function->getTemplateSpecializationArgs();
    if (auto*ptemplates=C.Function->getPrimaryTemplate()){
      if (ptemplates->getTemplateParameters()){
        for (size_t i=0; i<templateArgs->size();++i){
          res.push_back({});
          llvm::raw_string_ostream os(res.back());
          os<<ptemplates->getTemplateParameters()->asArray()[i]->getNameAsString()<<" = ";
          templateArgs->data()[i].dump(os);
        }
      }
    }
    return res;
  };
  std::vector<std::tuple<QualType,bool>> getSignatureTypes(const OverloadCandidate& C)const{
    if (C.Function==nullptr)
      return {};
    std::vector<std::tuple<QualType,bool>> res;
    QualType thisType=getThisType(C);
    if (thisType!=QualType{}){
      res.emplace_back(std::tuple{thisType,false});
    }
    if (!C.Function->param_empty()){
      for (size_t i=0;i!=C.Function->param_size();++i){
        const auto& x=C.Function->parameters()[i];
        res.emplace_back(x->getType(),x->hasDefaultArg());
      }
    }
    return res;
  }
  std::deque<std::string> getSignature(const OverloadCandidate& C) const{
    //const FunctionDecl* f=C.Function;
    std::deque<std::string> res;
    //llvm::raw_string_ostream os(res);
    const auto types=getSignatureTypes(C);
    size_t i=0;
    if (getThisType(C)!=QualType{}){
      res.push_back(getThisType(C).getCanonicalType().getAsString()+"=this");
      ++i;
    }
    for (; i<types.size();i++){
      const auto&[type,isDefaulted]=types[i];
      res.push_back(type.getCanonicalType().getAsString()+(isDefaulted?"=default":""));
    }
    if (C.Function && C.Function->getEllipsisLoc()!=SourceLocation())
      res.push_back("...");
    return res;
  }
  std::vector<ExprValueKind> getCallKinds()const{
    std::vector<ExprValueKind> res;
    if (const Expr* Oe=getSetArgs().ObjectExpr){
      res.push_back(Oe->getValueKind());
    }
    auto Args=getSetArgs().Args;
    /*if (Args.size()==1&&isa<clang::InitListExpr>(Args[0])){
      Args = llvm::dyn_cast<const clang::InitListExpr>(Args[0])->inits();
    }*/
    for (const auto *Arg:Args){
      res.push_back(Arg->getValueKind());
    }
    return res;
  }
  SourceRange sr;
  OvdlResEntry getResEntry(OverloadingResult ovres,
                           const OverloadCandidate* BestOrProblem) {
    OvdlResEntry res;
    ArrayRef<Expr*> Args=getSetArgs().Args;
    sr=makeSR({
          Loc,(Args.size()&&Args[0]?Args[0]->getBeginLoc():SourceLocation{})
        },{
          Loc,(Args.size()&&Args[0]?Args[0]->getEndLoc():SourceLocation{}),
          (Args.size()&&Args.back()?Args.back()->getEndLoc():SourceLocation{}),
          getSetArgs().EndLoc
        });
    
    res.ovRes=ovres;
    if (ovres==OR_Success) {
      res.best=getCandEntry(*BestOrProblem);
      res.problems={};
    }else if (ovres==clang::OR_Ambiguous){
      res.best={};
      res.problems = {};
      for (const auto& cand:*Set){
        if (cand.Best)
          res.problems.push_back(getCandEntry(cand));
      }
    }else{
      res.best={};
      if (BestOrProblem)
        res.problems={getCandEntry(*BestOrProblem)};
    }
    {
      llvm::raw_string_ostream os(res.callLocation);
      res.callLoc=Loc;
      Loc.print(os,S->SourceMgr);
    }
    CharSourceRange charRange;
    res.isImplicit=(Args.size()==1)&& Args[0]->getBeginLoc()==sr.getBegin() && Args[0]->getEndLoc()==sr.getEnd();
    charRange=CharSourceRange::getCharRange(sr.getBegin(),Lexer::getLocForEndOfToken(sr.getEnd(), 0,
                                     S->getSourceManager(), S->getLangOpts()));
    res.callRange={sr.getBegin(),Lexer::getLocForEndOfToken(sr.getEnd(), 1,
                                     S->getSourceManager(), S->getLangOpts())};
    const auto& callKinds=getCallKinds();
    res.callSignature =
        Lexer::getSourceText(charRange, S->getSourceManager(), S->getLangOpts());
    if (const Expr* Oe=getSetArgs().ObjectExpr) {
      QualType objType;
      if (auto* UOe= llvm::dyn_cast<UnresolvedMemberExpr>(Oe))
        objType=UOe->getBaseType().getCanonicalType();
      else
        objType=Oe->getType().getCanonicalType();
      res.callTypes.push_back("(Obj:" + objType.getAsString() + ')');
    }
    for (const auto& x:Args){
      if (x==nullptr) {res.callTypes.push_back("NULL"); continue;}
      if (isa<clang::InitListExpr>(x))
        res.callTypes.push_back("InitizerList");
      else
        res.callTypes.push_back(x->getType().getCanonicalType().getAsString());
    }
    for (size_t i=0; i!=callKinds.size();++i){
      res.callTypes[i]+=Kinds[callKinds[i]];
    }
    for (const auto& cand:*Set){
      if (cand.Viable)
        addCand(res.viableCandidates,getCandEntry(cand));
      else if (settings.ShowNonViableCands){
        const auto x=getCandEntry(cand);
        if (x.declLocation!="Built-in" || settings.ShowBuiltInNonViable)
          addCand(res.nonViableCandidates,getCandEntry(cand));
      }
    }
    return res;
  }
  void addCand(std::vector<OvdlCandEntry>& v,const OvdlCandEntry& cand)const{
    for (const auto&c:v){
      if (c==cand) return;
    }
    v.emplace_back(std::move(cand));
  }
};
}//namespace
std::unique_ptr<ASTConsumer>
OvdlDumpAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
    return std::make_unique<ASTConsumer>();
}
void OvdlDumpAction::ExecuteAction(){
  CompilerInstance &CI = getCompilerInstance();
  EnsureSemaIsCreated(CI, *this);
  auto x=std::make_unique<DefaultOverloadInstCallback>();
  x->setSettings(CI.getFrontendOpts().OvdlSettings);
  CI.getSema().OverloadInspectionCallbacks.push_back(std::move(x));
  ASTFrontendAction::ExecuteAction();
}
