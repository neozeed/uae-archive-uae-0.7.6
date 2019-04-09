/*==========================================================================
 *
 *  Copyright (C) 1996 Brian King
 *
 *  File:       wingui.c
 *  Content:    Win32-specific gui features for UAE port.
 *
-U 4 -c 4 -F 8 -n i3 -w 2 -m system3.1:d:\amiga\hd0 -m work:d:\amiga\hd1
 ***************************************************************************/
#ifdef __GNUC__
#define NONAMELESSUNION
#define __int64 long long
#include "machdep/winstuff.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <winuser.h>
#include <ddraw.h>
#include <process.h>
#include <shlobj.h>
#include <prsht.h>
#include <richedit.h>
#endif
#include "config.h"
#include "resource.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "gui.h"
#include "options.h"
#include "memory.h"
#ifdef __GNUC__
#include "machdep/memory.h"
#endif
#include "custom.h"
#include "readcpu.h"
#include "newcpu.h"
#include "disk.h"
#include "uae.h"
#include "autoconf.h"

#include "picasso96.h"
#include "osdep/win32.h"
#include "osdep/win32gui.h"

extern LONG GetDisplayModeLogicalNumber( int w, int h );
extern LONG GetDisplayModeNumber( LONG logical );

extern HANDLE hEmuThread;
extern BOOL viewing_child;
extern BOOL                    allow_quit_from_property_sheet;
extern BOOL                    viewing_child;
extern HDC hBackDC;
extern HBITMAP hBackBM;
extern HINSTANCE hInst;
extern HWND hMainWnd, hAmigaWnd;
extern uae_u32 fastmem_size, chipmem_size, bogomem_size, gfxmem_size, z3fastmem_size; // BDK
extern int new_fullscreen_flag;
extern int mouseactive;
extern char *start_path;
HANDLE win32uae_key = NULL;

#define Error(x) MessageBox( NULL, (x), "WinUAE Error", MB_OK )

#ifdef __GNUC__
#define DUN DUMMYUNIONNAME ## .
#define DUN2 DUMMYUNIONNAME2 ## .
#define DUN3 DUMMYUNIONNAME3 ## .
#else
#define DUN
#define DUN2
#define DUN3
#endif
BOOL GetSettings(void)
{
    BOOL rc = TRUE;
    PROPSHEETPAGE pages[C_PAGES];
    PROPSHEETHEADER pHeader;

    pages[ LOADSAVE_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ LOADSAVE_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ LOADSAVE_ID ].hInstance   = hInst;
    pages[ LOADSAVE_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_LOADSAVE);
    pages[ LOADSAVE_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_LOADSAVE);
    pages[ LOADSAVE_ID ].pszTitle    = MAKEINTRESOURCE(IDS_LOADSAVE);
    pages[ LOADSAVE_ID ].pfnDlgProc  = (LPFNPSPCALLBACK) LoadSaveDlgProc;
    pages[ LOADSAVE_ID ].lParam      = 0;
    pages[ LOADSAVE_ID ].pfnCallback = NULL;
    pages[ LOADSAVE_ID ].pcRefParent = NULL;

    pages[ STARTUP_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ STARTUP_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ STARTUP_ID ].hInstance   = hInst;
    pages[ STARTUP_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_STARTUP);
    pages[ STARTUP_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_STARTUP);
    pages[ STARTUP_ID ].pszTitle    = MAKEINTRESOURCE(IDS_STARTUP);
    pages[ STARTUP_ID ].pfnDlgProc  = (LPFNPSPCALLBACK)StartupDlgProc;
    pages[ STARTUP_ID ].lParam      = 0;
    pages[ STARTUP_ID ].pfnCallback = NULL;
    pages[ STARTUP_ID ].pcRefParent = NULL;

    pages[ SOUND_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ SOUND_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ SOUND_ID ].hInstance   = hInst;
    pages[ SOUND_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_SOUND);
    pages[ SOUND_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_SOUND);
    pages[ SOUND_ID ].pszTitle    = MAKEINTRESOURCE(IDS_SOUND);
    pages[ SOUND_ID ].pfnDlgProc  = (LPFNPSPCALLBACK)SoundDlgProc;
    pages[ SOUND_ID ].lParam      = 0;
    pages[ SOUND_ID ].pfnCallback = NULL;
    pages[ SOUND_ID ].pcRefParent = NULL;

    pages[ DISPLAY_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ DISPLAY_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ DISPLAY_ID ].hInstance   = hInst;
    pages[ DISPLAY_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_DISPLAY);
    pages[ DISPLAY_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_DISPLAY);
    pages[ DISPLAY_ID ].pszTitle    = MAKEINTRESOURCE(IDS_DISPLAY);
    pages[ DISPLAY_ID ].pfnDlgProc  = (LPFNPSPCALLBACK)DisplayDlgProc;
    pages[ DISPLAY_ID ].lParam      = 0;
    pages[ DISPLAY_ID ].pfnCallback = NULL;
    pages[ DISPLAY_ID ].pcRefParent = NULL;

    pages[ HARDDISK_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ HARDDISK_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ HARDDISK_ID ].hInstance   = hInst;
    pages[ HARDDISK_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_HARDDISK);
    pages[ HARDDISK_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_HARDDRIVE);
    pages[ HARDDISK_ID ].pszTitle    = MAKEINTRESOURCE(IDS_HARDDRIVES);
    pages[ HARDDISK_ID ].pfnDlgProc  = (LPFNPSPCALLBACK)HarddiskDlgProc;
    pages[ HARDDISK_ID ].lParam      = 0;
    pages[ HARDDISK_ID ].pfnCallback = NULL;
    pages[ HARDDISK_ID ].pcRefParent = NULL;

    pages[ FLOPPY_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ FLOPPY_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ FLOPPY_ID ].hInstance   = hInst;
    pages[ FLOPPY_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_FLOPPIES);
    pages[ FLOPPY_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_FLOPPYDRIVE);
    pages[ FLOPPY_ID ].pszTitle    = MAKEINTRESOURCE(IDS_FLOPPIES);
    pages[ FLOPPY_ID ].pfnDlgProc  = (LPFNPSPCALLBACK)FloppiesDlgProc;
    pages[ FLOPPY_ID ].lParam      = 0;
    pages[ FLOPPY_ID ].pfnCallback = NULL;
    pages[ FLOPPY_ID ].pcRefParent = NULL;

    pages[ PORTS_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ PORTS_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ PORTS_ID ].hInstance   = hInst;
    pages[ PORTS_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_PORTS);
    pages[ PORTS_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_PORTS);
    pages[ PORTS_ID ].pszTitle    = MAKEINTRESOURCE(IDS_PORTS);
    pages[ PORTS_ID ].pfnDlgProc  = (LPFNPSPCALLBACK)PortsDlgProc;
    pages[ PORTS_ID ].lParam      = 0;
    pages[ PORTS_ID ].pfnCallback = NULL;
    pages[ PORTS_ID ].pcRefParent = NULL;

    pages[ ADVANCED_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ ADVANCED_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ ADVANCED_ID ].hInstance   = hInst;
    pages[ ADVANCED_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_ADVANCED);
    pages[ ADVANCED_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_ADVANCED);
    pages[ ADVANCED_ID ].pszTitle    = MAKEINTRESOURCE(IDS_ADVANCED);
    pages[ ADVANCED_ID ].pfnDlgProc  = (LPFNPSPCALLBACK)AdvancedDlgProc;
    pages[ ADVANCED_ID ].lParam      = 0;
    pages[ ADVANCED_ID ].pfnCallback = NULL;
    pages[ ADVANCED_ID ].pcRefParent = NULL;

    pages[ ABOUT_ID ].dwSize      = sizeof(PROPSHEETPAGE);
    pages[ ABOUT_ID ].dwFlags     = PSP_USETITLE | PSP_USEICONID;
    pages[ ABOUT_ID ].hInstance   = hInst;
    pages[ ABOUT_ID ].DUN pszTemplate = MAKEINTRESOURCE(IDD_ABOUTME);
    pages[ ABOUT_ID ].DUN2 pszIcon     = MAKEINTRESOURCE(IDI_UAEINFO);
    pages[ ABOUT_ID ].pszTitle    = MAKEINTRESOURCE(IDS_ABOUTME);
    pages[ ABOUT_ID ].pfnDlgProc  = (LPFNPSPCALLBACK) AboutDlgProc;
    pages[ ABOUT_ID ].lParam      = 0;
    pages[ ABOUT_ID ].pfnCallback = NULL;
    pages[ ABOUT_ID ].pcRefParent = NULL;

    pHeader.dwSize     = sizeof(PROPSHEETHEADER);
    pHeader.dwFlags    = PSH_PROPSHEETPAGE | PSH_PROPTITLE | PSH_USEICONID | PSH_USECALLBACK | PSH_NOAPPLYNOW;
    pHeader.hwndParent = hAmigaWnd;
    pHeader.hInstance  = hInst;
    pHeader.DUN pszIcon    = MAKEINTRESOURCE(IDI_UAEICON);
    pHeader.pszCaption = "WinUAE";
    pHeader.nPages     = C_PAGES;
    pHeader.DUN2 nStartPage = 0;
    pHeader.DUN3 ppsp       = (LPCPROPSHEETPAGE)pages;
    pHeader.pfnCallback= (PFNPROPSHEETCALLBACK)InitPropertySheet;
    if(PropertySheet(&pHeader)==-1 || quit_program)
        rc = FALSE;
    return rc;
}

void SetupVolumes( void )
{
    int loop;
    for( loop = 0; loop < 4; loop++ )
    {
        if( *drives[loop].name && *drives[loop].path )
            add_filesys_unit( drives[loop].name, drives[loop].path, !drives[loop].rw, 0, 0, 0 );
    }
}

HWND pages[C_PAGES];

void CALLBACK InitPropertySheet( HWND hDlg, UINT msg, LPARAM lParam )
{
    int i;
    static BOOL first_time = TRUE;

    if( __argc != 1 )
    {
        first_time = FALSE;
    }

    if( !first_time )
    {
        // Remove the configurations page
        PropSheet_RemovePage( hDlg, 0, NULL );

        // Remove the startup property-sheet page
        PropSheet_RemovePage(hDlg,0,NULL);

        // Remove the sound property-sheet page
        PropSheet_RemovePage(hDlg,0,NULL);

		// Remove the harddrives property-sheet page
		PropSheet_RemovePage(hDlg,2,NULL);

        // Remove the advanced property-sheet page
        PropSheet_RemovePage(hDlg,3,NULL);

        allow_quit_from_property_sheet = FALSE;
    }

    switch(msg)
    {
        case PSCB_INITIALIZED:
            if( first_time )
            {
                first_time = FALSE;
                for( i=0;i<C_PAGES;pages[i++]=NULL);
            }
        break;
   }
}

void CalculateFPS( DWORD setting, char *buffer )
{
    char nth[10];
    switch( setting )
    {
    case 1:
        strcpy( nth, "" );
        break;
    case 2:
        strcpy( nth, "second " );
        break;
    case 3:
        strcpy( nth, "third " );
        break;
    case 4:
        strcpy( nth, "fourth " );
        break;
    case 5:
        strcpy( nth, "fifth " );
        break;
    case 6:
        strcpy( nth, "sixth " );
        break;
    case 7:
        strcpy( nth, "seventh " );
        break;
    case 8:
        strcpy( nth, "eighth " );
        break;
    case 9:
        strcpy( nth, "ninth " );
        break;
    default:
        strcpy( nth, "tenth " );
        break;
    }
     
    sprintf( buffer, "Every %sFrame", nth );
}

void CalculateSpeed( DWORD setting, char *buffer )
{
    sprintf( buffer, "%d", setting );
    currprefs.m68k_speed = setting;
}

void CalculateRealMem(DWORD setting, int *size, char *buffer, int memtype)
{
    if( memtype == 0 )
    {
        switch(setting)
        {
        case 0:
            if( buffer )
                strcpy(buffer,"512 K");
            if( size )
                *size = 0x00080000;
            break;
        case 1:
            if( buffer )
                strcpy(buffer,"1 Meg");
            if( size )
                *size = 0x00100000;
            break;
        case 2:
            if( buffer )
                strcpy(buffer,"2 Megs");
            if( size )
                *size = 0x00200000;
            break;
        case 3:
            if( buffer )
                strcpy(buffer,"4 Megs");
            if( size )
                *size = 0x00400000;
            break;
        default:
            if( buffer )
                strcpy(buffer,"8 Megs");
            if( size )
                *size = 0x00800000;
            break;
        }
    }
    else
    {
        switch(setting)
        {
        case 0:
            if( buffer )
                strcpy(buffer,"None");
            if( size )
                *size = 0x00000000;
            break;
        case 1:
            if( buffer )
                strcpy(buffer,"1 Meg");
            if( size )
                *size = 0x00100000;
            break;
        case 2:
            if( buffer )
                strcpy(buffer,"2 Megs");
            if( size )
                *size = 0x00200000;
            break;
        case 3:
            if( buffer )
                strcpy(buffer,"4 Megs");
            if( size )
                *size = 0x00400000;
            break;
        case 4:
            if( buffer )
                strcpy(buffer,"8 Megs");
            if( size )
                *size = 0x00800000;
            break;
        case 5:
            if( memtype == 3 )
            {
                if( buffer )
                    strcpy( buffer, "16 Megs" );
                if( size )
                    *size = 0x01000000;
            }
            break;
        case 6:
            if( memtype == 3 )
            {
                if( buffer )
                    strcpy( buffer, "32 Megs" );
                if( size )
                    *size = 0x02000000;
            }
            break;
        }
    }
}

