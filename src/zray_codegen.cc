// ZRay: portable compiler-assisted memory traffic characterization.
// Emission of the counter-increment and timing-event calls.
//
// Authors: Hayden Coffey, Ashwin Poduval
//
// See AUTHORS for contributor details and CITATION.cff for how to cite.

/******************************
 * Hayden Coffey
 *
 * Code used for modifying
 * LLVM IR.
 */
#include "zray_pass.h"

#define USE_CE 0

namespace zray
{
#if 0
    // TODO Verify this works with changes to getBBOffset
    //  Insert custom events into if/else CFG blocks and remove instruction counts from main region profile
    void ZRayPass::insertDiamondEvents(ProfileData &regionProfile, size_t regionStart, size_t &regionEnd, Function &F)
    {
        Module *M = F.getParent();

        if (F.getBasicBlockList().size() != 0)
        {
            std::vector<LoopData> loopDataList = getLoopInRegion(regionStart, regionEnd, F);

            for (Function::iterator BBI = F.begin(); BBI != F.end(); ++BBI)
            {
                // If basic block falls into profile region, remove its instructions
                // from the region counters and insert a new custom event for the block.
                size_t bbOffset = getBBOffset(&(*BBI));
                if (INBOUNDS(regionStart, regionEnd, bbOffset))
                {
                    if (isIfElse(BBI->getName().str()))
                    {
                        ProfileData ctrlProfile;
                        clearRecords(&ctrlProfile);

                        size_t bbStart = bbOffset - BBI->size();
                        size_t pos = bbStart;
                        for (BasicBlock::iterator i = BBI->begin(); i != BBI->end(); i++)
                        {
                            size_t scaleFactor = getScaleFactor(loopDataList, pos, F);
                            recordInst((i), -scaleFactor, regionProfile); // Remove counters from profile region
                            recordInst((i), 1, ctrlProfile);              // Collect new counter values for BB

                            bbStart++;
                        }

                        // Insert new custom event using BB counters
                        insertCustomEvent(M, F, BBI->getFirstInsertionPt(), ctrlProfile);
                        regionEnd++;
                    }
                }
            }
        }
    }
#endif

    void ZRayPass::insertPrint(Module *M, IRBuilder<> * builder, std::string message, Value * val)
    {
        Type * IntType = Type::getInt64Ty(M->getContext());

        // Declare C standard library printf 
        std::vector<Type *> printfArgsTypes({Type::getInt8PtrTy(M->getContext())});
        //FunctionType *ct = FunctionType::get(Type::getInt32Ty(M->getContext()), false);

        FunctionType *printfType = FunctionType::get(IntType, printfArgsTypes, true);
        auto printfFunc = M->getOrInsertFunction("printf", printfType);

        #if 0 //OMP Thread Number printing
        auto threadNumFunc = M->getOrInsertFunction("omp_get_thread_num", ct);
        // The format string for the printf function, declared as a global literal
        Value *str = builder->CreateGlobalStringPtr("ZRAY TID %d:" + message, "str");
        Value * threadNumber = builder->CreateCall(threadNumFunc, None);

        if(val != nullptr)
        {
            std::vector<Value *> argsV({str, threadNumber, val});
            builder->CreateCall(printfFunc, argsV, "calltmp");
        }
        else
        {
            std::vector<Value *> argsV({str, threadNumber});
            builder->CreateCall(printfFunc, argsV, "calltmp");
        }
        #else
        Value *str = builder->CreateGlobalStringPtr("ZRAY: " + message, "str");
        if(val != nullptr)
        {
            std::vector<Value *> argsV({str, val});
            builder->CreateCall(printfFunc, argsV, "calltmp");
        }
        else
        {
            std::vector<Value *> argsV({str});
            builder->CreateCall(printfFunc, argsV, "calltmp");
        }
        #endif
    }

