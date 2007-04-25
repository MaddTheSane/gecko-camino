/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

//
// This file implements a garbage-cycle collector based on the paper
// 
//   Concurrent Cycle Collection in Reference Counted Systems
//   Bacon & Rajan (2001), ECOOP 2001 / Springer LNCS vol 2072
//
// We are not using the concurrent or acyclic cases of that paper; so
// the green, red and orange colors are not used.
//
// The collector is based on tracking pointers of four colors:
//
// Black nodes are definitely live. If we ever determine a node is
// black, it's ok to forget about, drop from our records.
//
// White nodes are definitely garbage cycles. Once we finish with our
// scanning, we unlink all the white nodes and expect that by
// unlinking them they will self-destruct (since a garbage cycle is
// only keeping itself alive with internal links, by definition).
//
// Grey nodes are being scanned. Nodes that turn grey will turn
// either black if we determine that they're live, or white if we
// determine that they're a garbage cycle. After the main collection
// algorithm there should be no grey nodes.
//
// Purple nodes are *candidates* for being scanned. They are nodes we
// haven't begun scanning yet because they're not old enough, or we're
// still partway through the algorithm.
//
// XPCOM objects participating in garbage-cycle collection are obliged
// to inform us when they ought to turn purple; that is, when their
// refcount transitions from N+1 -> N, for nonzero N. Furthermore we
// require that *after* an XPCOM object has informed us of turning
// purple, they will tell us when they either transition back to being
// black (incremented refcount) or are ultimately deleted.


// Safety:
//
// An XPCOM object is either scan-safe or scan-unsafe, purple-safe or
// purple-unsafe.
//
// An object is scan-safe if:
//
//  - It can be QI'ed to |nsCycleCollectionParticipant|, though this
//    operation loses ISupports identity (like nsIClassInfo).
//  - The operation |traverse| on the resulting
//    nsCycleCollectionParticipant does not cause *any* refcount
//    adjustment to occur (no AddRef / Release calls).
//
// An object is purple-safe if it satisfies the following properties:
//
//  - The object is scan-safe.  
//  - If the object calls |nsCycleCollector::suspect(this)|, 
//    it will eventually call |nsCycleCollector::forget(this)|, 
//    exactly once per call to |suspect|, before being destroyed.
//
// When we receive a pointer |ptr| via
// |nsCycleCollector::suspect(ptr)|, we assume it is purple-safe. We
// can check the scan-safety, but have no way to ensure the
// purple-safety; objects must obey, or else the entire system falls
// apart. Don't involve an object in this scheme if you can't
// guarantee its purple-safety.
//
// When we have a scannable set of purple nodes ready, we begin
// our walks. During the walks, the nodes we |traverse| should only
// feed us more scan-safe nodes, and should not adjust the refcounts
// of those nodes. 
//
// We do not |AddRef| or |Release| any objects during scanning. We
// rely on purple-safety of the roots that call |suspect| and
// |forget| to hold, such that we will forget about a purple pointer
// before it is destroyed.  The pointers that are merely scan-safe,
// we hold only for the duration of scanning, and there should be no
// objects released from the scan-safe set during the scan (there
// should be no threads involved).
//
// We *do* call |AddRef| and |Release| on every white object, on
// either side of the calls to |Unlink|. This keeps the set of white
// objects alive during the unlinking.
// 

#ifndef __MINGW32__
#ifdef WIN32
#include <crtdbg.h>
#include <errno.h>
#endif
#endif

#include "nsCycleCollectionParticipant.h"
#include "nsIProgrammingLanguage.h"
#include "nsBaseHashtable.h"
#include "nsHashKeys.h"
#include "nsDeque.h"
#include "nsCycleCollector.h"
#include "nsThreadUtils.h"
#include "prenv.h"
#include "prprf.h"
#include "plstr.h"
#include "prtime.h"
#include "nsPrintfCString.h"
#include "nsTArray.h"

#include <stdio.h>
#ifdef WIN32
#include <io.h>
#include <process.h>
#endif

#define DEFAULT_SHUTDOWN_COLLECTIONS 5
#ifdef DEBUG_CC
#define SHUTDOWN_COLLECTIONS(params) params.mShutdownCollections
#else
#define SHUTDOWN_COLLECTIONS(params) DEFAULT_SHUTDOWN_COLLECTIONS
#endif

// Various parameters of this collector can be tuned using environment
// variables.

struct nsCycleCollectorParams
{
    PRBool mDoNothing;
#ifdef DEBUG_CC
    PRBool mReportStats;
    PRBool mHookMalloc;
    PRBool mDrawGraphs;
    PRBool mFaultIsFatal;
    PRBool mLogPointers;

    PRUint32 mShutdownCollections;
#endif
    
    PRUint32 mScanDelay;
    
    nsCycleCollectorParams() :
#ifdef DEBUG_CC
        mDoNothing     (PR_GetEnv("XPCOM_CC_DO_NOTHING") != NULL),
        mReportStats   (PR_GetEnv("XPCOM_CC_REPORT_STATS") != NULL),
        mHookMalloc    (PR_GetEnv("XPCOM_CC_HOOK_MALLOC") != NULL),
        mDrawGraphs    (PR_GetEnv("XPCOM_CC_DRAW_GRAPHS") != NULL),
        mFaultIsFatal  (PR_GetEnv("XPCOM_CC_FAULT_IS_FATAL") != NULL),
        mLogPointers   (PR_GetEnv("XPCOM_CC_LOG_POINTERS") != NULL),

        mShutdownCollections(DEFAULT_SHUTDOWN_COLLECTIONS),
#else
        mDoNothing     (PR_FALSE),
#endif

        // The default number of collections to "age" candidate
        // pointers in the purple buffer before we decide that any
        // garbage cycle they're in has stabilized and we want to
        // consider scanning it.
        //
        // Making this number smaller causes:
        //   - More time to be spent in the collector (bad)
        //   - Less delay between forming garbage and collecting it (good)

        mScanDelay(10)
    {
#ifdef DEBUG_CC
        char *s = PR_GetEnv("XPCOM_CC_SCAN_DELAY");
        if (s)
            PR_sscanf(s, "%d", &mScanDelay);
        s = PR_GetEnv("XPCOM_CC_SHUTDOWN_COLLECTIONS");
        if (s)
            PR_sscanf(s, "%d", &mShutdownCollections);
#endif
    }
};

#ifdef DEBUG_CC
// Various operations involving the collector are recorded in a
// statistics table. These are for diagnostics.

struct nsCycleCollectorStats
{
    PRUint32 mFailedQI;
    PRUint32 mSuccessfulQI;

    PRUint32 mVisitedNode;
    PRUint32 mVisitedJSNode;
    PRUint32 mWalkedGraph;
    PRUint32 mCollectedBytes;
    PRUint32 mFreeCalls;
    PRUint32 mFreedBytes;

    PRUint32 mSetColorGrey;
    PRUint32 mSetColorBlack;
    PRUint32 mSetColorWhite;

    PRUint32 mFailedUnlink;
    PRUint32 mCollectedNode;
    PRUint32 mBumpGeneration;
    PRUint32 mZeroGeneration;

    PRUint32 mSuspectNode;
    PRUint32 mSpills;    
    PRUint32 mForgetNode;
    PRUint32 mFreedWhilePurple;
  
    PRUint32 mCollection;

    nsCycleCollectorStats()
    {
        memset(this, 0, sizeof(nsCycleCollectorStats));
    }
  