void UpdateRadioButtons( HWND hDlg, HWND lParam )
{
    int which_button;

    if( GetDlgItem( hDlg, IDC_FREQUENCY ) == lParam )
    {
        switch( currprefs.sound_freq )
        {
        case 11025:
            which_button = IDC_11KHZ;
            break;
        case 44100:
            which_button = IDC_44KHZ;
            break;
        case 48000:
            which_button = IDC_48KHZ;
            break;
        default:
            which_button = IDC_22KHZ;
            break;
        }
        CheckRadioButton( hDlg, IDC_11KHZ, IDC_48KHZ, which_button );
    }
    else if( GetDlgItem( hDlg, IDC_SOUNDSIZE ) == lParam )
    {
        switch( currprefs.sound_bits )
        {
        case 16:
            which_button = IDC_16BIT;
            break;
        default:
            which_button = IDC_8BIT;
            break;
        }
        CheckRadioButton( hDlg, IDC_8BIT, IDC_16BIT, which_button );
    }
    else if( GetDlgItem( hDlg, IDC_SOUND ) == lParam )
    {
        switch( currprefs.produce_sound )
        {
        case -1:
            which_button = IDC_SOUND0;
            break;
        case 1:
            which_button = IDC_SOUND2;
            break;
        case 2:
            which_button = IDC_SOUND3;
            break;
        case 3:
            which_button = IDC_SOUND4;
            break;
        default:
            which_button = IDC_SOUND1;
            break;
        }
        if( CheckRadioButton( hDlg, IDC_SOUND0, IDC_SOUND4, which_button ) == 0 )
            which_button = 0;
    }
    else if( GetDlgItem( hDlg, IDC_PORT0 ) == lParam )
    {
        switch( currprefs.fake_joystick & 0x00FF )
        {
        case 0:
            which_button = IDC_PORT0_JOY0;
            break;
        case 1:
            which_button = IDC_PORT0_JOY1;
            break;
        case 2:
            which_button = IDC_PORT0_MOUSE;
            break;
        case 3:
            which_button = IDC_PORT0_KBDA;
            break;
        case 4:
            which_button = IDC_PORT0_KBDB;
            break;
        default:
            which_button = IDC_PORT0_KBDC;
            break;
        }
        if( CheckRadioButton( hDlg, IDC_PORT0_JOY0, IDC_PORT0_KBDC, which_button ) == 0 )
            which_button = 0;
    }
    else if( GetDlgItem( hDlg, IDC_PORT1 ) == lParam )
    {
        switch( ( currprefs.fake_joystick & 0xFF00 ) >> 8 )
        {
        case 0:
            which_button = IDC_PORT1_JOY0;
            break;
        case 1:
            which_button = IDC_PORT1_JOY1;
            break;
        case 2:
            which_button = IDC_PORT1_MOUSE;
            break;
        case 3:
            which_button = IDC_PORT1_KBDA;
            break;
        case 4:
            which_button = IDC_PORT1_KBDB;
            break;
        default:
            which_button = IDC_PORT1_KBDC;
            break;
        }
        if( CheckRadioButton( hDlg, IDC_PORT1_JOY0, IDC_PORT1_KBDC, which_button ) == 0 )
            which_button = 0;
    }
}

void UpdateScroller(HWND hDlg, HWND lParam)
{
    char buffer[32];
    DWORD dwPos = SendMessage( (HWND)lParam, TBM_GETPOS, 0 ,0 ), dwPos2;

    if( GetDlgItem( hDlg, IDC_CHIPMEM ) == lParam )
    {
        CalculateRealMem( dwPos, NULL, buffer, 0 );
        SetDlgItemText( hDlg, IDC_CHIPRAM, buffer );

        dwPos2 = SendDlgItemMessage( hDlg, IDC_FASTMEM, TBM_GETPOS, 0, 0 );
        if( ( dwPos > 2 ) &&
            ( dwPos2 > 0 ) )
        {
            SendDlgItemMessage( hDlg, IDC_FASTMEM, TBM_SETPOS, TRUE, 0 );
            UpdateScroller( hDlg, GetDlgItem( hDlg, IDC_FASTMEM ) );
        }
    }
    else if( GetDlgItem( hDlg, IDC_FASTMEM ) == lParam )
    {
        CalculateRealMem( dwPos, NULL, buffer, 1 );
        SetDlgItemText( hDlg, IDC_FASTRAM, buffer );

        dwPos2 = SendDlgItemMessage( hDlg, IDC_CHIPMEM, TBM_GETPOS, 0, 0 );
        if( ( dwPos > 0 ) &&
            ( dwPos2 > 2 ) )
        {
            SendDlgItemMessage( hDlg, IDC_CHIPMEM, TBM_SETPOS, TRUE, 2 );
            UpdateScroller( hDlg, GetDlgItem( hDlg, IDC_CHIPMEM ) );
        }
    }
    else if( GetDlgItem( hDlg, IDC_FRAMERATE ) == lParam )
    {
        CalculateFPS( dwPos, buffer );
        SetDlgItemText(hDlg,IDC_RATETEXT,buffer);
    }
    else if( GetDlgItem( hDlg, IDC_SPEED ) == lParam )
    {
        CalculateSpeed( dwPos, buffer );
        SetDlgItemText( hDlg, IDC_SPEEDBOX, buffer );
    }
    else if( GetDlgItem( hDlg, IDC_P96MEM ) == lParam )
    {
        CalculateRealMem( dwPos, NULL, buffer, 2 );
        SetDlgItemText( hDlg, IDC_P96RAM, buffer );
    }
    else if( GetDlgItem( hDlg, IDC_Z3FASTMEM ) == lParam )
    {
        CalculateRealMem( dwPos, NULL, buffer, 3 );
        SetDlgItemText( hDlg, IDC_Z3FASTRAM, buffer );
    }
}

ConfigStructPtr AllocateConfigStruct( void )
{
    ConfigStructPtr ptr = NULL;
    if( ptr = malloc( sizeof( ConfigStruct ) ) )
    {
        ptr->Size = sizeof( ConfigStruct );
        ptr->VersionMajor = CONFIG_VERSION_MAJOR;
        ptr->VersionMinor = CONFIG_VERSION_MINOR;
    }
    return ptr;
}

void FreeConfigStruct( ConfigStructPtr cfgptr )
{
    free( cfgptr );
}

