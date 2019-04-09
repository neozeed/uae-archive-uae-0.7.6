	SECTION code
; This file can be translated with A68k or PhxAss and then linked with BLink
; to produce an Amiga executable. Make sure it does not contain any
; relocations, then run it through the filesys.sh script

	dc.l 16
our_seglist:
	dc.l 0 ;/* NextSeg */
start:
	bra filesys_mainloop
	dc.l make_dev-start
	dc.l filesys_init-start
	dc.l exter_server-start
	dc.l bootcode-start

bootcode:
	lea.l doslibname(pc),a1
	jsr.l -96(a6) ; FindResident
	move.l d0,a0
	move.l 22(a0),d0
	move.l d0,a0
	jsr (a0)
	rts

exter_data:
exter_server:
	movem.l a2,-(sp)
	moveq.l #0,d0
	jsr.l $F0FF50
	tst.l d0
	beq.b exter_server_exit
	; This is the hard part - we have to send some messages.
	move.l 4.w,a6
EXTS_loop:
	moveq.l #2,d0
	jsr.l $F0FF50
	tst.l d0
	beq.b EXTS_done
	move.l d0,a2
	moveq.l #3,d0
	jsr.l $F0FF50
	move.l a2,a0
	move.l d0,a1
	jsr -366(a6) ; PutMsg
	bra.b EXTS_loop

EXTS_done:
	moveq.l #4,d0
	jsr.l $F0FF50
	moveq.l #1,d0 ; clear Z - it was for us.
exter_server_exit:
	movem.l (sp)+,a2
	rts

filesys_init:
	movem.l d0-d7/a0-a6,-(sp)
	move.l 4.w,a6
	move.l $F0FFFC,a5 ; filesys base
	lea.l explibname(pc),a1 ; expansion lib name
	moveq.l #36,d0
	moveq.l #0,d5
	jsr.l  -552(a6) ; OpenLibrary
	tst.l d0
	bne.b FSIN_explibok
	lea.l explibname(pc),a1 ; expansion lib name
	moveq.l #0,d0
	moveq.l #1,d5
	jsr.l  -552(a6) ; OpenLibrary
FSIN_explibok:
	move.l d0,a4
	moveq.l #88,d0
	moveq.l #1,d1 ; MEMF_PUBLIC
	jsr.l -198(a6) ; AllocMem
	move.l d0,a3  ; param packet

	moveq.l #84,d7 ; initialize it
FSIN_loop:
	move.l 0(a5,d7.l),0(a3,d7.l)
	subq.l #4,d7
	bcc.b FSIN_loop

	moveq.l #0,d6
FSIN_init_units:
	cmp.l  $10c(a5),d6
	bcc.b FSIN_units_ok
	move.l a3,a0
	movem.l d6/a3,-(sp)
	move.l  #1,d7
	bsr.b   make_dev
	movem.l (sp)+,d6/a3
	addq.l #$1,d6
	bra.b  FSIN_init_units

FSIN_units_ok:
	move.l 4.w,a6
	move.l a4,a1
	jsr.l -414(a6) ; CloseLibrary
	bsr.b setup_exter
	move.l 4.w,a6
	jsr.l $F0FF80
	moveq.l #3,d1
	moveq.l #-10,d2
	move.l #$200000,a0
	sub.l a0,d0
	bcs.b FSIN_chip_done
	beq.b FSIN_chip_done
	moveq.l #0,d4
	move.l d4,a1
	jsr -618(a6)
FSIN_chip_done
	movem.l (sp)+,d0-d7/a0-a6
general_ret:
	rts

setup_exter:
	move.l 4.w,a6
	moveq.l #26,d0
	move.l #$10001,d1
	jsr -198(a6) ; AllocMem
	move.l d0,a1
	lea.l exter_name(pc),a0
	move.l a0,10(a1)
	lea.l exter_data(pc),a0
	move.l a0,14(a1)
	lea.l exter_server(pc),a0
	move.l a0,18(a1)
	moveq.l #13,d0
	jmp.l -168(a6) ; AddIntServer