    void Dump()
    {
        fprintf(stderr, "\f\n");
#define DUMP(entry) fprintf(stderr, "%30.30s: %-20.20d\n", #entry, entry)
        DUMP(mFailedQI);
        DUMP(mSuccessfulQI);
    
        DUMP(mVisitedNode);
        DUMP(mVisitedJSNode);
        DUMP(mWalkedGraph);
        DUMP(mCollectedBytes);
        DUMP(mFreeCalls);
        DUMP(mFreedBytes);
    
        DUMP(mSetColorGrey);
        DUMP(mSetColorBlack);
        DUMP(mSetColorWhite);
    
        DUMP(mFailedUnlink);
        DUMP(mCollectedNode);
        DUMP(mBumpGeneration);
        DUMP(mZeroGeneration);
    
        DUMP(mSuspectNode);
        DUMP(mSpills);
        DUMP(mForgetNode);
        DUMP(mFreedWhilePurple);
    
        DUMP(mCollection);
#undef DUMP
    }
};
#endif

static void
ToParticipant(nsISupports *s, nsCycleCollectionParticipant **cp);

#ifdef DEBUG_CC
static PRBool
nsCycleCollector_shouldSuppress(nsISupports *s);
#endif

////////////////////////////////////////////////////////////////////////
// Base types
////////////////////////////////////////////////////////////////////////

enum NodeColor { black, white, grey };

// This structure should be kept as small as possible; we may expect
// a million of them to be allocated and touched repeatedly during
// each cycle collection.

struct PtrInfo
    : public PLDHashEntryStub
{
    PRUint32 mColor : 2;
    PRUint32 mInternalRefs : 30;
    // FIXME: mLang expands back to a full word when bug 368774 lands.
    PRUint32 mLang : 2;
    PRUint32 mRefCount : 30;

#ifdef DEBUG_CC
    size_t mBytes;
    const char *mName;
#endif
};

PR_STATIC_CALLBACK(PRBool)
InitPtrInfo(PLDHashTable *table, PLDHashEntryHdr *entry, const void *key)
{
    PtrInfo* pi = (PtrInfo*)entry;
    pi->key = key;
    pi->mColor = black;
    pi->mInternalRefs = 0;
    pi->mLang = nsIProgrammingLanguage::CPLUSPLUS;
    pi->mRefCount = 0;
#ifdef DEBUG_CC
    pi->mBytes = 0;
    pi->mName = nsnull;
#endif
    return PR_TRUE;
}

static PLDHashTableOps GCTableOps = {
    PL_DHashAllocTable,
    PL_DHashFreeTable,
    PL_DHashVoidPtrKeyStub,
    PL_DHashMatchEntryStub,
    PL_DHashMoveEntryStub,
    PL_DHashClearEntryStub,
    PL_DHashFinalizeStub,
    InitPtrInfo
 };

struct GCTable
{
    PLDHashTable mTab;

    GCTable()
    {
        Init();
    }
    ~GCTable()
    {
        if (mTab.ops)
            PL_DHashTableFinish(&mTab);
    }

    void Init()
    {
        if (!PL_DHashTableInit(&mTab, &GCTableOps, nsnull, sizeof(PtrInfo),
                               32768))
            mTab.ops = nsnull;
    }
    void Clear()
    {
        if (!mTab.ops || mTab.entryCount > 0) {
            if (mTab.ops)
                PL_DHashTableFinish(&mTab);
            Init();
        }
    }

    PtrInfo *Lookup(void *key)
    {
        if (!mTab.ops)
            return nsnull;

        PLDHashEntryHdr *entry =
            PL_DHashTableOperate(&mTab, key, PL_DHASH_LOOKUP);

        return PL_DHASH_ENTRY_IS_BUSY(entry) ? (PtrInfo*)entry : nsnull;
    }

    PtrInfo *Add(void *key)
    {
        if (!mTab.ops)
            return nsnull;

        return (PtrInfo*)PL_DHashTableOperate(&mTab, key, PL_DHASH_ADD);
    }
    
    void Enumerate(PLDHashEnumerator etor, void *arg)
    {
        if (mTab.ops)
            PL_DHashTableEnumerate(&mTab, etor, arg);
    }
};


// XXX Would be nice to have an nsHashSet<KeyType> API that has
// Add/Remove/Has rather than PutEntry/RemoveEntry/GetEntry.
typedef nsTHashtable<nsVoidPtrHashKey> PointerSet;
typedef nsBaseHashtable<nsVoidPtrHashKey, PRUint32, PRUint32>
    PointerSetWithGeneration;

static void
Fault(const char *msg, const void *ptr);

struct nsPurpleBuffer
{

#define ASSOCIATIVITY 2
#define INDEX_LOW_BIT 6
#define N_INDEX_BITS 13

#define N_ENTRIES (1 << N_INDEX_BITS)
#define N_POINTERS (N_ENTRIES * ASSOCIATIVITY)
#define TOTAL_BYTES (N_POINTERS * PR_BYTES_PER_WORD)
#define INDEX_MASK PR_BITMASK(N_INDEX_BITS)
#define POINTER_INDEX(P) ((((PRUword)P) >> INDEX_LOW_BIT) & (INDEX_MASK))

#if (INDEX_LOW_BIT + N_INDEX_BITS > (8 * PR_BYTES_PER_WORD))
#error "index bit overflow"
#endif

    // This class serves as a generational wrapper around a pldhash
    // table: a subset of generation zero lives in mCache, the
    // remainder spill into the mBackingStore hashtable. The idea is
    // to get a higher hit rate and greater locality of reference for
    // generation zero, in which the vast majority of suspect/forget
    // calls annihilate one another.

    nsCycleCollectorParams &mParams;
#ifdef DEBUG_CC
    nsCycleCollectorStats &mStats;
#endif
    void* mCache[N_POINTERS];
    PRUint32 mCurrGen;    
    PointerSetWithGeneration mBackingStore;
    nsDeque *mTransferBuffer;
    
#ifdef DEBUG_CC
    nsPurpleBuffer(nsCycleCollectorParams &params,
                   nsCycleCollectorStats &stats) 
        : mParams(params),
          mStats(stats),
          mCurrGen(0),
          mTransferBuffer(nsnull)
    {
        Init();
    }
#else
    nsPurpleBuffer(nsCycleCollectorParams &params) 
        : mParams(params),
          mCurrGen(0),
          mTransferBuffer(nsnull)
    {
        Init();
    }
#endif

    ~nsPurpleBuffer()
    {
        memset(mCache, 0, sizeof(mCache));
        mBackingStore.Clear();
    }

    void Init()
    {
        memset(mCache, 0, sizeof(mCache));
        mBackingStore.Init();
    }

    void BumpGeneration();
    void SelectAgedPointers(nsDeque *transferBuffer);

    PRBool Exists(void *p)
    {
        PRUint32 idx = POINTER_INDEX(p);
        for (PRUint32 i = 0; i < ASSOCIATIVITY; ++i) {
            if (mCache[idx+i] == p)
                return PR_TRUE;
        }
        PRUint32 gen;
        return mBackingStore.Get(p, &gen);
    }

    void Put(void *p)
    {
        PRUint32 idx = POINTER_INDEX(p);
        for (PRUint32 i = 0; i < ASSOCIATIVITY; ++i) {
            if (!mCache[idx+i]) {
                mCache[idx+i] = p;
                return;
            }
        }
#ifdef DEBUG_CC
        mStats.mSpills++;
#endif
        SpillOne(p);
    }

    void Remove(void *p)     
    {
        PRUint32 idx = POINTER_INDEX(p);
        for (PRUint32 i = 0; i < ASSOCIATIVITY; ++i) {
            if (mCache[idx+i] == p) {
                mCache[idx+i] = (void*)0;
                return;
            }
        }
        mBackingStore.Remove(p);
    }

