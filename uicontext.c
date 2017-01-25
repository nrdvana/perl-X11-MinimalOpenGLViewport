#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdarg.h>

// The .xs includes this file, and provides definitions for the
//  logging functions, and also perl's "croak".
// This file can be compiled on its own, separate from the .xs
//  with these alternate versions of the macros.
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
	Display     *dpy;
	
	// Information about the GLX subsystem, initialized during connect
	int          glx_version_major;
	int          glx_version_minor;
	const char  *glx_extensions;
	
	// GL context, initialized by setup_glcontext
	XVisualInfo *xvisi;    // Pointer to chosen X visual
	GLXContext   glctx;    // Pointer to GL context struct
	GLXContextID glctx_id; // The X11 ID of the GL context, sharable between processes
	int          glctx_is_imported;
	
	// X Window or X Pixmap rendering target, initialized by setup_window or setup_pixmap
	int          target_ready;
	Colormap     cmap;
	Window       wnd;
} UIContext;

static int UIContext_X_handler_installed= 0;
static int UIContext_X_Fatal= 0; // global flag to prevent running more X calls during error handler
#define CROAK_IF_XLIB_FATAL()     do { if (UIContext_X_Fatal) croak("Cannot call XLib functions after a fatal error"); } while(0)
#define CROAK_IF_NO_DISPLAY(cx)   do { if (!cx->dpy) croak("Not connected to a display"); } while (0)
#define CROAK_IF_NO_GLCONTEXT(cx) do { if (!cx->glctx) croak("No GL Context"); } while (0)
#define CROAK_IF_NO_WINDOW(cx)    do { if (!cx->wnd) croak("Window not created yet"); } while (0)
#define CROAK_IF_NO_TARGET(cx)    do { if (!cx->target_ready) croak("OpenGL context has no target"); } while (0)

int UIContext_X_IO_error_handler(Display *d);
int UIContext_X_error_handler(Display *d, XErrorEvent *e);

UIContext *UIContext_new();
void UIContext_connect(UIContext *cx, const char* dispName);
void UIContext_setup_glcontext(UIContext *cx, GLXContextID link_to, int direct);
void UIContext_setup_window(UIContext *cx, int win_x, int win_y, int win_w, int win_h);
void UIContext_get_screen_metrics(UIContext *cx, int *w, int *h, int *w_mm, int *h_mm);
void UIContext_get_window_rect(UIContext *cx, int *x, int *y, unsigned int *width, unsigned int *height);
void UIContext_flip(UIContext *cx);
void UIContext_teardown_target(UIContext *cx);
void UIContext_teardown_glcontext(UIContext *cx);
void UIContext_disconnect(UIContext *cx);
void UIContext_free(UIContext *cx);

typedef GLXContext ( * PFNGLXIMPORTCONTEXTEXTPROC) (Display* dpy, GLXContextID contextID);
typedef GLXContextID ( * PFNGLXGETCONTEXTIDEXTPROC) (const GLXContext context);
typedef void ( * PFNGLXFREECONTEXTEXTPROC) (Display* dpy, GLXContext context);

UIContext *UIContext_new() {
	UIContext *cx= (UIContext*) calloc(1, sizeof(UIContext));
	log_trace("XS UIContext allocated");
	return cx;
}

void UIContext_free(UIContext *cx) {
	UIContext_disconnect(cx);
	free(cx);
	log_trace("XS UIContext freed");
}

// Written according to http://www.mesa3d.org/MiniGLX.html
// Also, see http://tronche.com/gui/x/xlib/

