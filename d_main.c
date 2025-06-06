// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id: d_main.c,v 1.3 2000-08-12 21:29:25 fraggle Exp $
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// DESCRIPTION:
//  DOOM main program (D_DoomMain) and game loop, plus functions to
//  determine game mode (shareware, registered), parse command line
//  parameters, configure game parameters (turbo), and call the startup
//  functions.
//
//-----------------------------------------------------------------------------

static const char rcsid[] = "$Id: d_main.c,v 1.3 2000-08-12 21:29:25 fraggle Exp $";

#include <unistd.h>
//#include <sys/types.h>  // GB 2014 not needed here 
#include <sys/stat.h>
//#include <fcntl.h>      // GB 2014 not needed here

#include <gppconio.h>     // GB 2014, gotoxy, textcolor 
#include <sys/nearptr.h>  // needed for __djgpp_nearptr_enable() -- stan 
#include <dos.h>          // GB 2014, for get dos version (and delay)

#include "doomdef.h"    // GB 2014 not needed here. Sakitoshi 2019 needed now :)
#include "doomstat.h"
#include "dstrings.h"
//#include "sounds.h"     // GB 2014 not needed here
#include "z_zone.h"     
#include "w_wad.h"
#include "s_sound.h"
#include "v_video.h"
#include "f_finale.h"
#include "f_wipe.h"
#include "m_argv.h"
#include "m_misc.h"
#include "m_menu.h"
#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"
#include "p_setup.h"
#include "r_draw.h"
#include "r_main.h"
#include "d_main.h"
#include "d_deh.h"  // Ty 04/08/98 - Externalizations
#include "crc32.h"  // Sakitoshi 2019, to calculate crc of the mission packs


// DEHacked support - Ty 03/09/97
// killough 10/98:
// Add lump number as third argument, for use when filename==NULL
void ProcessDehFile(char *filename, char *outfilename, int lump);

// killough 10/98: support -dehout filename
static char *D_dehout(void)
{
  static char *s;      // cache results over multiple calls
  if (!s)
    {
      int p = M_CheckParm("-dehout");
      if (!p)
        p = M_CheckParm("-bexout");
      s = p && ++p < myargc ? myargv[p] : "";
    }
  return s;
}

char **wadfiles;

// killough 10/98: preloaded files
#define MAXLOADFILES 2
char *wad_files[MAXLOADFILES], *deh_files[MAXLOADFILES];


// jff 1/24/98 add new versions of these variables to remember command line
boolean clnomonsters;   // checkparm of -nomonsters
boolean clrespawnparm;  // checkparm of -respawn
boolean clfastparm;     // checkparm of -fast

// jff 1/24/98 end definition of command line version of play mode switches
boolean devparm;        // working -devparm
boolean nomonsters;     // working -nomonsters
boolean respawnparm;    // working -respawn
boolean fastparm;       // working -fast
boolean ssgparm;        // working -ssg    GB 2013
boolean nolfbparm;      // working -nolfb  GB 2014
boolean nopmparm;       // working -nopm   GB 2014
boolean noasmparm;      // working -noasm  GB 2014
boolean noasmxparm;     // working -noasmx GB 2014
boolean asmp6parm;      // working -asmp6  GB 2014
boolean safeparm;       // working -safe   GB 2014
boolean stdvidparm;     // working -stdvid GB 2014
boolean bestvidparm;    // working -stdvid GB 2014
boolean lowdetparm;     // working -lowdet GB 2015
boolean nosfxparm;      // jff 1/22/98 parms for disabling music and sound, -nosfx
boolean nomusicparm;    // jff 1/22/98 parms for disabling music and sound, -nomusic
boolean v12_compat=false;// GB 2014, for v1.2 WAD stock demos

boolean singletics = false; // debug flag to cancel adaptiveness


//jff 4/18/98
extern boolean inhelpscreens;

skill_t startskill;
int     startepisode;
int     startmap;
boolean autostart;
FILE    *debugfile;

boolean advancedemo;

extern boolean timingdemo, singledemo, demoplayback, fastdemo; // killough

char    wadfile[PATH_MAX+1];       // primary wad file
char    mapdir[PATH_MAX+1];        // directory of development maps
char    basedefault[PATH_MAX+1];   // default file
char    baseiwad[PATH_MAX+1];      // jff 3/23/98: iwad directory
char    basesavegame[PATH_MAX+1];  // killough 2/16/98: savegame directory

//jff 4/19/98 list of standard IWAD names
const char *const standard_iwads[]=
{
  "/doom2f.wad",
  "/doom2.wad",
  "/plutonia.wad",
  "/tnt.wad",
  "/doom.wad",
  "/doom1.wad",
  "/doomu.wad", // GB 2014
};
static const int nstandard_iwads = sizeof standard_iwads/sizeof*standard_iwads;

void D_CheckNetGame (void);
void D_ProcessEvents (void);
void G_BuildTiccmd (ticcmd_t* cmd);
void D_DoAdvanceDemo (void);

//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//

event_t events[MAXEVENTS];
int eventhead, eventtail;

//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent(event_t *ev)
{
  events[eventhead++] = *ev;
  eventhead &= MAXEVENTS-1;
}

//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//

