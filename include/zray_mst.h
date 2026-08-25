#ifndef _ZRAY_MST_H_
#define _ZRAY_MST_H_

// Binary format contract for the MST placement arm's per-function sidecar.
//
// The MST arm (--placement=mst) instruments non-tree CFG edges with counters
// that measure EDGE traversals. Recovering per-block execution counts (needed to
// scale each block's static instruction mix) requires the CFG + spanning-tree
// topology, which ZRAY's native ProfileData log does not carry. This sidecar
// supplies it. It is written by the pass at compile time (zray_mst.cc) and read
// by post_process's MST mode, which solves flow over the tree (see
// docs/adr/0002-mst-arm-data-model.md).
//
// Path: "<ZRAY_LOGFILE>.mst" (a separate file; the ZRAY_LOGFILE format is left
// untouched). Endianness/packing: native — writer and reader are the same build.
//
// Layout: a sequence of per-function records until EOF. Each record:
//
//   uint64_t  Magic                 (== ZRAY_MST_MAGIC; resync/sanity check)
//   uint64_t  RegionID              (PragmaRegionID; counter array row)
//   uint8_t   IsIndirect            (1 for cloned/indirect functions)
//   uint64_t  FunctionNameLen
//   char      FunctionName[FunctionNameLen]
//   uint64_t  NumBlocks
//     repeated NumBlocks times:
//       uint64_t      BlockId       (CFGMST BBInfo.Index; the fake super-node
//                                     that joins all exits back to entry is a
//                                     normal block here with all-zero mix)
//       ProfileData   Mix           (per-single-execution mix of that block)
//   uint64_t  NumEdges
//     repeated NumEdges times:
//       uint64_t  SrcBlockId
//       uint64_t  DstBlockId
//       uint8_t   InMST             (1 => tree edge, count solved by flow;
//                                     0 => instrumented, count is measured)
//       int64_t   CounterIndex      (>=0 index into this region's counter array
//                                     when InMST==0; -1 when InMST==1)
//
// A block's execution count = sum of incoming edge counts (== sum of outgoing).
// The counter array slot for an instrumented edge is
// CounterArray[RegionID * ArrayWidth + CounterIndex].

#include <inttypes.h>

static const uint64_t ZRAY_MST_MAGIC = 0x5A5241594D535431ULL; // "ZRAYMST1"

#endif