// Get a ptr to a ConfigStruct based on entry-number
// Return NULL if invalid entry-number
ConfigStructPtr GetFirstConfigEntry( HANDLE *file_handle, LPWIN32_FIND_DATA find_data )
{
    HANDLE cfg_file;
    ConfigStructPtr config = NULL;
    DWORD num_bytes = 0;
    char init_path[MAX_PATH] = "", *posn;

    if( start_path )
    {
        strncpy( init_path, start_path, MAX_PATH );
        strncat( init_path, "\\Configurations\\*.CFG", MAX_PATH );
    }

    if( ( *file_handle = FindFirstFile( init_path, find_data ) ) != INVALID_HANDLE_VALUE )
    {
        config = AllocateConfigStruct();
        sprintf( init_path, "%s\\Configurations\\%s", start_path, find_data->cFileName );
        cfg_file = CreateFile( init_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
        ReadFile( cfg_file, config, config->Size, &num_bytes, NULL );
        if( num_bytes == config->Size )
        {
            if( posn = strrchr( find_data->cFileName, '.' ) )
                *posn = '\0';
            strcpy( config->Name, find_data->cFileName );
        }
        else
        {
            FreeConfigStruct( config );
            config = NULL;
        }
        CloseHandle( cfg_file );
    }
    else
    {

    }
    return config;
}

ConfigStructPtr GetNextConfigEntry( HANDLE *file_handle, LPWIN32_FIND_DATA find_data )
{
    HANDLE cfg_file;
    ConfigStructPtr config = NULL;
    DWORD num_bytes = 0;
    char init_path[MAX_PATH] = "", *posn;

    if( FindNextFile( *file_handle, find_data ) == 0 )
    {
        FindClose( *file_handle );
    }
    else
    {
        config = AllocateConfigStruct();
        sprintf( init_path, "%s\\Configurations\\%s", start_path, find_data->cFileName );
        cfg_file = CreateFile( init_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
        ReadFile( cfg_file, config, config->Size, &num_bytes, NULL );
        if( posn = strrchr( find_data->cFileName, '.' ) )
            *posn = '\0';
        strcpy( config->Name, find_data->cFileName );
        CloseHandle( cfg_file );
    }
    return config;
}

// This function basically sets up the defaults in our ConfigStruct... it follows the same
// procedure as each DlgProc() does for a WM_INITDIALOG msg...
void GetDefaultsForPage( ConfigStructPtr cfgptr, int page )
{
    int i;

    switch( page )
    {
    case ADVANCED_ID:
        cfgptr->NoAutoConfig         = !currprefs.automount_uaedev;
        cfgptr->CopperHack           = currprefs.copper_pos;
        //cfgptr->DebuggerEnabled      = use_debugger;
        cfgptr->InvalidAddresses     = currprefs.illegal_mem;
        cfgptr->M68KSpeed = currprefs.m68k_speed;
        cfgptr->AddressingIs24Bit    = address_space_24;
        break;
    case STARTUP_ID:
        cfgptr->ChipMem              = chipmem_size;
        cfgptr->FastMem              = fastmem_size;
        cfgptr->P96Mem               = gfxmem_size;
        cfgptr->Z3Mem                = z3fastmem_size;
        strncpy( cfgptr->KickstartName, romfile, CFG_ROM_LENGTH );
        strncpy( cfgptr->KeyFileName, keyfile, CFG_KEY_LENGTH );
        break;
    case DISPLAY_ID:
        cfgptr->FrameRate     = currprefs.framerate;
        cfgptr->LineDouble    = currprefs.gfx_linedbl;
        cfgptr->FullScreen    = amiga_fullscreen;
        cfgptr->CorrectAspect = currprefs.gfx_correct_aspect;
        cfgptr->CenterY       = currprefs.gfx_ycenter;
        cfgptr->CenterX       = currprefs.gfx_xcenter;
        cfgptr->Blit32        = currprefs.blits_32bit_enabled;
        cfgptr->BlitImmediate = currprefs.immediate_blits;
        cfgptr->Lores         = currprefs.gfx_lores;
        cfgptr->CustomSize    = customsize;
        cfgptr->ScreenWidth   = currprefs.gfx_width;
        cfgptr->ScreenHeight  = currprefs.gfx_height;
        break;
    case HARDDISK_ID:
        for( i = 0; i < NUM_DRIVES; i++ )
        {
            memcpy( &(cfgptr->drives[i]), &drives[i], sizeof( drive_specs ) );
        }
        cfgptr->HardFileBootPri = hf_bootpri;
        break;
    case FLOPPY_ID:
        for( i = 0; i < NUM_DRIVES; i++ )
        {
            memcpy( cfgptr->df[i], currprefs.df[i], 256 );
        }
        break;
    case SOUND_ID:
        cfgptr->SoundSupport = currprefs.produce_sound;
        cfgptr->SoundBits    = currprefs.sound_bits;
        cfgptr->SoundFreq    = currprefs.sound_freq;
        cfgptr->SoundStereo  = currprefs.stereo;
        break;
    case PORTS_ID:
        cfgptr->FakeJoystick = currprefs.fake_joystick;
        cfgptr->SerialPort   = currprefs.use_serial;
        strncpy( cfgptr->SerialName,  sername, CFG_SER_LENGTH );
        strncpy( cfgptr->PrinterName, prtname, CFG_PAR_LENGTH );
        break;
    case LOADSAVE_ID:
    case ABOUT_ID:
    default:
        break;
    }
}

// flag = 0 - save
//      = 1 - load
//      = 2 - delete
//      = 3 - import
void ConfigurationFile( HWND hDlg, ConfigStructPtr cfgptr, int flag )
{
    HANDLE file_handle;
    DWORD byte_count;
    OPENFILENAME openFileName;
    char full_path[MAX_PATH] = "";
    char file_name[MAX_PATH] = "";
    char init_path[MAX_PATH] = "";

    if( start_path )
    {
        strncpy( init_path, start_path, MAX_PATH );
        strncat( init_path, "\\Configurations\\", MAX_PATH );
        strcpy( full_path, cfgptr->Name );
    }

    if( flag == 3 )
    {
        openFileName.lStructSize       = sizeof(OPENFILENAME);
        openFileName.hwndOwner         = hDlg;
        openFileName.hInstance         = hInst;
        openFileName.lpstrFilter       = "WinUAE Configuration Files (*.ACF)\0*.ACF\0\0";
        openFileName.lpstrTitle        = "Choose the configuration file to import...";
        openFileName.Flags             = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        openFileName.lpstrDefExt       = "ACF";
        openFileName.lpstrCustomFilter = NULL;
        openFileName.nMaxCustFilter    = 0;
        openFileName.nFilterIndex      = 0;
        openFileName.lpstrFile         = full_path;
        openFileName.nMaxFile          = MAX_PATH;
        openFileName.lpstrFileTitle    = file_name;
        openFileName.nMaxFileTitle     = MAX_PATH;
        if( start_path )
            openFileName.lpstrInitialDir   = init_path;
        else
            openFileName.lpstrInitialDir   = NULL;
        openFileName.lpfnHook          = NULL;
        openFileName.lpTemplateName    = NULL;
        openFileName.lCustData         = 0;

        if( GetOpenFileName( &openFileName ) )
        {
            // Do the import stuff here...
        }
    }
    if( flag == 1 )
    {
        SetCursor( LoadCursor( NULL, IDC_WAIT ) );

        // full_path is our path to load the file from...
        sprintf( full_path, "%s%s.CFG", init_path, cfgptr->Name );
        if( ( file_handle = CreateFile( full_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL ) ) == INVALID_HANDLE_VALUE )
        {
            // handle error
        }
        else
        {
            if( ReadFile( file_handle, cfgptr, cfgptr->Size, &byte_count, NULL ) == FALSE )
            {
                // handle error
            }
            CloseHandle( file_handle );
        }
        SetCursor( LoadCursor( NULL, IDC_ARROW ) );
    }
    else if( flag == 0 )
    {
        SetCursor( LoadCursor( NULL, IDC_WAIT ) );

        // full_path is our path to save the file into...
        sprintf( full_path, "%s%s.CFG", init_path, cfgptr->Name );
        if( ( file_handle = CreateFile( full_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL ) ) == INVALID_HANDLE_VALUE )
        {
            // handle error
        }
        else
        {
            if( WriteFile( file_handle, cfgptr, cfgptr->Size, &byte_count, NULL ) == FALSE )
            {
                // handle error
            }
            CloseHandle( file_handle );
        }
        SetCursor( LoadCursor( NULL, IDC_ARROW ) );
    }
    else
    {
        // full_path is our file to delete...
        sprintf( full_path, "%s%s.CFG", init_path, cfgptr->Name );
        DeleteFile( full_path );
    }
}

// the flag is a 1 if we're transferring from a cfgptr to the dialog stuff, or a 0 if the other way...
void TransferConfiguration( HWND hDlg, int flag )
{
    int i;
    ConfigStructPtr cfgptr;

    if( cfgptr = AllocateConfigStruct() )
    {
        switch( flag )
        {
        case 0: // we're doing a save...
            GetDlgItemText( hDlg, IDC_EDITNAME, cfgptr->Name, MAX_PATH );
            if( cfgptr->Name[0] == '\0' )
            {
                MessageBox( hDlg, "You must enter a name for your configuration...", "Save Error", MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND );
            }
            else
            {
                GetDlgItemText( hDlg, IDC_EDITDESCRIPTION, cfgptr->Description, CFG_DESCRIPTION_LENGTH );

                // The for() loop below handles the other pages that were never viewed...
                for( i = 0; i < C_PAGES; i++ )
                {
                    if( pages[i] == NULL )
                        GetDefaultsForPage( cfgptr, i );
                    else
                        SendMessage( pages[i], PSM_QUERYSIBLINGS, 0, (LPARAM)cfgptr );
                }
                ConfigurationFile( hDlg, cfgptr, flag );
            }
            break;

        case 1: // we're doing a load...
            GetDlgItemText( hDlg, IDC_EDITNAME, cfgptr->Name, MAX_PATH );
            if( cfgptr->Name[0] == '\0' )
            {
                MessageBox( hDlg, "You must select a configuration...", "Load Error", MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND );
            }
            else
            {
                ConfigurationFile( hDlg, cfgptr, flag );

                // Do a sanity-test on the loaded config file:
                // - check its Size member
                // - check its VersionMajor
                // - check its VersionMinor
                if( ( cfgptr->Size != sizeof( ConfigStruct ) ) ||
                    ( cfgptr->VersionMajor != CONFIG_VERSION_MAJOR ) ||
                    ( cfgptr->VersionMinor != CONFIG_VERSION_MINOR ) )
                {
                    // Dire error situation...
                    MessageBox( hDlg, "This configuration is of the wrong version... automatic conversion is not yet available, sorry!", "Config-File Error", MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND );
                }
                else
                {
                    // Insert stuff here to take the cfgptr contents and put them into the right configuration variables (currprefs, etc).

                    // Basically ripped from GetDefaultsForPage() function above... and reversed direction
                    currprefs.automount_uaedev    = !cfgptr->NoAutoConfig;
                    currprefs.fake_joystick       = cfgptr->FakeJoystick;
                    currprefs.illegal_mem         = cfgptr->InvalidAddresses;
                    currprefs.use_serial          = cfgptr->SerialPort;
                    currprefs.gfx_correct_aspect  = cfgptr->CorrectAspect;
                    currprefs.gfx_ycenter         = cfgptr->CenterY;
                    currprefs.gfx_xcenter         = cfgptr->CenterX;
                    currprefs.copper_pos          = cfgptr->CopperHack;
                    currprefs.blits_32bit_enabled = cfgptr->Blit32;
                    currprefs.immediate_blits     = cfgptr->BlitImmediate;
                    currprefs.gfx_lores           = cfgptr->Lores;
                    currprefs.framerate           = cfgptr->FrameRate;
                    currprefs.gfx_linedbl         = cfgptr->LineDouble;
                    currprefs.gfx_width           = cfgptr->ScreenWidth;
                    currprefs.gfx_height          = cfgptr->ScreenHeight;
                    currprefs.produce_sound       = (int)(cfgptr->SoundSupport);
                    currprefs.sound_bits          = cfgptr->SoundBits;
                    currprefs.sound_freq          = cfgptr->SoundFreq;
                    currprefs.stereo              = cfgptr->SoundStereo;

                    //use_debugger           = cfgptr->DebuggerEnabled;
                    amiga_fullscreen       = cfgptr->FullScreen;
                    chipmem_size           = cfgptr->ChipMem;
                    fastmem_size           = cfgptr->FastMem;
                    gfxmem_size            = cfgptr->P96Mem;
                    z3fastmem_size         = cfgptr->Z3Mem;
                    currprefs.m68k_speed   = cfgptr->M68KSpeed;
                    hf_bootpri             = cfgptr->HardFileBootPri;
                    customsize             = cfgptr->CustomSize;
                    address_space_24       = cfgptr->AddressingIs24Bit;

                    strncpy( sername, cfgptr->SerialName, CFG_SER_LENGTH );
                    strncpy( prtname, cfgptr->PrinterName, CFG_PAR_LENGTH );
                    strncpy( romfile, cfgptr->KickstartName, CFG_ROM_LENGTH );
                    strncpy( keyfile, cfgptr->KeyFileName, CFG_KEY_LENGTH );

                    for( i = 0; i < NUM_DRIVES; i++ )
                    {
                        memcpy( &drives[i], &(cfgptr->drives[i]), sizeof( drive_specs ) );
                    }

                    for( i = 0; i < NUM_DRIVES; i++ )
                    {
                        memcpy( currprefs.df[i], cfgptr->df[i], 256 );
                    }

                    // Now, re-init the pages to have the right visual state...  not done when a page is viewed
                    // Only needs to be sent to those pages which have been viewed so far.
                    for( i = 0; i < C_PAGES; i++ )
                    {
                        if( pages[i] != NULL )
                            SendMessage( pages[i], WM_USER, 0, 0 );
                    }
                }
            }
            break;

        case 2: // we're doing a delete...
            GetDlgItemText( hDlg, IDC_EDITNAME, cfgptr->Name, MAX_PATH );
            if( cfgptr->Name[0] == '\0' )
            {
                MessageBox( hDlg, "You must select a configuration to delete...", "Delete Error", MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND );
            }
            else
            {
                ConfigurationFile( hDlg, cfgptr, flag );
            }
            break;

        case 3: // we're doing an import...
        default:
            ConfigurationFile( hDlg, cfgptr, flag );
            break;
        }
        FreeConfigStruct( cfgptr );
    }
}

void InitializeListView( HWND hDlg )
{
#define NUM_COLUMNS 2
    HANDLE file_handle = NULL;
    WIN32_FIND_DATA find_data;
    BOOL rc = TRUE;
    HWND list;
    LV_ITEM lvstruct;
    LV_COLUMN lvcolumn;
    RECT rect;
    char *column_heading[ NUM_COLUMNS ] = { "Name", "Description" };
    int width = 0, column_width[ NUM_COLUMNS ] = { 0, 0 };
    int items = 0, result = 0, i, entry = 0, temp = 0;
    ConfigStructPtr config = NULL;

    list = GetDlgItem( hDlg, IDC_CONFIGLIST );
    ListView_DeleteAllItems( list );

    for( i = 0; i < NUM_COLUMNS; i++ )
        column_width[i] = ListView_GetStringWidth( list, column_heading[i] ) + 15;

    // If there are no columns, then insert some
    lvcolumn.mask = LVCF_WIDTH;
    if( ListView_GetColumn( list, 1, &lvcolumn ) == FALSE )
    {
        for( i = 0; i < NUM_COLUMNS; i++ )
        {
            lvcolumn.mask     = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            lvcolumn.iSubItem = i;
            lvcolumn.fmt      = LVCFMT_CENTER;
            lvcolumn.pszText  = column_heading[i];
            lvcolumn.cx       = column_width[i];
            ListView_InsertColumn( list, i, &lvcolumn );
        }
    }
    if( config = GetFirstConfigEntry( &file_handle, &find_data ) )
    {
        do
        {
            lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
            lvstruct.pszText  = config->Name;
            lvstruct.lParam   = 0;
            lvstruct.iItem    = entry;
            lvstruct.iSubItem = 0;
            result = ListView_InsertItem( list, &lvstruct );
            if( result != -1 )
            {
                width = ListView_GetStringWidth( list, lvstruct.pszText ) + 15;
                if( width > column_width[ lvstruct.iSubItem ] )
                    column_width[ lvstruct.iSubItem ] = width;

                ListView_SetItemText( list, result, 1, config->Description );
                width = ListView_GetStringWidth( list, config->Description ) + 15;
                if( width > column_width[ 1 ] )
                    column_width[ 1 ] = width;

                entry++;
            }
            FreeConfigStruct( config );
        }
        while( config = GetNextConfigEntry( &file_handle, &find_data ) );
    }
    if( rc == FALSE )
    {
        FreeConfigStruct( config );
    }

    if( result != -1 )
    {
        if( GetWindowRect( list, &rect ) )
        {
            ScreenToClient( hDlg, (LPPOINT)&rect );
            ScreenToClient( hDlg, (LPPOINT)&rect.right );
            if( ( temp = rect.right - rect.left - column_width[ 0 ] - 4 ) > column_width[1] )
                column_width[1] = temp;
        }

        // Adjust our column widths so that we can see the contents...
        for( i = 0; i < NUM_COLUMNS; i++ )
        {
            ListView_SetColumnWidth( list, i, column_width[i] );
        }

        // Redraw the items in the list...
        items = ListView_GetItemCount( list );
        ListView_RedrawItems( list, 0, items );
    }
}

BOOL CALLBACK LoadSaveDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
#define NUM_COLUMNS 2
    char name_buf[ MAX_PATH ] = "", desc_buf[ 128 ] = "";
    int found = 0;
    BOOL rc = TRUE;
    RECT rect;
    DWORD pos;
    POINT point;
    HWND list;
    UINT flag, dblclick = 0;
    NM_LISTVIEW *nmlistview;
    int items = 0, result = 0, entry = 0;
    ConfigStructPtr config = NULL;

    switch(msg)
    {
        case WM_INITDIALOG:
            InitializeListView( hDlg );
            if( hEmuThread )
            {
                EnableWindow( GetDlgItem( hDlg, IDC_SAVE ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_IMPORT ), FALSE );
            }
            return(TRUE);
            break;

        case WM_COMMAND:
            switch( wParam )
            {
            case IDC_SAVE:
                TransferConfiguration( hDlg, 0 );
                InitializeListView( hDlg );
                break;
            case IDC_LOAD:
                TransferConfiguration( hDlg, 1 );
                break;
            case IDC_DELETE:
                TransferConfiguration( hDlg, 2 );
                InitializeListView( hDlg );
                break;
            case IDC_IMPORT:
                TransferConfiguration( hDlg, 3 );
                break;
            }
            break;
        case WM_NOTIFY:
            if( ((LPNMHDR)lParam)->idFrom == IDC_CONFIGLIST )
            {
                nmlistview = (NM_LISTVIEW *)lParam;
                list = nmlistview->hdr.hwndFrom;

                switch( nmlistview->hdr.code )
                {
                case NM_DBLCLK:
                    dblclick = 1;
                    // fall-through
                case NM_CLICK:
                    pos = GetMessagePos();
                    point.x = LOWORD( pos );
                    point.y = HIWORD( pos );
                    ScreenToClient( list, &point );
                    entry = ListView_GetTopIndex( list );
                    items = entry + ListView_GetCountPerPage( list );
                    if( items > ListView_GetItemCount( list ) )
                        items = ListView_GetItemCount( list );

                    while( entry <= items )
                    {
                        // Get the bounding rectangle of an item. If the mouse
                        // location is within the bounding rectangle of the item,
                        // you know you have found the item that was being clicked.
                        ListView_GetItemRect( list, entry, &rect, LVIR_BOUNDS);
                        if( PtInRect( &rect, point ) )
                        {
                            flag = LVIS_SELECTED | LVIS_FOCUSED;
                            ListView_SetItemState( list, entry, flag, flag);
                            found = 1;
                            break;
                        }

                        // Get the next item in listview control.
                        entry++;
                    }

                    // Copy the item's name and description to the gadgets at the bottom...
                    if( entry >= 0 && found)
                    {
                        ListView_GetItemText( list, entry, 0, name_buf, MAX_PATH );
                        ListView_GetItemText( list, entry, 1, desc_buf, 128 );
                        SetDlgItemText( hDlg, IDC_EDITNAME, name_buf );
                        SetDlgItemText( hDlg, IDC_EDITDESCRIPTION, desc_buf );
                        ListView_RedrawItems( list, 0, items );

                        if( dblclick )
                        {
                            // do the config-loading
                            TransferConfiguration( hDlg, 1 );

                            // start the emulation...
                        }
                    }
                    break;
                }
            }
            else
            {
                switch(((NMHDR *)lParam)->code)
                {
                    case PSN_RESET:
                        if(allow_quit_from_property_sheet)
                        {
                            quit_program = 1;
                            regs.spcflags |= SPCFLAG_BRK;
                        }
                        break;
                }
            }
            break;
    }

    return(FALSE);
}

int CALLBACK ContributorsProc( HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam )
{
    CHARFORMAT CharFormat;

    switch( msg )
    {
    case WM_COMMAND:
        if( wParam == IDC_GREATWORK )
        {
            EndDialog( hDlg, 1 );
            return TRUE;
        }
        break;
    case WM_INITDIALOG:
        CharFormat.cbSize = sizeof( CharFormat );

        SetDlgItemText( hDlg, IDC_CONTRIBUTORS, 
            "Bernd Schmidt - The Grand-Master\n"
            "Mathias Ortmann - WinUAE Main Guy\n"
            "Brian King - Picasso96 Support, Integrated GUI for WinUAE, AHI (Working on it)\n"
            "Gustavo Goedert/Peter Remmers/Michael Sontheimer/Tomi Hakala/Tim Gunn/Nemo Pohle - DOS Port Stuff\n"
            "Samuel Devulder/Olaf Barthel - Amiga Port\n"
            "Krister Bergman - XFree86 and OS/2 Port\n"
            "A. Blanchard/Ernesto Corvi - MacOS Port\n"
            "Christian Bauer - BeOS Port\n"
            "Ian Stephenson - NextStep Port\n"
            "Peter Teichmann - Acorn/RiscOS Port\n"
            "Stefan Reinauer - ZorroII/III AutoConfig, Serial Support\n"
            "Christian Schmitt/Chris Hames - Serial Support\n"
            "Herman ten Brugge - 68020/68881 Emulation Code\n"
            "Tauno Taipaleenmaki - Various UAE-Control/UAE-Library Support\n"
            "Brett Eden/Tim Gunn/Paolo Besser/Nemo Pohle - Various Docs and Web-Sites\n"
            "Special thanks to Alexander Kneer and Tobias Abt (The Picasso96 Team)"
            );
        SendDlgItemMessage(hDlg, IDC_CONTRIBUTORS, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
        CharFormat.dwMask |= CFM_SIZE | CFM_FACE;
        CharFormat.yHeight = 10*20; // height in twips, where a twip is 1/20th of a point - for a pt.size of 18
        strcpy( CharFormat.szFaceName, "Times New Roman" );
        SendDlgItemMessage(hDlg, IDC_CONTRIBUTORS, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
        //SendDlgItemMessage(hDlg, IDC_CONTRIBUTORS, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );

        return TRUE;
        break;
    }
    return FALSE;
}

void DisplayContributors( HWND hDlg )
{
    DialogBox( hInst, MAKEINTRESOURCE( IDD_CONTRIBUTORS ), hDlg, ContributorsProc );
}

BOOL CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    POINT point;
    RECT rect;
    CHARFORMAT CharFormat;
    switch(msg)
    {
        case WM_INITDIALOG:
            CharFormat.cbSize = sizeof( CharFormat );

            SetDlgItemText( hDlg, IDC_RICHEDIT1, "UAE for Win32/DirectX" );
            SendDlgItemMessage(hDlg, IDC_RICHEDIT1, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
            CharFormat.dwMask |= CFM_BOLD | CFM_SIZE | CFM_FACE;
            CharFormat.dwEffects = CFE_BOLD;
            CharFormat.yHeight = 18*20; // height in twips, where a twip is 1/20th of a point - for a pt.size of 18
            strcpy( CharFormat.szFaceName, "Times New Roman" );
            SendDlgItemMessage(hDlg, IDC_RICHEDIT1, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
            SendDlgItemMessage(hDlg, IDC_RICHEDIT1, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );

            SetDlgItemText( hDlg, IDC_RICHEDIT2, PROGNAME );
            SendDlgItemMessage(hDlg, IDC_RICHEDIT2, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
            CharFormat.dwMask |= CFM_SIZE | CFM_FACE;
            CharFormat.yHeight = 10*20;
            strcpy( CharFormat.szFaceName, "Times New Roman" );
            SendDlgItemMessage(hDlg, IDC_RICHEDIT2, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
            SendDlgItemMessage(hDlg, IDC_RICHEDIT2, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );

            SetDlgItemText( hDlg, IDC_AMIGAHOME, "Amiga" );
            SendDlgItemMessage(hDlg, IDC_AMIGAHOME, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
            CharFormat.dwMask |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
            CharFormat.dwEffects = CFE_UNDERLINE;
            CharFormat.yHeight = 12*20; // height in twips, where a twip is 1/20th of a point - for a pt.size of 18
            CharFormat.crTextColor = GetSysColor( COLOR_ACTIVECAPTION );
            strcpy( CharFormat.szFaceName, "Times New Roman" );
            SendDlgItemMessage(hDlg, IDC_AMIGAHOME, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
            SendDlgItemMessage(hDlg, IDC_AMIGAHOME, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );

            SetDlgItemText( hDlg, IDC_CLOANTOHOME, "Cloanto" );
            SendDlgItemMessage(hDlg, IDC_CLOANTOHOME, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
            CharFormat.dwMask |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
            CharFormat.dwEffects = CFE_UNDERLINE;
            CharFormat.yHeight = 12*20; // height in twips, where a twip is 1/20th of a point - for a pt.size of 18
            CharFormat.crTextColor = GetSysColor( COLOR_ACTIVECAPTION );
            strcpy( CharFormat.szFaceName, "Times New Roman" );
            SendDlgItemMessage(hDlg, IDC_CLOANTOHOME, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
            SendDlgItemMessage(hDlg, IDC_CLOANTOHOME, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );

            SetDlgItemText( hDlg, IDC_WINUAEHOME, "WinUAE" );
            SendDlgItemMessage(hDlg, IDC_WINUAEHOME, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
            CharFormat.dwMask |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
            CharFormat.dwEffects = CFE_UNDERLINE;
            CharFormat.yHeight = 12*20; // height in twips, where a twip is 1/20th of a point - for a pt.size of 18
            CharFormat.crTextColor = GetSysColor( COLOR_ACTIVECAPTION );
            strcpy( CharFormat.szFaceName, "Times New Roman" );
            SendDlgItemMessage(hDlg, IDC_WINUAEHOME, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
            SendDlgItemMessage(hDlg, IDC_WINUAEHOME, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );

            SetDlgItemText( hDlg, IDC_UAEHOME, "UAE" );
            SendDlgItemMessage(hDlg, IDC_UAEHOME, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
            CharFormat.dwMask |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
            CharFormat.dwEffects = CFE_UNDERLINE;
            CharFormat.yHeight = 12*20; // height in twips, where a twip is 1/20th of a point - for a pt.size of 18
            CharFormat.crTextColor = GetSysColor( COLOR_ACTIVECAPTION );
            strcpy( CharFormat.szFaceName, "Times New Roman" );
            SendDlgItemMessage(hDlg, IDC_UAEHOME, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
            SendDlgItemMessage(hDlg, IDC_UAEHOME, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );

            SetDlgItemText( hDlg, IDC_PICASSOHOME, "Picasso96" );
            SendDlgItemMessage(hDlg, IDC_PICASSOHOME, EM_GETCHARFORMAT,0,(LPARAM)&CharFormat);
            CharFormat.dwMask |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
            CharFormat.dwEffects = CFE_UNDERLINE;
            CharFormat.yHeight = 12*20; // height in twips, where a twip is 1/20th of a point - for a pt.size of 18
            CharFormat.crTextColor = GetSysColor( COLOR_ACTIVECAPTION );
            strcpy( CharFormat.szFaceName, "Times New Roman" );
            SendDlgItemMessage(hDlg, IDC_PICASSOHOME, EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&CharFormat);
            SendDlgItemMessage(hDlg, IDC_PICASSOHOME, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) );
            break;

        case WM_COMMAND:
            if( wParam == IDC_CONTRIBUTORS )
            {
                DisplayContributors( hDlg );
            }
            break;
        case WM_LBUTTONDOWN:
            point.x = LOWORD( lParam );
            point.y = HIWORD( lParam );
            GetWindowRect( GetDlgItem( hDlg, IDC_CLOANTOHOME ), &rect );
            ScreenToClient( hDlg, (POINT *)&rect );
            ScreenToClient( hDlg, (POINT *)&(rect.right) );
            if( PtInRect( &rect, point ) )
            {
                ShellExecute( NULL, NULL, "http://www.cloanto.com/amiga/forever", NULL, NULL, SW_SHOWNORMAL );
                break;
            }
            
            GetWindowRect( GetDlgItem( hDlg, IDC_AMIGAHOME ), &rect );
            ScreenToClient( hDlg, (POINT *)&rect );
            ScreenToClient( hDlg, (POINT *)&(rect.right) );
            if( PtInRect( &rect, point ) )
            {
                ShellExecute( NULL, NULL, "http://www.amiga.de", NULL, NULL, SW_SHOWNORMAL );
                break;
            }

            GetWindowRect( GetDlgItem( hDlg, IDC_PICASSOHOME ), &rect );
            ScreenToClient( hDlg, (POINT *)&rect );
            ScreenToClient( hDlg, (POINT *)&(rect.right) );
            if( PtInRect( &rect, point ) )
            {
                ShellExecute( NULL, NULL, "http://www.villagetronic.com/amiga/support/ftp96.html", NULL, NULL, SW_SHOWNORMAL );
                break;
            }
            
            GetWindowRect( GetDlgItem( hDlg, IDC_UAEHOME ), &rect );
            ScreenToClient( hDlg, (POINT *)&rect );
            ScreenToClient( hDlg, (POINT *)&(rect.right) );
            if( PtInRect( &rect, point ) )
            {
                ShellExecute( NULL, NULL, "http://www.freiburg.linux.de/~uae/", NULL, NULL, SW_SHOWNORMAL );
                break;
            }

            GetWindowRect( GetDlgItem( hDlg, IDC_WINUAEHOME ), &rect );
            ScreenToClient( hDlg, (POINT *)&rect );
            ScreenToClient( hDlg, (POINT *)&(rect.right) );
            if( PtInRect( &rect, point ) )
            {
                ShellExecute( NULL, NULL, "http://www.informatik.tu-muenchen.de/~ortmann/uae/", NULL, NULL, SW_SHOWNORMAL );
                break;
            }

            break;
        case WM_NOTIFY:
            switch(((NMHDR *)lParam)->code)
            {
            case PSN_RESET:
                if(allow_quit_from_property_sheet)
                {
                    quit_program = 1;
                    regs.spcflags |= SPCFLAG_BRK;
                }
                break;
            }
            break;
    }

    return(FALSE);
}

// Handle messages for the Display Settings page of our property-sheet
BOOL CALLBACK DisplayDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int i, item_height;
    RECT rect;
    LONG posn;
    ConfigStructPtr cfgptr;

    switch(msg)
    {
        case PSM_QUERYSIBLINGS:
            cfgptr = (ConfigStructPtr)lParam;
            cfgptr->FrameRate     = SendDlgItemMessage(hDlg,IDC_FRAMERATE,TBM_GETPOS,0,0);
            cfgptr->LineDouble    = IsDlgButtonChecked(hDlg, IDC_LINEDBL);
            cfgptr->FullScreen    = IsDlgButtonChecked( hDlg, IDC_FULLSCREEN );
            cfgptr->CorrectAspect = IsDlgButtonChecked(hDlg, IDC_ASPECT);
            cfgptr->CenterY       = IsDlgButtonChecked(hDlg, IDC_YCENTER);
            cfgptr->CenterX       = IsDlgButtonChecked(hDlg, IDC_XCENTER);
            cfgptr->Blit32        = IsDlgButtonChecked(hDlg, IDC_BLIT32);
            cfgptr->BlitImmediate = IsDlgButtonChecked(hDlg, IDC_BLITIMM);
            cfgptr->Lores         = IsDlgButtonChecked(hDlg, IDC_LORES);
            cfgptr->CustomSize    = 0;
            if( IsDlgButtonChecked( hDlg, IDC_CUSTOMSIZE ) && !cfgptr->FullScreen )
            {
                cfgptr->ScreenWidth  = SendDlgItemMessage( hDlg, IDC_XSPIN, UDM_GETPOS, 0, 0 );
                cfgptr->ScreenHeight = SendDlgItemMessage( hDlg, IDC_YSPIN, UDM_GETPOS, 0, 0 );
                cfgptr->CustomSize = 1;
            }
            else if( ( posn = SendDlgItemMessage( hDlg, IDC_RESOLUTION, CB_GETCURSEL, 0, 0 ) ) != CB_ERR )
            {
                posn = GetDisplayModeNumber( posn );
                cfgptr->ScreenWidth  = DisplayModes[posn].res.width;
                cfgptr->ScreenHeight = DisplayModes[posn].res.height;
            }
            else
            {
                cfgptr->ScreenWidth  = currprefs.gfx_width;
                cfgptr->ScreenHeight = currprefs.gfx_height;
            }
            break;

        case WM_INITDIALOG:
            pages[ DISPLAY_ID ] = hDlg;
        case WM_USER:
            SendDlgItemMessage(hDlg, IDC_FRAMERATE, TBM_SETRANGE,TRUE,MAKELONG(MIN_REFRESH_RATE,MAX_REFRESH_RATE));
            SendDlgItemMessage(hDlg,IDC_FRAMERATE,TBM_SETPOS,TRUE,currprefs.framerate);
            SendDlgItemMessage( hDlg, IDC_FRAMERATE, TBM_SETPAGESIZE, 0, 1 );
            UpdateScroller(hDlg, GetDlgItem(hDlg, IDC_FRAMERATE) );
            CheckDlgButton(hDlg, IDC_LINEDBL, currprefs.gfx_linedbl);
            CheckDlgButton( hDlg, IDC_FULLSCREEN, amiga_fullscreen );
            CheckDlgButton( hDlg, IDC_CUSTOMSIZE, customsize );

            if( pages[ STARTUP_ID ] && wParam == 1 )
            {
                if( SendDlgItemMessage( pages[ STARTUP_ID ], IDC_P96MEM, TBM_GETPOS, 0, 0 ) > 0 )
                {
                    CheckDlgButton( hDlg, IDC_FULLSCREEN, TRUE );
                    EnableWindow( GetDlgItem( hDlg, IDC_FULLSCREEN ), FALSE );
                }
                else
                {
                    EnableWindow( GetDlgItem( hDlg, IDC_FULLSCREEN ), TRUE );
                }
            }
            else
            {
                if( gfxmem_size )
                {
                    CheckDlgButton( hDlg, IDC_FULLSCREEN, TRUE );
                    EnableWindow( GetDlgItem( hDlg, IDC_FULLSCREEN ), FALSE );
                }
            }

            SendDlgItemMessage(hDlg,IDC_RESOLUTION,CB_RESETCONTENT,0,0);
            for(i=0;i<mode_count;i++)
            {
                if( DisplayModes[i].depth == 2 &&
                    DisplayModes[i].res.width <= 800 &&
                    DisplayModes[i].res.height <= 600 )
                    SendDlgItemMessage(hDlg,IDC_RESOLUTION,CB_ADDSTRING,0,(LPARAM)(LPCTSTR)DisplayModes[i].name);
            }

            SendDlgItemMessage( hDlg, IDC_XSPIN, UDM_SETRANGE, 0, MAKELONG( 800, 320 ) );
            SendDlgItemMessage( hDlg, IDC_XSPIN, UDM_SETPOS, TRUE, currprefs.gfx_width );
            SendDlgItemMessage( hDlg, IDC_YSPIN, UDM_SETRANGE, 0, MAKELONG( 600, 200 ) );
            SendDlgItemMessage( hDlg, IDC_YSPIN, UDM_SETPOS, TRUE, currprefs.gfx_height );

            CheckDlgButton( hDlg, IDC_CUSTOMSIZE, customsize );

            if( !amiga_fullscreen )
            {
                EnableWindow( GetDlgItem( hDlg, IDC_CUSTOMSIZE ), TRUE );
                EnableWindow( GetDlgItem( hDlg, IDC_XSPIN ), TRUE );
                EnableWindow( GetDlgItem( hDlg, IDC_YSPIN ), TRUE );
                SendDlgItemMessage( hDlg, IDC_RESOLUTION, CB_SETCURSEL, GetDisplayModeLogicalNumber( 800, 600 ), 0 );
            }
            else
            {
                EnableWindow( GetDlgItem( hDlg, IDC_CUSTOMSIZE ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_XSPIN ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_YSPIN ), FALSE );
                SendDlgItemMessage( hDlg, IDC_RESOLUTION, CB_SETCURSEL, GetDisplayModeLogicalNumber( currprefs.gfx_width, currprefs.gfx_height ), 0 );
            }

            // Retrieve the height, in pixels, of a list item.
            item_height = SendDlgItemMessage(hDlg,IDC_RESOLUTION,CB_GETITEMHEIGHT,0,0);
            if( item_height != CB_ERR )
            {
                // Get actual box position and size
                GetWindowRect( GetDlgItem(hDlg,IDC_RESOLUTION), &rect);
                rect.bottom = rect.top + item_height*mode_count + SendDlgItemMessage(hDlg,IDC_RESOLUTION,CB_GETITEMHEIGHT,(WPARAM)-1,0) + item_height;
                SetWindowPos( GetDlgItem(hDlg,IDC_RESOLUTION), 0, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE|SWP_NOZORDER);
            }

            CheckDlgButton(hDlg, IDC_ASPECT, currprefs.gfx_correct_aspect);
            CheckDlgButton(hDlg, IDC_YCENTER, currprefs.gfx_ycenter);
            CheckDlgButton(hDlg, IDC_XCENTER, currprefs.gfx_xcenter);
            CheckDlgButton(hDlg, IDC_BLIT32, currprefs.blits_32bit_enabled);
            CheckDlgButton(hDlg, IDC_BLITIMM, currprefs.immediate_blits);
            CheckDlgButton(hDlg, IDC_LORES, currprefs.gfx_lores);
            if(hEmuThread)
            {   // Disable certain controls which are only to be set once at start-up...
                EnableWindow(GetDlgItem(hDlg, IDC_LORES), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_XCENTER), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_YCENTER), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_ASPECT), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_LINEDBL), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_RESOLUTION), FALSE);
                EnableWindow( GetDlgItem( hDlg, IDC_CUSTOMSIZE ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_XSPIN ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_YSPIN ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_XSIZE ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_YSIZE ), FALSE );
            }
            break;
        case WM_HSCROLL:
            UpdateScroller( hDlg, (HWND)lParam );
            break;
        case WM_COMMAND:
            if( LOWORD(wParam) == IDC_FULLSCREEN )
            {
                if( hEmuThread == NULL )
                {
                    EnableWindow( GetDlgItem( hDlg, IDC_CUSTOMSIZE ), !IsDlgButtonChecked( hDlg, IDC_FULLSCREEN ) );
                    EnableWindow( GetDlgItem( hDlg, IDC_XSPIN ), !IsDlgButtonChecked( hDlg, IDC_FULLSCREEN ) );
                    EnableWindow( GetDlgItem( hDlg, IDC_YSPIN ), !IsDlgButtonChecked( hDlg, IDC_FULLSCREEN ) );
                    EnableWindow( GetDlgItem( hDlg, IDC_XSIZE ), !IsDlgButtonChecked( hDlg, IDC_FULLSCREEN ) );
                    EnableWindow( GetDlgItem( hDlg, IDC_YSIZE ), !IsDlgButtonChecked( hDlg, IDC_FULLSCREEN ) );
                }
            }
            break;

        case WM_NOTIFY:
            switch(((NMHDR *)lParam)->code)
            {
                case PSN_APPLY:
                    if( amiga_fullscreen != IsDlgButtonChecked( hDlg, IDC_FULLSCREEN ) )
                    {
                        if( hEmuThread )
                            new_fullscreen_flag = 1;
                        else
                            amiga_fullscreen = IsDlgButtonChecked( hDlg, IDC_FULLSCREEN );
                    }

                    customsize = IsDlgButtonChecked( hDlg, IDC_CUSTOMSIZE );
                    if( customsize && !amiga_fullscreen )
                    {
                        currprefs.gfx_width  = SendDlgItemMessage( hDlg, IDC_XSPIN, UDM_GETPOS, 0, 0 );
                        currprefs.gfx_height = SendDlgItemMessage( hDlg, IDC_YSPIN, UDM_GETPOS, 0, 0 );
                    }
                    else if( ( posn = SendDlgItemMessage( hDlg, IDC_RESOLUTION, CB_GETCURSEL, 0, 0 ) ) != CB_ERR )
                    {
                        posn = GetDisplayModeNumber( posn );
                        currprefs.gfx_width           = DisplayModes[posn].res.width;
                        currprefs.gfx_height          = DisplayModes[posn].res.height;
                    }
                    currprefs.gfx_ycenter         = IsDlgButtonChecked(hDlg, IDC_YCENTER);
                    currprefs.gfx_xcenter         = IsDlgButtonChecked(hDlg, IDC_XCENTER);
                    currprefs.blits_32bit_enabled = IsDlgButtonChecked(hDlg, IDC_BLIT32);
                    currprefs.immediate_blits     = IsDlgButtonChecked(hDlg, IDC_BLITIMM);
                    currprefs.gfx_lores           = IsDlgButtonChecked(hDlg, IDC_LORES);
                    currprefs.framerate           = SendDlgItemMessage(hDlg,IDC_FRAMERATE,TBM_GETPOS,0,0);
                    currprefs.gfx_correct_aspect  = IsDlgButtonChecked(hDlg, IDC_ASPECT);
                    currprefs.gfx_linedbl         = IsDlgButtonChecked(hDlg, IDC_LINEDBL);
                    break;

                case PSN_RESET:
                    if(allow_quit_from_property_sheet)
                    {
                        quit_program = 1;
                        regs.spcflags |= SPCFLAG_BRK;
                    }
                    break;
            }
    }
    return(FALSE);
}

BOOL CALLBACK StartupDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ConfigStructPtr cfgptr;
    LONG mem_size;

    switch(msg)
    {
        case PSM_QUERYSIBLINGS:
            cfgptr = (ConfigStructPtr)lParam;

            CalculateRealMem( SendMessage(GetDlgItem(hDlg, IDC_CHIPMEM),TBM_GETPOS,0,0), &cfgptr->ChipMem, NULL, 0 );
            CalculateRealMem( SendMessage(GetDlgItem(hDlg, IDC_FASTMEM),TBM_GETPOS,0,0), &cfgptr->FastMem, NULL, 1 );
#if CPU_LEVEL > 1
            CalculateRealMem( SendMessage(GetDlgItem(hDlg, IDC_P96MEM), TBM_GETPOS, 0, 0 ), &cfgptr->P96Mem, NULL, 2 );
            CalculateRealMem( SendMessage(GetDlgItem(hDlg, IDC_Z3FASTMEM), TBM_GETPOS, 0, 0 ), &cfgptr->Z3Mem, NULL, 3 );
#endif
            GetDlgItemText( hDlg, IDC_ROMFILE, cfgptr->KickstartName, CFG_ROM_LENGTH );
            GetDlgItemText( hDlg, IDC_KEYFILE, cfgptr->KeyFileName, CFG_KEY_LENGTH );
            break;

        case WM_INITDIALOG:
            pages[ STARTUP_ID ] = hDlg;
        case WM_USER:
            SetDlgItemText( hDlg, IDC_ROMFILE, romfile );
            SetDlgItemText( hDlg, IDC_KEYFILE, keyfile );
            SendDlgItemMessage(hDlg, IDC_CHIPMEM, TBM_SETRANGE,TRUE,MAKELONG(MIN_CHIP_MEM,MAX_CHIP_MEM));
            SendDlgItemMessage(hDlg, IDC_FASTMEM, TBM_SETRANGE,TRUE,MAKELONG(MIN_FAST_MEM,MAX_FAST_MEM));
#if CPU_LEVEL > 1
            SendDlgItemMessage(hDlg, IDC_P96MEM, TBM_SETRANGE,TRUE,MAKELONG(MIN_P96_MEM,MAX_P96_MEM));
            SendDlgItemMessage(hDlg, IDC_Z3FASTMEM, TBM_SETRANGE, TRUE,MAKELONG(MIN_Z3_MEM, MAX_Z3_MEM ));
#else
            EnableWindow( GetDlgItem( hDlg, IDC_P96MEM ), FALSE );
            EnableWindow( GetDlgItem( hDlg, IDC_Z3FASTMEM ), FALSE );
#endif
            switch( chipmem_size )
            {
            case 0x00080000:
                mem_size = 0;
                break;
            case 0x00100000:
                mem_size = 1;
                break;
            case 0x00200000:
                mem_size = 2;
                break;
            case 0x00400000:
                mem_size = 3;
                break;
            case 0x00800000:
                mem_size = 4;
                break;
            }
            SendDlgItemMessage(hDlg, IDC_CHIPMEM, TBM_SETPOS, TRUE, mem_size );

            switch( fastmem_size )
            {
            case 0x00000000:
                mem_size = 0;
                break;
            case 0x00100000:
                mem_size = 1;
                break;
            case 0x00200000:
                mem_size = 2;
                break;
            case 0x00400000:
                mem_size = 3;
                break;
            case 0x00800000:
                mem_size = 4;
                break;
            case 0x01000000:
                mem_size = 5;
                break;
            }
            SendDlgItemMessage(hDlg, IDC_FASTMEM, TBM_SETPOS, TRUE, mem_size );

            switch( gfxmem_size )
            {
            case 0x00000000:
                mem_size = 0;
                break;
            case 0x00100000:
                mem_size = 1;
                break;
            case 0x00200000:
                mem_size = 2;
                break;
            case 0x00400000:
                mem_size = 3;
                break;
            case 0x00800000:
                mem_size = 4;
                break;
            }
            SendDlgItemMessage(hDlg, IDC_P96MEM, TBM_SETPOS, TRUE, mem_size );
#if CPU_LEVEL > 1
            switch( z3fastmem_size )
            {
            case 0x00000000:
                mem_size = 0;
                break;
            case 0x00100000:
                mem_size = 1;
                break;
            case 0x00200000:
                mem_size = 2;
                break;
            case 0x00400000:
                mem_size = 3;
                break;
            case 0x00800000:
                mem_size = 4;
                break;
            case 0x01000000:
                mem_size = 5;
                break;
            case 0x02000000:
                mem_size = 6;
                break;
            }
            SendDlgItemMessage(hDlg, IDC_Z3FASTMEM, TBM_SETPOS, TRUE, mem_size );
            UpdateScroller(hDlg, GetDlgItem(hDlg, IDC_P96MEM));
            UpdateScroller(hDlg, GetDlgItem(hDlg, IDC_Z3FASTMEM));
#endif
            UpdateScroller(hDlg, GetDlgItem(hDlg, IDC_CHIPMEM));
            UpdateScroller(hDlg, GetDlgItem(hDlg, IDC_FASTMEM));

            return(TRUE);

    case WM_COMMAND:
        switch( wParam )
        {
        case IDC_KICKCHOOSER:
            KickStuffSelection( hDlg, 1 );
            break;
        case IDC_KEYCHOOSER:
            KickStuffSelection( hDlg, 0 );
            break;
        }
        break;

    case WM_HSCROLL:
        UpdateScroller(hDlg, (HWND)lParam);
#if CPU_LEVEL > 1
        if( ( GetDlgItem( hDlg, IDC_P96MEM) == lParam ) && pages[ DISPLAY_ID ] )
        {
            SendMessage( pages[ DISPLAY_ID ], WM_USER, 1, 0 );
        }
#endif
        break;

    case WM_NOTIFY:
        switch(((NMHDR *)lParam)->code)
        {
            case PSN_APPLY:
                // OK or Apply has been pressed
                GetDlgItemText( hDlg, IDC_ROMFILE, romfile, CFG_ROM_LENGTH );
                GetDlgItemText( hDlg, IDC_KEYFILE, keyfile, CFG_KEY_LENGTH );
                break;
            case PSN_RESET:
                if(allow_quit_from_property_sheet)
                {
                    quit_program = 1;
                    regs.spcflags |= SPCFLAG_BRK;
                }
                break;
        }
    }
    return(FALSE);
}

BOOL CALLBACK AdvancedDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ConfigStructPtr cfgptr;
    DWORD dwPos;

    switch(msg)
    {
        case PSM_QUERYSIBLINGS:
            cfgptr = (ConfigStructPtr)lParam;

            cfgptr->NoAutoConfig         = IsDlgButtonChecked( hDlg, IDC_NOAUTOCONFIG );
            cfgptr->CopperHack           = (int)(short)(SendDlgItemMessage( hDlg, IDC_COPPERSPIN, UDM_GETPOS, 0, 0 ));
            cfgptr->DebugLogging         = IsDlgButtonChecked( hDlg, IDC_LOGGING );
            cfgptr->InvalidAddresses     = IsDlgButtonChecked( hDlg, IDC_ILLEGAL);
//            cfgptr->DebuggerEnabled      = IsDlgButtonChecked( hDlg, IDC_DEBUGGER );
#if CPU_LEVEL > 1
            cfgptr->AddressingIs24Bit    = IsDlgButtonChecked( hDlg, IDC_24BIT );
#endif
            cfgptr->M68KSpeed = (char)SendDlgItemMessage(hDlg, IDC_SPEED,TBM_GETPOS,0,0);
            break;

        case WM_INITDIALOG:
            pages[ ADVANCED_ID ] = hDlg;
        case WM_USER:
#if CPU_LEVEL > 1
            CheckDlgButton( hDlg, IDC_24BIT, address_space_24 );
#else
            CheckDlgButton( hDlg, IDC_24BIT, TRUE );
            EnableWindow( GetDlgItem( hDlg, IDC_24BIT ), FALSE );
#endif
            //CheckDlgButton( hDlg, IDC_DEBUGGER, use_debugger );
            CheckDlgButton( hDlg, IDC_ILLEGAL, currprefs.illegal_mem );
            CheckDlgButton( hDlg, IDC_NOAUTOCONFIG, !currprefs.automount_uaedev );
            SendDlgItemMessage(hDlg, IDC_SPEED, TBM_SETRANGE, TRUE, MAKELONG(MIN_M68K_PRIORITY,MAX_M68K_PRIORITY));
            SendDlgItemMessage(hDlg, IDC_SPEED, TBM_SETPOS, TRUE, currprefs.m68k_speed );
            SendDlgItemMessage( hDlg, IDC_SPEED, TBM_SETPAGESIZE, 0, 1 );
            UpdateScroller(hDlg, GetDlgItem(hDlg, IDC_SPEED));
            SendDlgItemMessage( hDlg, IDC_COPPERSPIN, UDM_SETRANGE, 0, MAKELONG( 224, -1 ) );
            SendDlgItemMessage( hDlg, IDC_COPPERSPIN, UDM_SETPOS, TRUE, currprefs.copper_pos );

            return(TRUE);

    case WM_HSCROLL:
        UpdateScroller(hDlg, (HWND)lParam);
        break;

    case WM_NOTIFY:
        switch(((NMHDR *)lParam)->code)
        {
            case PSN_APPLY:
                // OK or Apply has been pressed
                currprefs.illegal_mem      = IsDlgButtonChecked( hDlg, IDC_ILLEGAL);
                currprefs.automount_uaedev = !IsDlgButtonChecked( hDlg, IDC_NOAUTOCONFIG );
                currprefs.m68k_speed       = SendMessage( GetDlgItem( hDlg, IDC_SPEED),TBM_GETPOS,0,0);
                currprefs.copper_pos       = (int)(short)( SendDlgItemMessage( hDlg, IDC_COPPERSPIN, UDM_GETPOS, 0, 0 ));
#if CPU_LEVEL > 1
                address_space_24           = IsDlgButtonChecked( hDlg, IDC_24BIT );
#endif
                break;
            case PSN_RESET:
                if(allow_quit_from_property_sheet)
                {
                    quit_program = 1;
                    regs.spcflags |= SPCFLAG_BRK;
                }
                break;
        }
    }
    return(FALSE);
}

BOOL CALLBACK SoundDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ConfigStructPtr cfgptr;

    switch(msg)
    {
    case PSM_QUERYSIBLINGS:
        cfgptr = (ConfigStructPtr)lParam;
        if( IsDlgButtonChecked( hDlg, IDC_8BIT ) )
            cfgptr->SoundBits = 8;
        if( IsDlgButtonChecked( hDlg, IDC_16BIT ) )
            cfgptr->SoundBits = 16;
        if( IsDlgButtonChecked( hDlg, IDC_11KHZ ) )
            cfgptr->SoundFreq = 11025;
        if( IsDlgButtonChecked( hDlg, IDC_22KHZ ) )
            cfgptr->SoundFreq = 22050;
        if( IsDlgButtonChecked( hDlg, IDC_44KHZ ) )
            cfgptr->SoundFreq = 44100;
        if( IsDlgButtonChecked( hDlg, IDC_48KHZ ) )
            cfgptr->SoundFreq = 48000;
        if( IsDlgButtonChecked( hDlg, IDC_SOUND0 ) )
            cfgptr->SoundSupport = -1;
        if( IsDlgButtonChecked( hDlg, IDC_SOUND1 ) )
            cfgptr->SoundSupport = 0;
        if( IsDlgButtonChecked( hDlg, IDC_SOUND2 ) )
            cfgptr->SoundSupport = 1;
        if( IsDlgButtonChecked( hDlg, IDC_SOUND3 ) )
            cfgptr->SoundSupport = 2;
        if( IsDlgButtonChecked( hDlg, IDC_SOUND4 ) )
            cfgptr->SoundSupport = 3;
        if( IsDlgButtonChecked( hDlg, IDC_STEREO ) )
            cfgptr->SoundStereo = 1;
        break;

    case WM_INITDIALOG:
        pages[ SOUND_ID ] = hDlg;
    case WM_USER:
        UpdateRadioButtons( hDlg, GetDlgItem( hDlg, IDC_SOUND ) );
        UpdateRadioButtons( hDlg, GetDlgItem( hDlg, IDC_FREQUENCY ) );
        UpdateRadioButtons( hDlg, GetDlgItem( hDlg, IDC_SOUNDSIZE ) );
        CheckDlgButton( hDlg, IDC_STEREO, currprefs.stereo );
        return(TRUE);

    case WM_NOTIFY:
        switch(((NMHDR *)lParam)->code)
        {
        case PSN_APPLY:
            // OK or Apply has been pressed
            if( IsDlgButtonChecked( hDlg, IDC_8BIT ) )
                currprefs.sound_bits = 8;
            if( IsDlgButtonChecked( hDlg, IDC_16BIT ) )
                currprefs.sound_bits = 16;
            if( IsDlgButtonChecked( hDlg, IDC_11KHZ ) )
                currprefs.sound_freq = 11025;
            if( IsDlgButtonChecked( hDlg, IDC_22KHZ ) )
                currprefs.sound_freq = 22050;
            if( IsDlgButtonChecked( hDlg, IDC_44KHZ ) )
                currprefs.sound_freq = 44100;
            if( IsDlgButtonChecked( hDlg, IDC_48KHZ ) )
                currprefs.sound_freq = 48000;
            if( IsDlgButtonChecked( hDlg, IDC_SOUND0 ) )
                currprefs.produce_sound = -1;
            if( IsDlgButtonChecked( hDlg, IDC_SOUND1 ) )
                currprefs.produce_sound = 0;
            if( IsDlgButtonChecked( hDlg, IDC_SOUND2 ) )
                currprefs.produce_sound = 1;
            if( IsDlgButtonChecked( hDlg, IDC_SOUND3 ) )
                currprefs.produce_sound = 2;
            if( IsDlgButtonChecked( hDlg, IDC_SOUND4 ) )
                currprefs.produce_sound = 3;
            if( IsDlgButtonChecked( hDlg, IDC_STEREO ) )
                currprefs.stereo = 1;
            break;
        case PSN_RESET:
            if(allow_quit_from_property_sheet)
            {
                quit_program = 1;
                regs.spcflags |= SPCFLAG_BRK;
            }
            break;
        }
        break;
    }
    return(FALSE);
}

void KickStuffSelection( HWND hDlg, int which )
{
    OPENFILENAME openFileName;
    char full_path[MAX_PATH] = "";
    char file_name[MAX_PATH] = "";
    char init_path[MAX_PATH] = "";
    char *amiga_path = NULL;

    if( amiga_path = getenv( "AmigaPath" ) )
    {
        strncpy( init_path, amiga_path, MAX_PATH );
        strncat( init_path, "\\ROMs\\", MAX_PATH );
    }
    else if( start_path )
    {
        strncpy( init_path, start_path, MAX_PATH );
        strncat( init_path, "..\\ROMs\\", MAX_PATH );
    }

    openFileName.lStructSize       = sizeof(OPENFILENAME);
    openFileName.hwndOwner         = hDlg;
    openFileName.hInstance         = hInst;
    if( which )
    {
        openFileName.lpstrFilter   = "Amiga Kickstart Files (*.ROM)\0*.ROM\0\0";
        openFileName.lpstrTitle    = "Select an Amiga ROM file...";
        openFileName.lpstrDefExt   = "ROM";
    }
    else
    {
        openFileName.lpstrFilter   = "Amiga Kickstart Key-Files (*.KEY)\0*.KEY\0\0";
        openFileName.lpstrTitle    = "Select an Amiga Key-File...";
        openFileName.lpstrDefExt   = "KEY";
    }
    openFileName.Flags             = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    openFileName.lpstrCustomFilter = NULL;
    openFileName.nMaxCustFilter    = 0;
    openFileName.nFilterIndex      = 0;
    openFileName.lpstrFile         = full_path;
    openFileName.nMaxFile          = MAX_PATH;
    openFileName.lpstrFileTitle    = file_name;
    openFileName.nMaxFileTitle     = MAX_PATH;
    if( start_path )
        openFileName.lpstrInitialDir   = init_path;
    else
        openFileName.lpstrInitialDir   = NULL;
    openFileName.lpfnHook          = NULL;
    openFileName.lpTemplateName    = NULL;
    openFileName.lCustData         = 0;
    if(GetOpenFileName(&openFileName)==FALSE)
        MyOutputDebugString("GetOpenFileName() failed.\n");
    else
    {
        if( which )
            SetDlgItemText(hDlg,IDC_ROMFILE,full_path);
        else
            SetDlgItemText(hDlg,IDC_KEYFILE,full_path);
    }
}

void DiskSelection( HWND hDlg, WPARAM wParam, int flag )
{
    OPENFILENAME openFileName;
    char full_path[MAX_PATH] = "";
    char file_name[MAX_PATH] = "";
    char init_path[MAX_PATH] = "";
    BOOL result = FALSE;
    char *amiga_path = NULL;

    if( amiga_path = getenv( "AmigaPath" ) )
    {
        strncpy( init_path, amiga_path, MAX_PATH );
        strncat( init_path, "\\DiskFiles\\", MAX_PATH );
    }
    else if( start_path )
    {
        strncpy( init_path, start_path, MAX_PATH );
        strncat( init_path, "..\\DiskFiles\\", MAX_PATH );
    }

    openFileName.lStructSize       = sizeof(OPENFILENAME);
    openFileName.hwndOwner         = hDlg;
    openFileName.hInstance         = hInst;
    openFileName.lpstrFilter       = "Amiga Disk Files (*.ADF;*.ADZ)\0*.ADF;*.ADZ\0";
    if( flag )
        openFileName.lpstrTitle    = "Choose your blank Amiga Disk File...";
    else
        openFileName.lpstrTitle    = "Select an Amiga Disk File image...";
    openFileName.Flags             = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    openFileName.lpstrDefExt       = "ADF";
    openFileName.lpstrCustomFilter = NULL;
    openFileName.nMaxCustFilter    = 0;
    openFileName.nFilterIndex      = 0;
    openFileName.lpstrFile         = full_path;
    openFileName.nMaxFile          = MAX_PATH;
    openFileName.lpstrFileTitle    = file_name;
    openFileName.nMaxFileTitle     = MAX_PATH;
    if( start_path )
        openFileName.lpstrInitialDir   = init_path;
    else
        openFileName.lpstrInitialDir   = NULL;
    openFileName.lpfnHook          = NULL;
    openFileName.lpTemplateName    = NULL;
    openFileName.lCustData         = 0;
    if( flag )
        result = GetSaveFileName( &openFileName );
    else
        result = GetOpenFileName( &openFileName );

    if( result == FALSE )
        MyOutputDebugString("GetOpenFileName() failed.\n");
    else
    {
        switch(wParam)
        {
        case IDC_DF0:
            SetDlgItemText(hDlg,IDC_DF0TEXT,full_path);
            break;
        case IDC_DF1:
            SetDlgItemText(hDlg,IDC_DF1TEXT,full_path);
            break;
        case IDC_DF2:
            SetDlgItemText(hDlg,IDC_DF2TEXT,full_path);
            break;
        case IDC_DF3:
            SetDlgItemText(hDlg,IDC_DF3TEXT,full_path);
            break;
        case IDC_CREATE:
            CreateDiskFile( full_path );
            break;
        }
    }
}

void GetSetDriveValues( HWND hDlg, DWORD drive_id, BOOL set )
{
    int dlg_item1, dlg_item2, dlg_item3;

    switch(drive_id)
    {
    case 0:
        dlg_item1 = IDC_UAE0PATH;
        dlg_item2 = IDC_UAE0NAME;
        dlg_item3 = IDC_RW0;
        break;
    case 1:
        dlg_item1 = IDC_UAE1PATH;
        dlg_item2 = IDC_UAE1NAME;
        dlg_item3 = IDC_RW1;
        break;
    case 2:
        dlg_item1 = IDC_UAE2PATH;
        dlg_item2 = IDC_UAE2NAME;
        dlg_item3 = IDC_RW2;
        break;
    case 3:
        dlg_item1 = IDC_UAE3PATH;
        dlg_item2 = IDC_UAE3NAME;
        dlg_item3 = IDC_RW3;
        break;
    }
    if(set)
    {
        GetDlgItemText(hDlg, dlg_item1, drives[drive_id].path, MAX_PATH);
        GetDlgItemText(hDlg, dlg_item2, drives[drive_id].name, MAX_PATH);
        drives[drive_id].rw = IsDlgButtonChecked(hDlg, dlg_item3);
    }
    else
    {
        SetDlgItemText(hDlg, dlg_item1, drives[drive_id].path);
        SetDlgItemText(hDlg, dlg_item2, drives[drive_id].name);
        CheckDlgButton(hDlg, dlg_item3, drives[drive_id].rw);
    }
}

void CreateDiskFile( char *name )
{
    HANDLE adf;
    int i, file_size = 880 * 1024; // size in bytes
    char *chunk = NULL;
    DWORD count;

    SetCursor( LoadCursor( NULL, IDC_WAIT ) );
    adf = CreateFile( name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL );
    if( adf != INVALID_HANDLE_VALUE )
    {
        if( chunk = malloc( 10240 ) )
        {
            for( i = 0; i < file_size; i += 10240 )
            {
                WriteFile( adf, chunk, 10240, &count, NULL );
            }
        }
        CloseHandle( adf );
    }
    SetCursor( LoadCursor( NULL, IDC_ARROW ) );
}

long CreateHardFile( LONG hfsize )
{
    HANDLE hf;
    long result = 0;
    int i, file_size = hfsize * 1024 * 1024; // size in bytes
    char init_path[MAX_PATH] = "";
    char *chunk = NULL;
    DWORD count;

    SetCursor( LoadCursor( NULL, IDC_WAIT ) );

    if( start_path )
    {
        strncpy( init_path, start_path, MAX_PATH );
        strncat( init_path, "\\HardFile", MAX_PATH );
    }
    else
        strcpy( init_path, "HardFile" );

    if( file_size == 0 )
        DeleteFile( init_path );
    else
    {
        hf = CreateFile( init_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL );
        if( hf != INVALID_HANDLE_VALUE )
        {
            if( chunk = malloc( 16384 ) )
            {
                for( i = 0; i < file_size; i += 16384 )
                {
                    WriteFile( hf, chunk, 16384, &count, NULL );
                }
            }
            CloseHandle( hf );
            result = i;
        }
    }
    SetCursor( LoadCursor( NULL, IDC_ARROW ) );
    return result;
}

BOOL CALLBACK HarddiskDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    UDACCEL accel[2] = { { 1, 1 }, { 3, 5 } };
    char hfsize[9] = "";
    ConfigStructPtr cfgptr;
    int i, result = IDYES;
    static BROWSEINFO browse_info;
    char directory_path[MAX_PATH];
    LPITEMIDLIST browse;
    LONG setting;

    switch(msg)
    {
        case PSM_QUERYSIBLINGS:
            cfgptr = (ConfigStructPtr)lParam;
            for( i = 0; i < NUM_DRIVES; i++ )
            {
                GetSetDriveValues( hDlg, i, TRUE );
                memcpy( &(cfgptr->drives[i]), &drives[i], sizeof( drive_specs ) );
            }
            cfgptr->HardFileBootPri = IsDlgButtonChecked( hDlg, IDC_BOOTPRI );
            break;

        case WM_INITDIALOG:
            pages[ HARDDISK_ID ] = hDlg;
        case WM_USER:
            browse_info.hwndOwner = hDlg;
            browse_info.pidlRoot  = NULL;
            browse_info.pszDisplayName = directory_path;
            browse_info.lpszTitle = "Please select your file-system root directory...";
            browse_info.ulFlags   = BIF_DONTGOBELOWDOMAIN | BIF_RETURNONLYFSDIRS;
            browse_info.lpfn      = NULL;
            browse_info.iImage    = 0;
            for(i=0;i<NUM_DRIVES;i++)
                GetSetDriveValues(hDlg, i, FALSE); // FALSE indicates Getting

            CheckDlgButton(hDlg, IDC_BOOTPRI, hf_bootpri);
            SendDlgItemMessage( hDlg, IDC_HFSPIN, UDM_SETRANGE, 0, MAKELONG( 256, 0 ) );
            SendDlgItemMessage( hDlg, IDC_HFSPIN, UDM_SETPOS, 0, hardfile_size / 1048576 );

            if( hardfile_size == 0)
            {
                // hardfile does not exist, so disable the "Hardfile priority" check-box
                EnableWindow( GetDlgItem( hDlg, IDC_BOOTPRI ), FALSE );
            }
            break;

        case WM_COMMAND:
            switch(wParam)
            {
            case IDC_CREATEHF:
                setting = SendDlgItemMessage( hDlg, IDC_HFSPIN, UDM_GETPOS, 0, 0 );
                if( hardfile_size != 0 )
                {
                    result = MessageBox( hDlg, "You already have a hardfile.  Creating a new one will destroy the old one.  Proceed?", "HardFile Warning...", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_APPLMODAL | MB_SETFOREGROUND );
                }
                if( result == IDYES )
                {
                    hardfile_size = CreateHardFile( setting );
                    EnableWindow( GetDlgItem( hDlg, IDC_BOOTPRI ), hardfile_size ? TRUE:FALSE );
                }
                break;
            case IDC_UAE0:
            case IDC_UAE1:
            case IDC_UAE2:
            case IDC_UAE3:
                if((browse=SHBrowseForFolder(&browse_info))!=NULL)
                {
                    SHGetPathFromIDList( browse, directory_path );
                    if( directory_path[ strlen( directory_path ) - 1 ] == '\\' )
                        directory_path[ strlen( directory_path ) - 1 ] = 0;
                    if( wParam == IDC_UAE0 )
                        SetDlgItemText( hDlg, IDC_UAE0PATH, directory_path );
                    else if( wParam == IDC_UAE1 )
                        SetDlgItemText( hDlg, IDC_UAE1PATH, directory_path );
                    else if( wParam == IDC_UAE2 )
                        SetDlgItemText( hDlg, IDC_UAE2PATH, directory_path );
                    else
                        SetDlgItemText( hDlg, IDC_UAE3PATH, directory_path );
                }
                break;
            }
            break;

        case WM_NOTIFY:
            switch(((NMHDR *)lParam)->code)
            {
                case PSN_APPLY:
                    for(i=0;i<NUM_DRIVES;i++)
                        GetSetDriveValues( hDlg, i, TRUE); // TRUE indicates Setting
                    hf_bootpri = IsDlgButtonChecked( hDlg, IDC_BOOTPRI );
                    break;
                case PSN_RESET:
                    if(allow_quit_from_property_sheet)
                    {
                        quit_program = 1;
                        regs.spcflags |= SPCFLAG_BRK;
                    }
                    break;
            }
            return(TRUE);
        default:
            return(FALSE);
    }

    return(FALSE);
}

BOOL CALLBACK FloppiesDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ConfigStructPtr cfgptr;

    switch(msg)
    {
        case PSM_QUERYSIBLINGS:
            cfgptr = (ConfigStructPtr)lParam;
            GetDlgItemText(hDlg, IDC_DF0TEXT, cfgptr->df[0], MAX_PATH);
            GetDlgItemText(hDlg, IDC_DF1TEXT, cfgptr->df[1], MAX_PATH);
            GetDlgItemText(hDlg, IDC_DF2TEXT, cfgptr->df[2], MAX_PATH);
            GetDlgItemText(hDlg, IDC_DF3TEXT, cfgptr->df[3], MAX_PATH);
            break;

        case WM_INITDIALOG:
            pages[ FLOPPY_ID ] = hDlg;
        case WM_USER:
            SetDlgItemText(hDlg, IDC_DF0TEXT, currprefs.df[0]);
            SetDlgItemText(hDlg, IDC_DF1TEXT, currprefs.df[1]);
            SetDlgItemText(hDlg, IDC_DF2TEXT, currprefs.df[2]);
            SetDlgItemText(hDlg, IDC_DF3TEXT, currprefs.df[3]);
            break;

        case WM_COMMAND:
            switch(wParam)
            {
            case IDC_DF0:
                DiskSelection(hDlg,wParam,0);
                break;
            case IDC_DF1:
                DiskSelection(hDlg,wParam,0);
                break;
            case IDC_DF2:
                DiskSelection(hDlg,wParam,0);
                break;
            case IDC_DF3:
                DiskSelection(hDlg,wParam,0);
                break;
            case IDC_EJECT0:
                SetDlgItemText( hDlg, IDC_DF0TEXT, "" );
                disk_eject( 0 );
                break;
            case IDC_EJECT1:
                SetDlgItemText( hDlg, IDC_DF1TEXT, "" );
                disk_eject( 1 );
                break;
            case IDC_EJECT2:
                SetDlgItemText( hDlg, IDC_DF2TEXT, "" );
                disk_eject( 2 );
                break;
            case IDC_EJECT3:
                SetDlgItemText( hDlg, IDC_DF3TEXT, "" );
                disk_eject( 3 );
                break;
            case IDC_CREATE:
                DiskSelection( hDlg, wParam, 1 );
                break;
            }
            break;

        case WM_NOTIFY:
            switch(((NMHDR *)lParam)->code)
            {
                case PSN_APPLY:
                    GetDlgItemText(hDlg, IDC_DF0TEXT, currprefs.df[0], MAX_PATH);
                    if( currprefs.df[0][0] )
                    {
                        disk_insert(0,currprefs.df[0]);
                    }
                    GetDlgItemText(hDlg, IDC_DF1TEXT, currprefs.df[1], MAX_PATH);
                    if( currprefs.df[1][0] )
                    {
                        disk_insert(1,currprefs.df[1]);
                    }
                    GetDlgItemText(hDlg, IDC_DF2TEXT, currprefs.df[2], MAX_PATH);
                    if( currprefs.df[2][0] )
                    {
                        disk_insert(2,currprefs.df[2]);
                    }
                    GetDlgItemText(hDlg, IDC_DF3TEXT, currprefs.df[3], MAX_PATH);
                    if( currprefs.df[3][0] )
                    {
                        disk_insert(3,currprefs.df[3]);
                    }
                    break;
                case PSN_RESET:
                    if(allow_quit_from_property_sheet)
                    {
                        quit_program = 1;
                        regs.spcflags |= SPCFLAG_BRK;
                    }
                    break;
            }
            return(TRUE);
        default:
            return(FALSE);
    }

    return(FALSE);
}

// Handle messages for the Joystick Settings page of our property-sheet
BOOL CALLBACK PortsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ConfigStructPtr cfgptr;
    char joyspec[3] = "MA";
    LONG ser_port, item_height;
    RECT rect;

    switch(msg)
    {
        case PSM_QUERYSIBLINGS:
            cfgptr = (ConfigStructPtr)lParam;
            if( IsDlgButtonChecked( hDlg, IDC_PORT0_JOY0 ) )
                joyspec[0] = '0';
            if( IsDlgButtonChecked( hDlg, IDC_PORT0_JOY1 ) )
                joyspec[0] = '1';
            if( IsDlgButtonChecked( hDlg, IDC_PORT0_MOUSE ) )
                joyspec[0] = 'M';
            if( IsDlgButtonChecked( hDlg, IDC_PORT0_KBDA ) )
                joyspec[0] = 'A';
            if( IsDlgButtonChecked( hDlg, IDC_PORT0_KBDB ) )
                joyspec[0] = 'B';
            if( IsDlgButtonChecked( hDlg, IDC_PORT0_KBDC ) )
                joyspec[0] = 'C';

            if( IsDlgButtonChecked( hDlg, IDC_PORT1_JOY0 ) )
                joyspec[1] = '0';
            if( IsDlgButtonChecked( hDlg, IDC_PORT1_JOY1 ) )
                joyspec[1] = '1';
            if( IsDlgButtonChecked( hDlg, IDC_PORT1_MOUSE ) )
                joyspec[1] = 'M';
            if( IsDlgButtonChecked( hDlg, IDC_PORT1_KBDA ) )
                joyspec[1] = 'A';
            if( IsDlgButtonChecked( hDlg, IDC_PORT1_KBDB ) )
                joyspec[1] = 'B';
            if( IsDlgButtonChecked( hDlg, IDC_PORT1_KBDC ) )
                joyspec[1] = 'C';

            cfgptr->FakeJoystick = parse_joy_spec( joyspec );

            GetDlgItemText( hDlg, IDC_PARALLEL, cfgptr->PrinterName, CFG_PAR_LENGTH );

            switch( ser_port = SendDlgItemMessage( hDlg, IDC_SERIAL, CB_GETCURSEL, 0, 0L) )
            {
            case 1:
            case 2:
            case 3:
            case 4:
                cfgptr->SerialPort = 1;
#ifdef __GNUC__
                sprintf( cfgptr->SerialName, "COM%d", ser_port );
#else
                _snprintf( cfgptr->SerialName, CFG_SER_LENGTH, "COM%d", ser_port );
#endif
                break;
            default:
                cfgptr->SerialPort = 0;
                break;
            }

            break;

        case WM_INITDIALOG:
            pages[ PORTS_ID ] = hDlg;
        case WM_USER:
            UpdateRadioButtons( hDlg, GetDlgItem( hDlg, IDC_PORT0 ) );
            UpdateRadioButtons( hDlg, GetDlgItem( hDlg, IDC_PORT1 ) );
            SetDlgItemText( hDlg, IDC_PARALLEL, prtname );
            SendDlgItemMessage( hDlg, IDC_SERIAL, CB_RESETCONTENT, 0, 0L);
            SendDlgItemMessage( hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)"None" );
            SendDlgItemMessage( hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)"COM1" );
            SendDlgItemMessage( hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)"COM2" );
            SendDlgItemMessage( hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)"COM3" );
            SendDlgItemMessage( hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)"COM4" );
            if( currprefs.use_serial == 0 )
                SendDlgItemMessage( hDlg, IDC_SERIAL, CB_SETCURSEL, 0, 0L );
            else
            {
                switch( sername[ strlen( sername ) - 1 ] )
                {
                case '2':
                    SendDlgItemMessage( hDlg, IDC_SERIAL, CB_SETCURSEL, 2, 0L );
                    break;
                case '3':
                    SendDlgItemMessage( hDlg, IDC_SERIAL, CB_SETCURSEL, 3, 0L );
                    break;
                case '4':
                    SendDlgItemMessage( hDlg, IDC_SERIAL, CB_SETCURSEL, 4, 0L );
                    break;
                default:
                    SendDlgItemMessage( hDlg, IDC_SERIAL, CB_SETCURSEL, 1, 0L );
                    break;
                }
            }

            // Retrieve the height, in pixels, of a list item.
            item_height = SendDlgItemMessage( hDlg, IDC_SERIAL, CB_GETITEMHEIGHT, 0, 0L);
            if (item_height != CB_ERR) 
            {
                // Get actual box position and size.
                GetWindowRect( GetDlgItem( hDlg, IDC_SERIAL ), &rect);
                rect.bottom = rect.top + item_height * 5
                                  + SendDlgItemMessage( hDlg, IDC_SERIAL, CB_GETITEMHEIGHT, (WPARAM)-1, 0L)
                                  + item_height;
                SetWindowPos( GetDlgItem( hDlg, IDC_SERIAL ), 0, 0, 0, rect.right - rect.left,
                             rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
            }
            if( hEmuThread )
            {
                EnableWindow( GetDlgItem( hDlg, IDC_SERIAL ), FALSE );
                EnableWindow( GetDlgItem( hDlg, IDC_PARALLEL ), FALSE );
            }
            return(TRUE);

        case WM_NOTIFY:
            switch(((NMHDR *)lParam)->code)
            {
            case PSN_APPLY:
                // OK or Apply has been pressed
                if( IsDlgButtonChecked( hDlg, IDC_PORT0_JOY0 ) )
                    joyspec[0] = '0';
                if( IsDlgButtonChecked( hDlg, IDC_PORT0_JOY1 ) )
                    joyspec[0] = '1';
                if( IsDlgButtonChecked( hDlg, IDC_PORT0_MOUSE ) )
                    joyspec[0] = 'M';
                if( IsDlgButtonChecked( hDlg, IDC_PORT0_KBDA ) )
                    joyspec[0] = 'A';
                if( IsDlgButtonChecked( hDlg, IDC_PORT0_KBDB ) )
                    joyspec[0] = 'B';
                if( IsDlgButtonChecked( hDlg, IDC_PORT0_KBDC ) )
                    joyspec[0] = 'C';

                if( IsDlgButtonChecked( hDlg, IDC_PORT1_JOY0 ) )
                    joyspec[1] = '0';
                if( IsDlgButtonChecked( hDlg, IDC_PORT1_JOY1 ) )
                    joyspec[1] = '1';
                if( IsDlgButtonChecked( hDlg, IDC_PORT1_MOUSE ) )
                    joyspec[1] = 'M';
                if( IsDlgButtonChecked( hDlg, IDC_PORT1_KBDA ) )
                    joyspec[1] = 'A';
                if( IsDlgButtonChecked( hDlg, IDC_PORT1_KBDB ) )
                    joyspec[1] = 'B';
                if( IsDlgButtonChecked( hDlg, IDC_PORT1_KBDC ) )
                    joyspec[1] = 'C';

                currprefs.fake_joystick = parse_joy_spec( joyspec );

                GetDlgItemText( hDlg, IDC_PARALLEL, prtname, CFG_PAR_LENGTH );

                switch( item_height = SendDlgItemMessage( hDlg, IDC_SERIAL, CB_GETCURSEL, 0, 0L) )
                {
                case 1:
                case 2:
                case 3:
                case 4:
                    currprefs.use_serial = 1;
#ifdef __GNUC__
                    sprintf( sername, "COM%d", item_height );
#else
                    _snprintf( sername, CFG_SER_LENGTH, "COM%d", item_height );
#endif
                    break;
                default:
                    currprefs.use_serial = 0;
                    break;
                }


                    break;
                case PSN_RESET:
                    if(allow_quit_from_property_sheet)
                    {
                        quit_program = 1;
                        regs.spcflags |= SPCFLAG_BRK;
                    }
                    break;
            }
            return(FALSE);
    }
    return(FALSE);
}