void D_ProcessEvents (void)
{
  // IF STORE DEMO, DO NOT ACCEPT INPUT
  if (gamemode != commercial || W_CheckNumForName("map01") >= 0)
    for (; eventtail != eventhead; eventtail = (eventtail+1) & (MAXEVENTS-1))
      if (!M_Responder(events+eventtail))
        G_Responder(events+eventtail);
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//

// wipegamestate can be set to -1 to force a wipe on the next draw
gamestate_t    wipegamestate = GS_DEMOSCREEN;
extern boolean setsizeneeded;
extern int     showMessages;
void           R_ExecuteSetViewSize(void);

void D_Display (void)
{
  static boolean viewactivestate = false;
  static boolean menuactivestate = false;
  static boolean inhelpscreensstate = false;
  static boolean fullscreen = false;
  static gamestate_t oldgamestate = -1;
  static int borderdrawcount;
  int wipestart;
  boolean done, wipe, redrawsbar;

  if (nodrawers)                    // for comparative timing / profiling
    return;
  
  redrawsbar = false;

  if (setsizeneeded)                // change the view size if needed
    {
      R_ExecuteSetViewSize();
      oldgamestate = -1;            // force background redraw
      borderdrawcount = 3;
    }

  // save the current screen if about to wipe
  if ((wipe = gamestate != wipegamestate))
    wipe_StartScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);

  if (gamestate == GS_LEVEL && gametic)
    HU_Erase();

  switch (gamestate)                // do buffered drawing
    {
    case GS_LEVEL:
      if (!gametic)
        break;
      if (automapactive)
        AM_Drawer();
      if (wipe || (scaledviewheight != 200 && fullscreen) // killough 11/98
          || (inhelpscreensstate && !inhelpscreens))
        redrawsbar = true;              // just put away the help screen
      ST_Drawer(scaledviewheight == 200, redrawsbar );    // killough 11/98
      fullscreen = scaledviewheight == 200;               // killough 11/98
      break;
    case GS_INTERMISSION:
      WI_Drawer();
      break;
    case GS_FINALE:
      F_Drawer();
      break;
    case GS_DEMOSCREEN:
      D_PageDrawer();
      break;
    }

  // draw buffered stuff to screen
  I_UpdateNoBlit();

  // draw the view directly
  if (gamestate == GS_LEVEL && !automapactive && gametic)
    R_RenderPlayerView (&players[displayplayer]);

  if (gamestate == GS_LEVEL && gametic)
    HU_Drawer ();

  // clean up border stuff
  if (gamestate != oldgamestate && gamestate != GS_LEVEL)
    I_SetPalette (W_CacheLumpName ("PLAYPAL",PU_CACHE));

  // see if the border needs to be initially drawn
  if (gamestate == GS_LEVEL && oldgamestate != GS_LEVEL)
    {
      viewactivestate = false;        // view was not active
      R_FillBackScreen ();    // draw the pattern into the back screen
    }

  // see if the border needs to be updated to the screen
  if (gamestate == GS_LEVEL && !automapactive && scaledviewwidth != 320)
    {
      if (menuactive || menuactivestate || !viewactivestate)
        borderdrawcount = 3;
      if (borderdrawcount)
        {
          R_DrawViewBorder ();    // erase old menu stuff
          borderdrawcount--;
        }
    }

  menuactivestate = menuactive;
  viewactivestate = viewactive;
  inhelpscreensstate = inhelpscreens;
  oldgamestate = wipegamestate = gamestate;

  // draw pause pic
  if (paused)
    {
      int y = 4;
      if (!automapactive)
        y += viewwindowy;
      V_DrawPatchDirect(viewwindowx+(scaledviewwidth-68)/2,
                        y,0,W_CacheLumpName ("M_PAUSE", PU_CACHE));
    }

  // menus go directly to the screen
  M_Drawer();          // menu is drawn even on top of everything
  NetUpdate();         // send out any new accumulation

  // normal update
  if (!wipe)
    {
      I_FinishUpdate ();              // page flip or blit buffer
      return;
    }

  // wipe update
  wipe_EndScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);

  wipestart = I_GetTime () - 1;

  do
    {
      int nowtime, tics;
	  statusbar_dirty=2; // GB 2014
      do
        {
          nowtime = I_GetTime();
          tics = nowtime - wipestart;
        }
      while (!tics);
      wipestart = nowtime;
      done = wipe_ScreenWipe(wipe_Melt,0,0,SCREENWIDTH,SCREENHEIGHT,tics);
      I_UpdateNoBlit();
      M_Drawer();                   // menu is drawn even on top of wipes
      I_FinishUpdate();             // page flip or blit buffer
    }
  while (!done);
}

//
//  DEMO LOOP
//

static int demosequence;         // killough 5/2/98: made static
static int pagetic;
static char *pagename;

//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker(void)
{
  // killough 12/98: don't advance internal demos if a single one is 
  // being played. The only time this matters is when using -loadgame with
  // -fastdemo, -playdemo, or -timedemo, and a consistency error occurs.

  if (!singledemo && --pagetic < 0)
    D_AdvanceDemo();
}

//
// D_PageDrawer
//
// killough 11/98: add credits screen
//

void D_PageDrawer(void)
{
  if (pagename)
    {
      int l = W_CheckNumForName(pagename);
      byte *t = W_CacheLumpNum(l, PU_CACHE);
      size_t s = W_LumpLength(l);
      unsigned c = 0;
      while (s--)
	c = c*3 + t[s];
      V_DrawPatch(0, 0, 0, (patch_t *) t);
#ifdef DOGS // GB 2014
      if (c==2119826587u || c==2391756584u)
	V_DrawPatch(0, 0, 0, W_CacheLumpName("DOGOVRLY", PU_CACHE));
#endif //DOGS
    }
  else
    M_DrawCredits();
}

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//

void D_AdvanceDemo (void)
{
  advancedemo = true;
}

// killough 11/98: functions to perform demo sequences

static void D_SetPageName(char *name)
{
  pagename = name;
}

static void D_DrawTitle1(char *name)
{
  S_StartMusic(mus_intro);
  pagetic = (TICRATE*170)/35;
  if (W_CheckNumForName("SIGILINT") != -1 || W_CheckNumForName("SIGILIN2") != -1) // Sakitoshi 2019: if Sigil detected, wait for the title theme to end.
    pagetic = (TICRATE*404)/35;
  D_SetPageName(name);
}

static void D_DrawTitle2(char *name)
{
  S_StartMusic(mus_dm2ttl);
  D_SetPageName(name);
}

// killough 11/98: tabulate demo sequences

static struct 
{
  void (*func)(char *);
  char *name;
} const demostates[][4] =
  {
    {
      {D_DrawTitle1, "TITLEPIC"},
      {D_DrawTitle1, "TITLEPIC"},
      {D_DrawTitle2, "TITLEPIC"},
      {D_DrawTitle1, "TITLEPIC"},
    },

    {
      {G_DeferedPlayDemo, "demo1"},
      {G_DeferedPlayDemo, "demo1"},
      {G_DeferedPlayDemo, "demo1"},
      {G_DeferedPlayDemo, "demo1"},
    },

    {
      {D_SetPageName, NULL},
      {D_SetPageName, NULL},
      {D_SetPageName, NULL},
      {D_SetPageName, NULL},
    },

    {
      {G_DeferedPlayDemo, "demo2"},
      {G_DeferedPlayDemo, "demo2"},
      {G_DeferedPlayDemo, "demo2"},
      {G_DeferedPlayDemo, "demo2"},
    },

    {
      {D_SetPageName, "HELP2"},
      {D_SetPageName, "HELP2"},
      {D_SetPageName, "CREDIT"},
      {D_DrawTitle1,  "TITLEPIC"},
    },

    {
      {G_DeferedPlayDemo, "demo3"},
      {G_DeferedPlayDemo, "demo3"},
      {G_DeferedPlayDemo, "demo3"},
      {G_DeferedPlayDemo, "demo3"},
    },

    {
      {NULL},
      {NULL},
      {NULL},
      {D_SetPageName, "CREDIT"},
    },

    {
      {NULL},
      {NULL},
      {NULL},
      {G_DeferedPlayDemo, "demo4"},
    },

    {
      {NULL},
      {NULL},
      {NULL},
      {NULL},
    }
  };

//
// This cycles through the demo sequences.
//
// killough 11/98: made table-driven

void D_DoAdvanceDemo(void)
{
  players[consoleplayer].playerstate = PST_LIVE;  // not reborn
  advancedemo = usergame = paused = false;
  gameaction = ga_nothing;

  pagetic = TICRATE * 11;         // killough 11/98: default behavior
  gamestate = GS_DEMOSCREEN;

  if (!demostates[++demosequence][gamemode].func)
    demosequence = 0;
  demostates[demosequence][gamemode].func
    (demostates[demosequence][gamemode].name);
}

//
// D_StartTitle
//
void D_StartTitle (void)
{
  gameaction = ga_nothing;
  demosequence = -1;
  D_AdvanceDemo();
}

// print title for every printed line
static char title[128];

//
// D_AddFile
//
// Rewritten by Lee Killough
//
// killough 11/98: remove limit on number of files
//

void D_AddFile(const char *file)
{
  static int numwadfiles, numwadfiles_alloc;

  if (numwadfiles >= numwadfiles_alloc)
    wadfiles = realloc(wadfiles, (numwadfiles_alloc = numwadfiles_alloc ?
                                  numwadfiles_alloc * 2 : 8)*sizeof*wadfiles);
  wadfiles[numwadfiles++] = !file ? NULL : strdup(file);
}