void UIContext_connect(UIContext *cx, const char* dispName) {
	CROAK_IF_XLIB_FATAL();

	int en_debug= log_debug_enabled();
	int en_trace= log_trace_enabled();

	// Ensure XLib error handlers have been installed.
	// This happens globally, but lazy-initialize in the spirit of fast startups.
	if (!UIContext_X_handler_installed) {
		XSetIOErrorHandler(&UIContext_X_IO_error_handler);
		XSetErrorHandler(&UIContext_X_error_handler);
		UIContext_X_handler_installed= 1;
	}

	// teardown any previous connection
	UIContext_disconnect(cx);

	if (en_debug)
		log_debug("connecting to %s", dispName);

	cx->dpy= XOpenDisplay(dispName);
	if (!cx->dpy)
		croak("XOpenDisplay failed");

	if (en_trace)
		log_trace("Getting GLX version");

	if (!glXQueryVersion(cx->dpy, &cx->glx_version_major, &cx->glx_version_minor))
		croak("Display does not support GLX");
	if (en_debug)
		log_debug("GLX Version %d.%d", cx->glx_version_major, cx->glx_version_minor);

	// glXQueryExtensionsString doesn't exist before 1.1
	if (cx->glx_version_major >= 1 && cx->glx_version_minor >= 1) {
		if (en_trace)
			log_trace("Getting GLX extensions");
		// TODO: find out if this needs freed.  Docs don't say, and all examples I can find
		// hold onto the pointer for the life of the program.
		cx->glx_extensions= glXQueryExtensionsString(cx->dpy, DefaultScreen(cx->dpy));
		if (en_trace)
			log_trace("GLX Extensions supported: %s", cx->glx_extensions);
	}
}

void UIContext_disconnect(UIContext *cx) {
	// delete all Xlib objects
	log_trace("Freeing any graphic objects");
	UIContext_teardown_glcontext(cx);
	
	cx->glx_version_major= 0;
	cx->glx_version_minor= 0;
	if (cx->dpy) {
		if (UIContext_X_Fatal) {
			log_trace("Would free objects, but XLib is broken and we can't, so leak them");
		} else {
			log_debug("Disconnecting from display");
			XCloseDisplay(cx->dpy);
		}
		cx->dpy= NULL;
	}
}

void UIContext_setup_glcontext(UIContext *cx, GLXContextID link_to, int direct) {
	PFNGLXIMPORTCONTEXTEXTPROC import_context_fn;
	PFNGLXGETCONTEXTIDEXTPROC  get_context_id_fn;
	
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	UIContext_teardown_glcontext(cx);

	int en_debug= log_debug_enabled();
	int en_trace= log_trace_enabled();

	if (en_trace)
		log_trace("calling glXChooseVisual");
	int attrs[]= { GLX_USE_GL, GLX_RGBA,
		GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
		GLX_DOUBLEBUFFER, None
	};
	cx->xvisi= glXChooseVisual(cx->dpy, DefaultScreen(cx->dpy), attrs);
	if (!cx->xvisi)
		croak("glXChooseVisual failed");
	if (en_debug)
		log_debug("Selected Visual 0x%.2X", (int) cx->xvisi->visualid);

	// Either create a new context, or connect to an indirect one
	int support_import_context= cx->glx_extensions
		&& strstr(cx->glx_extensions, "GLX_EXT_import_context")
		&& (import_context_fn= (PFNGLXIMPORTCONTEXTEXTPROC) glXGetProcAddress("glXImportContextEXT"))
		&& (get_context_id_fn= (PFNGLXGETCONTEXTIDEXTPROC)  glXGetProcAddress("glXGetContextIDEXT"));

	if (link_to) {
		if (!support_import_context)
			croak("Can't connect to shared GL context; extension not supported by this X server.");

		cx->glctx_is_imported= 1;
		if (en_trace)
			log_trace("calling glXImportContextEXT");
		cx->glctx= import_context_fn(cx->dpy, link_to);
		if (!cx->glctx)
			croak("Can't import remote GL context %d", link_to);
	}
	else {
		if (en_trace)
			log_trace("calling glXCreateContext");
		cx->glctx= glXCreateContext(cx->dpy, cx->xvisi, NULL, direct);
		if (!cx->glctx)
			croak("glXCreateContext failed");
	}

	cx->glctx_id= support_import_context? get_context_id_fn(cx->glctx) : 0;
}