    void SpillOne(void* &p)
    {
        mBackingStore.Put(p, mCurrGen);
        p = (void*)0;
    }

    void SpillAll()
    {
        for (PRUint32 i = 0; i < N_POINTERS; ++i) {
            if (mCache[i]) {
                SpillOne(mCache[i]);
            }
        }
    }
};

static PR_CALLBACK PLDHashOperator
zeroGenerationCallback(const void*  ptr,
                       PRUint32&    generation,
                       void*        userArg)
{
#ifdef DEBUG_CC
    nsPurpleBuffer *purp = NS_STATIC_CAST(nsPurpleBuffer*, userArg);
    purp->mStats.mZeroGeneration++;
#endif
    generation = 0;
    return PL_DHASH_NEXT;
}

void nsPurpleBuffer::BumpGeneration()
{
    SpillAll();
    if (mCurrGen == 0xffffffff) {
        mBackingStore.Enumerate(zeroGenerationCallback, this);
        mCurrGen = 0;
    } else {
        ++mCurrGen;
    }
#ifdef DEBUG_CC
    mStats.mBumpGeneration++;
#endif
}

static inline PRBool
SufficientlyAged(PRUint32 generation, nsPurpleBuffer *p)
{
    return generation + p->mParams.mScanDelay < p->mCurrGen;
}

static PR_CALLBACK PLDHashOperator
ageSelectionCallback(const void*  ptr,
                     PRUint32&    generation,
                     void*        userArg)
{
    nsPurpleBuffer *purp = NS_STATIC_CAST(nsPurpleBuffer*, userArg);
    if (SufficientlyAged(generation, purp)) {
        nsISupports *root = NS_STATIC_CAST(nsISupports *, 
                                           NS_CONST_CAST(void*, ptr));
        purp->mTransferBuffer->Push(root);
    }
    return PL_DHASH_NEXT;
}

void
nsPurpleBuffer::SelectAgedPointers(nsDeque *transferBuffer)
{
    mTransferBuffer = transferBuffer;
    mBackingStore.Enumerate(ageSelectionCallback, this);
    mTransferBuffer = nsnull;
}



////////////////////////////////////////////////////////////////////////
// Implement the LanguageRuntime interface for C++/XPCOM 
////////////////////////////////////////////////////////////////////////


struct nsCycleCollectionXPCOMRuntime : 
    public nsCycleCollectionLanguageRuntime 
{
    nsresult BeginCycleCollection() 
    {
        return NS_OK;
    }

    nsresult Traverse(void *p, nsCycleCollectionTraversalCallback &cb) 
    {
        nsresult rv;

        nsISupports *s = NS_STATIC_CAST(nsISupports *, p);        
        nsCycleCollectionParticipant *cp;
        ToParticipant(s, &cp);
        if (!cp) {
            Fault("walking wrong type of pointer", s);
            return NS_ERROR_FAILURE;
        }

        rv = cp->Traverse(s, cb);
        if (NS_FAILED(rv)) {
            Fault("XPCOM pointer traversal failed", s);
            return NS_ERROR_FAILURE;
        }
        return NS_OK;
    }

    nsresult Root(const nsDeque &nodes)
    {
        for (PRInt32 i = 0; i < nodes.GetSize(); ++i) {
            void *p = nodes.ObjectAt(i);
            nsISupports *s = NS_STATIC_CAST(nsISupports *, p);
            NS_ADDREF(s);
        }
        return NS_OK;
    }

    nsresult Unlink(const nsDeque &nodes)
    {
        nsresult rv;

        for (PRInt32 i = 0; i < nodes.GetSize(); ++i) {
            void *p = nodes.ObjectAt(i);

            nsISupports *s = NS_STATIC_CAST(nsISupports *, p);
            nsCycleCollectionParticipant *cp;
            ToParticipant(s, &cp);
            if (!cp) {
                Fault("unlinking wrong kind of pointer", s);
                return NS_ERROR_FAILURE;
            }

            rv = cp->Unlink(s);

            if (NS_FAILED(rv)) {
                Fault("failed unlink", s);
                return NS_ERROR_FAILURE;
            }
        }
        return NS_OK;
    }

    nsresult Unroot(const nsDeque &nodes)
    {
        for (PRInt32 i = 0; i < nodes.GetSize(); ++i) {
            void *p = nodes.ObjectAt(i);
            nsISupports *s = NS_STATIC_CAST(nsISupports *, p);
            NS_RELEASE(s);
        }
        return NS_OK;
    }

    nsresult FinishCycleCollection() 
    {
        return NS_OK;
    }
};


struct nsCycleCollector
{
    PRBool mCollectionInProgress;
    PRBool mScanInProgress;

    GCTable mGraph;
    nsCycleCollectionLanguageRuntime *mRuntimes[nsIProgrammingLanguage::MAX+1];
    nsCycleCollectionXPCOMRuntime mXPCOMRuntime;

    // The set of buffers |mBufs| serves a variety of purposes; mostly
    // involving the transfer of pointers from a hashtable iterator
    // routine to some outer logic that might also need to mutate the
    // hashtable. In some contexts, only buffer 0 is used (as a
    // set-of-all-pointers); in other contexts, one buffer is used
    // per-language (as a set-of-pointers-in-language-N).

    nsDeque mBufs[nsIProgrammingLanguage::MAX + 1];
    
    nsCycleCollectorParams mParams;

    nsPurpleBuffer mPurpleBuf;

    void RegisterRuntime(PRUint32 langID, 
                         nsCycleCollectionLanguageRuntime *rt);
    void ForgetRuntime(PRUint32 langID);

    void CollectPurple(); // XXXldb Should this be called SelectPurple?
    void MarkRoots();
    void ScanRoots();
    void CollectWhite();

    nsCycleCollector();
    ~nsCycleCollector();

    void Suspect(nsISupports *n, PRBool current = PR_FALSE);
    void Forget(nsISupports *n);
    void Allocated(void *n, size_t sz);
    void Freed(void *n);
    void Collect(PRUint32 aTryCollections = 1);
    void Shutdown();

#ifdef DEBUG_CC
    nsCycleCollectorStats mStats;    

    FILE *mPtrLog;

    void MaybeDrawGraphs();
    void ExplainLiveExpectedGarbage();
    void ShouldBeFreed(nsISupports *n);
    void WasFreed(nsISupports *n);
    PointerSet mExpectedGarbage;
#endif
};


class GraphWalker :
    public nsCycleCollectionTraversalCallback
{
private:
    nsDeque mQueue;
    PtrInfo *mCurrPi;

protected:
    GCTable &mGraph;
    nsCycleCollectionLanguageRuntime **mRuntimes;

public:
    GraphWalker(GCTable & tab,
                nsCycleCollectionLanguageRuntime **runtimes) : 
        mQueue(nsnull),
        mCurrPi(nsnull),
        mGraph(tab),
        mRuntimes(runtimes)
    {}

    virtual ~GraphWalker() 
    {}
   
    void Walk(void *s0);

    // nsCycleCollectionTraversalCallback methods.
#ifdef DEBUG_CC
    void DescribeNode(size_t refCount, size_t objSz, const char *objName);
#else
    void DescribeNode(size_t refCount);
#endif
    void NoteXPCOMChild(nsISupports *child);
    void NoteScriptChild(PRUint32 langID, void *child);

    // Provided by concrete walker subtypes.
    virtual PRBool ShouldVisitNode(PtrInfo const *pi) = 0;
    virtual void VisitNode(PtrInfo *pi, size_t refcount) = 0;
    virtual void NoteChild(PtrInfo *childpi) = 0;
};


