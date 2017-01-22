#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdarg.h>
#include <errno.h>

// The .xs includes this file, and provides definitions for the
//  logging functions, and also perl's "croak".
// This file can be compiled on its own, separate from the .xs
//  with these alternate versions of the maxros.
#ifndef log_error
 #include <stdio.h>
 #define log_info_enabled()  log_enabled("is_info")
 #define log_debug_enabled() log_enabled("is_debug")
 #define log_trace_enabled() log_enabled("is_trace")
 static int log_enabled(const char *method) { return 1; }
 #define log_error(x...) fprintf(stderr, "\nerror: " x)
 #define log_info(x...) fprintf(stderr, "\n" x)
 #define log_debug(x...) fprintf(stderr, "\ndebug: " x)
 #define log_trace(x...) fprintf(stderr, "\ntrace: " x)
 #define croak(x...) do { fprintf(stderr, "\nfatal: " x); exit(2); } while (0)
#endif

typedef struct UIContext {
	Display *dpy;
	XVisualInfo *xvisi;
	Colormap cmap;
	Window wnd;
	GLXContext glctx;
	int contextReady;
	SV* error_handler;
} UIContext;

static int UIContext_X_Fatal= 0; // global flag to prevent running more X calls during error handler

UIContext *UIContext_new();
void UIContext_free(UIContext *cx);
void UIContext_connect(UIContext *cx, const char* dispName);
void UIContext_disconnect(UIContext *cx);
void UIContext_setup_window(UIContext *cx, int win_x, int win_y, int win_w, int win_h);
void UIContext_get_screen_metrics(UIContext *cx, int *w, int *h, int *w_mm, int *h_mm);
void UIContext_get_window_rect(UIContext *cx, int *x, int *y, unsigned int *width, unsigned int *height);
void UIContext_flip(UIContext *cx);
void UIContext_set_error_handler(UIContext *cx, SV* coderef);
static void UIContext_tracked_add(UIContext *cx);
static void UIContext_tracked_rm(UIContext *cx);

UIContext *UIContext_new() {
	UIContext *cx= (UIContext*) calloc(1, sizeof(UIContext));
	log_trace("XS UIContext allocated");
	return cx;
}

void UIContext_free(UIContext *cx) {
	UIContext_set_error_handler(cx, NULL);
	UIContext_disconnect(cx);
	free(cx);
	log_trace("XS UIContext freed");
}

// Written according to http://www.mesa3d.org/MiniGLX.html
// Also, see http://tronche.com/gui/x/xlib/
void UIContext_connect(UIContext *cx, const char* dispName) {
	int en_debug= log_debug_enabled();
	int en_trace= log_trace_enabled();
	UIContext_disconnect(cx);

	if (!dispName || !dispName[0]) dispName= getenv("DISPLAY");
	if (!dispName || !dispName[0]) dispName= ":0";
	if (en_debug)
		log_debug("connecting to %s", dispName);
	
	errno= 0;
	cx->dpy= XOpenDisplay(dispName);
	if (!cx->dpy)
		croak("XOpenDisplay failed%s%s", errno? ": ":"", errno? strerror(errno) : "");
}

void UIContext_disconnect(UIContext *cx) {
	cx->contextReady= 0;

	// delete all Xlib objects
	log_trace("Freeing any graphic objects");
	if (UIContext_X_Fatal) return;  // can't de-allocate anything anyway
	if (cx->glctx) glXDestroyContext(cx->dpy, cx->glctx), cx->glctx= 0;
	if (cx->wnd)   XDestroyWindow(cx->dpy, cx->wnd), cx->wnd= 0;
	if (cx->cmap)  XFreeColormap(cx->dpy, cx->cmap), cx->cmap= 0;
	if (cx->xvisi) XFree(cx->xvisi), cx->xvisi= NULL;
	if (cx->dpy) {
		log_debug("Disconnecting from display");
		XCloseDisplay(cx->dpy);
		cx->dpy= NULL;
	}
}

