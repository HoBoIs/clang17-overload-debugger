#ifndef LLVM_CLANG_SEMA_OVERLOADCALLBACK_H
#define LLVM_CLANG_SEMA_OVERLOADCALLBACK_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Overload.h"
#include "llvm/ADT/ArrayRef.h"
#include <optional>
namespace clang{

enum BetterOverloadCandidateReason{
  viability,CUDAEmit,badConversion,betterConversion,betterImplicitConversion,
  constructor,isSpecialization,moreSpecialized,isInherited,
  derivedFromOther,RewriteKind,guideImplicit,guideCopy,guideTemplated,enableIf,
  parameterObjectSize,multiversion,CUDApreference,addressSpace,inconclusive
};

struct SetInfo{
  std::optional<const ArrayRef<Expr*>> Args={};
  std::optional<const SourceLocation> EndLoc={};
  std::optional<const Expr*> ObjectExpr={};
};

class OverloadCallback{
public:
  virtual void addSetInfo(const OverloadCandidateSet& Set,const SetInfo& S)=0;
  virtual bool needAllCompareInfo() const=0;
  virtual void setCompareInfo(const std::vector<ImplicitConversionSequence::CompareKind>&)=0;
  virtual ~OverloadCallback()=default;
  virtual void initialize(const Sema& TheSema)=0;
  virtual void finalize(const Sema& TheSema)=0;
  virtual void atEnd()=0;
  virtual void atOverloadBegin(const Sema& TheSema,const SourceLocation& Loc,const OverloadCandidateSet& Set)=0;
  virtual void atOverloadEnd(const Sema& TheSema,const SourceLocation& Loc,
    const OverloadCandidateSet& Set, OverloadingResult res, const OverloadCandidate* BestOrProblem)=0;
  virtual void atCompareOverloadBegin(const Sema& TheSema,const SourceLocation& Loc,
    const OverloadCandidate &Cand1, const OverloadCandidate &Cand2)=0;
  virtual void atCompareOverloadEnd(const Sema& TheSema,const SourceLocation& Loc,
    const OverloadCandidate &Cand1, const OverloadCandidate &Cand2, bool res,BetterOverloadCandidateReason reason,int infoIdx)=0;
};

template <class OverloadCallbackPtrs>
void addSetInfo(OverloadCallbackPtrs &Callbacks,const OverloadCandidateSet& Set,
    const SetInfo& S){
  for (auto &C : Callbacks) 
    if (C)
      C->addSetInfo(Set,S);
}

template <class OverloadCallbackPtrs>
void atOverloadBegin(OverloadCallbackPtrs &Callbacks,
                     const Sema &TheSema,
                     const SourceLocation Loc,
         const OverloadCandidateSet& Set) {
  for (auto &C : Callbacks) {
    if (C)
      C->atOverloadBegin(TheSema, Loc, Set);
  }
}

template <class OverloadCallbackPtrs>
void atOverloadEnd(OverloadCallbackPtrs &Callbacks, const Sema &TheSema,
                   const SourceLocation Loc, const OverloadCandidateSet& Set,
                    OverloadingResult Res, const OverloadCandidate* BestOrProblem) {
  for (auto &C : Callbacks) {
    if (C)
      C->atOverloadEnd(TheSema, Loc, Set,Res,BestOrProblem);
  }
}

template <class OverloadCallbackPtrs>
void atCompareOverloadBegin(OverloadCallbackPtrs &Callbacks, const Sema& TheSema,const SourceLocation& Loc,
    const OverloadCandidate &Cand1, const OverloadCandidate &Cand2){
  for (auto &C : Callbacks) {
    if (C)
      C->atCompareOverloadBegin(TheSema, Loc, Cand1,Cand2);
  }
}
template <class OverloadCallbackPtrs>
void atCompareOverloadEnd(OverloadCallbackPtrs &Callbacks, const Sema& TheSema,const SourceLocation& Loc,
                        const OverloadCandidate &Cand1, const OverloadCandidate &Cand2,
                        bool res,BetterOverloadCandidateReason reason,int infoIdx=-1){
  for (auto &C : Callbacks) {
    if (C)
      C->atCompareOverloadEnd(TheSema, Loc, Cand1,Cand2,res,reason,infoIdx);
  }
}


} // namespace clang

#endif