make_dev: ; IN: A0 param_packet, D6: unit_no, D7: boot, A4: expansionbase
	move.l $F0FFFC,a5 ; filesys base

	jsr $F0FF28 ; fill in unit-dependent info, return 1 if hardfile.
	move.l d0,d3

	; Don't init hardfiles if < V36
	and.l d5,d0
	bne.b general_ret

	move.l a4,a6
	jsr.l -144(a6) ; MakeDosNode()
	move.l d0,a3 ; devicenode
	jsr.l $F0FF20 ; record in ui.startup
	moveq.l #0,d0
	move.l d0,8(a3)          ; dn_Task
	move.l d0,16(a3)         ; dn_Handler
	move.l d0,32(a3)         ; dn_SegList

	tst.l d3
	bne.b MKDV_doboot

MKDV_is_filesys:
	move.l #4000,20(a3)     ; dn_StackSize
	lea.l our_seglist(pc),a1
	move.l a1,d0
	lsr.l  #2,d0
	move.l d0,32(a3)        ; dn_SegList
	move.l #-1,36(a3)       ; dn_GlobalVec

MKDV_doboot:
	tst.l d7
	beq.b MKDV_noboot

	move.l 4.w,a6
	moveq.l #20,d0
	moveq.l #0,d1
	jsr.l  -198(a6) ; AllocMem
	move.l d0,a1 ; bootnode
	moveq.l #0,d0
	move.l d0,(a1)
	move.l d0,4(a1)
	move.w d0,14(a1)
	move.w #$10FF,d0
	sub.b  d6,d0
	move.w d0,8(a1)
	move.l $104(a5),10(a1) ; filesys_configdev
	move.l a3,16(a1)        ; devicenode
	lea.l  74(a4),a0 ; MountList
	jmp.l  -270(a6) ; Enqueue()

MKDV_noboot:
	move.l a3,a0
	moveq.l #0,d1
	move.l d1,a1
	moveq.l #-1,d0
	move.l a4,a6 ; expansion base
	jmp.l  -150(a6) ; AddDosNode

filesys_mainloop:
	move.l 4.w,a6
	moveq.l #0,d0
	move.l d0,a1
	jsr -294(a6) ; FindTask
	move.l d0,a0
	lea.l $5c(a0),a5 ; pr_MsgPort

	; Open DOS library
	lea.l doslibname(pc),a1
	moveq.l #0,d0
	jsr -552(a6) ; OpenLibrary
	move.l d0,a2

	; Allocate some memory. Usage:
	; 0: lock chain
	; 4: command chain
	; 8: second thread's lock chain
	; 12: dummy message
	; 32: the volume (80+44+1 bytes)
	move.l #80+44+1+20+12,d0
	move.l #$10001,d1 ; MEMF_PUBLIC | MEMF_CLEAR
	jsr.l -198(a6) ; AllocMem
	move.l d0,a3
	moveq.l #0,d6
	move.l d6,(a3)
	move.l d6,4(a3)
	move.l d6,8(a3)

	moveq.l #0,d5 ; No commands queued.

	; Fetch our startup packet
	move.l a5,a0
	jsr -384(a6) ; WaitPort
	move.l a5,a0
	jsr -372(a6) ; GetMsg
	move.l d0,a4
	move.l 10(a4),d3 ; ln_Name
	moveq.l #0,d0
	jsr.l $F0FF40
	bra.b FSML_Reply

	; We abuse some of the fields of the message we get. Offset 0 is
	; used for chaining unprocessed commands, and offset 1 is used for
	; indicating the status of a command. 0 means the command was handed
	; off to some UAE thread and did not complete yet, 1 means we didn't
	; even hand it over yet because we were afraid that might blow some
	; pipe limit, and -1 means the command was handed over and has completed
	; processing by now, so it's safe to reply to it.