////////////////////////////////////////////////////////////////////////
// The static collector object
////////////////////////////////////////////////////////////////////////


static nsCycleCollector *sCollector = nsnull;


////////////////////////////////////////////////////////////////////////
// Utility functions
////////////////////////////////////////////////////////////////////////

#ifdef DEBUG_CC

struct safetyCallback :     
    public nsCycleCollectionTraversalCallback
{
    // This is just a dummy interface to feed to children when we're
    // called, to force potential segfaults to happen early, so gdb
    // can give us an informative stack trace. If we don't use it, the
    // collector runs faster but segfaults happen after pointers have
    // been queued and dequeued, at which point their owner is
    // obscure.
    void DescribeNode(size_t refCount, size_t objSz, const char *objName) {}
    void NoteXPCOMChild(nsISupports *child) {}
    void NoteScriptChild(PRUint32 langID, void *child) {}
};

static safetyCallback sSafetyCallback;

#endif

static void
Fault(const char *msg, const void *ptr=nsnull)
{
#ifdef DEBUG_CC
    // This should be nearly impossible, but just in case.
    if (!sCollector)
        return;

    if (sCollector->mParams.mFaultIsFatal) {

        if (ptr)
            printf("Fatal fault in cycle collector: %s (ptr: %p)\n", msg, ptr);
        else
            printf("Fatal fault in cycle collector: %s\n", msg);

        exit(1);

    } 
#endif

    NS_NOTREACHED(nsPrintfCString(256,
                  "Fault in cycle collector: %s (ptr: %p)\n",
                  msg, ptr).get());

    // When faults are not fatal, we assume we're running in a
    // production environment and we therefore want to disable the
    // collector on a fault. This will unfortunately cause the browser
    // to leak pretty fast wherever creates cyclical garbage, but it's
    // probably a better user experience than crashing. Besides, we
    // *should* never hit a fault.

    sCollector->mParams.mDoNothing = PR_TRUE;
}


void 
#ifdef DEBUG_CC
GraphWalker::DescribeNode(size_t refCount, size_t objSz, const char *objName)
#else
GraphWalker::DescribeNode(size_t refCount)
#endif
{
    if (refCount == 0)
        Fault("zero refcount", mCurrPi->key);

#ifdef DEBUG_CC
    mCurrPi->mBytes = objSz;
    mCurrPi->mName = objName;
#endif

    this->VisitNode(mCurrPi, refCount);
#ifdef DEBUG_CC
    sCollector->mStats.mVisitedNode++;
    if (mCurrPi->mLang == nsIProgrammingLanguage::JAVASCRIPT)
        sCollector->mStats.mVisitedJSNode++;
#endif
}


static nsISupports *
canonicalize(nsISupports *in)
{
    nsCOMPtr<nsISupports> child;
    in->QueryInterface(NS_GET_IID(nsCycleCollectionISupports),
                       getter_AddRefs(child));
    return child.get();
}


void 
GraphWalker::NoteXPCOMChild(nsISupports *child) 
{
    if (!child)
        return; 
   
    child = canonicalize(child);

    PRBool scanSafe = nsCycleCollector_isScanSafe(child);
#ifdef DEBUG_CC
    scanSafe &= !nsCycleCollector_shouldSuppress(child);
#endif
    if (scanSafe) {
        PtrInfo *childPi = mGraph.Add(child);
        if (!childPi)
            return;
        this->NoteChild(childPi);
#ifdef DEBUG_CC
        mRuntimes[nsIProgrammingLanguage::CPLUSPLUS]->Traverse(child, sSafetyCallback);
#endif
        mQueue.Push(child);
    }
}


void
GraphWalker::NoteScriptChild(PRUint32 langID, void *child) 
{
    if (!child)
        return;

    if (langID > nsIProgrammingLanguage::MAX || !mRuntimes[langID]) {
        Fault("traversing pointer for unregistered language", child);
        return;
    }

    PtrInfo *childPi = mGraph.Add(child);
    if (!childPi)
        return;
    childPi->mLang = langID;
    this->NoteChild(childPi);
#ifdef DEBUG_CC
    mRuntimes[langID]->Traverse(child, sSafetyCallback);
#endif
    mQueue.Push(child);
}


void
GraphWalker::Walk(void *s0)
{
    mQueue.Empty();
    mQueue.Push(s0);

    while (mQueue.GetSize() > 0) {

        void *ptr = mQueue.Pop();
        mCurrPi = mGraph.Lookup(ptr);

        if (!mCurrPi) {
            Fault("unknown pointer", ptr);
            continue;
        }

#ifdef DEBUG_CC
        if (mCurrPi->mLang > nsIProgrammingLanguage::MAX ) {
            Fault("unknown language during walk");
            continue;
        }

        if (!mRuntimes[mCurrPi->mLang]) {
            Fault("script pointer for unregistered language");
            continue;
        }
#endif
        
        if (this->ShouldVisitNode(mCurrPi)) {
            nsresult rv = mRuntimes[mCurrPi->mLang]->Traverse(ptr, *this);
            if (NS_FAILED(rv)) {
                Fault("script pointer traversal failed", ptr);
            }
        }
    }

#ifdef DEBUG_CC
    sCollector->mStats.mWalkedGraph++;
#endif
}


////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |MarkRoots| routine.
////////////////////////////////////////////////////////////////////////


struct MarkGreyWalker : public GraphWalker
{
    MarkGreyWalker(GCTable &tab,
                   nsCycleCollectionLanguageRuntime **runtimes)
        : GraphWalker(tab, runtimes) 
    {}

    PRBool ShouldVisitNode(PtrInfo const *pi)
    { 
        return pi->mColor != grey;
    }

    void VisitNode(PtrInfo *pi, size_t refcount)
    { 
        pi->mColor = grey;
        pi->mRefCount = refcount;
#ifdef DEBUG_CC
        sCollector->mStats.mSetColorGrey++;
#endif
    }

    void NoteChild(PtrInfo *childpi)
    { 
        childpi->mInternalRefs++;
    }
};

void 
nsCycleCollector::CollectPurple()
{
    mPurpleBuf.SelectAgedPointers(&mBufs[0]);
}

void
nsCycleCollector::MarkRoots()
{
    int i;
    for (i = 0; i < mBufs[0].GetSize(); ++i) {
        nsISupports *s = NS_STATIC_CAST(nsISupports *, mBufs[0].ObjectAt(i));
        s = canonicalize(s);
        mGraph.Add(s);
        MarkGreyWalker(mGraph, mRuntimes).Walk(s);
    }
}


////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |ScanRoots| routine.
////////////////////////////////////////////////////////////////////////


struct ScanBlackWalker : public GraphWalker
{
    ScanBlackWalker(GCTable &tab,
                   nsCycleCollectionLanguageRuntime **runtimes)
        : GraphWalker(tab, runtimes) 
    {}

    PRBool ShouldVisitNode(PtrInfo const *pi)
    { 
        return pi->mColor != black;
    }

    void VisitNode(PtrInfo *pi, size_t refcount)
    { 
        pi->mColor = black;
#ifdef DEBUG_CC
        sCollector->mStats.mSetColorBlack++;
#endif
    }

    void NoteChild(PtrInfo *childpi) {}
};


struct scanWalker : public GraphWalker
{
    scanWalker(GCTable &tab,
               nsCycleCollectionLanguageRuntime **runtimes)
        : GraphWalker(tab, runtimes) 
    {}

    PRBool ShouldVisitNode(PtrInfo const *pi)
    { 
        return pi->mColor == grey;
    }