void UIContext_teardown_glcontext(UIContext *cx) {
	PFNGLXFREECONTEXTEXTPROC free_context_fn;
	
	UIContext_teardown_target(cx);
	
	if (!UIContext_X_Fatal && cx->glctx) {
		if (cx->glctx_is_imported) {
			if (!(free_context_fn= (PFNGLXFREECONTEXTEXTPROC) glXGetProcAddress("glXFreeContextEXT")))
				croak("Can't load glXFreeContextEXT"); // should never happen if we were able to import it
			free_context_fn(cx->dpy, cx->glctx);
		}
		else
			glXDestroyContext(cx->dpy, cx->glctx);
	}
	cx->glctx= NULL;
	cx->glctx_id= 0;
	cx->glctx_is_imported= 0;
	
	if (!UIContext_X_Fatal && cx->xvisi) XFree(cx->xvisi);
	cx->xvisi= NULL;
}

static Bool WaitForWndMapped( Display *dpy, XEvent *event, XPointer arg ) {
    return (event->type == MapNotify) && (event->xmap.window == (Window) arg);
}
void UIContext_setup_window(UIContext *cx, int win_x, int win_y, int win_w, int win_h) {
	XColor black;
	static char noData[] = { 0,0,0,0,0,0,0,0 };
	int w, h, en_debug, en_trace;
	XSetWindowAttributes wndAttrs;
	Pixmap bitmapNoData;
	Cursor invisibleCursor;
	XEvent event;
	XSizeHints sh;

	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	CROAK_IF_NO_GLCONTEXT(cx);

	UIContext_teardown_target(cx);

	en_debug= log_debug_enabled();
	en_trace= log_trace_enabled();

	if (en_trace)
		log_trace("calling XCreateColormap");
	cx->cmap= XCreateColormap(cx->dpy, DefaultRootWindow(cx->dpy), cx->xvisi->visual, AllocNone);
	if (!cx->cmap)
		croak("XCreateColormap failed");
	
	memset(&wndAttrs, 0, sizeof(wndAttrs));
	wndAttrs.background_pixel= 0;
	wndAttrs.border_pixel= 0;
	wndAttrs.colormap= cx->cmap;
	
	UIContext_get_screen_metrics(cx, &w, &h, NULL, NULL);
	if (en_debug)
		log_debug("X11 screen is %dx%d", w, h);

	if (win_w <= 0) win_w= w;
	if (win_h <= 0) win_h= h;
	if (en_trace)
		log_trace("calling XCreateWindow( {%d,%d,%d,%d} )", win_x, win_y, win_w, win_h);
	
	cx->wnd= XCreateWindow(cx->dpy, DefaultRootWindow(cx->dpy),
		win_x, win_y, win_w, win_h, 0, cx->xvisi->depth,
		InputOutput, cx->xvisi->visual,
		CWBackPixel|CWBorderPixel|CWColormap, &wndAttrs);
	if (!cx->wnd)
		croak("XCreateWindow failed");

	if (en_trace)
		log_trace("setting invisible cursor");
	black.red = black.green = black.blue = 0;
	bitmapNoData= XCreateBitmapFromData(cx->dpy, cx->wnd, noData, 8, 8);
	if (!bitmapNoData)
		croak("XCreateBitmapFromData failed");
	invisibleCursor= XCreatePixmapCursor(cx->dpy, bitmapNoData, bitmapNoData, &black, &black, 0, 0);
	if (!invisibleCursor)
		croak("XCreatePixmapCursor failed");
	XDefineCursor(cx->dpy, cx->wnd, invisibleCursor);
	XFreeCursor(cx->dpy, invisibleCursor);

	sh.width = sh.min_width = win_w;
	sh.height = sh.min_height = win_h;
	sh.x = 0;
	sh.y = 0;
	sh.flags = PSize | PMinSize | PPosition;
	if (en_trace)
		log_trace("calling XSetWMNormalHints");
	XSetWMNormalHints(cx->dpy, cx->wnd, &sh);

	if (en_trace)
		log_trace("calling XMapWindow");
	XMapWindow(cx->dpy, cx->wnd);
	//XIfEvent(cx->dpy, &event, WaitForWndMapped, (XPointer) cx->wnd);

	if (en_trace)
		log_trace("calling glXMakeCurrent");
	if (!glXMakeCurrent(cx->dpy, cx->wnd, cx->glctx))
		croak("glXMakeCurrent failed");

	if (en_trace)
		log_trace("setup_window succeeded");

	log_info("OpenGL vendor: %s, renderer: %s, version: %s",
		glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

	cx->target_ready= 1;
}

void UIContext_teardown_target(UIContext *cx) {
	if (!UIContext_X_Fatal && cx->wnd)   XDestroyWindow(cx->dpy, cx->wnd);
	cx->wnd= 0;
	if (!UIContext_X_Fatal && cx->cmap)  XFreeColormap(cx->dpy, cx->cmap);
	cx->cmap= 0;
	cx->target_ready= 0;
}

void UIContext_get_window_rect(
	UIContext *cx,
	int *x, int *y,	unsigned int *width, unsigned int *height
) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	CROAK_IF_NO_WINDOW(cx);
	Window root;
	unsigned int border= 0, depth= 0;
	XGetGeometry(cx->dpy, cx->wnd, &root, x, y, width, height, &border, &depth);
}

