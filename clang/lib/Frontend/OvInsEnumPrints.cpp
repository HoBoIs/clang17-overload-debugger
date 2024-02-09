#include "llvm/Support/YAMLTraits.h"
#include "clang/Frontend/OvInsEnumPrints.h"
#include "clang/Sema/OverloadCallback.h"
#include "clang/Sema/Sema.h"
#include <string>
using namespace clang;
namespace llvm::yaml{
} // namespace llvm::yaml

namespace str{
std::string toString(BetterOverloadCandidateReason r){
  switch(r){
  case viability:return "viability";
  case CUDAEmit:return "CUDAEmit";
  case badConversion:return "badConversion";
  case betterConversion:return "betterConversion";
  case betterImplicitConversion:return "betterImplicitConversion";
  case constructor:return "constructor";
  case isSpecialization:return "isSpecialization";
  case moreSpecialized:return "moreSpecialized";
  case isInherited:return "isInherited";
  case derivedFromOther:return "derivedFromOther";
  case RewriteKind:return "RewriteKind";
  case guideImplicit:return "guideImplicit";
  case guideCopy:return "guideCopy";
  case guideTemplated:return "guideTemplated";
  case enableIf:return "enableIf";
  case parameterObjectSize:return "parameterObjectSize";
  case multiversion:return "multiversion";
  case CUDApreference:return "CUDApreference";
  case addressSpace:return "addressSpace";
  case inconclusive:return "inconclusive";
  };
  llvm_unreachable("Unknown BetterOverloadCandidateReason");
}
std::string toString(OverloadCandidateRewriteKind r){
  static constexpr llvm::StringLiteral rewriteMessage[]={"","Rewriten from ","Rewersed from ","Reversed and rewriten from "};
  return std::string(rewriteMessage[r]);
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
  case ovl_fail_final_conversion_not_exact: 
                                return "ovl_fail_final_conversion_not_exact";
  case ovl_fail_bad_target: return "ovl_fail_bad_target";
  case ovl_fail_enable_if: return "ovl_fail_enable_if";
  case ovl_fail_explicit: return "ovl_fail_explicit";
  case ovl_fail_addr_not_available: return "ovl_fail_addr_not_available";
  case ovl_fail_inhctor_slice: return "ovl_fail_inhctor_slice";
  case ovl_non_default_multiversion_function: 
                               return "ovl_non_default_multiversion_function";
  case ovl_fail_object_addrspace_mismatch: 
                               return "ovl_fail_object_addrspace_mismatch";
  case ovl_fail_constraints_not_satisfied: 
                               return "ovl_fail_constraints_not_satisfied";
  case ovl_fail_module_mismatched: return "ovl_fail_module_mismatched";
  }
  llvm_unreachable("Unknown OFK");
}
std::string toString(clang::OverloadingResult r){
  switch(r){
  case OR_Success:return "OR_Success";
  case OR_No_Viable_Function:return "OR_No_Viable_Function";
  case OR_Ambiguous:return "OR_Ambiguous";
  case OR_Deleted:return "OR_Deleted";
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
std::string toString(ImplicitConversionSequence::Kind k){
  switch (k) {
  case ImplicitConversionSequence::StandardConversion:
    return "StandardConversion";
  case ImplicitConversionSequence::StaticObjectArgumentConversion:
    return "StaticObjectArgumentConversion";
  case ImplicitConversionSequence::UserDefinedConversion:
    return "UserDefinedConversion";
  case ImplicitConversionSequence::AmbiguousConversion:
    return "AmbiguousConversion";
  case ImplicitConversionSequence::EllipsisConversion:
    return "EllipsisConversion";
  case ImplicitConversionSequence::BadConversion:
    return "BadConversion";
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
} // namespace str