    void VisitNode(PtrInfo *pi, size_t refcount)
    {
        if (pi->mColor != grey)
            Fault("scanning non-grey node", pi->key);

        if (pi->mInternalRefs > refcount)
            Fault("traversed refs exceed refcount", pi->key);

        if (pi->mInternalRefs == refcount) {
            pi->mColor = white;
#ifdef DEBUG_CC
            sCollector->mStats.mSetColorWhite++;
#endif
        } else {
            ScanBlackWalker(mGraph, mRuntimes).Walk(NS_CONST_CAST(void*,
                                                                  pi->key));
            NS_ASSERTION(pi->mColor == black,
                         "Why didn't ScanBlackWalker make pi black?");
        }
    }
    void NoteChild(PtrInfo *childpi) {}
};


#ifdef DEBUG_CC
PR_STATIC_CALLBACK(PLDHashOperator)
NoGreyCallback(PLDHashTable *table, PLDHashEntryHdr *hdr, PRUint32 number,
               void *arg)
{
    PtrInfo *pinfo = (PtrInfo *) hdr;
    if (pinfo->mColor == grey)
        Fault("valid grey node after scanning", pinfo->key);
    return PL_DHASH_NEXT;
}
#endif


void
nsCycleCollector::ScanRoots()
{
    int i;

    for (i = 0; i < mBufs[0].GetSize(); ++i) {
        nsISupports *s = NS_STATIC_CAST(nsISupports *, mBufs[0].ObjectAt(i));
        s = canonicalize(s);
        scanWalker(mGraph, mRuntimes).Walk(s); 
    }

#ifdef DEBUG_CC
    // Sanity check: scan should have colored all grey nodes black or
    // white. So we ensure we have no grey nodes at this point.
    mGraph.Enumerate(NoGreyCallback, this);
#endif
}


////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |CollectWhite| routine, somewhat modified.
////////////////////////////////////////////////////////////////////////


PR_STATIC_CALLBACK(PLDHashOperator)
FindWhiteCallback(PLDHashTable *table, PLDHashEntryHdr *hdr, PRUint32 number,
                  void *arg)
{
    nsCycleCollector *collector = NS_STATIC_CAST(nsCycleCollector*, arg);
    PtrInfo *pinfo = (PtrInfo *) hdr;
    void *p = NS_CONST_CAST(void*, pinfo->key);

    NS_ASSERTION(pinfo->mLang == nsIProgrammingLanguage::CPLUSPLUS ||
                 !collector->mPurpleBuf.Exists(p),
                 "Need to remove non-CPLUSPLUS objects from purple buffer!");
    if (pinfo->mColor == white) {
        if (pinfo->mLang > nsIProgrammingLanguage::MAX)
            Fault("White node has bad language ID", p);
        else
            collector->mBufs[pinfo->mLang].Push(p);

        if (pinfo->mLang == nsIProgrammingLanguage::CPLUSPLUS) {
            nsISupports* s = NS_STATIC_CAST(nsISupports*, p);
            collector->Forget(s);
        }
    }
    else if (pinfo->mLang == nsIProgrammingLanguage::CPLUSPLUS) {
        nsISupports* s = NS_STATIC_CAST(nsISupports*, p);
        nsCycleCollectionParticipant* cp;
        CallQueryInterface(s, &cp);
        if (cp)
            cp->UnmarkPurple(s);
        collector->Forget(s);
    }
    return PL_DHASH_NEXT;
}


void
nsCycleCollector::CollectWhite()
{
    // Explanation of "somewhat modified": we have no way to collect the
    // set of whites "all at once", we have to ask each of them to drop
    // their outgoing links and assume this will cause the garbage cycle
    // to *mostly* self-destruct (except for the reference we continue
    // to hold). 
    // 
    // To do this "safely" we must make sure that the white nodes we're
    // operating on are stable for the duration of our operation. So we
    // make 3 sets of calls to language runtimes:
    //
    //   - Root(whites), which should pin the whites in memory.
    //   - Unlink(whites), which drops outgoing links on each white.
    //   - Unroot(whites), which returns the whites to normal GC.

    PRUint32 i;
    nsresult rv;

    for (i = 0; i < nsIProgrammingLanguage::MAX+1; ++i)
        mBufs[i].Empty();

#if defined(DEBUG_CC) && !defined(__MINGW32__) && defined(WIN32)
    struct _CrtMemState ms1, ms2;
    _CrtMemCheckpoint(&ms1);
#endif

    mGraph.Enumerate(FindWhiteCallback, this);

    for (i = 0; i < nsIProgrammingLanguage::MAX+1; ++i) {
        if (mRuntimes[i] &&
            mBufs[i].GetSize() > 0) {
            rv = mRuntimes[i]->Root(mBufs[i]);
            if (NS_FAILED(rv))
                Fault("Failed root call while unlinking");
        }
    }

    for (i = 0; i < nsIProgrammingLanguage::MAX+1; ++i) {
        if (mRuntimes[i] &&
            mBufs[i].GetSize() > 0) {
            rv = mRuntimes[i]->Unlink(mBufs[i]);
            if (NS_FAILED(rv)) {
                Fault("Failed unlink call while unlinking");
#ifdef DEBUG_CC
                mStats.mFailedUnlink++;
#endif
            } else {
#ifdef DEBUG_CC
                mStats.mCollectedNode += mBufs[i].GetSize();
#endif
            }
        }
    }

    for (i = 0; i < nsIProgrammingLanguage::MAX+1; ++i) {
        if (mRuntimes[i] &&
            mBufs[i].GetSize() > 0) {
            rv = mRuntimes[i]->Unroot(mBufs[i]);
            if (NS_FAILED(rv))
                Fault("Failed unroot call while unlinking");
        }
    }

    for (i = 0; i < nsIProgrammingLanguage::MAX+1; ++i)
        mBufs[i].Empty();

#if defined(DEBUG_CC) && !defined(__MINGW32__) && defined(WIN32)
    _CrtMemCheckpoint(&ms2);
    if (ms2.lTotalCount < ms1.lTotalCount)
        mStats.mFreedBytes += (ms1.lTotalCount - ms2.lTotalCount);
#endif
}


#ifdef DEBUG_CC
////////////////////////////////////////////////////////////////////////
// Extra book-keeping functions.
////////////////////////////////////////////////////////////////////////

struct graphVizWalker : public GraphWalker
{
    // We can't just use _popen here because graphviz-for-windows
    // doesn't set up its stdin stream properly, sigh.
    PointerSet mVisited;
    const void *mParent;
    FILE *mStream;

    graphVizWalker(GCTable &tab,
                   nsCycleCollectionLanguageRuntime **runtimes)
        : GraphWalker(tab, runtimes), 
          mParent(nsnull), 
          mStream(nsnull)        
    {
#ifdef WIN32
        mStream = fopen("c:\\cycle-graph.dot", "w+");
#else
        mStream = popen("dotty -", "w");
#endif
        mVisited.Init();
        fprintf(mStream, 
                "digraph collection {\n"
                "rankdir=LR\n"
                "node [fontname=fixed, fontsize=10, style=filled, shape=box]\n"
                );
    }

    ~graphVizWalker()
    {
        fprintf(mStream, "\n}\n");
#ifdef WIN32
        fclose(mStream);
        // Even dotty doesn't work terribly well on windows, since
        // they execute lefty asynchronously. So we'll just run 
        // lefty ourselves.
        _spawnlp(_P_WAIT, 
                 "lefty", 
                 "lefty",
                 "-e",
                 "\"load('dotty.lefty');"
                 "dotty.simple('c:\\cycle-graph.dot');\"",
                 NULL);
        unlink("c:\\cycle-graph.dot");
#else
        pclose(mStream);
#endif
    }

