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
 * The Original Code is [Open Source Virtual Machine].
 *
 * The Initial Developer of the Original Code is
 * Adobe System Incorporated.
 * Portions created by the Initial Developer are Copyright (C) 2004-2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Adobe AS3 Team
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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


#ifndef __nanojit_Assembler__
#define __nanojit_Assembler__


namespace avmplus { class InterpState; }

namespace nanojit
{
	/**
	 * Some notes on this Assembler (Emitter).
	 * 
	 *     LIR_call is a generic call operation that is encoded using form [2].  The 24bit
	 *     integer is used as an index into a function look-up table that contains information
	 *     about the target that is to be called; including address, # parameters, etc.
	 * 
	 *   The class RegAlloc is essentially the register allocator from MIR
	 * 
	 *   The Assembler class parses the LIR instructions starting at any point and converts 
	 *   them to machine code.  It does the translation using expression trees which are simply
	 *   LIR instructions in the stream that have side-effects.  Any other instruction in the 
	 *   stream is simply ignored.  
	 *   This approach is interesting in that dead code elimination occurs for 'free', strength
	 *   reduction occurs fairly naturally, along with some other optimizations.
	 * 
	 *   A negative is that we require state as we 'push' and 'pop' nodes along the tree.  
	 *   Also, this is most easily performed using recursion which may not be desirable in 
	 *   the mobile environment. 
	 *   
	 */

	/**
	 * These objects lie directly in the native code pages and are used to house 
	 * state information across the edge of side exits from a fragment.  
	 */
	struct GuardRecord
	{
		GuardRecord* next;			/* link to next record in chain for branches that target the same fragment */
        Fragment *   from;
        Fragment *   target;
		NIns*		 jmp;
		NIns*        origTarget;
		int32_t		 calldepth;
		SideExit*	 exit;
		GuardRecord* outgoing;			/* list of guards in a fragment */
		verbose_only( uint32_t gid; )
		verbose_only( uint32_t compileNbr; )
	};

	/**
	 * The Assembler is only concerned with transforming LIR to native instructions
	 */
    struct Reservation
	{
		uint32_t arIndex:16;	/* index into stack frame.  displ is -4*arIndex */
		Register reg:8;			/* register UnkownReg implies not in register */
        int cost:8;
	};

	struct AR
	{
		LIns*			entry[ NJ_MAX_STACK_ENTRY ];	/* maps to 4B contiguous locations relative to the frame pointer */
		uint32_t		tos;							/* current top of stack entry */
		uint32_t		highwatermark;					/* max tos hit */
		uint32_t		lowwatermark;					/* we pre-allocate entries from 0 upto this index-1; so dynamic entries are added above this index */
		LIns*			parameter[ NJ_MAX_PARAMETERS ]; /* incoming parameters */
	};

	const uint32_t ARGSIZE_NONE = 0;
	const uint32_t ARGSIZE_F = 1;
	const uint32_t ARGSIZE_LO = 2;
	const uint32_t ARGSIZE_Q = 3;
	const uint32_t _ARGSIZE_MASK_INT = 2;
	const uint32_t _ARGSIZE_MASK_ANY = 3;

	struct CallInfo
	{
		intptr_t	_address;
		uint16_t	_argtypes;		// 6 2-bit fields indicating arg type, by ARGSIZE above (including ret type): a1 a2 a3 a4 a5 ret
		uint8_t		_cse;			// true if no side effects
		uint8_t		_fold;			// true if no side effects
		verbose_only ( const char* _name; )
		
		uint32_t FASTCALL _count_args(uint32_t mask) const;

		inline uint32_t FASTCALL count_args() const { return _count_args(_ARGSIZE_MASK_ANY); }
		inline uint32_t FASTCALL count_iargs() const { return _count_args(_ARGSIZE_MASK_INT); }
		// fargs = args - iargs
	};

	#define FUNCTIONID(name) CI_avmplus_##name