HWND AddStatusWindow(void)
{
    HWND hwndStatus; 
    RECT rcClient; 
    HLOCAL hloc; 
    LPINT lpParts; 
 
    // Create the status window. 
    hwndStatus = CreateWindowEx( 
        0,                       // no extended styles 
        STATUSCLASSNAME,         // name of status window class 
        (LPCTSTR) NULL,          // no text when first created 
        WS_CHILD | WS_VISIBLE,   // creates a child window 
        0, 0, 0, 0,              // ignores size and position 
        hMainWnd,                // handle to parent window 
        (HMENU) 0,               // child window identifier 
        hInst,                   // handle to application instance 
        NULL);                   // no window creation data 
 
    // Get the coordinates of the parent window's client area. 
    GetClientRect(hMainWnd, &rcClient); 
 
    // Allocate an array for holding the right edge coordinates. 
    hloc = LocalAlloc(LHND, sizeof(int) * 6); // six parts for status-window
    lpParts = LocalLock(hloc); 
 
    // Calculate the right edge coordinate for each part, and 
    // copy the coordinates to the array. 
    lpParts[0] = rcClient.right - 120;
    lpParts[1] = rcClient.right - 105;
    lpParts[2] = rcClient.right - 90;
    lpParts[3] = rcClient.right - 75;
    lpParts[4] = rcClient.right - 60;
    lpParts[5] = rcClient.right;
 
    // Tell the status window to create the six window parts. 
    SendMessage(hwndStatus, SB_SETPARTS, (WPARAM) 6, 
        (LPARAM) lpParts); 
 
    // Free the array, and return. 
    LocalUnlock(hloc); 
    LocalFree(hloc); 
    return hwndStatus; 
} 

