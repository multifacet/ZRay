// ZRay: portable compiler-assisted memory traffic characterization.
// Public region-of-interest markers: ZRAY_BEGIN / ZRAY_END.
//
// Authors: Hayden Coffey
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

#ifndef ZRAY_H
#define ZRAY_H

// ZRay region-of-interest markers.
//
// A ZRay region is delimited by two inline asm comments carrying a group ID:
//
//     ZRAY_BEGIN(15);
//     ... code to profile ...
//     ZRAY_END(15);
//
// The markers survive optimization (they are volatile asm with side effects)
// but emit no machine code — they are assembler comments. The ZRay IR pass
// scans for them to find region boundaries.
//
// The group ID lets you profile a subset of regions: set ZRAY_PATCH_ID to a
// group ID to instrument only regions in that group, or leave it unset/0 to
// instrument all of them. IDs need not be unique; regions sharing an ID form
// a group.
//
// These expand to exactly the IR that `#pragma begin_instrument N` produced in
// the older custom-clang flow, so a stock LLVM/clang toolchain is sufficient.

#define ZRAY_BEGIN(group_id) __asm__ volatile("#ZRAY_ROI_BEGIN " #group_id)
#define ZRAY_END(group_id)   __asm__ volatile("#ZRAY_ROI_END " #group_id)

#endif // ZRAY_H
