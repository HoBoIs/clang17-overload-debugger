#ifndef LLVM_CLANG_FRONTEND_OV_INS_ENUM_PRINTS_H
#define LLVM_CLANG_FRONTEND_OV_INS_ENUM_PRINTS_H
#include "llvm/Support/YAMLTraits.h"
#include "clang/Sema/OverloadCallback.h"
#include "clang/Sema/Sema.h"
#include <string>
namespace llvm::yaml{
  
template <> struct ScalarEnumerationTraits<clang::OverloadingResult>{
  static void enumeration(IO& io, clang::OverloadingResult& val){
    
    io.enumCase(val,"Success" ,clang::OR_Success);
    io.enumCase(val,"NoViableFunction" ,clang::OR_No_Viable_Function);
    io.enumCase(val,"Deleted" ,clang::OR_Deleted);
    io.enumCase(val,"Ambigius" ,clang::OR_Ambiguous);

  }
};
 
template <> struct ScalarEnumerationTraits<clang::BetterOverloadCandidateReason>{
  static void enumeration(IO& io, clang::BetterOverloadCandidateReason& val){
    io.enumCase(val,"viability",clang::viability);
    io.enumCase(val,"CUDAEmit",clang::CUDAEmit);
    io.enumCase(val,"badConversion",clang::badConversion);
    io.enumCase(val,"betterConversion",clang::betterConversion);
    io.enumCase(val,"betterImplicitConversion",clang::betterImplicitConversion);
    io.enumCase(val,"constructor",clang::constructor);
    io.enumCase(val,"isSpecialization",clang::isSpecialization);
    io.enumCase(val,"moreSpecialized",clang::moreSpecialized);
    io.enumCase(val,"isInherited",clang::isInherited);
    io.enumCase(val,"derivedFromOther",clang::derivedFromOther);
    io.enumCase(val,"RewriteKind",clang::RewriteKind);
    io.enumCase(val,"guideImplicit",clang::guideImplicit);
    io.enumCase(val,"guideTemplated",clang::guideTemplated);
    io.enumCase(val,"guideCopy",clang::guideCopy);
    io.enumCase(val,"enableIf",clang::enableIf);
    io.enumCase(val,"parameterObjectSize",clang::parameterObjectSize);
    io.enumCase(val,"multiversion",clang::multiversion);
    io.enumCase(val,"CUDApreference",clang::CUDApreference);
    io.enumCase(val,"addressSpace",clang::addressSpace);
    io.enumCase(val,"inconclusive",clang::inconclusive);
  }
};
template <> struct ScalarEnumerationTraits<clang::OverloadCandidateRewriteKind>{
  static void enumeration(IO& io, clang::OverloadCandidateRewriteKind& val){
    io.enumCase(val, "None", clang::CRK_None);
    io.enumCase(val, "Rewriten", clang::CRK_DifferentOperator);
    io.enumCase(val, "Reversed", clang::CRK_Reversed);
    io.enumCase(val, "Rewriten and reversed", clang::OverloadCandidateRewriteKind(clang::CRK_Reversed|clang::CRK_DifferentOperator));
  }
};
template <> struct ScalarEnumerationTraits<clang::OverloadFailureKind>{
  static void enumeration(IO& io, clang::OverloadFailureKind& val){
    io.enumCase(val, "ovl_fail_too_many_arguments",
                clang::ovl_fail_too_many_arguments);
    io.enumCase(val, "ovl_fail_too_few_arguments",
                clang::ovl_fail_too_few_arguments);
    io.enumCase(val, "ovl_fail_bad_conversion",clang::ovl_fail_bad_conversion);
    io.enumCase(val, "ovl_fail_bad_deduction",clang::ovl_fail_bad_deduction);
    io.enumCase(val, "ovl_fail_trivial_conversion",
                clang::ovl_fail_trivial_conversion);
    io.enumCase(val, "ovl_fail_illegal_constructor",
                clang::ovl_fail_illegal_constructor);
    io.enumCase(val, "ovl_fail_bad_final_conversion",
                clang::ovl_fail_bad_final_conversion);
    io.enumCase(val, "ovl_fail_final_conversion_not_exact",
                clang::ovl_fail_final_conversion_not_exact);
    io.enumCase(val, "ovl_fail_bad_target",clang::ovl_fail_bad_target);
    io.enumCase(val, "ovl_fail_enable_if",clang::ovl_fail_enable_if);
    io.enumCase(val, "ovl_fail_explicit",clang::ovl_fail_explicit);
    io.enumCase(val, "ovl_fail_addr_not_available",clang::
                ovl_fail_addr_not_available);
    io.enumCase(val, "ovl_fail_inhctor_slice",clang::ovl_fail_inhctor_slice);
    io.enumCase(val, "ovl_non_default_multiversion_function",
                clang::ovl_non_default_multiversion_function);
    io.enumCase(val, "ovl_fail_object_addrspace_mismatch",
                clang::ovl_fail_object_addrspace_mismatch);
    io.enumCase(val, "ovl_fail_constraints_not_satisfied",
                clang::ovl_fail_constraints_not_satisfied);
    io.enumCase(val, "ovl_fail_module_mismatched",
                clang::ovl_fail_module_mismatched);
    }
};
} // namespace llvm::yaml
namespace str{

std::string toString(clang::BetterOverloadCandidateReason r);
std::string toString(clang::OverloadFailureKind r);
std::string toString(clang::OverloadCandidateRewriteKind r);
std::string toString(clang::OverloadingResult r);
std::string toString(clang::Sema::TemplateDeductionResult r);
std::string toString(clang::ImplicitConversionSequence::Kind k);
std::string toString(clang::BadConversionSequence::FailureKind k);

} // namespace str
#endif
