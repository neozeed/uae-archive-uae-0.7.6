/* UAEGUI.rexx - A nice GUI for uae using MUIREXX
 *
 * How to use it: Open a shell a type "run rx UAEGUI.rexx".
 * Then, run UAE. Once uae's window is opened, the GUI will
 * popup.
 *
 * (c) By Samuel Devulder, 01/97.
 */

TRUE = 1
FALSE = 0
MUIA_Application_Title = 0x804281b8
MUIA_Application_Version = 0x8042b33f
MUIA_Application_Copyright = 0x8042ef4d
MUIA_Application_Author = 0x80424842
MUIA_Application_Base = 0x8042e07a
MUIA_Application_OpenConfigWindow = 0x804299ba
MUIA_Application_AboutMUI = 0x8042d21d
MUIA_Background = 0x8042545b
MUII_BACKGROUND = 128
MUII_SHADOWFILL = 133
MUIA_Image_FreeHoriz = 0x8042da84
MUIA_Image_FontMatchWidth = 0x804239bf
MUIA_Image_FontMatchHeight = 0x804239c0
MUIA_Width = 0x8042b59c
MUIA_Image_FontMatch = 0x8042815d
MUIA_Image_FontMatchHeight = 0x80429f26
MUIA_FixWidthTxt = 0x8042d044
MUIA_Weight = 0x80421d1f
MUIA_Pressed = 0x80423535
MUIA_ShowMe = 0x80429ba8
MUIA_Slider_Horiz = 0x8042fad1
MUIA_Slider_Min = 0x8042e404
MUIA_Slider_Max = 0x8042d78a
MUIA_Slider_Level = 0x8042ae3a
MUIA_Frame = 0x8042ac64
MUIV_Frame_Text = 3
MUIV_Frame_Group = 9

Cycle_Active = 0x80421788
Menuitem_Shortcut = 0x80422030
Menuitem_Title = 0x804218be
Selected = 0x8042654b
Disabled = 0x80423661;

BLK = "\033I[2:00000000,00000000,00000000]"
RED = "\033I[2:ffffffff,00000000,00000000]"
GRN = "\033I[2:00000000,ffffffff,6f000000]"

BLK = BLK||BLK||BLK||BLK
RED = RED||RED||RED||RED
GRN = GRN||GRN||GRN||GRN

options results

/* Run MUIREXX */
if ~show('p','UAEGUI') then do
   address command "run >nil: muirexx port UAEGUI"
   address command "run >nil: muirexx:muirexx port UAEGUI"
   do while ~show('p','UAEGUI')
      address command "wait 1"
   end
end

/* wait for UAE to setup */
if ~show('p','UAE') then do
/*   address command "run <>con: uae" */
   do while ~show('p','UAE')
      address command "wait 1"
   end 
end

address UAEGUI

window ID MAIN COMMAND """QUIT""" PORT UAEGUI TITLE """UAE Gui"""

 menu LABEL '"Project"'
  item COMMAND '"method 'MUIA_Application_AboutMUI' 0"',
   PORT UAEGUI LABEL '"About MUI"'
  menu LABEL '"Settings"'
   item COMMAND '"method 'MUIA_Application_OpenConfigWindow'"',
    PORT UAEGUI LABEL '"MUI..."'
  endmenu
  item ATTRS Menuitem_Title '-1'
  item COMMAND '"quit"' PORT UAEGUI ATTRS Menuitem_Shortcut 'Q' LABEL '"Quit"'
 endmenu

 address UAE VERSION NUM;    NUM     = RESULT
 address UAE VERSION AUTHOR; AUTHOR  = RESULT
 address UAE VERSION PORT;   PORTAUT = RESULT

 text ATTRS MUIA_Frame MUIV_Frame_Group MUIA_Background MUII_SHADOWFILL LABEL,
   "\0338\033c\033bUAE v"||NUM%100||"."||(NUM%10)//10||"."||NUM//10||,
   "\033n - Un*x Amiga Emulator\n"||AUTHOR||"\n\n"||PORTAUT 

 group LABELS """Actions""" FRAME HORIZ
  button PRESS HELP """This button makes UAE and the GUI exit""",
   COMMAND """QUIT""" PORT UAE LABEL "Quit"
  button PRESS HELP """This button makes UAE do a hard reset""",
   COMMAND """RESET""" PORT UAE LABEL "Reset"
  button PRESS HELP """This button makes UAE enter in debug mode""",
   COMMAND """Debug""" PORT UAE LABEL "Debug"
 endgroup

 group LABELS """Parameters""" FRAME
  group HORIZ
   label "Display:"
   address UAE QUERY DISPLAY
   cycle ID DRAW,
    HELP """This cycle button enables or disables the display output""",
    COMMAND """DISPLAY %s""" PORT UAE labels "OFF,ON",
    ATTRS Cycle_Active RESULT
   label "Sound:"
   address UAE QUERY SOUND
   IF RESULT = -1 THEN
        cycle id sound ATTRS Disabled TRUE COMMAND """SOUND %s""",
         PORT UAE LABELS "OFF,ON,BEST" 
   ELSE IF RESULT = 0 THEN
        cycle id sound ATTRS Cycle_Active 0,
         HELP """This cycle button enables or disables the sound output""",
         COMMAND """SOUND %s""" PORT UAE LABELS "OFF,ON,BEST" 
   ELSE cycle id sound,
         HELP """This cycle button enables or disables the sound output""",
         ATTRS Cycle_Active RESULT-1 COMMAND """SOUND %s""",
         PORT UAE LABELS "OFF,ON,BEST" 
   label "Joystick:"
   address UAE QUERY FAKEJOYSTICK
   cycle ID JOY,
    HELP """This cycle button enables or disables the joystick emulation""",
    COMMAND """FAKEJOYSTICK %s""" PORT UAE LABELS "OFF,ON",
    ATTRS Cycle_Active RESULT
  endgroup
  group HORIZ
   label "POW:"
   address UAE QUERY LED_POW;if RESULT = 1 then COL = RED; else COL = BLK
   button ID POW HELP """This image represents the power-led state""",
    ATTRS MUIA_FixWidthTxt 1 label COL
   label "Frame Rate:"
   address UAE QUERY FRAMERATE
   slider ID SLDR HELP """Use this slider gadget to select the frame rate""",
    COMMAND """FRAMERATE %s""" PORT UAE ATTRS MUIA_Slider_Horiz TRUE,
    MUIA_Slider_Min 1 MUIA_Slider_Max 20 MUIA_Weight 230,
    MUIA_SLIDER_LEVEL RESULT
  endgroup
 endgroup

 group LABELS """Disk files""" FRAME
  call SetDfx(0)
  call SetDfx(1)
  call SetDfx(2)
  call SetDfx(3)
 endgroup