// Return the path where the executable lies -- Lee Killough
char *D_DoomExeDir(void)
{
  static char *base;
  if (!base)        // cache multiple requests
    {
      size_t len = strlen(*myargv);
      char *p = (base = malloc(len+1)) + len;
      strcpy(base,*myargv);
      while (p > base && *p!='/' && *p!='\\')
        *p--=0;
    }
  return base;
}

// killough 10/98: return the name of the program the exe was invoked as
char *D_DoomExeName(void)
{
  static char *name;    // cache multiple requests
  if (!name)
    {
      char *p = *myargv + strlen(*myargv);
      int i = 0;
      while (p > *myargv && p[-1] != '/' && p[-1] != '\\' && p[-1] != ':')
        p--;
      while (p[i] && p[i] != '.')
        i++;
      strncpy(name = malloc(i+1), p, i)[i] = 0;
    }
  return name;
}

//
// CheckIWAD
//
// Verify a file is indeed tagged as an IWAD
// Scan its lumps for levelnames and return gamemode as indicated
// Detect missing wolf levels in DOOM II
//
// The filename to check is passed in iwadname, the gamemode detected is
// returned in gmode, hassec returns the presence of secret levels
//
// jff 4/19/98 Add routine to test IWAD for validity and determine
// the gamemode from it. Also note if DOOM II, whether secret levels exist
//
// killough 11/98:
// Rewritten to considerably simplify
// Added Final Doom support (thanks to Joel Murdoch)
//

static void CheckIWAD(const char *iwadname,
                      GameMode_t *gmode,
                      //GameMission_t *gmission,  // joel 10/17/98 Final DOOM fix
                      boolean *hassec)
{
  FILE *fp = fopen(iwadname, "rb");
  int ud, rg, sw, cm, sc, tnt, plut;
  filelump_t lump;
  wadinfo_t header;
  const char *n = lump.name;

  if (!fp)
    I_Error("Can't open IWAD: %s\n",iwadname);

  // read IWAD header
  if (fread(&header, 1, sizeof header, fp) != sizeof header ||
      header.identification[0] != 'I' || header.identification[1] != 'W' ||
      header.identification[2] != 'A' || header.identification[3] != 'D')
    I_Error("IWAD tag not present: %s\n",iwadname);

  fseek(fp, LONG(header.infotableofs), SEEK_SET);

  // Determine game mode from levels present
  // Must be a full set for whichever mode is present
  // Lack of wolf-3d levels also detected here

  for (ud=rg=sw=cm=sc=tnt=plut=0, header.numlumps = LONG(header.numlumps);
       header.numlumps && fread(&lump, sizeof lump, 1, fp); header.numlumps--)
    *n=='E' && n[2]=='M' && !n[4] ?
      n[1]=='4' ? ++ud : n[1]!='1' ? rg += n[1]=='3' || n[1]=='2' : ++sw :
    *n=='M' && n[1]=='A' && n[2]=='P' && !n[5] ?
      ++cm, sc += n[3]=='3' && (n[4]=='1' || n[4]=='2') :
    *n=='C' && n[1]=='A' && n[2]=='V' && !n[7] ? ++tnt :
    *n=='M' && n[1]=='C' && !n[3] && ++plut;

  fclose(fp);

  //*gmission = doom;
  *hassec = false;
  *gmode =
    /*cm >= 30 ? (*gmission = tnt >= 4 ? pack_tnt :
                plut >= 8 ? pack_plut : doom2,
                *hassec = sc >= 2, commercial) :*/
    cm >= 30 ? (*hassec = sc >= 2, commercial) :
    ud >= 9 ? retail :
    rg >= 18 ? registered :
    sw >= 9 ? shareware :
    indetermined;
}

//
// AddIWAD
//
void AddIWAD(const char *iwad)
{
  size_t i;

  if (!(iwad && *iwad))
    return;

  //jff 9/3/98 use logical output routine.
  //Sakitoshi 2019 lprintf not implemented, so using regular printf.
  printf("IWAD found: %s\n",iwad); //jff 4/20/98 print only if found
  CheckIWAD(iwad,&gamemode,&haswolflevels);

  /* jff 8/23/98 set gamemission global appropriately in all cases
  * cphipps 12/1999 - no version output here, leave that to the caller
  */
  i = strlen(iwad);
  switch(gamemode)
  {
  case retail:
  case registered:
  case shareware:
    gamemission = doom;
    //if (i>=8 && !strnicmp(iwad+i-8,"chex.wad",8))
    //  gamemission = chex;
    break;
  case commercial:
    gamemission = doom2;
    if (i>=10 && !strnicmp(iwad+i-10,"doom2f.wad",10))
      language=french;
    else if (i>=7 && !strnicmp(iwad+i-7,"tnt.wad",7))
      gamemission = pack_tnt;
    else if (i>=12 && !strnicmp(iwad+i-12,"plutonia.wad",12))
      gamemission = pack_plut;
    //else if (i>=8 && !strnicmp(iwad+i-8,"hacx.wad",8))
    //  gamemission = hacx;
    break;
  default:
    gamemission = none;
    break;
  }
  if (gamemode == indetermined)
    //jff 9/3/98 use logical output routine
    //Sakitoshi 2019 lprintf not implemented, so using regular printf.
    printf("Unknown Game Version, may not work\n");
  D_AddFile(iwad);
}

// jff 4/19/98 Add routine to check a pathname for existence as
// a file or directory. If neither append .wad and check if it
// exists as a file then. Else return non-existent.

boolean WadFileStatus(char *filename,boolean *isdir)
{
  struct stat sbuf;
  int i;

  *isdir = false;                 //default is directory to false
  if (!filename || !*filename)    //if path NULL or empty, doesn't exist
    return false;

  if (!stat(filename,&sbuf))      //check for existence
    {
      *isdir=S_ISDIR(sbuf.st_mode); //if it does, set whether a dir or not
      return true;                  //return does exist
    }

  i = strlen(filename);           //get length of path
  if (i>=4)
    if(!strnicmp(filename+i-4,".wad",4))
      return false;               //if already ends in .wad, not found

  strcat(filename,".wad");        //try it with .wad added
  if (!stat(filename,&sbuf))      //if it exists then
    {
      if (S_ISDIR(sbuf.st_mode))  //but is a dir, then say we didn't find it
        return false;
      return true;                //otherwise return file found, w/ .wad added
    }
  filename[i]=0;                  //remove .wad
  return false;                   //and report doesn't exist
}

//
// FindIWADFIle
//
// Search in all the usual places until an IWAD is found.
//
// The global baseiwad contains either a full IWAD file specification
// or a directory to look for an IWAD in, or the name of the IWAD desired.
//
// The global standard_iwads lists the standard IWAD names
//
// The result of search is returned in baseiwad, or set blank if none found
//
// IWAD search algorithm:
//
// Set customiwad blank
// If -iwad present set baseiwad to normalized path from -iwad parameter
//  If baseiwad is an existing file, thats it
//  If baseiwad is an existing dir, try appending all standard iwads
//  If haven't found it, and no : or / is in baseiwad,
//   append .wad if missing and set customiwad to baseiwad
//
// Look in . for customiwad if set, else all standard iwads
//
// Look in DoomExeDir. for customiwad if set, else all standard iwads
//
// If $DOOMWADDIR is an existing file
//  If customiwad is not set, thats it
//  else replace filename with customiwad, if exists thats it
// If $DOOMWADDIR is existing dir, try customiwad if set, else standard iwads
//
// If $HOME is an existing file
//  If customiwad is not set, thats it
//  else replace filename with customiwad, if exists thats it
// If $HOME is an existing dir, try customiwad if set, else standard iwads
//
// IWAD not found
//
// jff 4/19/98 Add routine to search for a standard or custom IWAD in one
// of the standard places. Returns a blank string if not found.
//
// killough 11/98: simplified, removed error-prone cut-n-pasted code
//