    void ZRayPass::setCustomEventHandler(Module *M, inst_iterator posI)
    {
        // Create a function type of void _ (void *, int32)
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext())},
                                                     false);

        Value *sizeVal = ConstantInt::get(Type::getInt64Ty(M->getContext()), ROI_Count);
        Value *ceVal = ConstantInt::get(Type::getInt64Ty(M->getContext()), GlobalCounterEventTotal);

        // GlobalVariable *gvPragmaRegionCount = new GlobalVariable(*M, Type::getInt64Ty(M->getContext()), false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                                                 // 0, "PragmaRegionCount", nullptr, llvm::GlobalValue::NotThreadLocal);
        // gvPragmaRegionCount->setInitializer(ConstantInt::get(Type::getInt64Ty(M->getContext()), ROI_Count));

        // Get reference to function we have written in other file
        std::string funcName = mangleFunctionName("zray_runtime_init(void*,size_t,size_t)");
        auto customCalleeHandler = M->getOrInsertFunction(funcName, customType);

        // Create and insert the function call
        IRBuilder<> builder(&*posI);
        builder.CreateCall(customCalleeHandler, {sizeVal, ceVal})->setDebugLoc(builder.getCurrentDebugLocation());
    }
    
    //Insert call to function that will spawn host polling process
    void ZRayPass::initHostInterface(Module *M, inst_iterator posI, llvm::DILocation *DI)
    {
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {},
                                                     false);

        std::string funcName = mangleFunctionName("__start_host_proc()");
        auto customCalleeHandler = M->getOrInsertFunction(funcName, customType);

        // Create and insert the function call
        IRBuilder<> builder(&*posI);
        CallInst *Call = builder.CreateCall(customCalleeHandler, {});
        Call->setDebugLoc(DI); //TODO: May need to include debugloc if crashes come
    }

    //Insert call to function that will register thread with host polling process
    void ZRayPass::registerThreadInterface(Module *M, llvm::BasicBlock::iterator posI, llvm::DILocation *DI)
    {
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {},
                                                     false);

        std::string funcName = mangleFunctionName("__register_thread()");
        auto customCalleeHandler = M->getOrInsertFunction(funcName, customType);

        // Create and insert the function call
        IRBuilder<> builder(&*posI);
        CallInst *Call = builder.CreateCall(customCalleeHandler, {});
        Call->setDebugLoc(DI); //TODO: May need to include debugloc if crashes come
    }

    //Insert call to function that will unregister thread with host polling process
    void ZRayPass::unregisterThreadInterface(Module *M, llvm::BasicBlock::iterator posI, llvm::DILocation *DI)
    {
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {},
                                                     false);

        std::string funcName = mangleFunctionName("__unregister_thread()");
        auto customCalleeHandler = M->getOrInsertFunction(funcName, customType);

        // Create and insert the function call
        IRBuilder<> builder(&*posI);
        CallInst *Call = builder.CreateCall(customCalleeHandler, {});
        Call->setDebugLoc(DI); //TODO: May need to include debugloc if crashes come
    }

    // Used to trigger deconstructor on thread exit for thread cleanup code.
    void ZRayPass::insertThreadExitHandler(Module *M, llvm::BasicBlock::iterator posI, llvm::DILocation *DI, bool enableMonitor)
    {
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {},
                                                     false);
        std::string funcName;
        // Get reference to function we have written in other file
        if(enableMonitor)
        {
             funcName = mangleFunctionName("on_thread_exit_monitor()");
        }
        else
        {
             funcName = mangleFunctionName("on_thread_exit()");
        }

        auto customCalleeHandler = M->getOrInsertFunction(funcName, customType);

        // Create and insert the function call
        IRBuilder<> builder(&*posI);
        // auto castedArray = builder.CreateBitCast(CounterArray, Type::getInt64PtrTy(M->getContext()));
        CallInst *Call = builder.CreateCall(customCalleeHandler, {});
        Call->setDebugLoc(DI);
    }

    void ZRayPass::insertCounterArrayInc(Module *M, llvm::BasicBlock::iterator posI, const ProfileData &profile)
    {
        // Create function type for custom event
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext())},
                                                      //Type::getInt1Ty(M->getContext())},
                                                     false);

        // Get function
        // auto customCallee = M->getOrInsertFunction(mangleFunctionName("incrementCounterArray(size_t,size_t,bool)"), customType);
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("incrementCounterArray(size_t,size_t)"), customType);

        IRBuilder<> builder(&(*posI));

        // Get Region and Set IDs value
        Value *RegionID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PragmaRegionID);
        Value *PDSetID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PostDomSetID);
        // Value * offset = builder.getInt64(profile.PostDomSetID);

        // builder.CreateCall(customCallee, {RegionID, PDSetID, true})->setDebugLoc(builder.getCurrentDebugLocation());
        builder.CreateCall(customCallee, {RegionID, PDSetID})->setDebugLoc(builder.getCurrentDebugLocation());
    }

    void ZRayPass::insertLoadArrayInc(Module *M, llvm::BasicBlock::iterator posI, const ProfileData &profile, Value *Size)
    {
        // Create function type for custom event
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext())},
                                                      //Type::getInt1Ty(M->getContext())},
                                                     false);

        // Get function
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("incrementLoadArray(size_t,size_t,size_t)"), customType);

        IRBuilder<> builder(&(*posI));

        // Get Region and Set IDs value
        Value *RegionID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PragmaRegionID);
        Value *PDSetID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PostDomSetID);
        // Value * offset = builder.getInt64(profile.PostDomSetID);

        builder.CreateCall(customCallee, {RegionID, PDSetID, Size})->setDebugLoc(builder.getCurrentDebugLocation());
    }

    void ZRayPass::insertStoreArrayInc(Module *M, llvm::BasicBlock::iterator posI, const ProfileData &profile, Value *Size)
    {
        // Create function type for custom event
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext())},
                                                      //Type::getInt1Ty(M->getContext())},
                                                     false);

        // Get function
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("incrementStoreArray(size_t,size_t,size_t)"), customType);

        IRBuilder<> builder(&(*posI));

        // Get Region and Set IDs value
        Value *RegionID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PragmaRegionID);
        Value *PDSetID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PostDomSetID);
        // Value * offset = builder.getInt64(profile.PostDomSetID);

        builder.CreateCall(customCallee, {RegionID, PDSetID, Size})->setDebugLoc(builder.getCurrentDebugLocation());
    }

    void ZRayPass::insertLoadStoreArrayInc(Module *M, llvm::BasicBlock::iterator posI, const ProfileData &profile, Value *Size)
    {
        // Create function type for custom event
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext())},
                                                      //Type::getInt1Ty(M->getContext())},
                                                     false);

        // Get function
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("incrementLoadStoreArray(size_t,size_t,size_t)"), customType);

        IRBuilder<> builder(&(*posI));

        // Get Region and Set IDs value
        Value *RegionID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PragmaRegionID);
        Value *PDSetID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PostDomSetID);
        // Value * offset = builder.getInt64(profile.PostDomSetID);

        builder.CreateCall(customCallee, {RegionID, PDSetID, Size})->setDebugLoc(builder.getCurrentDebugLocation());
    }

    // Insert an xray custom event
    void ZRayPass::insertTimerEvent(Module *M, llvm::BasicBlock::iterator posI, const ProfileData &profile)
    {
        // Create function type for custom event
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {//Type::getInt8PtrTy(M->getContext()),
                                                      Type::getInt64Ty(M->getContext())},
                                                     false);

        // Get customevent function
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("timingEvent(size_t)"), customType);
        // auto customCallee = M->getOrInsertFunction(mangleFunctionName("timingEvent(void*,size_t)"), customType);
        //auto customCallee = M->getOrInsertFunction("llvm.xray.customevent", customType);

        IRBuilder<> builder(&(*posI));

        Value *sizeVal = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PragmaRegionID);

        // auto indexed = getConstantInt64GV(M, profile.PragmaRegionID);

        // XRAY CustomEvent based system
        // auto castedPtr = builder.CreateBitCast(indexed, Type::getInt8PtrTy(M->getContext()));
        builder.CreateCall(customCallee, {sizeVal})->setDebugLoc(builder.getCurrentDebugLocation());
        // builder.CreateCall(customCallee, {castedPtr, sizeVal})->setDebugLoc(builder.getCurrentDebugLocation());

        TimingEventCount++;
    }

    void ZRayPass::insertStartTimerEvent(Module *M, llvm::BasicBlock::iterator posI, const size_t &pragmaRegionID)
    {
        // Create function type for custom event
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext())},
                                                     false);

        // Get function
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("startTimingEvent(size_t)"), customType);

        IRBuilder<> builder(&(*posI));

        // Get index value
        Value *sizeVal = ConstantInt::get(Type::getInt64Ty(M->getContext()), pragmaRegionID);

        builder.CreateCall(customCallee, {sizeVal})->setDebugLoc(builder.getCurrentDebugLocation());

        TimingEventCount++;
    }

    void ZRayPass::insertEndTimerEvent(Module *M, llvm::BasicBlock::iterator posI, const size_t &pragmaRegionID)
    {
        // Create function type for custom event
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext())},
                                                     false);

        // Get function
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("endTimingEvent(size_t)"), customType);

        IRBuilder<> builder(&(*posI));

        // Get index value
        Value *sizeVal = ConstantInt::get(Type::getInt64Ty(M->getContext()), pragmaRegionID);

        builder.CreateCall(customCallee, {sizeVal})->setDebugLoc(builder.getCurrentDebugLocation());

        TimingEventCount++;
    }

    // Insert a dynamic counter update, primarily for loop hoisted counters that are evaluated at runtime
    void ZRayPass::insertDynamicLoopEvent(Module *M, Function &F, llvm::Loop * L, llvm::BasicBlock::iterator posI, const ProfileData &profile, bool IsIndirect)
    {
        IRBuilder<> builder(&(*posI));

        const SCEV *v = SE->getBackedgeTakenCount(L, llvm::ScalarEvolution::ExitCountKind::Exact);
        SCEVExpander expander(*SE, _M->getDataLayout(), "dynLabel");

        //Could be options to consider later?
        //expander.disableCanonicalMode();
        //expander.enableLSRMode();
        
        // Type * IntPtrType = Type::getInt64PtrTy(M->getContext());
        Type * IntType = Type::getInt64Ty(M->getContext());

        // Non-atomic increment

        //Load i64 TLS pointer value
        // Value * loadedPtrValue = builder.CreateLoad(IntType, CounterArrayRegionOffset, true);

        //Cast i64 to i64*
        // Value * castedPtr = builder.CreateIntToPtr(loadedPtrValue, IntPtrType);

        //Apply offset to ptr
        // Value * offset = builder.getInt64(profile.PostDomSetID);
        // Value * offsetPtr = builder.CreateGEP(IntType, castedPtr, offset);

        //Load, increment, store
        // Value * loadedValue = builder.CreateLoad(IntType, offsetPtr, true);

        Value * dynSF = expander.expandCodeFor(v, v->getType(), &*builder.GetInsertPoint());

        Value * dynSFInt64 = builder.CreateIntCast(dynSF, IntType, false);

        // Create function call to incrementCounterArraySF
        FunctionType *customType = FunctionType::get(Type::getVoidTy(M->getContext()),
                                                     {Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext()),
                                                      Type::getInt64Ty(M->getContext())},
                                                     false);
        auto customCallee = M->getOrInsertFunction(mangleFunctionName("incrementCounterArraySF(size_t,size_t,size_t)"), customType);
        Value * RegionID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PragmaRegionID);
        Value * PDSetID = ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PostDomSetID);
        builder.CreateCall(customCallee, {RegionID, PDSetID, dynSFInt64})->setDebugLoc(builder.getCurrentDebugLocation());

        // Value * incValue = builder.CreateAdd(loadedValue, dynSFInt64);

        // builder.CreateStore(incValue, offsetPtr, true);

        // Write profile to log
        PragmaRegionLogFile.write(reinterpret_cast<const char *>(&profile), sizeof(ProfileData));

        // Write checksum
        int Check = checksum(reinterpret_cast<const char *>(&profile), sizeof(ProfileData));
        PragmaRegionLogFile.write(reinterpret_cast<const char *>(&Check), sizeof(Check));

        // Write function name to log
        std::string FunctionName = F.getName().str();
        size_t FunctionNameLen = FunctionName.size();
        PragmaRegionLogFile.write(reinterpret_cast<const char *>(&FunctionNameLen), sizeof(FunctionNameLen));
        PragmaRegionLogFile.write(FunctionName.c_str(), FunctionNameLen);

        TotalLoadCount += profile.LoadCount;
        TotalStoreCount += profile.StoreCount;
        GlobalCounterEventTotal++;
        RegionCounterEventTotal++;

        if(IsIndirect)
        {
            IndirectFunctionCounterOffset++;
        }
    }

    // Insert a static based software counter 
    void ZRayPass::insertCustomEvent(Module *M, Function &F, const ProfileData &profile, bool IsIndirect)
    {
        /* IRBuilder<> builder(&(*posI));

        FunctionType *ct = FunctionType::get(Type::getVoidTy(M->getContext()), false);
        auto IA = llvm::InlineAsm::get(ct, "#ZRAY_COUNTER_START", "", true);
        builder.CreateCall(IA, None);

        // Manually increment counters

        // Generate random offset-----------------------
        // Using rand();
        // std::string funcName = "rand";----------
        // FunctionType *customType = FunctionType::get(Type::getInt32Ty(M->getContext()),
        //                                              {},
        //                                              false);

        // auto customCallee = M->getOrInsertFunction(funcName, customType);
        // auto randNumber = builder.CreateCall(customCallee);

        // Using readcyclecounter;------------
        // FunctionType *readType = FunctionType::get(Type::getInt64Ty(M->getContext()),
        //                                              {},
        //                                              false);

        // auto readtdsc = M->getOrInsertFunction("llvm.readcyclecounter", readType);
        // auto randNumber = builder.CreateCall(readtdsc);

        // Mask random value and add to offset------------
        // auto maskedRand = builder.CreateAnd(randNumber, 3);
        // auto randIndex = builder.CreateAdd(maskedRand, ConstantInt::get(Type::getInt64Ty(M->getContext()), profile.PostDomSetID));

        // Atomic update
        // builder.CreateAtomicRMW(llvm::AtomicRMWInst::Add, castedPtr,
        //         ConstantInt::get(Type::getInt64Ty(M->getContext()), 1), MaybeAlign(), AtomicOrdering::Monotonic);
        
        Type * IntPtrType = Type::getInt64PtrTy(M->getContext());
        Type * IntType = Type::getInt64Ty(M->getContext());

        // Non-atomic increment

        //Load i64 TLS pointer value
        Value * loadedPtrValue = builder.CreateLoad(IntType, CounterArrayRegionOffset, true);

        //Cast i64 to i64*
        Value * castedPtr = builder.CreateIntToPtr(loadedPtrValue, IntPtrType);

        //Apply offset to ptr
        // Value * offset = builder.getInt64(profile.PostDomSetID);
        Value * offset = builder.getInt64(profile.PostDomSetID);
        Value * offsetPtr = builder.CreateGEP(IntType, castedPtr, offset);

        //Load, increment, store
        Value * loadedValue = builder.CreateLoad(IntType, offsetPtr, true);
        Value * incValue = builder.CreateAdd(loadedValue, ConstantInt::get(IntType, 1));

        //if(!IsIndirect)
        builder.CreateStore(incValue, offsetPtr, true);


        auto IA2 = llvm::InlineAsm::get(ct, "#ZRAY_COUNTER_END", "", true);
        builder.CreateCall(IA2, None); */

        // Write profile to log
        PragmaRegionLogFile.write(reinterpret_cast<const char *>(&profile), sizeof(ProfileData));

        // Write checksum
        int Check = checksum(reinterpret_cast<const char *>(&profile), sizeof(ProfileData));
        PragmaRegionLogFile.write(reinterpret_cast<const char *>(&Check), sizeof(Check));

        // Write function name to log
        std::string FunctionName = F.getName().str();
        size_t FunctionNameLen = FunctionName.size();
        PragmaRegionLogFile.write(reinterpret_cast<const char *>(&FunctionNameLen), sizeof(FunctionNameLen));
        PragmaRegionLogFile.write(FunctionName.c_str(), FunctionNameLen);


        TotalLoadCount += profile.LoadCount;
        TotalStoreCount += profile.StoreCount;
        GlobalCounterEventTotal++;
        RegionCounterEventTotal++;

        if(IsIndirect)
        {
            IndirectFunctionCounterOffset++;
        }
    }

    // Insert ASM at end of basic block for MIR pass to identify split blocks
    void ZRayPass::insertEndMarker(Module *M, llvm::BasicBlock::reverse_iterator posI)
    {
        IRBuilder<> builder(&(*posI));

        FunctionType *ct = FunctionType::get(Type::getVoidTy(M->getContext()), false);
        auto IA = llvm::InlineAsm::get(ct, "#BB_HAS_ZRAY_COUNTER", "", true);
        builder.CreateCall(IA, None);
    }

    // Insert a software counter for runtime intrinsic data
    void ZRayPass::insertLoadRuntimeIntrinsicEvent(Module *M, llvm::BasicBlock::iterator posI, const ProfileData &profile, Value * size)
    {
        IRBuilder<> builder(&(*posI));

        FunctionType *ct = FunctionType::get(Type::getVoidTy(M->getContext()), false);
        auto IA = llvm::InlineAsm::get(ct, "#ZRAY_COUNTER_START", "", true);
        builder.CreateCall(IA, None);

        Type * IntPtrType = Type::getInt64PtrTy(M->getContext());
        Type * IntType = Type::getInt64Ty(M->getContext());

        // Non-atomic increment

        //Load i64 TLS pointer value
        Value * loadedPtrValue = builder.CreateLoad(IntType, LoadRuntimeArrayRegionOffset, true);

        //Cast i64 to i64*
        Value * castedPtr = builder.CreateIntToPtr(loadedPtrValue, IntPtrType);

        //Apply offset to ptr
        // Value * offset = builder.getInt64(profile.PostDomSetID);
        Value * offset = builder.getInt64(profile.PostDomSetID);
        Value * offsetPtr = builder.CreateGEP(IntType, castedPtr, offset);

        //Load, increment, store
        Value * loadedValue = builder.CreateLoad(IntType, offsetPtr, true);
        Value * incValue = builder.CreateAdd(loadedValue, size);
        // Value * adjustedValue = builder.CreateAdd(loadedValue, ConstantInt::get(IntType, 1));
        // Value * byteValue = builder.CreateUDiv(size, ConstantInt::get(IntType, 8));
        // Value * incValue = builder.CreateAdd(adjustedValue, byteValue);

        builder.CreateStore(incValue, offsetPtr, true);


        auto IA2 = llvm::InlineAsm::get(ct, "#ZRAY_COUNTER_END", "", true);
        builder.CreateCall(IA2, None);
    }

    // Insert a software counter for runtime intrinsic data
    void ZRayPass::insertStoreRuntimeIntrinsicEvent(Module *M, llvm::BasicBlock::iterator posI, const ProfileData &profile, Value * size)
    {
        IRBuilder<> builder(&(*posI));

        FunctionType *ct = FunctionType::get(Type::getVoidTy(M->getContext()), false);
        auto IA = llvm::InlineAsm::get(ct, "#ZRAY_COUNTER_START", "", true);
        builder.CreateCall(IA, None);

        Type * IntPtrType = Type::getInt64PtrTy(M->getContext());
        Type * IntType = Type::getInt64Ty(M->getContext());

        // Non-atomic increment

        //Load i64 TLS pointer value
        Value * loadedPtrValue = builder.CreateLoad(IntType, StoreRuntimeArrayRegionOffset, true);

        //Cast i64 to i64*
        Value * castedPtr = builder.CreateIntToPtr(loadedPtrValue, IntPtrType);

        //Apply offset to ptr
        // Value * offset = builder.getInt64(profile.PostDomSetID);
        Value * offset = builder.getInt64(profile.PostDomSetID);
        Value * offsetPtr = builder.CreateGEP(IntType, castedPtr, offset);

        //Load, increment, store
        Value * loadedValue = builder.CreateLoad(IntType, offsetPtr, true);
        Value * incValue = builder.CreateAdd(loadedValue, size);
        // Value * adjustedValue = builder.CreateAdd(loadedValue, ConstantInt::get(IntType, 1));
        // Value * byteValue = builder.CreateUDiv(size, ConstantInt::get(IntType, 8));
        // Value * incValue = builder.CreateAdd(adjustedValue, byteValue);

        builder.CreateStore(incValue, offsetPtr, true);


        auto IA2 = llvm::InlineAsm::get(ct, "#ZRAY_COUNTER_END", "", true);
        builder.CreateCall(IA2, None);
    }

    //Insert a placeholder instruction to mark the Pragma Region entry block
    void ZRayPass::registerPragmaPtrInst(Module * M, BasicBlock * BB, size_t RegionID)
    {
        //Insert placeholder with Region ID as first operand to use later
        Type * IntType = Type::getInt64Ty(M->getContext());
        IRBuilder<> builder(&(*BB->getFirstInsertionPt()));
        // RegionOffsetUpdateInstList.push_back(builder.CreateStore(ConstantInt::get(IntType, RegionID), CounterArrayRegionOffset));
        // LoadRuntimeRegionOffsetUpdateInstList.push_back(builder.CreateStore(ConstantInt::get(IntType, RegionID), LoadRuntimeArrayRegionOffset));
        // StoreRuntimeRegionOffsetUpdateInstList.push_back(builder.CreateStore(ConstantInt::get(IntType, RegionID), StoreRuntimeArrayRegionOffset));
    }

    //Replace placeholder instruction with proper update to TLS pointer. Done in post once
    //we know how many pragma regions are present.
    void ZRayPass::applyPragmaPtrInst(Module * M, StoreInst * Store)
    {
        Type * IntType = Type::getInt64Ty(M->getContext());
        Type * IntPtrType = Type::getInt64PtrTy(M->getContext());
        
        //Set IR builder to start of basic block
        BasicBlock * BB = Store->getParent();
        IRBuilder<> builder(&(*BB->getFirstInsertionPt()));

        //Convert global counter array from array type to i64*
        Value * ConvertedPtr = builder.CreateBitCast(CounterArray, IntPtrType);

        //We saved the Region Id as the first operand in the placeholder store
        Value * RegionID = Store->getValueOperand();

        //Create base offset index by multiplying region ID by 1D counter array size
        Value * ArraySize = builder.getInt64(CountArraySize);
        Value * Offset = builder.CreateMul(RegionID, ArraySize);

        //Apply offset to converted global array pointer
        Value * OffsetPtr = builder.CreateGEP(IntType, ConvertedPtr, Offset);

        //Save computed pointer value to TTS pointer variable
        Value * PtrIntVal = builder.CreatePtrToInt(OffsetPtr, IntType);
        builder.CreateStore(PtrIntVal, CounterArrayRegionOffset);

        //Remove placeholder instruction
        Store->removeFromParent();
        Store->deleteValue();
    }

    //Replace placeholder instruction with proper update to TLS pointer. Done in post once
    //we know how many pragma regions are present.
    void ZRayPass::applyLoadRuntimePragmaPtrInst(Module * M, StoreInst * Store)
    {
        Type * IntType = Type::getInt64Ty(M->getContext());
        Type * IntPtrType = Type::getInt64PtrTy(M->getContext());

        //Set IR builder to start of basic block
        BasicBlock * BB = Store->getParent();
        IRBuilder<> builder(&(*BB->getFirstInsertionPt()));

        //Convert global counter array from array type to i64*
        Value * ConvertedPtr = builder.CreateBitCast(LoadRuntimeArray, IntPtrType);

        //We saved the Region Id as the first operand in the placeholder store
        Value * RegionID = Store->getValueOperand();

        //Create base offset index by multiplying region ID by 1D counter array size
        Value * ArraySize = builder.getInt64(CountArraySize);
        Value * Offset = builder.CreateMul(RegionID, ArraySize);

        //Apply offset to converted global array pointer
        Value * OffsetPtr = builder.CreateGEP(IntType, ConvertedPtr, Offset);

        //Save computed pointer value to TTS pointer variable
        Value * PtrIntVal = builder.CreatePtrToInt(OffsetPtr, IntType);
        builder.CreateStore(PtrIntVal, LoadRuntimeArrayRegionOffset);

        //Remove placeholder instruction
        Store->removeFromParent();
        Store->deleteValue();
    }

    //Replace placeholder instruction with proper update to TLS pointer. Done in post once
    //we know how many pragma regions are present.
    void ZRayPass::applyStoreRuntimePragmaPtrInst(Module * M, StoreInst * Store)
    {
        Type * IntType = Type::getInt64Ty(M->getContext());
        Type * IntPtrType = Type::getInt64PtrTy(M->getContext());

        //Set IR builder to start of basic block
        BasicBlock * BB = Store->getParent();
        IRBuilder<> builder(&(*BB->getFirstInsertionPt()));

        //Convert global counter array from array type to i64*
        Value * ConvertedPtr = builder.CreateBitCast(StoreRuntimeArray, IntPtrType);

        //We saved the Region Id as the first operand in the placeholder store
        Value * RegionID = Store->getValueOperand();

        //Create base offset index by multiplying region ID by 1D counter array size
        Value * ArraySize = builder.getInt64(CountArraySize);
        Value * Offset = builder.CreateMul(RegionID, ArraySize);

        //Apply offset to converted global array pointer
        Value * OffsetPtr = builder.CreateGEP(IntType, ConvertedPtr, Offset);

        //Save computed pointer value to TTS pointer variable
        Value * PtrIntVal = builder.CreatePtrToInt(OffsetPtr, IntType);
        builder.CreateStore(PtrIntVal, StoreRuntimeArrayRegionOffset);

        //Remove placeholder instruction
        Store->removeFromParent();
        Store->deleteValue();
    }

    //Iterate over list of placeholder instructions and update
    void ZRayPass::processPragmaPtrInstList(Module * M)
    {
        /* for(auto Store : this->RegionOffsetUpdateInstList)
        {
            applyPragmaPtrInst(M, Store);
        }*/
        /* for(auto Store : this->LoadRuntimeRegionOffsetUpdateInstList)
        {
            applyLoadRuntimePragmaPtrInst(M, Store);
        }
        for(auto Store : this->StoreRuntimeRegionOffsetUpdateInstList)
        {
            applyStoreRuntimePragmaPtrInst(M, Store);
        }*/
    }

    void ZRayPass::insertBBTag(Module *M, llvm::BasicBlock::iterator posI, size_t ceID, size_t scaleFactor)
    {
        IRBuilder<> builder(&(*posI));

        FunctionType *ct = FunctionType::get(Type::getVoidTy(M->getContext()), false);
        // TODO: Verify we have removed side-effect influence
        auto IA = llvm::InlineAsm::get(ct, "#BB_TAG " + std::to_string(ceID) + " " + std::to_string(scaleFactor), "", true);
        builder.CreateCall(IA, None);
    }

    // Allocate and return reference to privately linked constant int64 variable
    GlobalVariable *ZRayPass::getConstantInt64GV(Module *M, size_t val)
    {
        return new GlobalVariable(*M,
                                  Type::getInt64Ty(M->getContext()),
                                  true,
                                  GlobalVariable::LinkageTypes::PrivateLinkage,
                                  ConstantInt::get(Type::getInt64Ty(M->getContext()), val));
    }

    // Allocate i64 array of @size elements
    GlobalVariable *ZRayPass::getArray(Module *M, size_t size)
    {
        // https://stackoverflow.com/questions/23330018/llvm-global-integer-array-zeroinitializer
        auto array_type = ArrayType::get(Type::getInt64Ty(M->getContext()), size);

        GlobalVariable *gv = new GlobalVariable(*M, array_type, false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                                0, "CounterArray", nullptr, llvm::GlobalValue::GeneralDynamicTLSModel);

        ConstantAggregateZero *const_array = ConstantAggregateZero::get(array_type);

        gv->setInitializer(const_array);

        return gv;
    }

    // Allocate i64 array of @size elements
    GlobalVariable *ZRayPass::getLoadRuntimeArray(Module *M, size_t size)
    {
        // https://stackoverflow.com/questions/23330018/llvm-global-integer-array-zeroinitializer
        auto array_type = ArrayType::get(Type::getInt64Ty(M->getContext()), size);

        GlobalVariable *gv = new GlobalVariable(*M, array_type, false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                                0, "LoadRuntimeArray", nullptr, llvm::GlobalValue::GeneralDynamicTLSModel);

        ConstantAggregateZero *const_array = ConstantAggregateZero::get(array_type);

        gv->setInitializer(const_array);

        return gv;
    }

    // Allocate i64 array of @size elements
    GlobalVariable *ZRayPass::getStoreRuntimeArray(Module *M, size_t size)
    {
        // https://stackoverflow.com/questions/23330018/llvm-global-integer-array-zeroinitializer
        auto array_type = ArrayType::get(Type::getInt64Ty(M->getContext()), size);

        GlobalVariable *gv = new GlobalVariable(*M, array_type, false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                                0, "StoreRuntimeArray", nullptr, llvm::GlobalValue::GeneralDynamicTLSModel);

        ConstantAggregateZero *const_array = ConstantAggregateZero::get(array_type);

        gv->setInitializer(const_array);

        return gv;
    }

    GlobalVariable *ZRayPass::setArrayWidth(Module *M, size_t Val)
    {
        return new GlobalVariable(*M, Type::getInt64Ty(M->getContext()), false, GlobalVariable::LinkageTypes::ExternalLinkage,
                           ConstantInt::get(Type::getInt64Ty(M->getContext()), Val), "ZRAY_CounterDimension");
    }

    // Allocate PragmaRegionCount global variable
    GlobalVariable *ZRayPass::setPragmaRegionCount(Module *M, size_t Val)
    {
        return new GlobalVariable(*M, Type::getInt64Ty(M->getContext()), false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                                                 ConstantInt::get(Type::getInt64Ty(M->getContext()), Val), "PragmaRegionCount", nullptr, llvm::GlobalValue::NotThreadLocal);
        // return new GlobalVariable(*M, Type::getInt64Ty(M->getContext()), false, GlobalVariable::LinkageTypes::ExternalLinkage,
        //                    ConstantInt::get(Type::getInt64Ty(M->getContext()), Val), "ZRAY_CounterDimension");
    }

    /* GlobalVariable *ZRayPass::setPragmaRegionCount(Module *M)
    {
        Value *sizeVal = ConstantInt::get(Type::getInt64Ty(M->getContext()), ROI_Count);
        GlobalVariable *gvPragmaRegionCount = new GlobalVariable(*M, Type::getInt64Ty(M->getContext()), false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                                                 0, "PragmaRegionCount", nullptr, llvm::GlobalValue::NotThreadLocal);
        gvPragmaRegionCount->setInitializer(ConstantInt::get(Type::getInt64Ty(M->getContext()), ROI_Count));

        return gvPragmaRegionCount;
    } */

    // Allocate the global TLS counter selection pointer
    GlobalVariable *ZRayPass::getArrayRegionOffset(Module *M)
    {
        Type *IntType = Type::getInt64Ty(M->getContext());

        return new GlobalVariable(*M, IntType, false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                  ConstantInt::get(IntType, 0), "CounterArrayRegionOffset", nullptr, llvm::GlobalValue::GeneralDynamicTLSModel);
    }

    // Allocate the global TLS counter selection pointer
    GlobalVariable *ZRayPass::getLoadRuntimeArrayRegionOffset(Module *M)
    {
        Type *IntType = Type::getInt64Ty(M->getContext());

        return new GlobalVariable(*M, IntType, false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                  ConstantInt::get(IntType, 0), "LoadRuntimeArrayRegionOffset", nullptr, llvm::GlobalValue::GeneralDynamicTLSModel);
    }

    // Allocate the global TLS counter selection pointer
    GlobalVariable *ZRayPass::getStoreRuntimeArrayRegionOffset(Module *M)
    {
        Type *IntType = Type::getInt64Ty(M->getContext());

        return new GlobalVariable(*M, IntType, false, GlobalVariable::LinkageTypes::ExternalLinkage,
                                  ConstantInt::get(IntType, 0), "StoreRuntimeArrayRegionOffset", nullptr, llvm::GlobalValue::GeneralDynamicTLSModel);
    }

    // Get memory address of the indexed element in given array
    GetElementPtrInst *ZRayPass::getArrayElementPtr(Module *M, GlobalVariable *array, size_t index)
    {
        auto array_type = ArrayType::get(Type::getInt64Ty(M->getContext()), CountArraySize);

        Value *indices[] =
            {
                ConstantInt::get(Type::getInt64Ty(M->getContext()), 0), // Needed to access array (see GEP documentation)
                ConstantInt::get(Type::getInt64Ty(M->getContext()), index)};

        return GetElementPtrInst::CreateInBounds(array_type, array, indices);
    }