void UIContext_setup_window(UIContext *cx, int win_x, int win_y, int win_w, int win_h) {
	int en_debug= log_debug_enabled();
	int en_trace= log_trace_enabled();
	
	if (cx->glctx) glXDestroyContext(cx->dpy, cx->glctx), cx->glctx= 0;
	if (cx->wnd)   XDestroyWindow(cx->dpy, cx->wnd), cx->wnd= 0;
	if (cx->cmap)  XFreeColormap(cx->dpy, cx->cmap), cx->cmap= 0;
	if (cx->xvisi) XFree(cx->xvisi), cx->xvisi= NULL;
	cx->contextReady= 0;

	if (!cx->dpy)
		UIContext_connect(cx, NULL);
	
	if (en_debug)
		log_debug("Testing for glX support");
	errno= 0;
	if (!glXQueryExtension(cx->dpy, NULL, NULL))
		croak("Display does not support GLX");
		
	if (en_trace)
		log_trace("calling glXChooseVisual");
	int attrs[]= { GLX_USE_GL, GLX_RGBA,
		GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
		GLX_DOUBLEBUFFER, None
	};
	errno= 0;
	cx->xvisi= glXChooseVisual(cx->dpy, 0, attrs);
	if (!cx->xvisi)
		croak("glXChooseVisual failed%s%s", errno? ": ":"", errno? strerror(errno) : "");
	if (en_debug)
		log_debug("Selected Visual 0x%.2x", (int) cx->xvisi->visualid);
	
	if (en_trace)
		log_trace("calling XCreateColormap");
	errno= 0;
	cx->cmap= XCreateColormap(cx->dpy, RootWindow(cx->dpy, 0), cx->xvisi->visual, AllocNone);
	if (!cx->cmap)
		croak("XCreateColormap failed: %s%s", errno? ": ":"", errno? strerror(errno) : "");
	
	XSetWindowAttributes wndAttrs;
	memset(&wndAttrs, 0, sizeof(wndAttrs));
	wndAttrs.background_pixel= 0;
	wndAttrs.border_pixel= 0;
	wndAttrs.colormap= cx->cmap;
	
	int w, h;
	UIContext_get_screen_metrics(cx, &w, &h, NULL, NULL);
	if (en_debug)
		log_debug("X11 screen is %dx%d", w, h);
	if (win_w <= 0) win_w= w;
	if (win_h <= 0) win_h= h;
	if (en_trace)
		log_trace("calling XCreateWindow( {%d,%d,%d,%d} )", win_x, win_y, win_w, win_h);
	errno= 0;
	cx->wnd= XCreateWindow(cx->dpy, RootWindow(cx->dpy, 0),
		win_x, win_y, win_w, win_h, 0, cx->xvisi->depth,
		InputOutput, cx->xvisi->visual,
		CWBackPixel|CWBorderPixel|CWColormap, &wndAttrs);
	if (!cx->wnd)
		croak("XCreateWindow failed%s%s", errno? ": ":"", errno? strerror(errno) : "");

	if (en_trace)
		log_trace("setting invisible cursor");
	XColor black;
	static char noData[] = { 0,0,0,0,0,0,0,0 };
	black.red = black.green = black.blue = 0;
	Pixmap bitmapNoData= XCreateBitmapFromData(cx->dpy, cx->wnd, noData, 8, 8);
	if (!bitmapNoData)
		croak("XCreateBitmapFromData failed");
	Cursor invisibleCursor= XCreatePixmapCursor(cx->dpy, bitmapNoData, bitmapNoData, &black, &black, 0, 0);
	if (!invisibleCursor)
		croak("XCreatePixmapCursor failed");
	XDefineCursor(cx->dpy, cx->wnd, invisibleCursor);
	XFreeCursor(cx->dpy, invisibleCursor);
	
	if (en_trace)
		log_trace("calling XMapWindow");
	errno= 0;
	XMapWindow(cx->dpy, cx->wnd);
	
	if (en_trace)
		log_trace("calling glXCreateContext");
	errno= 0;
	cx->glctx= glXCreateContext(cx->dpy, cx->xvisi, NULL, 1);
	if (!cx->glctx)
		croak("glXCreateContext failed%s%s", errno? ": ":"", errno? strerror(errno) : "");
	
	if (en_trace)
		log_trace("calling glXMakeCurrent");
	if (!glXMakeCurrent(cx->dpy, cx->wnd, cx->glctx))
		log_error("glXMakeCurrent failed%s%s", errno? ": ":"", errno? strerror(errno) : "");

	if (en_trace)
		log_trace("setup_window succeeded");
	
	log_info("OpenGL vendor: %s, renderer: %s, version: %s",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
	
	cx->contextReady= 1;
}

void UIContext_get_window_rect(
	UIContext *cx,
	int *x, int *y,	unsigned int *width, unsigned int *height
) {
	if (!cx->dpy) croak("Not connected to a display");
	if (!cx->wnd) croak("Window not created yet");
	Window root;
	unsigned int border= 0, depth= 0;
		XGetGeometry(cx->dpy, cx->wnd, &root, x, y, width, height, &border, &depth);
}

void UIContext_get_screen_metrics(UIContext *cx, int *w, int *h, int *w_mm, int *h_mm) {
	if (!cx->dpy) croak("Not connected to a display");
	
	errno= 0;
	Screen *s= DefaultScreenOfDisplay(cx->dpy);
	if (!s)
		croak("DefaultScreenOfDisplay failed%s%s", errno? ": ":"", errno? strerror(errno) : "");
	
	if (w) *w= WidthOfScreen(s);
	if (h) *h= HeightOfScreen(s);
	if (w_mm) *w_mm= WidthMMOfScreen(s);
	if (h_mm) *h_mm= HeightMMOfScreen(s);
}

void UIContext_flip(UIContext *cx) {
	if (cx->contextReady) {
		glXSwapBuffers(cx->dpy, cx->wnd);
		glFlush();
	}
}

/*
What a mess.   So my OCD insisted that I add a handler for
XLib errors so that the perl program has a chance to exit
gracefully.  However XLib is designed stupidly and gives
you exactly one callback and forcibly terminates the program
if te callback returns, regardless of what you do to recover.

I wanted the callbacks to be something you set on the UIContext
object rather than global, so I was forced to do this elaborate
tracking of UIContext objects.  Note that the destructor for
UIContext un-sets the handler thus removing it from this list
before it is destroyed.

This is not thread safe, but generally perl isn't either.
*/

static UIContext **UIContext_tracked= NULL;
static int UIContext_count= 0;

int UIContext_X_IO_error_handler(Display *d) {
	int i;
	log_error("Encountered fatal X I/O error.  XLib forces us to exit.");
	UIContext_X_Fatal= 1;
	for (i= 0; i < UIContext_count; i++) {
		dSP;
		PUSHMARK(SP);
		call_sv(UIContext_tracked[i]->error_handler, G_DISCARD|G_NOARGS);
	}
	return 0;
}
static void UIContext_tracked_add(UIContext *cx) {
	UIContext **larger= realloc((void*)UIContext_tracked,
		(UIContext_count+1) * sizeof(UIContext*));
	if (!larger)
		croak("realloc failed for setting up error handler");
	// If array was NULL, then this is the first time it was
	//  used and we should set the XLib handler
	if (!UIContext_count) {
		log_debug("Setting X IO error handler");
		XSetIOErrorHandler(&UIContext_X_IO_error_handler);
	}
	UIContext_tracked= larger;
	UIContext_tracked[UIContext_count++]= cx;
}
static void UIContext_tracked_rm(UIContext *cx) {
	int i;
	for (i= 0; i < UIContext_count; i++) {
		if (UIContext_tracked[i] == cx) {
			UIContext_tracked[i]= UIContext_tracked[UIContext_count-1];
			UIContext_count--;
			break;
		}
	}
}

void UIContext_set_error_handler(UIContext *cx, SV* coderef) {
	if (coderef && !cx->error_handler)
		UIContext_tracked_add(cx);
	else if (cx->error_handler && !coderef)
		UIContext_tracked_rm(cx);
	
	if (cx->error_handler)
		sv_2mortal(cx->error_handler);
	
	cx->error_handler= coderef;
	
	if (cx->error_handler)
		SvREFCNT_inc(cx->error_handler);
}