char *FindIWADFile(void)
{
  static const char *envvars[] = {"DOOMWADDIR", "HOME"};
  static char iwad[PATH_MAX+1], customiwad[PATH_MAX+1];
  boolean isdir=false;
  int i,j;
  char *p;

  *iwad = 0;       // default return filename to empty
  *customiwad = 0; // customiwad is blank

  //jff 3/24/98 get -iwad parm if specified else use .
  if ((i = M_CheckParm("-iwad")) && i < myargc-1)
    {
      NormalizeSlashes(strcpy(baseiwad,myargv[i+1]));
      if (WadFileStatus(strcpy(iwad,baseiwad),&isdir))
        if (!isdir)
          return iwad;
        else
          for (i=0;i<nstandard_iwads;i++)
            {
              int n = strlen(iwad);
              strcat(iwad,standard_iwads[i]);
              if (WadFileStatus(iwad,&isdir) && !isdir)
                return iwad;
              iwad[n] = 0; // reset iwad length to former
            }
      else
        if (!strchr(iwad,':') && !strchr(iwad,'/'))
          AddDefaultExtension(strcat(strcpy(customiwad, "/"), iwad), ".wad");
    }

  for (j=0; j<2; j++)
    {
      strcpy(iwad, j ? D_DoomExeDir() : ".");
      NormalizeSlashes(iwad);
      printf("Looking in %s\n",iwad);   // killough 8/8/98
      if (*customiwad)
        {
          strcat(iwad,customiwad);
          if (WadFileStatus(iwad,&isdir) && !isdir)
            return iwad;
        }
      else
        for (i=0;i<nstandard_iwads;i++)
          {
            int n = strlen(iwad);
            strcat(iwad,standard_iwads[i]);
            if (WadFileStatus(iwad,&isdir) && !isdir)
              return iwad;
            iwad[n] = 0; // reset iwad length to former
          }
    }

  // GB 2013, also look in iwad subfolder of D_DoomExeDir
  for (j=0; j<2; j++)
    {
      strcpy(iwad, j ? D_DoomExeDir() : ".");
      strcat(iwad, "/iwad");

	  NormalizeSlashes(iwad);
      printf("Looking in %s\n",iwad);   // killough 8/8/98
      if (*customiwad)
        {
          strcat(iwad,customiwad);
          if (WadFileStatus(iwad,&isdir) && !isdir)
            return iwad;
        }
      else
        for (i=0;i<nstandard_iwads;i++)
          {
            int n = strlen(iwad);
            strcat(iwad,standard_iwads[i]);
            if (WadFileStatus(iwad,&isdir) && !isdir)
              return iwad;
            iwad[n] = 0; // reset iwad length to former
          }
    }



  for (i=0; i<sizeof envvars/sizeof *envvars;i++) if ((p = getenv(envvars[i])))
  {
     NormalizeSlashes(strcpy(iwad,p));
     if (WadFileStatus(iwad,&isdir))
     {
        if (!isdir)
        {
     	   if (!*customiwad)
	   	   return printf("Looking for %s\n",iwad), iwad; // killough 8/8/98
	       else
		   if ((p = strrchr(iwad,'/')))
		   {
		      *p=0;
		      strcat(iwad,customiwad);
		      printf("Looking for %s\n",iwad);  // killough 8/8/98
		      if (WadFileStatus(iwad,&isdir) && !isdir)
		      return iwad;
		   }
	    }
	    else 
	    {
	       printf("Looking in %s\n",iwad);  // killough 8/8/98
           if (*customiwad)
		   {
		      if (WadFileStatus(strcat(iwad,customiwad),&isdir) && !isdir)
		      return iwad;
		   }
		   else
		   for (i=0;i<nstandard_iwads;i++)
		   {
		      int n = strlen(iwad);
  		      strcat(iwad,standard_iwads[i]);
		      if (WadFileStatus(iwad,&isdir) && !isdir)
		      return iwad;
		      iwad[n] = 0; // reset iwad length to former
   	       }
	    }
	 }
  }

  *iwad = 0;
  return iwad;
}

//
// IdentifyVersion
//
// Set the location of the defaults file and the savegame root
// Locate and validate an IWAD file
// Determine gamemode from the IWAD
//
// supports IWADs with custom names. Also allows the -iwad parameter to
// specify which iwad is being searched for if several exist in one dir.
// The -iwad parm may specify:
//
// 1) a specific pathname, which must exist (.wad optional)
// 2) or a directory, which must contain a standard IWAD,
// 3) or a filename, which must be found in one of the standard places:
//   a) current dir,
//   b) exe dir
//   c) $DOOMWADDIR
//   d) or $HOME
//
// jff 4/19/98 rewritten to use a more advanced search algorithm

void IdentifyVersion (void)
{
  int         i;    //jff 3/24/98 index of args on commandline
  struct stat sbuf; //jff 3/24/98 used to test save path for existence
  char *iwad;

  // get config file from same directory as executable
  // killough 10/98
  sprintf(basedefault,"%s/%s.cfg", D_DoomExeDir(), D_DoomExeName());

  // set save path to -save parm or current dir

  strcpy(basesavegame,".");       //jff 3/27/98 default to current dir
  if ((i=M_CheckParm("-save")) && i<myargc-1) //jff 3/24/98 if -save present
    {
      if (!stat(myargv[i+1],&sbuf) && S_ISDIR(sbuf.st_mode)) // and is a dir
        strcpy(basesavegame,myargv[i+1]);  //jff 3/24/98 use that for savegame
      else
        puts("Error: -save path does not exist, using current dir");  // killough 8/8/98
    }

  // locate the IWAD and determine game mode from it

  iwad = FindIWADFile();

  if (iwad && *iwad)
    {
      //printf("IWAD found: %s\n",iwad); //jff 4/20/98 print only if found. Sakitoshi 2019, not needed here anymore
      AddIWAD(iwad);
    }
  else
    I_Error("IWAD not found\n");
}

// killough 5/3/98: old code removed

//
// Find a Response File
//

#define MAXARGVS 100

void FindResponseFile (void)
{
  int i;

  for (i = 1;i < myargc;i++)
    if (myargv[i][0] == '@')
      {
        FILE *handle;
        int  size;
        int  k;
        int  index;
        int  indexinfile;
        char *infile;
        char *file;
        char *moreargs[MAXARGVS];
        char *firstargv;

        // READ THE RESPONSE FILE INTO MEMORY

        // killough 10/98: add default .rsp extension
        char *filename = malloc(strlen(myargv[i])+5);
        AddDefaultExtension(strcpy(filename,&myargv[i][1]),".rsp");

        handle = fopen(filename,"rb");
        if (!handle)
          I_Error("No such response file!");          // killough 10/98

        printf("Found response file %s!\n",filename);
        free(filename);

        fseek(handle,0,SEEK_END);
        size = ftell(handle);
        fseek(handle,0,SEEK_SET);
        file = malloc (size);
        fread(file,size,1,handle);
        fclose(handle);

        // KEEP ALL CMDLINE ARGS FOLLOWING @RESPONSEFILE ARG
        for (index = 0,k = i+1; k < myargc; k++)
          moreargs[index++] = myargv[k];

        firstargv = myargv[0];
        myargv = calloc(sizeof(char *),MAXARGVS);
        myargv[0] = firstargv;

        infile = file;
        indexinfile = k = 0;
        indexinfile++;  // SKIP PAST ARGV[0] (KEEP IT)
        do
          {
            myargv[indexinfile++] = infile+k;
            while(k < size &&
                  ((*(infile+k)>= ' '+1) && (*(infile+k)<='z')))
              k++;
            *(infile+k) = 0;
            while(k < size &&
                  ((*(infile+k)<= ' ') || (*(infile+k)>'z')))
              k++;
          }
        while(k < size);

        for (k = 0;k < index;k++)
          myargv[indexinfile++] = moreargs[k];
        myargc = indexinfile;

        // DISPLAY ARGS
        printf("%d command-line args:\n",myargc-1); // killough 10/98
        for (k=1;k<myargc;k++)
          printf("%s\n",myargv[k]);
        break;
      }
}

