.set  r1,  1
.set  r2,  2
.set  r3,  3
.set r12, 12
.set r31, 31

.text
.global gadget
gadget:
	addis	r2,r12,(.TOC.-gadget)@ha
	addi	r2,r2,(.TOC.-gadget)@l
.localentry gadget, .-gadget

	/* Load &saved_lr */
	addis	r3,r2,saved_lr@got@ha
	ld	r3,saved_lr@got@l(r3)
	/* Load *saved_lr */
	ld	r3,0(r3)
	/* Restore the LR to get us back to 'main' */
	mtlr	r3
	/* R3 becomes the return value from 'main' */
	li	r3,666
	/* XXX 'main' expects SP in R31 */
	mr	r31,r1
	blr

.data
.align 8
.global saved_lr
saved_lr:
	.long 0