endwindow

address UAE QUERY NAME_DF0;R0=RESULT
address UAE QUERY NAME_DF1;R1=RESULT
address UAE QUERY NAME_DF2;R2=RESULT
address UAE QUERY NAME_DF3;R3=RESULT
popasl ID NDF0 CONTENT R0
popasl ID NDF1 CONTENT R1
popasl ID NDF2 CONTENT R2
popasl ID NDF3 CONTENT R3

address UAE feedback LED_POW PORT UAEGUI,
 CMD_ON  """button ID POW LABEL "RED"""",
 CMD_OFF """button ID POW LABEL "BLK""""

address UAE feedback LED_DF0 PORT UAEGUI,
 CMD_ON  """button ID DF0 LABEL "GRN"""",
 CMD_OFF """button ID DF0 LABEL "BLK""""

address UAE feedback LED_DF1 PORT UAEGUI,
 CMD_ON  """button ID DF1 LABEL "GRN"""",
 CMD_OFF """button ID DF1 LABEL "BLK""""

address UAE feedback LED_DF2 PORT UAEGUI,
 CMD_ON  """button ID DF2 LABEL "GRN"""",
 CMD_OFF """button ID DF2 LABEL "BLK""""

address UAE feedback LED_DF3 PORT UAEGUI,
 CMD_ON  """button ID DF3 LABEL "GRN"""",
 CMD_OFF """button ID DF3 LABEL "BLK""""

address UAE feedback NAME_DF0 PORT UAEGUI,
 CMD """popasl ID NDF0 CONTENT %s"""

address UAE feedback NAME_DF1 PORT UAEGUI,
 CMD """popasl ID NDF1 CONTENT %s"""

address UAE feedback NAME_DF2 PORT UAEGUI,
 CMD """popasl ID NDF2 CONTENT %s"""

address UAE feedback NAME_DF3 PORT UAEGUI,
 CMD """popasl ID NDF3 CONTENT %s"""

address UAE feedback ON_EXIT PORT UAEGUI,
 CMD """quit"""

address UAE QUERY LED_POW;if RESULT = 1 then COL = RED; else COL = BLK
button ID POW label COL

exit 0

SetDFx: 
   PARSE ARG unit
   group ID GDF||unit HORIZ
    label LEFT DOUBLE "DF"||unit||":"
    address UAE QUERY LED_DF||unit;if RESULT=1 then COL=GRN; else COL=BLK
    button ID DF||unit,
     HELP """This image represents the state of drive "||unit||"'s led""",
     ATTRS MUIA_FixWidthTxt 1 label COL
    button PRESS,
     HELP """Use this button to eject the diskfile in drive DF"||unit||":""",
     COMMAND """EJECT "||unit||"""" PORT UAE ATTRS MUIA_Weight 100,
     LABEL "Eject"
    popasl ID NDF||unit,
     HELP """Select the name of diskfile for drive DF"||unit||":""",
     COMMAND """INSERT "||unit||" '%s'""" PORT UAE ATTRS MUIA_Weight 300
   endgroup ID GDF||unit
return