// killough 10/98: moved code to separate function

static void D_ProcessDehCommandLine(void)
{
  // ty 03/09/98 do dehacked stuff
  // Note: do this before any other since it is expected by
  // the deh patch author that this is actually part of the EXE itself
  // Using -deh in BOOM, others use -dehacked.
  // Ty 03/18/98 also allow .bex extension.  .bex overrides if both exist.
  // killough 11/98: also allow -bex

  int p = M_CheckParm ("-deh");
  if (p || (p = M_CheckParm("-bex")))
    {
      // the parms after p are deh/bex file names,
      // until end of parms or another - preceded parm
      // Ty 04/11/98 - Allow multiple -deh files in a row
      // killough 11/98: allow multiple -deh parameters

      boolean deh = true;
      while (++p < myargc)
        if (*myargv[p] == '-')
          deh = !strcasecmp(myargv[p],"-deh") || !strcasecmp(myargv[p],"-bex");
        else
          if (deh)
            {
			  char file[PATH_MAX+1];      // killough
              AddDefaultExtension(strcpy(file, myargv[p]), ".bex");
              if (access(file, F_OK))  // nope
                {
                  AddDefaultExtension(strcpy(file, myargv[p]), ".deh");
                  if (access(file, F_OK))  // still nope
                    I_Error("Cannot find .deh or .bex file named %s",
                            myargv[p]);
                }
              // during the beta we have debug output to dehout.txt
              // (apparently, this was never removed after Boom beta-killough)

              ProcessDehFile(file, D_dehout(), 0);  // killough 10/98   (D_ProcessDehCommandLine)
            }
    }
  // ty 03/09/98 end of do dehacked stuff
}

// killough 10/98: support preloaded wads

static void D_ProcessWadPreincludes(void)
{
  if (!M_CheckParm ("-noload"))
    {
      int i;
      char *s;
      for (i=0; i<MAXLOADFILES; i++)
        if ((s=wad_files[i]))
          {
            while (isspace(*s))
              s++;
            if (*s)
              {
                char file[PATH_MAX+1];
                AddDefaultExtension(strcpy(file, s), ".wad");
                if (!access(file, R_OK))
                  D_AddFile(file);
                else
                  printf("\nWarning: could not open %s\n", file);
              }
          }
    }
}

// killough 10/98: support preloaded deh/bex files

static void D_ProcessDehPreincludes(void)
{
  if (!M_CheckParm ("-noload"))
    {
      int i;
      char *s;
      for (i=0; i<MAXLOADFILES; i++)
        if ((s=deh_files[i]))
          {
            while (isspace(*s))
              s++;
            if (*s)
              {
                char file[PATH_MAX+1];
                AddDefaultExtension(strcpy(file, s), ".bex");
                if (!access(file, R_OK))
                  ProcessDehFile(file, D_dehout(), 0);   // (D_ProcessDehPreincludes)
                else
                  {
                    AddDefaultExtension(strcpy(file, s), ".deh");
                    if (!access(file, R_OK))
                      ProcessDehFile(file, D_dehout(), 0);   // (D_ProcessDehPreincludes)
                    else
                      printf("\nWarning: could not open %s .deh or .bex\n", s);
                  }
              }
          }
    }
}

// killough 10/98: support .deh from wads
//
// A lump named DEHACKED is treated as plaintext of a .deh file embedded in
// a wad (more portable than reading/writing info.c data directly in a wad).
//
// If there are multiple instances of "DEHACKED", we process each, in first
// to last order (we must reverse the order since they will be stored in
// last to first order in the chain). Passing NULL as first argument to
// ProcessDehFile() indicates that the data comes from the lump number
// indicated by the third argument, instead of from a file.

static void D_ProcessDehInWad(int i)
{
  if (i >= 0)
    {
      D_ProcessDehInWad(lumpinfo[i].next);
      if (!strncasecmp(lumpinfo[i].name, "dehacked", 8) &&
          lumpinfo[i].namespace == ns_global)
        ProcessDehFile(NULL, D_dehout(), i);  // (D_ProcessDehPreincludes)
    }
}

#define D_ProcessDehInWads() D_ProcessDehInWad(lumpinfo[W_LumpNameHash \
                                                       ("dehacked") % (unsigned) numlumps].index);

//
// D_DoomMain
//

