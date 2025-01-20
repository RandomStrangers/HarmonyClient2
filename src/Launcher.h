#ifndef HC_LAUNCHER_H
#define HC_LAUNCHER_H
#include "Bitmap.h"
HC_BEGIN_HEADER

/* Implements the launcher part of the game.
	Copyright 2014-2023 ClassiCube | Licensed under BSD-3
*/
struct LScreen;
struct FontDesc;
struct Context2D;
struct HttpRequest;

/* The screen/menu currently being shown */
extern struct LScreen* Launcher_Active;

/* Whether at the next tick, the launcher window should proceed to stop displaying frames and subsequently exit */
extern hc_bool Launcher_ShouldExit;
/* Whether game should be updated on exit */
extern hc_bool Launcher_ShouldUpdate;
/* (optional) Hash of the server the game should automatically try to connect to after signing in */
extern hc_string Launcher_AutoHash;
/* The username of currently active user */
extern hc_string Launcher_Username;
/* Whether to show empty servers in the server list */
extern hc_bool Launcher_ShowEmptyServers;

struct LauncherTheme {
	/* Whether to use stone tile background like minecraft.net */
	hc_bool ClassicBackground;
	/* Base colour of pixels before any widgets are drawn */
	BitmapCol BackgroundColor;
	/* Colour of pixels on the 4 line borders around buttons */
	BitmapCol ButtonBorderColor;
	/* Colour of button when user has mouse over it */
	BitmapCol ButtonForeActiveColor;
	/* Colour of button when user does not have mouse over it */
	BitmapCol ButtonForeColor;
	/* Colour of line at top of buttons to give them a less flat look*/
	BitmapCol ButtonHighlightColor;
};
/* Currently active theme */
extern struct LauncherTheme Launcher_Theme;
/* Modern / enhanced theme */
extern const struct LauncherTheme Launcher_ModernTheme;
/* Minecraft Classic theme */
extern const struct LauncherTheme Launcher_ClassicTheme;
/* Custom Nordic style theme */
extern const struct LauncherTheme Launcher_NordicTheme;

extern const struct LauncherTheme Launcher_CoreTheme;

/* Loads theme from options. */
void Launcher_LoadTheme(void);
/* Saves the theme to options. */
/* NOTE: Does not save options file itself. */
void Launcher_SaveTheme(void);

/* Whether logo should be drawn using bitmapped text */
hc_bool Launcher_BitmappedText(void);
/* Draws title styled text using the given font */
void Launcher_DrawTitle(struct FontDesc* font, const char* text, struct Context2D* ctx);
/* Allocates a font appropriate for drawing title text */
void Launcher_MakeTitleFont(struct FontDesc* font);

/* Attempts to load font and terrain from texture pack. */
void Launcher_TryLoadTexturePack(void);
/* Fills the given region of the given bitmap with the default background */
void Launcher_DrawBackground(struct Context2D* ctx, int x, int y, int width, int height);
/* Fills the entire contents of the given bitmap with the default background */
/* NOTE: Also draws titlebar at top, if current screen permits it */
void Launcher_DrawBackgroundAll(struct Context2D* ctx);

/* Sets currently active screen/menu, freeing old one. */
void Launcher_SetScreen(struct LScreen* screen);
/* Attempts to start the game by connecting to the given server. */
hc_bool Launcher_ConnectToServer(const hc_string* hash);
/* Launcher main loop. */
void Launcher_Run(void);
/* Starts the game from the given arguments. */
hc_bool Launcher_StartGame(const hc_string* user, const hc_string* mppass, const hc_string* ip, const hc_string* port, const hc_string* server, int numStates);
/* Prints information about a http error to dst. (for status widget) */
/* If req->result is non-zero, also displays a dialog box on-screen. */
void Launcher_DisplayHttpError(struct HttpRequest* req, const char* action, hc_string* dst);

HC_END_HEADER
#endif