void UIContext_get_screen_metrics(UIContext *cx, int *w, int *h, int *w_mm, int *h_mm) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	
	Screen *s= DefaultScreenOfDisplay(cx->dpy);
	if (!s)
		croak("DefaultScreenOfDisplay failed");
	
	if (w) *w= WidthOfScreen(s);
	if (h) *h= HeightOfScreen(s);
	if (w_mm) *w_mm= WidthMMOfScreen(s);
	if (h_mm) *h_mm= HeightMMOfScreen(s);
}

void UIContext_flip(UIContext *cx) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	CROAK_IF_NO_TARGET(cx);

	glXSwapBuffers(cx->dpy, cx->wnd);
	glFlush();
}

void UIContext_get_xlib_error_codes(HV* dest) {
	#define E(x) hv_stores(dest, #x, newSViv(x));
	E(BadAccess)
	E(BadAlloc)
	E(BadAtom)
	E(BadColor)
	E(BadCursor)
	E(BadDrawable)
	E(BadFont)
	E(BadGC)
	E(BadIDChoice)
	E(BadImplementation)
	E(BadLength)
	E(BadMatch)
	E(BadName)
	E(BadPixmap)
	E(BadRequest)
	E(BadValue)
	E(BadWindow)
	#undef E
}

int UIContext_X_error_handler(Display *d, XErrorEvent *e) {
	log_debug("XLib non-fatal error handler triggered");
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	EXTEND(SP, 1);
	
	// Convert the XErrorEvent to a hashref
	HV* err= newHV();
	hv_stores(err, "type",         newSViv((int) e->type));
	hv_stores(err, "display",      newSVpvf("%p", (void*) e->display));
	hv_stores(err, "serial",       newSViv((int) e->serial));
	hv_stores(err, "error_code",   newSViv((int) e->error_code));
	hv_stores(err, "request_code", newSViv((int) e->request_code));
	hv_stores(err, "minor_code",   newSViv((int) e->minor_code));
	hv_stores(err, "resourceid",   newSViv((int) e->resourceid));
	
	PUSHs(sv_2mortal(newRV_noinc((SV*)err)));
	PUTBACK;
	call_pv("X11::MinimalOpenGLContext::_X11_error", G_VOID|G_DISCARD|G_EVAL|G_KEEPERR);
	FREETMPS;
	LEAVE;
	return 0;
}

/*

What a mess.   So XLib has a stupid design where they forcibly abort the
program when an I/O error occurs and the X server is lost.  Even if you
install the error handler, they expect you to abort the program and they
do it for you if you return.  Furthermore, they tell you that you may not
call any more XLib functions at all.

Luckily we can cheat with croak (longjmp) back out of the callback and
avoid the forced program exit.  However now we can't officially use XLib
again for the duration of the program, and there could be lost resources
from our longjmp.  So, set a global flag to prevent any re-entry into XLib.

*/
int UIContext_X_IO_error_handler(Display *d) {
	int i;
	UIContext_X_Fatal= 1; // prevent UIContexts from calling back into XLib
	log_debug("XLib fatal error handler triggered");
	dSP;
	PUSHMARK(SP);
	call_pv("X11::MinimalOpenGLContext::_X11_error_fatal", G_VOID|G_DISCARD|G_NOARGS|G_EVAL|G_KEEPERR);
	croak("Fatal X11 I/O Error"); // longjmp past XLib, which wants to kill us
	return 0;
}