void D_DoomMain(void)
{
  int p, slot, unlockparm;
  char file[PATH_MAX+1];      // killough 3/22/98
  in_graphics_mode=0;         // GB 2014, required here, else flashing disk will draw when not allowed
  setbuf(stdout,NULL);

  FindResponseFile();         // Append response file arguments to command-line

  // killough 10/98: set default savename based on executable's name
  sprintf(savegamename = malloc(16), "%.4ssav", D_DoomExeName());

  safeparm                    = M_CheckParm ("-safe");   // GB 2014  
  // GB 2014, safeparm: skip nearptr_enable function, retain memory protection. 0,1 FPS less if I put it in i_main
  if (_get_dos_version(1)==0x532) {printf("Windows NT based OS detected: Safe mode enabled.\n"); safeparm=1;} // Windows NT, 2000, XP
  if (!safeparm) 
   // 2/2/98 Stan, Must call this here.  It's required by both netgames and i_video.c.
  if (!__djgpp_nearptr_enable()) {printf ("Failed trying to allocate DOS near pointers, try -safe parameter.\n"); return;}

  IdentifyVersion();

  modifiedgame = 0;

  // killough 10/98: process all command-line DEH's first
  D_ProcessDehCommandLine(); 

  //rest(4000); //GB 2015

#ifdef BETA
  // killough 7/19/98: beta emulation option
  beta_emulation = !!M_CheckParm("-beta");

  if (beta_emulation)
    { // killough 10/98: beta lost soul has different behavior frames
      mobjinfo[MT_SKULL].spawnstate   = S_BSKUL_STND;
      mobjinfo[MT_SKULL].seestate     = S_BSKUL_RUN1;
      mobjinfo[MT_SKULL].painstate    = S_BSKUL_PAIN1;
      mobjinfo[MT_SKULL].missilestate = S_BSKUL_ATK1;
      mobjinfo[MT_SKULL].deathstate   = S_BSKUL_DIE1;
      mobjinfo[MT_SKULL].damage       = 1;
    }
  else
    mobjinfo[MT_SCEPTRE].doomednum = mobjinfo[MT_BIBLE].doomednum = -1;
#endif

  // jff 1/24/98 set both working and command line value of play parms
  nomonsters  = clnomonsters  = M_CheckParm ("-nomonsters");
  respawnparm = clrespawnparm = M_CheckParm ("-respawn");
  fastparm    = clfastparm    = M_CheckParm ("-fast");
  devparm                     = M_CheckParm ("-devparm");
  ssgparm                     = M_CheckParm ("-ssg");    // GB 2013
  nolfbparm                   = M_CheckParm ("-nolfb");  // GB 2014
  nopmparm                    = M_CheckParm ("-nopm");   // GB 2014
  noasmparm                   = M_CheckParm ("-noasm");  // GB 2014
  noasmxparm                  = M_CheckParm ("-noasmx"); // GB 2014
  asmp6parm                   = M_CheckParm ("-asmp6");  // GB 2014  
  unlockparm                  = M_CheckParm ("-unlock"); // GB 2014  
  stdvidparm                  = M_CheckParm ("-stdvid"); // GB 2014  
  bestvidparm                 = M_CheckParm ("-bestvid");// GB 2014  
  lowdetparm                  = M_CheckParm ("-lowdet"); // GB 2015

  #ifdef CALT // GB 2015: required for first frame
  if      (lowdetparm) colfunc = R_DrawColumn_C_LowDet; 
  else if (noasmparm)  colfunc = R_DrawColumn_C; 
  #endif // CALT

  // jff 1/24/98 end of set to both working and command line value
       if (M_CheckParm ("-altdeath"))   deathmatch = 2;
  else if (M_CheckParm ("-deathmatch")) deathmatch = 1;

  // add any files specified on the command line with -file wadfile
  // to the wad list

  // killough 1/31/98, 5/2/98: reload hack removed, -wart same as -warp now.

  if ((p = M_CheckParm ("-file")))
    {
      // the parms after p are wadfile/lump names,
      // until end of parms or another - preceded parm
      // killough 11/98: allow multiple -file parameters

      boolean file = true;            // homebrew levels
      while (++p < myargc)
	{
	  if (*myargv[p] == '-')
	    file = !strcasecmp(myargv[p],"-file");
	  else if (file)
          {
          struct stat sbuf; // Sakitoshi 2019, required to check the new mission packs
          char *pwad = AddDefaultExtension(myargv[p],".wad");
          unsigned int crcpwad;
          D_AddFile(myargv[p]);
          modifiedgame++;
          if ((!strnicmp(pwad,"nerve.wad",9) && !stat(pwad,&sbuf) && sbuf.st_size == 3819855) ||
              (!strnicmp(pwad,"nrftl.wad",9) && !stat(pwad,&sbuf) && sbuf.st_size == 3954644))
            {
            unsigned int crcnerve = 0xAD7F9292;
            unsigned int crcnrftl = 0x9BA0ABCA;
            crcpwad = CheckCrc32(pwad);
            if (crcpwad == crcnerve || crcpwad == crcnrftl)
              gamemission = pack_nerve;
            }
          if (((!strnicmp(pwad,"masterlevels.wad",16) && !stat(pwad,&sbuf)) ||
              (!strnicmp(pwad,"master~1.wad",12) && !stat(pwad,&sbuf)) ||
              (!strnicmp(pwad,"master.wad",10) && !stat(pwad,&sbuf)))
              && sbuf.st_size == 3479715)
            {
            unsigned int crcmaster = 0x6BAEC89F;
            crcpwad = CheckCrc32(pwad);
            if (crcpwad == crcmaster)
              {
              gamemission = pack_master;
              if (!stat("mstrsky.wad",&sbuf))
                D_AddFile("mstrsky.wad");
              }
            }
          }
	}
    }

  // GB 2014: colored title like original doom, requires conio.h
  clrscr();
  textbackground(LIGHTGRAY); textcolor(RED);  // default v1.9 colors


#if 0
  switch ( gamemode )
    {
    case retail:
      sprintf (title,
               "                       The Ultimate DOOM Startup v%i.%02i                          ",
               VERSION/100,VERSION%100);
      break;
    case shareware:
      sprintf (title,
               "                        DOOM Shareware Startup v%i.%02i                            ",
               VERSION/100,VERSION%100);
	  textbackground(BLUE); textcolor(YELLOW);  
	  break;

    case registered:
      sprintf (title,
               "                        DOOM Registered Startup v%i.%02i                           ",
               VERSION/100,VERSION%100);
	  textbackground(BLUE); textcolor(YELLOW);  
      break;

    case commercial:
      switch (gamemission)      // joel 10/16/98 Final DOOM fix
        {
        case pack_master:
          sprintf (title,
               "                       Master Levels for DOOM 2 v%i.%02i                           ",
               VERSION/100,VERSION%100);
          textbackground(RED); textcolor(WHITE);  
          break;

        case pack_nerve:
          sprintf (title,
               "                    DOOM 2: No Rest For The Living v%i.%02i                        ",
               VERSION/100,VERSION%100);
          textbackground(RED); textcolor(WHITE);  
          break;

        case pack_plut:
          sprintf (title,
               "                      DOOM 2: Plutonia Experiment v%i.%02i                         ",
               VERSION/100,VERSION%100);
          textbackground(RED); textcolor(WHITE);  
          break;

        case pack_tnt:
          sprintf (title,
               "                       DOOM 2: TNT - Evilution  v%i.%02i                           ",
               VERSION/100,VERSION%100);
          textbackground(RED); textcolor(WHITE);  
          break;

        case doom2:
        default:

          sprintf (title,
               "                        DOOM 2: Hell on Earth v%i.%02i                             ",
               VERSION/100,VERSION%100);
          textbackground(RED); textcolor(WHITE);  
          break;
        }
      break;
      // joel 10/16/98 end Final DOOM fix

    default:
      sprintf (title,
		       "                            Public DOOM - v%i.%02i                                 ",
               VERSION/100,VERSION%100);
      break;
    }

 // GB 2014: colored title like original doom, requires conio.h
 cprintf("%s",title);
 textbackground(BLACK); textcolor(LIGHTGRAY);  
 gotoxy(1,wherey()); // GB 2014: display allegro version:
 cprintf("MBF - Built on %s by GB (with Allegro %s)\n",version_date,ALLEGRO_VERSION_STR);   
 gotoxy(1,wherey());

#endif

 printf("%s\nBuilt on %s\n", title, version_date);    // killough 2/1/98
 
 if (devparm)
    printf(D_DEVSTR);

  if (M_CheckParm("-cdrom"))
    {
      printf(D_CDROM);
      mkdir("c:/doomdata",0);

      // killough 10/98:
      sprintf(basedefault, "c:/doomdata/%s.cfg", D_DoomExeName());
    }

  // turbo option
  if ((p=M_CheckParm ("-turbo")))
    {
      int scale = 200;
      extern int forwardmove[2];
      extern int sidemove[2];

      if (p<myargc-1)
        scale = atoi(myargv[p+1]);
      if (scale < 10)
        scale = 10;
      if (scale > 400)
        scale = 400;
      printf ("turbo scale: %i%%\n",scale);
      forwardmove[0] = forwardmove[0]*scale/100;
      forwardmove[1] = forwardmove[1]*scale/100;
      sidemove[0] = sidemove[0]*scale/100;
      sidemove[1] = sidemove[1]*scale/100;
    }

#ifdef BETA
  if (beta_emulation)
    {
      char s[PATH_MAX+1];
      sprintf(s, "%s/betagrph.wad", D_DoomExeDir());  // killough 7/11/98
      D_AddFile(s);
    }
#endif

  if (!(p = M_CheckParm("-playdemo")) || p >= myargc-1)    // killough
    {
      if ((p = M_CheckParm ("-fastdemo")) && p < myargc-1)   // killough
	fastdemo = true;             // run at fastest speed possible
      else
	p = M_CheckParm ("-timedemo");
    }
  
  if (p && p < myargc-1)
    {
      strcpy(file,myargv[p+1]);
      AddDefaultExtension(file,".lmp");     // killough
      D_AddFile(file);
      printf("Playing demo %s\n",file);
    }

  // get skill / episode / map from parms

  startskill = sk_none; // jff 3/24/98 was sk_medium, just note not picked
  startepisode = 1;
  startmap = 1;
  autostart = false;

  if ((p = M_CheckParm ("-skill")) && p < myargc-1)
    {
      startskill = myargv[p+1][0]-'1';
      autostart = true;
    }

  if ((p = M_CheckParm ("-episode")) && p < myargc-1)
    {
      startepisode = myargv[p+1][0]-'0';
      startmap = 1;
      autostart = true;
    }

  if ((p = M_CheckParm ("-timer")) && p < myargc-1 && deathmatch)
    {
      int time = atoi(myargv[p+1]);
      printf("Levels will end after %d minute%s.\n", time, time>1 ? "s" : "");
    }

  if ((p = M_CheckParm ("-avg")) && p < myargc-1 && deathmatch)
    puts("Austin Virtual Gaming: Levels will end after 20 minutes");

  if (((p = M_CheckParm ("-warp")) ||      // killough 5/2/98
       (p = M_CheckParm ("-wart"))) && p < myargc-1)
    {
      if (gamemode == commercial)
	{
	  startmap = atoi(myargv[p+1]);
	  autostart = true;
	}
      else    // 1/25/98 killough: fix -warp xxx from crashing Doom 1 / UD
	if (p < myargc-2)
	  {
	    startepisode = atoi(myargv[++p]);
	    startmap = atoi(myargv[p+1]);
	    autostart = true;
	  }
    }

  //jff 1/22/98 add command line parms to disable sound and music
  {
    int nosound = M_CheckParm("-nosound");
    nomusicparm = nosound || M_CheckParm("-nomusic");
    nosfxparm   = nosound || M_CheckParm("-nosfx");
  }
  //jff end of sound/music command line parms

  // killough 3/2/98: allow -nodraw -noblit generally
  nodrawers = M_CheckParm ("-nodraw");
  noblit    = M_CheckParm ("-noblit");

  // jff 4/21/98 allow writing predefined lumps out as a wad
  if ((p = M_CheckParm("-dumplumps")) && p < myargc-1)
    WritePredefinedLumpWad(myargv[p+1]);

  puts("M_LoadDefaults: Load system defaults.");
  M_LoadDefaults();              // load before initing other systems

  bodyquesize = default_bodyquesize; // killough 10/98
  //snd_card = default_snd_card;
  //mus_card = default_mus_card;
  snd_card = 0;
  mus_card = 0;

  G_ReloadDefaults();    // killough 3/4/98: set defaults just loaded.
  // jff 3/24/98 this sets startskill if it was -1

  // 1/18/98 killough: Z_Init call moved to i_main.c

  // init subsystems
  puts("V_Init: allocate screens.");    // killough 11/98: moved down to here
  V_Init();

  D_ProcessWadPreincludes(); // killough 10/98: add preincluded wads at the end

  D_AddFile(NULL);           // killough 11/98

  puts("W_Init: Init WADfiles.");
  W_InitMultipleFiles(wadfiles); // GB 2014: note; someting in this procedure causes a GPF at exit in Windows NT OS

  //putchar('\n');     // killough 3/6/98: add a newline, by popular demand :) // GB 2014

  if (!M_CheckParm ("-noDehWad")) // GB 2015
    D_ProcessDehInWads();      // killough 10/98: now process all deh in wads

  D_ProcessDehPreincludes(); // killough 10/98: process preincluded .deh files

  // GB 2014:
  // Check for -file in shareware
  if (modifiedgame && !unlockparm)
  {
      // These are the lumps that will be checked in IWAD,
      // if any one is not present, execution will be aborted.
      static const char name[23][8]= {
        "e2m1","e2m2","e2m3","e2m4","e2m5","e2m6","e2m7","e2m8","e2m9",
        "e3m1","e3m3","e3m3","e3m4","e3m5","e3m6","e3m7","e3m8","e3m9",
        "dphoof","bfgga0","heada1","cybra1","spida1d1" };
      int i;

      if (gamemode == shareware)
        I_Error("\nYou cannot -file with the shareware version. Register!");

      // Check for fake IWAD with right name,
      // but w/o all the lumps of the registered version.
      if (gamemode == registered)
        for (i = 0;i < 23; i++)
          if (W_CheckNumForName(name[i])<0 &&
              (W_CheckNumForName)(name[i],ns_sprites)<0) // killough 4/18/98
            I_Error("\nThis is not the registered version.");
  }

  V_InitColorTranslation(); //jff 4/24/98 load color translation lumps

  // killough 2/22/98: copyright / "modified game" / SPA banners removed

  // Ty 04/08/98 - Add 5 lines of misc. data, only if nonblank
  // The expectation is that these will be set in a .bex file
  if (*startup1) puts(startup1);
  if (*startup2) puts(startup2);
  if (*startup3) puts(startup3);
  if (*startup4) puts(startup4);
  if (*startup5) puts(startup5);
  // End new startup strings

  puts("M_Init: Init miscellaneous info.");
  M_Init();

  printf("R_Init: Init DOOM refresh daemon - ");
  R_Init();

  puts("P_Init: Init Playloop state."); // GB 2014, removed newline in front of string
  P_Init();

  puts("I_Init: Setting up machine state.");
  I_Init();

  puts("D_CheckNetGame: Checking network game status.");
  D_CheckNetGame();

       if (key[KEY_RSHIFT]) {puts("Shift key is pressed, press Enter to continue..."); readkey(); } // GB 2015
  else if (key[KEY_LSHIFT]) {puts("Shift key is pressed, press Enter to continue..."); readkey(); } // GB 2015

//  if (key_shifts)           {puts("Shift key is pressed, waiting...");} do {clear_keybuf(); rest(50);} while (key_shifts); } // GB 2014
//  if (key[KEY_RSHIFT] || key[KEY_LSHIFT]) {puts("Shift key is pressed, waiting..."); do {clear_keybuf(); rest(50);}  while (key[KEY_RSHIFT] || key[KEY_LSHIFT]); } // GB 2014

// rest(3000);
  puts("S_Init: Setting up sound.");
  S_Init(snd_SfxVolume /* *8 */, snd_MusicVolume /* *8*/ );

  puts("HU_Init: Setting up heads up display.");
  HU_Init();

  puts("ST_Init: Init status bar.");
  ST_Init();

  idmusnum = -1; //jff 3/17/98 insure idmus number is blank

  // check for a driver that wants intermission stats
  if ((p = M_CheckParm ("-statcopy")) && p<myargc-1)
    {
      // for statistics driver
      extern  void* statcopy;

      // killough 5/2/98: this takes a memory
      // address as an integer on the command line!

      statcopy = (void*) atoi(myargv[p+1]);
      puts("External statistics registered.");
    }

  // start the apropriate game based on parms

  // killough 12/98: 
  // Support -loadgame with -record and reimplement -recordfrom.

  if ((slot = M_CheckParm("-recordfrom")) && (p = slot+2) < myargc)
    G_RecordDemo(myargv[p]);
  else
    {
      slot = M_CheckParm("-loadgame");
      if ((p = M_CheckParm("-record")) && ++p < myargc)
	{
	  autostart = true;
	  G_RecordDemo(myargv[p]);
	}
    }

  if ((p = M_CheckParm ("-fastdemo")) && ++p < myargc)
    {                                 // killough
      fastdemo = true;                // run at fastest speed possible
      timingdemo = true;              // show stats after quit
      G_DeferedPlayDemo(myargv[p]);
      singledemo = true;              // quit after one demo
    }
  else
    if ((p = M_CheckParm("-timedemo")) && ++p < myargc)
      {
	singletics = true;
	timingdemo = true;            // show stats after quit
	G_DeferedPlayDemo(myargv[p]);
	singledemo = true;            // quit after one demo
      }
    else
      if ((p = M_CheckParm("-playdemo")) && ++p < myargc)
	{
	  G_DeferedPlayDemo(myargv[p]);
	  singledemo = true;          // quit after one demo
	}

  if (slot && ++slot < myargc)
    {
      slot = atoi(myargv[slot]);        // killough 3/16/98: add slot info
      G_SaveGameName(file, slot);       // killough 3/22/98
      G_LoadGame(file, slot, true);     // killough 5/15/98: add command flag
    }
  else
    if (!singledemo)                    // killough 12/98
      {
	if (autostart || netgame)
	  {
	    G_InitNew(startskill, startepisode, startmap);
	    if (demorecording)
	      G_BeginRecording();
	  }
	else
	  D_StartTitle();                 // start up intro loop
      }
  
  // killough 12/98: inlined D_DoomLoop

  if (M_CheckParm ("-debugfile"))
    {
      char filename[20];
      sprintf(filename,"debug%i.txt",consoleplayer);
      printf("debug output to: %s\n",filename);
      debugfile = fopen(filename,"w");
    }
  
  I_InitGraphics();

  atexit(D_QuitNetGame);       // killough

  for (;;)
    {
      // frame syncronous IO operations
      I_StartFrame ();

      // process one or more tics
      if (singletics)
        {
          I_StartTic ();
          D_ProcessEvents ();
          G_BuildTiccmd (&netcmds[consoleplayer][maketic%BACKUPTICS]);
          if (advancedemo)
            D_DoAdvanceDemo ();
          M_Ticker ();
          G_Ticker ();
          gametic++;
          maketic++;
        }
      else
        TryRunTics (); // will run at least one tic

      // killough 3/16/98: change consoleplayer to displayplayer
      S_UpdateSounds(players[displayplayer].mo);// move positional sounds

      // Update display, next frame, with current state.
      D_Display();

      // Sound mixing for the buffer is snychronous.
      I_UpdateSound();

      // Synchronous sound output is explicitly called.
      // Update sound output.
      I_SubmitSound();
    }
}