/*
void SetStatusText(char *text, int part)
{
    if(status_window)
    {
        PostMessage(status_window, SB_SETTEXT, (WPARAM) part, (LPARAM) text);
    }
}
*/

int gui_init (void)
{
#ifndef _WIN32
    if (!GetSettings ()) {
	MyOutputDebugString ("GetSettings() trying to quit...\n");
	return 0;
    }
    fprintf (stderr, "%lx %lx\n", gfxmem_size, z3fastmem_size);
#endif
    return 1;
}

int gui_update (void)
{
}

void gui_exit (void)
{
}

void gui_led (int led, int on)
{

}

void gui_filename (int num, const char *name)
{
}

LONG GetDisplayModeLogicalNumber( int x, int y )
{
    LONG modenum = 0;
    int i;
    for( i = 0; i < mode_count; i++ )
    {
        if( DisplayModes[i].depth == 2 )
        {
            if( ( DisplayModes[i].res.width == x ) &&
                ( DisplayModes[i].res.height == y ) )
                return modenum;
            modenum++;
        }
    }
    return 0;
}

LONG GetDisplayModeNumber( LONG logical_number )
{
    LONG modenum = 0;
    int i;
    for( i = 0; i < mode_count; i++ )
    {
        if( DisplayModes[i].depth == 2 && DisplayModes[i].res.width <= 800 && DisplayModes[i].res.height <= 600)
        {
            if( modenum == logical_number )
                return i;
            modenum++;
        }
    }
    return 0;
}