    PRBool ShouldVisitNode(PtrInfo const *pi)
    { 
        return ! mVisited.GetEntry(pi->key);
    }

    void VisitNode(PtrInfo *pi, size_t refcount)
    {
        const void *p = pi->key;
        mVisited.PutEntry(p);
        mParent = p;
        fprintf(mStream, 
                "n%p [label=\"%s\\n%p\\n%u/%u refs found\", "
                "fillcolor=%s, fontcolor=%s]\n", 
                p,
                pi->mName,
                p,
                pi->mInternalRefs, pi->mRefCount,
                (pi->mColor == black ? "black" : "white"),
                (pi->mColor == black ? "white" : "black"));
    }

    void NoteChild(PtrInfo *childpi)
    { 
        fprintf(mStream, "n%p -> n%p\n", mParent, childpi->key);
    }
};


////////////////////////////////////////////////////////////////////////
// Memory-hooking stuff
// When debugging wild pointers, it sometimes helps to hook malloc and
// free. This stuff is disabled unless you set an environment variable.
////////////////////////////////////////////////////////////////////////

static PRBool hookedMalloc = PR_FALSE;

#ifdef __GLIBC__
#include <malloc.h>

static void* (*old_memalign_hook)(size_t, size_t, const void *);
static void* (*old_realloc_hook)(void *, size_t, const void *);
static void* (*old_malloc_hook)(size_t, const void *);
static void (*old_free_hook)(void *, const void *);

static void* my_memalign_hook(size_t, size_t, const void *);
static void* my_realloc_hook(void *, size_t, const void *);
static void* my_malloc_hook(size_t, const void *);
static void my_free_hook(void *, const void *);

static inline void 
install_old_hooks()
{
    __memalign_hook = old_memalign_hook;
    __realloc_hook = old_realloc_hook;
    __malloc_hook = old_malloc_hook;
    __free_hook = old_free_hook;
}

static inline void 
save_old_hooks()
{
    // Glibc docs recommend re-saving old hooks on
    // return from recursive calls. Strangely when 
    // we do this, we find ourselves in infinite
    // recursion.

    //     old_memalign_hook = __memalign_hook;
    //     old_realloc_hook = __realloc_hook;
    //     old_malloc_hook = __malloc_hook;
    //     old_free_hook = __free_hook;
}

static inline void 
install_new_hooks()
{
    __memalign_hook = my_memalign_hook;
    __realloc_hook = my_realloc_hook;
    __malloc_hook = my_malloc_hook;
    __free_hook = my_free_hook;
}

static void*
my_realloc_hook(void *ptr, size_t size, const void *caller)
{
    void *result;    

    install_old_hooks();
    result = realloc(ptr, size);
    save_old_hooks();

    if (sCollector) {
        sCollector->Freed(ptr);
        sCollector->Allocated(result, size);
    }

    install_new_hooks();

    return result;
}


static void* 
my_memalign_hook(size_t size, size_t alignment, const void *caller)
{
    void *result;    

    install_old_hooks();
    result = memalign(size, alignment);
    save_old_hooks();

    if (sCollector)
        sCollector->Allocated(result, size);

    install_new_hooks();

    return result;
}


static void 
my_free_hook (void *ptr, const void *caller)
{
    install_old_hooks();
    free(ptr);
    save_old_hooks();

    if (sCollector)
        sCollector->Freed(ptr);

    install_new_hooks();
}      


static void*
my_malloc_hook (size_t size, const void *caller)
{
    void *result;

    install_old_hooks();
    result = malloc (size);
    save_old_hooks();

    if (sCollector)
        sCollector->Allocated(result, size);

    install_new_hooks();

    return result;
}


static void 
InitMemHook(void)
{
    if (!hookedMalloc) {
        save_old_hooks();
        install_new_hooks();
        hookedMalloc = PR_TRUE;        
    }
}

#elif defined(WIN32)
#ifndef __MINGW32__

static int 
AllocHook(int allocType, void *userData, size_t size, int 
          blockType, long requestNumber, const unsigned char *filename, int 
          lineNumber)
{
    if (allocType == _HOOK_FREE)
        sCollector->Freed(userData);
    return 1;
}


static void InitMemHook(void)
{
    if (!hookedMalloc) {
        _CrtSetAllocHook (AllocHook);
        hookedMalloc = PR_TRUE;        
    }
}
#endif // __MINGW32__

#elif 0 // defined(XP_MACOSX)

#include <malloc/malloc.h>

static void (*old_free)(struct _malloc_zone_t *zone, void *ptr);

static void
freehook(struct _malloc_zone_t *zone, void *ptr)
{
    if (sCollector)
        sCollector->Freed(ptr);
    old_free(zone, ptr);
}


static void
InitMemHook(void)
{
    if (!hookedMalloc) {
        malloc_zone_t *default_zone = malloc_default_zone();
        old_free = default_zone->free;
        default_zone->free = freehook;
        hookedMalloc = PR_TRUE;
    }
}


#else

static void
InitMemHook(void)
{
}

#endif // GLIBC / WIN32 / OSX
#endif // DEBUG_CC

////////////////////////////////////////////////////////////////////////
// Collector implementation
////////////////////////////////////////////////////////////////////////

nsCycleCollector::nsCycleCollector() : 
    mCollectionInProgress(PR_FALSE),
    mScanInProgress(PR_FALSE),
#ifdef DEBUG_CC
    mPurpleBuf(mParams, mStats),
    mPtrLog(nsnull)
#else
    mPurpleBuf(mParams)
#endif
{
#ifdef DEBUG_CC
    mExpectedGarbage.Init();
#endif

    memset(mRuntimes, 0, sizeof(mRuntimes));
    mRuntimes[nsIProgrammingLanguage::CPLUSPLUS] = &mXPCOMRuntime;
}


nsCycleCollector::~nsCycleCollector()
{
    mGraph.Clear();    

    for (PRUint32 i = 0; i < nsIProgrammingLanguage::MAX+1; ++i) {
        mRuntimes[i] = NULL;
    }
}


void 
nsCycleCollector::RegisterRuntime(PRUint32 langID, 
                                  nsCycleCollectionLanguageRuntime *rt)
{
    if (mParams.mDoNothing)
        return;

    if (langID > nsIProgrammingLanguage::MAX)
        Fault("unknown language runtime in registration");

    if (mRuntimes[langID])
        Fault("multiple registrations of language runtime", rt);

    mRuntimes[langID] = rt;
}


void 
nsCycleCollector::ForgetRuntime(PRUint32 langID)
{
    if (mParams.mDoNothing)
        return;

    if (langID > nsIProgrammingLanguage::MAX)
        Fault("unknown language runtime in deregistration");

    if (! mRuntimes[langID])
        Fault("forgetting non-registered language runtime");

    mRuntimes[langID] = nsnull;
}


#ifdef DEBUG_CC
static PR_CALLBACK PLDHashOperator
FindAnyWhiteCallback(PLDHashTable *table, PLDHashEntryHdr *hdr, PRUint32 number,
                     void *arg)
{
    PtrInfo *pinfo = (PtrInfo *) hdr;
    if (pinfo->mColor == white) {
        *NS_STATIC_CAST(PRBool*, arg) = PR_TRUE;
        return PL_DHASH_STOP;
    }
    return PL_DHASH_NEXT;
}