	#define INTERP_FOPCODE_LIST_BEGIN											enum FunctionID {
	#define INTERP_FOPCODE_LIST_ENTRY_PRIM(nm)									
	#define INTERP_FOPCODE_LIST_ENTRY_FUNCPRIM(nm,argtypes,cse,fold,ret,args)	FUNCTIONID(nm),
	#define INTERP_FOPCODE_LIST_ENTRY_SUPER(nm,off)								
	#define INTERP_FOPCODE_LIST_ENTRY_EXTERN(nm,off)							
	#define INTERP_FOPCODE_LIST_ENTRY_LITC(nm,i)								
	#define INTERP_FOPCODE_LIST_END												CI_Max } ;
	#include "vm_fops.h"
	#undef INTERP_FOPCODE_LIST_BEGIN
	#undef INTERP_FOPCODE_LIST_ENTRY_PRIM
	#undef INTERP_FOPCODE_LIST_ENTRY_FUNCPRIM
	#undef INTERP_FOPCODE_LIST_ENTRY_SUPER
	#undef INTERP_FOPCODE_LIST_ENTRY_EXTERN
	#undef INTERP_FOPCODE_LIST_ENTRY_LITC
	#undef INTERP_FOPCODE_LIST_END

	struct Stats
	{
		counter_define(steals;)
		counter_define(remats;)
		counter_define(spills;)
		counter_define(native;)
        counter_define(exitnative;)

		DECLARE_PLATFORM_STATS()
#ifdef __GNUC__
		// inexplicably, gnuc gives padding/alignment warnings without this. pacify it.
		bool pad[4];
#endif
	};

	class Fragmento;
	
	// error codes
	enum AssmError
	{
		 None = 0
		,OutOMem
		,StackFull
		,ResvFull
		,RegionFull
        ,MaxLength
        ,MaxExit
        ,MaxXJump
        ,UnknownPrim
	};
	 
    /**
 	 * Information about the activation record for the method is built up 
 	 * as we generate machine code.  As part of the prologue, we issue
	 * a stack adjustment instruction and then later patch the adjustment
	 * value.  Temporary values can be placed into the AR as method calls
	 * are issued.   Also MIR_alloc instructions will consume space.
	 */
	class Assembler
	{
		friend class DeadCodeFilter;
		friend class VerboseBlockReader;
		public:
			#ifdef NJ_VERBOSE
			static char  outline[8192]; 
			static char* outputAlign(char* s, int col); 

			void FASTCALL output(const char* s); 
			void FASTCALL outputf(const char* format, ...); 
			void FASTCALL output_asm(const char* s); 
			
			bool _verbose, vpad[3];
			void printActivationState();

			StringList* _outputCache;
			#endif

			Assembler(Fragmento* frago);
            ~Assembler() {}

			LInsp		begin(LirWriter *writer);	// @todo remove this 

			NIns*		assemble(Fragment* frag);
			NIns*		endAssembly(Fragment* frag, NInsList& loopJumps);
			NIns*		beginAssembly(RegAllocMap* map);
			void		copyRegisters(RegAlloc* copyTo);
			void		releaseRegisters();
            void        patch(GuardRecord *lr);
			void		unpatch(GuardRecord *lr);
			AssmError   error()	{ return _err; }
			void		setError(AssmError e) { _err = e; }
			void		setCallTable(const CallInfo *functions);
			void		pageReset();
			Page*		handoverPages(bool exitPages=false);

			debug_only ( void		pageValidate(); )
			debug_only ( bool		onPage(NIns* where, bool exitPages=false); )
			
			// support calling out from a fragment ; used to debug the jit
			debug_only( void		resourceConsistencyCheck(); )
			debug_only( void		registerConsistencyCheck(LIns** resv); )
			
			Stats		_stats;		

			const CallInfo* callInfoFor(int32_t fid);
			const CallInfo* callInfoFor(LInsp call)
			{
				return callInfoFor(call->imm8());
			}

		private:
			
			NIns*		gen(LirFilter* toCompile);
			NIns*		genPrologue(RegisterMask);
			NIns*		genEpilogue(RegisterMask);

			bool		ignoreInstruction(LInsp ins);

			GuardRecord* placeGuardRecord(SideExit *exit);

			uint32_t	arReserve(LIns* l);
			uint32_t	arFree(uint32_t idx);
			void		arReset();

			Register	registerAlloc(RegisterMask allow);
			void		registerResetAll();
			void		restoreCallerSaved();
			void		mergeRegisterState(RegAlloc& saved);
	        LInsp       findVictim(RegAlloc& regs, RegisterMask allow, RegisterMask prefer);
		