#if 0
    GetElementPtrInst *ZRayPass::getArrayElementPtr(Module *M, GlobalVariable *array, llvm::Value * index)
    {
        auto array_type = ArrayType::get(Type::getInt64Ty(M->getContext()), CountArraySize);

        Value *indices[] =
            {
                ConstantInt::get(Type::getInt64Ty(M->getContext()), 0), // Needed to access array (see GEP documentation)
                index};

        return GetElementPtrInst::CreateInBounds(array_type, array, indices);
    }
#endif

    // Allocate and return reference to privately linked constant profile struct
    GlobalVariable *ZRayPass::getProfileStruct(Module *M, ProfileData pd)
    {
        GlobalVariable *gPragmaRegion = getConstantInt64GV(M, pd.PragmaRegionID);
        GlobalVariable *gStoreCount = getConstantInt64GV(M, pd.StoreCount);
        GlobalVariable *gLoadCount = getConstantInt64GV(M, pd.LoadCount);
        GlobalVariable *gGroupID = getConstantInt64GV(M, pd.GroupNumber);
        GlobalVariable *gIntInst = getConstantInt64GV(M, pd.IntInstructionCount);
        GlobalVariable *gFPInst = getConstantInt64GV(M, pd.FpInstructionCount);
        GlobalVariable *gTermInst = getConstantInt64GV(M, pd.TermInstructionCount);
        GlobalVariable *gMemInst = getConstantInt64GV(M, pd.MemInstructionCount);
        GlobalVariable *gCastInst = getConstantInt64GV(M, pd.CastInstructionCount);
        GlobalVariable *gGlobalOpRead = getConstantInt64GV(M, pd.GlobalOpReadCount);
        GlobalVariable *gGlobalOpWrite = getConstantInt64GV(M, pd.GlobalOpWriteCount);
        GlobalVariable *gStackRead = getConstantInt64GV(M, pd.StackReadCount);
        GlobalVariable *gStackWrite = getConstantInt64GV(M, pd.StackWriteCount);
        GlobalVariable *gHeapRead = getConstantInt64GV(M, pd.HeapReadCount);
        GlobalVariable *gHeapWrite = getConstantInt64GV(M, pd.HeapWriteCount);
        GlobalVariable *gOtherInst = getConstantInt64GV(M, pd.OtherInstCount);

        StructType *structWrapTy = StructType::get(gPragmaRegion->getType(),
                                                   gStoreCount->getType(),
                                                   gLoadCount->getType(),
                                                   gGroupID->getType(),
                                                   gIntInst->getType(),
                                                   gFPInst->getType(),
                                                   gTermInst->getType(),
                                                   gMemInst->getType(),
                                                   gCastInst->getType(),
                                                   gGlobalOpRead->getType(),
                                                   gGlobalOpWrite->getType(),
                                                   gStackRead->getType(),
                                                   gStackWrite->getType(),
                                                   gHeapRead->getType(),
                                                   gHeapWrite->getType(),
                                                   gOtherInst->getType());

        Constant *array[] = {
            gPragmaRegion,
            gStoreCount,
            gLoadCount,
            gGroupID,
            gIntInst,
            gFPInst,
            gTermInst,
            gMemInst,
            gCastInst,
            gGlobalOpRead,
            gGlobalOpWrite,
            gStackRead,
            gStackWrite,
            gHeapRead,
            gHeapWrite,
            gOtherInst};

        Constant *structVal = ConstantStruct::get(structWrapTy, array);

        GlobalVariable *globalStruct = new GlobalVariable(*M,
                                                          structVal->getType(),
                                                          true,
                                                          GlobalValue::LinkageTypes::PrivateLinkage,
                                                          structVal);

        return globalStruct;
    }

} // namespace zray
