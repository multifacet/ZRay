; A full-scan region has one entry and two reachable function exits. ZRay must
; emit endTimingEvent on both return paths.

define i32 @branchy(i1 %condition) {
entry:
  br i1 %condition, label %one, label %two

one:
  ret i32 1

two:
  ret i32 2
}

define i32 @main() {
entry:
  %first = call i32 @branchy(i1 true)
  %second = call i32 @branchy(i1 false)
  ret i32 0
}
