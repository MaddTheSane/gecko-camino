#!/usr/bin/python
# Script for generating the assembler code requried for virtual function
# stubs for SuperH processor in little-endian mode (perhaps it works big-endian
# too, I haven't checked). 

f = open("xptcstubs_asm_shle.s", 'w')

prefix = "_ZN14nsXPTCStubBase"
minStub = 3
maxStub = 250

# Frequency with which we store the address of the function to branch to
# If too high, we'll get errors from the assembler.
jumpRepeat = 20
jumpCount = 0
labelIndex = 2

f.write("""
	/* Automatically generated by xptcstubs_asm_shle.py */
	.text
	.little
	.section	.rodata

	.globl SharedStub
	.type  SharedStub, @function
SharedStub:
	mov	   r15, r1
	mov.l	r14,@-r15	
	sts.l	pr,@-r15
	mov		r15, r14

	/* Some parameters might have been passed in registers, so push them
	 * all onto the stack, PrepareAndDispatch can then work out whats what
	 * given method type information.
	 */
	mov.l r7, @-r15
	mov.l r6, @-r15
	mov.l r5, @-r15
	mov	  r15, r7		/* r7 = PrepareAndDispatch intRegParams param	*/

	fmov.s fr10, @-r15
	fmov.s fr11, @-r15
	fmov.s fr8, @-r15
	fmov.s fr9, @-r15
	fmov.s fr6, @-r15
	fmov.s fr7, @-r15
	fmov.s fr4, @-r15
	fmov.s fr5, @-r15
	mov.l  r15, @-r15	/* PrepareAndDispatch floatRegParams param		*/

	mov	   r1, r6		/* r6 = PrepareAndDispatch data param			*/

	mov.l  .L1, r1
	jsr	   @r1			/* Note, following instruction is executed first*/
	mov	   r2, r5		/* r5 = PrepareAndDispatch methodIndex param	*/

	mov		r14,r15
	lds.l	@r15+,pr
	mov.l	@r15+,r14
	rts
	nop
	.align 2
.L1:
	.long  PrepareAndDispatch

	/* Stubs.  Each stub simply saves the method number in r1 and jumps
	 * to SharedStub which does all the processing common to all stubs.
	 */
""")

for i in range(minStub,maxStub):
	jumpCount = jumpCount + 1
	if  jumpCount == jumpRepeat:
		f.write( '\t.align 2\n')
		f.write( '.L' + str(labelIndex) + ':\n')
		f.write( '\t.long\tSharedStub\n\n')
		jumpCount = 0
		labelIndex = labelIndex + 1
	funcName = 'Stub' + str(i)
	name = prefix + str(len(funcName)) + funcName +  'Ev'
	f.write( '\t.globl ' + name + '\n')
	f.write( '\t.type ' + name + '  @function\n')
	f.write( '\t.align 1\n')
	f.write( name + ':\n')
	f.write( '\tmov.l\t.L' + str(labelIndex) + ', r1\n')
	f.write( '\tjmp\t@r1\n')
	f.write( '\tmov\t#'  + str(i) + ', r2		/* Executed before jmp */\n\n')

f.write( '\t.align 2\n')
f.write( '.L' + str(labelIndex) + ':\n')
f.write( '\t.long\tSharedStub\n')