void 
nsCycleCollector::MaybeDrawGraphs()
{
    if (mParams.mDrawGraphs) {
        // We draw graphs only if there were any white nodes.
        PRBool anyWhites = PR_FALSE;
        mGraph.Enumerate(FindAnyWhiteCallback, &anyWhites);

        if (anyWhites) {
            graphVizWalker gw(mGraph, mRuntimes);
            while (mBufs[0].GetSize() > 0) {
                nsISupports *s = NS_STATIC_CAST(nsISupports *, mBufs[0].Pop());
                s = canonicalize(s);
                gw.Walk(s);
            }
        }
    }
}

class Suppressor :
    public nsCycleCollectionTraversalCallback
{
protected:
    static char *sSuppressionList;
    static PRBool sInitialized;
    PRBool mSuppressThisNode;
public:
    Suppressor()
    {
    }

    PRBool shouldSuppress(nsISupports *s)
    {
        if (!sInitialized) {
            sSuppressionList = PR_GetEnv("XPCOM_CC_SUPPRESS");
            sInitialized = PR_TRUE;
        }
        if (sSuppressionList == nsnull) {
            mSuppressThisNode = PR_FALSE;
        } else {
            nsresult rv;
            nsCOMPtr<nsCycleCollectionParticipant> cp = do_QueryInterface(s, &rv);
            if (NS_FAILED(rv)) {
                Fault("checking suppression on wrong type of pointer", s);
                return PR_TRUE;
            }
            cp->Traverse(s, *this);
        }
        return mSuppressThisNode;
    }

    void DescribeNode(size_t refCount, size_t objSz, const char *objName)
    {
        mSuppressThisNode = (PL_strstr(sSuppressionList, objName) != nsnull);
    }

    void NoteXPCOMChild(nsISupports *child) {}
    void NoteScriptChild(PRUint32 langID, void *child) {}
};

char *Suppressor::sSuppressionList = nsnull;
PRBool Suppressor::sInitialized = PR_FALSE;

static PRBool
nsCycleCollector_shouldSuppress(nsISupports *s)
{
    Suppressor supp;
    return supp.shouldSuppress(s);
}
#endif

void 
nsCycleCollector::Suspect(nsISupports *n, PRBool current)
{
    // Re-entering ::Suspect during collection used to be a fault, but
    // we are canonicalizing nsISupports pointers using QI, so we will
    // see some spurious refcount traffic here. 

    if (mScanInProgress)
        return;

    NS_ASSERTION(nsCycleCollector_isScanSafe(n),
                 "suspected a non-scansafe pointer");
    NS_ASSERTION(NS_IsMainThread(), "trying to suspect from non-main thread");

    if (mParams.mDoNothing)
        return;

#ifdef DEBUG_CC
    mStats.mSuspectNode++;

    if (nsCycleCollector_shouldSuppress(n))
        return;

#ifndef __MINGW32__
    if (mParams.mHookMalloc)
        InitMemHook();
#endif

    if (mParams.mLogPointers) {
        if (!mPtrLog)
            mPtrLog = fopen("pointer_log", "w");
        fprintf(mPtrLog, "S %p\n", NS_STATIC_CAST(void*, n));
    }
#endif

    if (current)
        mBufs[0].Push(n);
    else
        mPurpleBuf.Put(n);
}


void 
nsCycleCollector::Forget(nsISupports *n)
{
    // Re-entering ::Forget during collection used to be a fault, but
    // we are canonicalizing nsISupports pointers using QI, so we will
    // see some spurious refcount traffic here. 

    if (mScanInProgress)
        return;

    NS_ASSERTION(NS_IsMainThread(), "trying to forget from non-main thread");
    
    if (mParams.mDoNothing)
        return;

#ifdef DEBUG_CC
    mStats.mForgetNode++;

#ifndef __MINGW32__
    if (mParams.mHookMalloc)
        InitMemHook();
#endif

    if (mParams.mLogPointers) {
        if (!mPtrLog)
            mPtrLog = fopen("pointer_log", "w");
        fprintf(mPtrLog, "F %p\n", NS_STATIC_CAST(void*, n));
    }
#endif

    mPurpleBuf.Remove(n);
}

#ifdef DEBUG_CC
void 
nsCycleCollector::Allocated(void *n, size_t sz)
{
}

void 
nsCycleCollector::Freed(void *n)
{
    mStats.mFreeCalls++;

    if (!n) {
        // Ignore null pointers coming through
        return;
    }

    if (mPurpleBuf.Exists(n)) {
        mStats.mForgetNode++;
        mStats.mFreedWhilePurple++;
        Fault("freed while purple", n);
        mPurpleBuf.Remove(n);
        
        if (mParams.mLogPointers) {
            if (!mPtrLog)
                mPtrLog = fopen("pointer_log", "w");
            fprintf(mPtrLog, "R %p\n", n);
        }
    }
}
#endif

void
nsCycleCollector::Collect(PRUint32 aTryCollections)
{
#if defined(DEBUG_CC) && !defined(__MINGW32__)
    if (!mParams.mDoNothing && mParams.mHookMalloc)
        InitMemHook();
#endif

#ifdef COLLECT_TIME_DEBUG
    printf("cc: Starting nsCycleCollector::Collect(%d)\n", aTryCollections);
    PRTime start = PR_Now(), now;
#endif

    while (aTryCollections > 0) {
        // This triggers a JS GC. Our caller assumes we always trigger at
        // least one JS GC -- they rely on this fact to avoid redundant JS
        // GC calls -- so it's essential that we actually execute this
        // step!
        //
        // It is also essential to empty mBufs[0] here because starting up
        // collection in language runtimes may force some "current" suspects
        // into mBufs[0].
        mBufs[0].Empty();

#ifdef COLLECT_TIME_DEBUG
        now = PR_Now();
#endif
        for (PRUint32 i = 0; i <= nsIProgrammingLanguage::MAX; ++i) {
            if (mRuntimes[i])
                mRuntimes[i]->BeginCycleCollection();
        }

#ifdef COLLECT_TIME_DEBUG
        printf("cc: mRuntimes[*]->BeginCycleCollection() took %lldms\n",
               (PR_Now() - now) / PR_USEC_PER_MSEC);
#endif

        if (mParams.mDoNothing) {
            aTryCollections = 0;
        } else {
#ifdef COLLECT_TIME_DEBUG
            now = PR_Now();
#endif

            CollectPurple();

#ifdef COLLECT_TIME_DEBUG
            printf("cc: CollectPurple() took %lldms\n",
                   (PR_Now() - now) / PR_USEC_PER_MSEC);
#endif

            if (mBufs[0].GetSize() == 0) {
                aTryCollections = 0;
            } else {
                if (mCollectionInProgress)
                    Fault("re-entered collection");

                mCollectionInProgress = PR_TRUE;

                mScanInProgress = PR_TRUE;

                mGraph.Clear();

                // The main Bacon & Rajan collection algorithm.

#ifdef COLLECT_TIME_DEBUG
                now = PR_Now();
#endif
                MarkRoots();

#ifdef COLLECT_TIME_DEBUG
                {
                    PRTime then = PR_Now();
                    printf("cc: MarkRoots() took %lldms\n",
                           (then - now) / PR_USEC_PER_MSEC);
                    now = then;
                }
#endif

                ScanRoots();

#ifdef COLLECT_TIME_DEBUG
                printf("cc: ScanRoots() took %lldms\n",
                       (PR_Now() - now) / PR_USEC_PER_MSEC);
#endif

#ifdef DEBUG_CC
                MaybeDrawGraphs();
#endif

                mScanInProgress = PR_FALSE;


#ifdef COLLECT_TIME_DEBUG
                now = PR_Now();
#endif
                CollectWhite();

#ifdef COLLECT_TIME_DEBUG
                printf("cc: CollectWhite() took %lldms\n",
                       (PR_Now() - now) / PR_USEC_PER_MSEC);
#endif

                // Some additional book-keeping.

                mGraph.Clear();

                --aTryCollections;
            }

            mPurpleBuf.BumpGeneration();

#ifdef DEBUG_CC
            mStats.mCollection++;
            if (mParams.mReportStats)
                mStats.Dump();
#endif

            mCollectionInProgress = PR_FALSE;
        }

        for (PRUint32 i = 0; i <= nsIProgrammingLanguage::MAX; ++i) {
            if (mRuntimes[i])
                mRuntimes[i]->FinishCycleCollection();
        }
    }

#ifdef COLLECT_TIME_DEBUG
    printf("cc: Collect() took %lldms\n",
           (PR_Now() - start) / PR_USEC_PER_MSEC);
#endif
}