FSML_loop:
	move.l a5,a0
	jsr -384(a6) ; WaitPort
	move.l a5,a0
	jsr -372(a6) ; GetMsg
	move.l d0,a4
	move.l 10(a4),d3 ; ln_Name
	bne.b FSML_FromDOS

	; It's a dummy packet indicating that some queued command finished.
	moveq.l #1,d0
	jsr.l $F0FF50
	; Go through the queue and reply all those that finished.
	lea.l 4(a3),a2
	move.l (a2),a0
FSML_check_old:
	move.l a0,d0
	beq.b FSML_loop
	move.l (a0),a1
	move.l d0,a0
	; This field may be accessed concurrently by several UAE threads.
	; This _should_ be harmless on all reasonable machines.
	move.l 4(a0),d0
	bpl.b FSML_check_next
	movem.l a0/a1,-(a7)
	move.l 10(a0),a4
	bsr.b ReplyOne
	subq.l #1,d5  ; One command less in the queue
	movem.l (a7)+,a0/a1
	move.l a1,(a2)
	move.l a1,a0
	bra.b FSML_check_old
FSML_check_next:
	move.l a0,a2
	move.l a1,a0
	bra.b FSML_check_old

FSML_FromDOS:
	; Limit the number of outstanding started commands. We can handle an
	; unlimited number of unstarted commands.
	cmp.l #20,d5
	bcs  FSML_DoCommand
	; Too many commands queued.
	moveq.l #1,d0
	move.l d0,4(a4)
	bra.b FSML_Enqueue

FSML_DoCommand:
	bsr.b LockCheck  ; Make sure there are enough locks for the C code to grab.
	jsr.l $F0FF30
	tst.l d0
	beq.b FSML_Reply
	; The command did not complete yet. Enqueue it and increase number of
	; queued commands
	; The C code already set 4(a4) to 0
	addq.l #1,d5
FSML_Enqueue:
	move.l 4(a3),(a4)
	move.l a4,4(a3)
	bra.b FSML_loop

FSML_Reply:
	move.l d3,a4
	bsr.b ReplyOne
	bra.b FSML_loop

ReplyOne:
	move.l (a4),a1  ; dp_Link
	move.l 4(a4),a0 ; dp_Port
	move.l a5,4(a4)
	jmp -366(a6) ; PutMsg

; ugly code to avoid calling AllocMem / FreeMem from native C code.
; We keep a linked list of 3 locks. In theory, only one should ever
; be used. Before handling every packet, we check that the list has the
; right length.

LockCheck:
	move.l d5,-(a7)
	moveq.l #-4,d5  ; Keep three locks
	move.l (a3),a2
	move.l a2,d7
LKCK_Loop:
	move.l a2,d1
	beq LKCK_ListEnd
	addq.l #1,d5
	beq.b LKCK_TooMany
	move.l a2,a1
	move.l (a2),a2
	bra.b LKCK_Loop
LKCK_ListEnd:
	addq.l #1,d5
	beq.b LKCK_ret
	move.l d7,a2
	moveq.l #24,d0 ; sizeof Lock is 20, 4 for chain
	moveq.l #1,d1 ; MEMF_PUBLIC
	jsr.l -198(a6) ; AllocMem
	addq.w #1,d6
	move.l d0,a2
	move.l d7,(a2)
	move.l a2,d7
	bra.b LKCK_ListEnd
LKCK_TooMany:
	move.l (a2),d0 ; We have too many, but we tolerate that to some extent.
	beq.b LKCK_ret
	move.l d0,a0
	move.l (a0),d0
	beq.b LKCK_ret
	move.l d0,a0
	move.l (a0),d0
	beq.b LKCK_ret

	moveq.l #0,d0 ; Now we are sure that we really have too many. Delete some.
	move.l d0,(a1)
LKCK_TooManyLoop:
	move.l a2,a1
	move.l (a1),a2
	moveq.l #24,d0
	jsr -210(a6) ; FreeMem
	add.l #$10000,d6
	move.l a2,d0
	bne.b LKCK_TooManyLoop
LKCK_ret:
	move.l d7,(a3)
	move.l (a7)+,d5
	rts

exter_name: dc.b 'UAE filesystem',0
doslibname: dc.b 'dos.library',0
explibname: dc.b 'expansion.library',0
	END
