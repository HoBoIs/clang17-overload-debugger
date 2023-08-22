#ifndef LLVM_CLANG_SEMA_OVERLOADCALLBACK_H
#define LLVM_CLANG_SEMA_OverloadCALLBACK_H

#include "clang/Sema/Sema.h"
namespace clang{
enum BetterOverloadCandidateReason{
  viability,bettterConversion,bettterImplicitConversion,
  constructor,isSpecialization,moreSpecialized,isInherited,
  derivedFromOther,RewriteKind,guideImplicit,guideCopy,enalbeIf,
  parameterObjectSize,multiversion,CUDApreference,addressSpace,inconclusive
};
class OverloadCallback{
public:
  virtual ~OverloadCallback()=default;
  virtual void initialize(const Sema& TheSema)=0;
  virtual void finalize(const Sema& TheSema)=0;
//The Set is not const because it doesn't support constant iterators. DO NOT modify it
  virtual void atOverloadBegin(const Sema& TheSema,const SourceLocation& Loc,OverloadCandidateSet& Set)=0;
  virtual void atOverloadEnd(const Sema& TheSema,const SourceLocation& Loc,
		OverloadCandidateSet& Set, OverloadResult res, const OverloadCandidate* Best)=0;
  virtual void atCompareOverloadBegin(const Sema& TheSema,const SourceLocation& Loc
		const OverloadCandidate &Cand1, const OverloadCandidate &Cand2)=0;
  virtual void atCompareOverloadEnd(const Sema& TheSema,const SourceLocation& Loc
		const OverloadCandidate &Cand1, const OverloadCandidate &Cand2, bool res,BetterOverloadCandidateReason reason)=0;
}; 
template <class OverloadCallbackPtrs>
void atOverloadBegin(OverloadCallbackPtrs &Callbacks,
                     const Sema &TheSema,
                     const SourceLocation Loc,
			OverloadCandidateSet& Set) {
  for (auto &C : Callbacks) {
    if (C)
      C->atOverloadBegin(TheSema, Loc, Set);
  }
}

template <class OverloadCallbackPtrs>
void atOverloadEnd(OverloadCallbackPtrs &Callbacks,
                   const Sema &TheSema,
                   const SourceLocation Loc,
		OverloadCandidateSet& Set, 
		OverloadResult Res, 
		const OverloadCandidate* Best) {
  for (auto &C : Callbacks) {
    if (C)
      C->atOverloadEnd(TheSema, Loc, Set,Res,Best);
  }
}

template <class OverloadCallbackPtrs>
void atCompareOverloadBegin(OverloadCallbackPtrs &Callbacks, const Sema& TheSema,const SourceLocation& Loc
		const OverloadCandidate &Cand1, const OverloadCandidate &Cand2){
  for (auto &C : Callbacks) {
    if (C)
      C->atCompareOverloadBegin(TheSema, Loc, Cand1,Cand2);
  }
}
template <class OverloadCallbackPtrs>
void atCompareOverloadEnd(OverloadCallbackPtrs &Callbacks, const Sema& TheSema,const SourceLocation& Loc
		const OverloadCandidate &Cand1, const OverloadCandidate &Cand2,bool res,BetterOverloadCandidateReason reason){
  for (auto &C : Callbacks) {
    if (C)
      C->atCompareOverloadEnd(TheSema, Loc, Cand1,Cand2,res,reason);
  }
}


}

#endif
