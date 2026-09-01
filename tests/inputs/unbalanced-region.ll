; Deliberately start a region without ending it. This bypasses the pass so the
; test exercises the runtime's final balance check in isolation.

@PragmaRegionCount = global i64 1, align 8
@ZRAY_CounterDimension = global i64 1, align 8
@CounterArray = thread_local global [1 x i64] zeroinitializer, align 8
@LoadRuntimeArray = thread_local global [1 x i64] zeroinitializer, align 8
@StoreRuntimeArray = thread_local global [1 x i64] zeroinitializer, align 8

declare void @_Z16startTimingEventm(i64)
declare void @zray_finalize()

define i32 @main() {
entry:
  call void @_Z16startTimingEventm(i64 0)
  call void @zray_finalize()
  ret i32 0
}
