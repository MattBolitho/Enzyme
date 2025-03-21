; RUN: if [ %llvmver -lt 16 ]; then %opt < %s %loadEnzyme -enzyme -enzyme-preopt=false -mem2reg -simplifycfg -S | FileCheck %s; fi
; RUN: %opt < %s %newLoadEnzyme -enzyme-preopt=false -passes="enzyme,function(mem2reg,%simplifycfg)" -S | FileCheck %s

declare void @__enzyme_autodiff(...)

declare void @llvm.memset.p0i8.i64(i8* nocapture writeonly, i8, i64, i1)

declare void @g()

define void @f(i8* %x, i1 %c) {
  br i1 %c, label %t, label %exit

t:
  call void @llvm.memset.p0i8.i64(i8* %x, i8 0, i64 16, i1 false)
  %xp = bitcast i8* %x to double*
  %flt = load double, double* %xp, align 8, !tbaa !4
  %g = getelementptr inbounds double, double* %xp, i32 1
  %int = load double, double* %g, align 8, !tbaa !7
  br label %exit

exit:
  call void @g() "enzyme_inactive"
  ret void
}

define void @df(double* %x, double* %xp) {
  tail call void (...) @__enzyme_autodiff(i8* bitcast (void (i8*, i1)* @f to i8*), metadata !"enzyme_dup", double* %x, double* %xp, i1 true)
  ret void
}

!4 = !{!"long", !5, i64 0}
!7 = !{!"double", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C++ TBAA"}

; CHECK:  %1 = getelementptr inbounds i8, i8* %"x'", i32 8
; CHECK-NEXT:  call void @llvm.memset.p0i8.i64(i8* %1, i8 0, i64 8, i1 false)

