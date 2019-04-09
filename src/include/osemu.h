 /*
  * UAE - The Un*x Amiga Emulator
  *
  * OS emulation prototypes
  *
  * Copyright 1996 Bernd Schmidt
  */

static __inline__ char *raddr(uaecptr p)
{
    return p == 0 ? NULL : (char *)get_real_address(p);
}

extern void timerdev_init(void);
extern void gfxlib_install(void);
extern void execlib_install(void);
extern void execlib_sysinit(void);

/* exec.library */

extern uaecptr EXEC_sysbase;

extern void EXEC_NewList(uaecptr list);
extern void EXEC_Insert(uaecptr list, uaecptr node, uaecptr pred);
extern void EXEC_AddTail(uaecptr list, uaecptr node);
extern void EXEC_Enqueue(uaecptr list, uaecptr node);
extern void EXEC_Remove(uaecptr node);
extern uae_u32 EXEC_RemHead(uaecptr list);
extern uae_u32 EXEC_RemTail(uaecptr list);
extern uaecptr EXEC_FindName(uaecptr start, char *name);

extern uaecptr EXEC_Allocate(uaecptr memheader, unsigned long size);
extern uaecptr EXEC_AllocMem(unsigned long size, uae_u32 requirements);
extern void EXEC_FreeMem(uaecptr, unsigned long);
extern void EXEC_Deallocate(uaecptr, uaecptr, unsigned long);
extern void EXEC_FreeMem(uaecptr, unsigned long);
extern unsigned long EXEC_AvailMem(uae_u32 requirements);
extern void EXEC_AddMemList(unsigned long, uae_u32, int, uaecptr, uaecptr);
extern void EXEC_InitStruct(uaecptr inittable, uaecptr memory, unsigned long size);

extern uaecptr EXEC_SetIntVector(int number, uaecptr interrupt);
extern void EXEC_RemIntServer(uae_u32 nr, uaecptr interrupt);
extern void EXEC_AddIntServer(uae_u32 nr, uaecptr interrupt);

extern int EXEC_AllocSignal(int signum);
extern void EXEC_FreeSignal(int signum);
extern void EXEC_InitSemaphore(uaecptr sigsem);
extern uaecptr EXEC_GetMsg(uaecptr port);
extern void EXEC_SumLibrary(uaecptr lib);
extern uaecptr EXEC_SetFunction(uaecptr lib, int funcOffset, uaecptr function);
extern void EXEC_MakeFunctions(uaecptr target, uaecptr funcarray, uaecptr funcdispb);
extern uaecptr EXEC_MakeLibrary(uaecptr, uaecptr, uaecptr, unsigned long, uae_u32);
extern void EXEC_AddLibrary(uaecptr lib);
extern void EXEC_AddDevice(uaecptr lib);
extern void EXEC_AddResource(uaecptr lib);
extern void EXEC_AddPort(uaecptr port);
extern void EXEC_RemPort(uaecptr port);

extern uaecptr EXEC_FindTask(char *name);
extern uaecptr EXEC_FindResident(char *name);
extern uaecptr EXEC_RawDoFormat(uae_u8 *fstr, uaecptr data, uaecptr pcp, uae_u32 pcd);

/* These require a multi-tasking EXEC emulation */
extern uae_u32 EXEC_Wait(uae_u32 exec_sigmask);
extern void EXEC_ObtainSemaphoreList(uaecptr l);
extern void EXEC_ReleaseSemaphoreList(uaecptr l);

extern uae_u32 EXEC_Permit(void);
extern uae_u32 EXEC_Forbid(void);
extern uae_u32 EXEC_Disable(void);
extern uae_u32 EXEC_Enable(void);

extern uae_s32 EXEC_WaitIO(uaecptr ioreq);
extern uae_s32 EXEC_DoIO(uaecptr ioreq);
extern void EXEC_SendIO(uaecptr ioreq);
extern uaecptr EXEC_CheckIO(uaecptr ioreq);
extern void EXEC_AbortIO(uaecptr ioRequest);

extern void EXEC_InitSemaphore(uaecptr exec_sigsem);
extern uae_u32 EXEC_AttemptSemaphore(uaecptr exec_sigsem);
extern void EXEC_ObtainSemaphore(uaecptr exec_sigsem);
extern void EXEC_ReleaseSemaphore(uaecptr exec_sigsem);

extern uaecptr EXEC_OpenLibrary(char *name, int version);
extern uaecptr EXEC_OpenResource(char *name);
extern uaecptr EXEC_OpenDevice(char *name, uae_u32 unit, uaecptr ioRequest, uae_u32 flags);
extern uae_u32 EXEC_CloseLibrary(uaecptr lib);
extern uae_u32 EXEC_CloseDevice(uaecptr ioRequest);

extern uaecptr EXEC_AddTask(uaecptr task, uaecptr initPC, uaecptr finalPC);
extern void EXEC_RemTask(uaecptr task);
extern int EXEC_SetTaskPri(uaecptr task, int pri);

extern uae_u32 EXEC_SetSignal(uae_u32 newsig, uae_u32 exec_sigmask);
extern void EXEC_Signal(uaecptr task, uae_u32 exec_sigmask);

extern void EXEC_PutMsg(uaecptr port, uaecptr msg);
extern void EXEC_ReplyMsg(uaecptr msg);
extern uaecptr EXEC_WaitPort(uaecptr port);

extern void EXEC_QuantumElapsed(void);

extern uaecptr EXEC_InitResident(uaecptr resident, uae_u32 segList);

/* timer.device */

extern void TIMER_block(void);
extern void TIMER_unblock(void);
extern void TIMER_Interrupt(void);

/* graphics.library */

extern int GFX_WritePixel(uaecptr rp, int x, int y);