			int			findMemFor(LIns* i);
			Register	findRegFor(LIns* i, RegisterMask allow);
			void		findRegFor2(RegisterMask allow, LIns* ia, Reservation* &ra, LIns *ib, Reservation* &rb);
			Register	findSpecificRegFor(LIns* i, Register w);
			Register	prepResultReg(LIns *i, RegisterMask allow);
			void		freeRsrcOf(LIns *i, bool pop);
			void		evict(Register r);
			RegisterMask hint(LIns*i, RegisterMask allow);

			NIns*		pageAlloc(bool exitPage=false);
			void		pagesFree(Page*& list);
			void		internalReset();

			Reservation* reserveAlloc(LInsp i);
			void		reserveFree(LInsp i);
			void		reserveReset();

			Reservation* getresv(LIns *x) { return x->resv() ? &_resvTable[x->resv()] : 0; }

			DWB(Fragmento*)		_frago;
            GC*					_gc;
            DWB(Fragment*)		_thisfrag;
			RegAllocMap*		_branchStateMap;
			GuardRecord*		_latestGuard;
		
			const CallInfo	*_functions;
			
			NIns*		_nIns;			// current native instruction
			NIns*		_nExitIns;		// current instruction in exit fragment page
			NIns*       _epilogue;
			Page*		_nativePages;	// list of NJ_PAGE_SIZE pages that have been alloc'd
			Page*		_nativeExitPages; // list of pages that have been allocated for exit code
			AssmError	_err;			// 0 = means assemble() appears ok, otherwise it failed

			AR			_activation;
			RegAlloc	_allocator;

			Reservation _resvTable[ NJ_MAX_STACK_ENTRY ]; // table where we house stack and register information
			uint32_t	_resvFree;
			verbose_only( uint32_t gid;)
			bool		_inExit,vpad2[3];

			void		asm_cmp(LIns *cond);
#ifndef NJ_SOFTFLOAT
			void		asm_fcmp(LIns *cond);
#endif
			void		asm_mmq(Register rd, int dd, Register rs, int ds);
            NIns*       asm_exit(SideExit *exit);
			NIns*		asm_leave_trace(SideExit* exit);
            void        asm_qjoin(LIns *ins);
            void        asm_store32(LIns *val, int d, LIns *base);
            void        asm_store64(LIns *val, int d, LIns *base);
			void		asm_restore(LInsp, Reservation*, Register);
			void		asm_spill(LInsp i, Reservation *resv, bool pop);
			void		asm_load64(LInsp i);
			void		asm_pusharg(LInsp p);
			NIns*		asm_adjustBranch(NIns* at, NIns* target);

			// platform specific implementation (see NativeXXX.cpp file)
			void		nInit(uint32_t flags);
			void		nInit(AvmCore *);
			Register	nRegisterAllocFromSet(int32_t set);
			void		nRegisterResetAll(RegAlloc& a);
			void		nMarkExecute(Page* page, int32_t count=1, bool enable=true);
			void		nPostCallCleanup(const CallInfo* call);
			void		nArgEmitted(const CallInfo* call, uint32_t stackSlotCount, uint32_t iargs, uint32_t fargs);
			void		nFrameRestore(RegisterMask rmask);
			static void	nPatchBranch(NIns* branch, NIns* location);
			GuardRecord *nFragExit(SideExit *exit);

			// platform specific methods
        public:
			DECLARE_PLATFORM_ASSEMBLER()

		private:
			debug_only( int32_t	_fpuStkDepth; )
			debug_only( int32_t	_sv_fpuStkDepth; )

			// since we generate backwards the depth is negative
			inline void fpu_push() {
				debug_only( ++_fpuStkDepth; /*char foo[8]= "FPUSTK0"; foo[6]-=_fpuStkDepth; output_asm(foo);*/ NanoAssert(_fpuStkDepth<=0); )
			} 
			inline void fpu_pop() { 
				debug_only( --_fpuStkDepth; /*char foo[8]= "FPUSTK0"; foo[6]-=_fpuStkDepth; output_asm(foo);*/ NanoAssert(_fpuStkDepth<=0); )
			}
	};

	inline int32_t disp(Reservation* r) 
	{
		return stack_direction(4) * int32_t(r->arIndex) + NJ_STACK_OFFSET; 
	}
}
#endif // __nanojit_Assembler__