void
nsCycleCollector::Shutdown()
{
    // Here we want to run a final collection on everything we've seen
    // buffered, irrespective of age; then permanently disable
    // the collector because the program is shutting down.

    mPurpleBuf.BumpGeneration();
    mParams.mScanDelay = 0;
    Collect(SHUTDOWN_COLLECTIONS(mParams));

#ifdef DEBUG_CC
    CollectPurple();
    if (mBufs[0].GetSize() != 0) {
        printf("Might have been able to release more cycles if the cycle collector would "
               "run once more at shutdown.\n");
    }

    ExplainLiveExpectedGarbage();
#endif
    mParams.mDoNothing = PR_TRUE;
}

#ifdef DEBUG_CC

PR_STATIC_CALLBACK(PLDHashOperator)
AddExpectedGarbage(nsVoidPtrHashKey *p, void *arg)
{
    nsCycleCollector *c = NS_STATIC_CAST(nsCycleCollector*, arg);
    c->mBufs[0].Push(NS_CONST_CAST(void*, p->GetKey()));
    return PL_DHASH_NEXT;
}

struct explainWalker : public GraphWalker
{
    explainWalker(GCTable &tab,
                  nsCycleCollectionLanguageRuntime **runtimes)
        : GraphWalker(tab, runtimes) 
    {}

    PRBool ShouldVisitNode(PtrInfo const *pi)
    { 
        // We set them back to gray as we explain problems.
        return pi->mColor != grey;
    }

    void VisitNode(PtrInfo *pi, size_t refcount)
    {
        if (pi->mColor == grey)
            Fault("scanning grey node", pi->key);

        if (pi->mColor == white) {
            printf("nsCycleCollector: %s %p was not collected due to\n"
                   "  missing call to suspect or failure to unlink\n",
                   pi->mName, pi->key);
        }

        if (pi->mInternalRefs != refcount) {
            // Note that the external references may have been external
            // to a different node in the cycle collection that just
            // happened, if that different node was purple and then
            // black.
            printf("nsCycleCollector: %s %p was not collected due to %d\n"
                   "  external references\n",
                   pi->mName, pi->key, refcount - pi->mInternalRefs);
        }

        pi->mColor = grey;
    }
    void NoteChild(PtrInfo *childpi) {}
};

void
nsCycleCollector::ExplainLiveExpectedGarbage()
{
    if (mScanInProgress || mCollectionInProgress)
        Fault("can't explain expected garbage during collection itself");

    if (mParams.mDoNothing) {
        printf("nsCycleCollector: not explaining expected garbage since\n"
               "  cycle collection disabled\n");
        return;
    }

    for (PRUint32 i = 0; i <= nsIProgrammingLanguage::MAX; ++i) {
        if (mRuntimes[i])
            mRuntimes[i]->BeginCycleCollection();
    }

    mCollectionInProgress = PR_TRUE;
    mScanInProgress = PR_TRUE;

    mGraph.Clear();
    mBufs[0].Empty();

    // Instead of filling mBufs[0] from the purple buffer, we fill it
    // from the list of nodes we were expected to collect.
    mExpectedGarbage.EnumerateEntries(&AddExpectedGarbage, this);

    MarkRoots();
    ScanRoots();

    mScanInProgress = PR_FALSE;

    for (int i = 0; i < mBufs[0].GetSize(); ++i) {
        nsISupports *s = NS_STATIC_CAST(nsISupports *, mBufs[0].ObjectAt(i));
        s = canonicalize(s);
        explainWalker(mGraph, mRuntimes).Walk(s); 
    }

    mGraph.Clear();

    mCollectionInProgress = PR_FALSE;

    for (PRUint32 i = 0; i <= nsIProgrammingLanguage::MAX; ++i) {
        if (mRuntimes[i])
            mRuntimes[i]->FinishCycleCollection();
    }    
}

void
nsCycleCollector::ShouldBeFreed(nsISupports *n)
{
    mExpectedGarbage.PutEntry(n);
}

void
nsCycleCollector::WasFreed(nsISupports *n)
{
    mExpectedGarbage.RemoveEntry(n);
}
#endif

////////////////////////////////////////////////////////////////////////
// Module public API (exported in nsCycleCollector.h)
// Just functions that redirect into the singleton, once it's built.
////////////////////////////////////////////////////////////////////////

void 
nsCycleCollector_registerRuntime(PRUint32 langID, 
                                 nsCycleCollectionLanguageRuntime *rt)
{
    if (sCollector)
        sCollector->RegisterRuntime(langID, rt);
}


void 
nsCycleCollector_forgetRuntime(PRUint32 langID)
{
    if (sCollector)
        sCollector->ForgetRuntime(langID);
}


void 
nsCycleCollector_suspect(nsISupports *n)
{
    if (sCollector)
        sCollector->Suspect(n);
}


void 
nsCycleCollector_suspectCurrent(nsISupports *n)
{
    if (sCollector)
        sCollector->Suspect(n, PR_TRUE);
}


void 
nsCycleCollector_forget(nsISupports *n)
{
    if (sCollector)
        sCollector->Forget(n);
}


void 
nsCycleCollector_collect()
{
    if (sCollector)
        sCollector->Collect();
}

nsresult 
nsCycleCollector_startup()
{
    NS_ASSERTION(!sCollector, "Forgot to call nsCycleCollector_shutdown?");

    sCollector = new nsCycleCollector();
    return sCollector ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

void 
nsCycleCollector_shutdown()
{
    if (sCollector) {
        sCollector->Shutdown();
        delete sCollector;
        sCollector = nsnull;
    }
}

#ifdef DEBUG
void
nsCycleCollector_DEBUG_shouldBeFreed(nsISupports *n)
{
#ifdef DEBUG_CC
    if (sCollector)
        sCollector->ShouldBeFreed(n);
#endif
}

void
nsCycleCollector_DEBUG_wasFreed(nsISupports *n)
{
#ifdef DEBUG_CC
    if (sCollector)
        sCollector->WasFreed(n);
#endif
}
#endif

PRBool
nsCycleCollector_isScanSafe(nsISupports *s)
{
    if (!s)
        return PR_FALSE;

    nsCycleCollectionParticipant *cp;
    ToParticipant(s, &cp);

    return cp != nsnull;
}

static void
ToParticipant(nsISupports *s, nsCycleCollectionParticipant **cp)
{
    // We use QI to move from an nsISupports to an
    // nsCycleCollectionParticipant, which is a per-class singleton helper
    // object that implements traversal and unlinking logic for the nsISupports
    // in question.
    CallQueryInterface(s, cp);
#ifdef DEBUG_CC
    if (cp)
        ++sCollector->mStats.mSuccessfulQI;
    else
        ++sCollector->mStats.mFailedQI;
#endif
}