//----------------------------------------------------------------------------
//
// $Log: d_main.c,v $
// Revision 1.3  2000-08-12 21:29:25  fraggle
// change license header
//
// Revision 1.2  2000/07/29 23:28:23  fraggle
// fix ambiguous else warnings
//
// Revision 1.1.1.1  2000/07/29 13:20:39  fraggle
// imported sources
//
// Revision 1.47  1998/05/16  09:16:51  killough
// Make loadgame checksum friendlier
//
// Revision 1.46  1998/05/12  10:32:42  jim
// remove LEESFIXES from d_main
//
// Revision 1.45  1998/05/06  15:15:46  jim
// Documented IWAD routines
//
// Revision 1.44  1998/05/03  22:26:31  killough
// beautification, declarations, headers
//
// Revision 1.43  1998/04/24  08:08:13  jim
// Make text translate tables lumps
//
// Revision 1.42  1998/04/21  23:46:01  jim
// Predefined lump dumper option
//
// Revision 1.39  1998/04/20  11:06:42  jim
// Fixed print of IWAD found
//
// Revision 1.37  1998/04/19  01:12:19  killough
// Fix registered check to work with new lump namespaces
//
// Revision 1.36  1998/04/16  18:12:50  jim
// Fixed leak
//
// Revision 1.35  1998/04/14  08:14:18  killough
// Remove obsolete adaptive_gametics code
//
// Revision 1.34  1998/04/12  22:54:41  phares
// Remaining 3 Setup screens
//
// Revision 1.33  1998/04/11  14:49:15  thldrmn
// Allow multiple deh/bex files
//
// Revision 1.32  1998/04/10  06:31:50  killough
// Add adaptive gametic timer
//
// Revision 1.31  1998/04/09  09:18:17  thldrmn
// Added generic startup strings for BEX use
//
// Revision 1.30  1998/04/06  04:52:29  killough
// Allow demo_insurance=2, fix fps regression wrt redrawsbar
//
// Revision 1.29  1998/03/31  01:08:11  phares
// Initial Setup screens and Extended HELP screens
//
// Revision 1.28  1998/03/28  15:49:37  jim
// Fixed merge glitches in d_main.c and g_game.c
//
// Revision 1.27  1998/03/27  21:26:16  jim
// Default save dir offically . now
//
// Revision 1.26  1998/03/25  18:14:21  jim
// Fixed duplicate IWAD search in .
//
// Revision 1.25  1998/03/24  16:16:00  jim
// Fixed looking for wads message
//
// Revision 1.23  1998/03/24  03:16:51  jim
// added -iwad and -save parms to command line
//
// Revision 1.22  1998/03/23  03:07:44  killough
// Use G_SaveGameName, fix some remaining default.cfg's
//
// Revision 1.21  1998/03/18  23:13:54  jim
// Deh text additions
//
// Revision 1.19  1998/03/16  12:27:44  killough
// Remember savegame slot when loading
//
// Revision 1.18  1998/03/10  07:14:58  jim
// Initial DEH support added, minus text
//
// Revision 1.17  1998/03/09  07:07:45  killough
// print newline after wad files
//
// Revision 1.16  1998/03/04  08:12:05  killough
// Correctly set defaults before recording demos
//
// Revision 1.15  1998/03/02  11:24:25  killough
// make -nodraw -noblit work generally, fix ENDOOM
//
// Revision 1.14  1998/02/23  04:13:55  killough
// My own fix for m_misc.c warning, plus lots more (Rand's can wait)
//
// Revision 1.11  1998/02/20  21:56:41  phares
// Preliminarey sprite translucency
//
// Revision 1.10  1998/02/20  00:09:00  killough
// change iwad search path order
//
// Revision 1.9  1998/02/17  06:09:35  killough
// Cache D_DoomExeDir and support basesavegame
//
// Revision 1.8  1998/02/02  13:20:03  killough
// Ultimate Doom, -fastdemo -nodraw -noblit support, default_compatibility
//
// Revision 1.7  1998/01/30  18:48:15  phares
// Changed textspeed and textwait to functions
//
// Revision 1.6  1998/01/30  16:08:59  phares
// Faster end-mission text display
//
// Revision 1.5  1998/01/26  19:23:04  phares
// First rev with no ^Ms
//
// Revision 1.4  1998/01/26  05:40:12  killough
// Fix Doom 1 crashes on -warp with too few args
//
// Revision 1.3  1998/01/24  21:03:04  jim
// Fixed disappearence of nomonsters, respawn, or fast mode after demo play or IDCLEV
//
// Revision 1.1.1.1  1998/01/19  14:02:53  rand
// Lee's Jan 19 sources
//
//----------------------------------------------------------------------------
