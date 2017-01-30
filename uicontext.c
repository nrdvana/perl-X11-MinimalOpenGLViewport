#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdarg.h>
#include <stdlib.h>

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

#if None != 0
 #error Code makes invalid assumtion about XID "None"!
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
	
	// X Window or X Pixmap rendering target, initialized by set_gl_target
	Window       target;
} UIContext;

static int UIContext_X_handler_installed= 0;
static int UIContext_X_Fatal= 0; // global flag to prevent running more X calls during error handler
#define CROAK_IF_XLIB_FATAL()     do { if (UIContext_X_Fatal) croak("Cannot call XLib functions after a fatal error"); } while(0)
#define CROAK_IF_NO_DISPLAY(cx)   do { if (!cx->dpy) croak("Not connected to a display"); } while (0)
#define CROAK_IF_NO_GLCONTEXT(cx) do { if (!cx->glctx) croak("No GL Context"); } while (0)
#define CROAK_IF_NO_TARGET(cx)    do { if (!cx->target) croak("OpenGL context has no target"); } while (0)

int UIContext_X_IO_error_handler(Display *d);
int UIContext_X_error_handler(Display *d, XErrorEvent *e);

UIContext *UIContext_new();
void UIContext_free(UIContext *cx);
void UIContext_connect(UIContext *cx, const char* dispName);
void UIContext_disconnect(UIContext *cx);
void UIContext_get_screen_metrics(UIContext *cx, int *w, int *h, int *w_mm, int *h_mm);

void UIContext_setup_glcontext(UIContext *cx, int direct, GLXContextID link_to);
void UIContext_teardown_glcontext(UIContext *cx);

void UIContext_get_window_rect(UIContext *cx, Window wnd, int *x, int *y, unsigned int *width, unsigned int *height);
void UIContext_glXSwapBuffers(UIContext *cx);

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

int UIContext_get_xlib_socket(UIContext *cx) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	
	return ConnectionNumber(cx->dpy);
}

int UIContext_wait_xlib_socket(UIContext *cx, struct timeval tv) {
	fd_set fds;
	int ready, x11_fd;

	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	x11_fd= ConnectionNumber(cx->dpy);
	FD_ZERO(&fds);
	FD_SET(x11_fd, &fds);
	return select(x11_fd+1, &fds, NULL, &fds, &tv);
}

void UIContext_get_screen_metrics(UIContext *cx, int *w, int *h, int *w_mm, int *h_mm) {
	Screen *s;

	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	if (!(s= DefaultScreenOfDisplay(cx->dpy)))
		croak("DefaultScreenOfDisplay failed");

	if (w) *w= WidthOfScreen(s);
	if (h) *h= HeightOfScreen(s);
	if (w_mm) *w_mm= WidthMMOfScreen(s);
	if (h_mm) *h_mm= HeightMMOfScreen(s);
}

void UIContext_setup_glcontext(UIContext *cx, int direct, GLXContextID link_to) {
	PFNGLXIMPORTCONTEXTEXTPROC    import_context_fn;
	PFNGLXGETCONTEXTIDEXTPROC     get_context_id_fn;
	PFNGLXQUERYCONTEXTINFOEXTPROC query_context_info_fn;
	PFNGLXFREECONTEXTEXTPROC      free_context_fn;
	int visual_id;
	GLXContext remote_context;
	
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
	if (link_to) {
		import_context_fn= (PFNGLXIMPORTCONTEXTEXTPROC) glXGetProcAddress("glXImportContextEXT");
		free_context_fn=   (PFNGLXFREECONTEXTEXTPROC)   glXGetProcAddress("glXFreeContextEXT");
		//query_context_info_fn= (PFNGLXQUERYCONTEXTINFOEXTPROC) glXGetProcAddress("glXQueryContextInfoEXT");
		if (!import_context_fn || !free_context_fn)
			croak("Can't connect to shared GL context; extension not supported by this X server.");

		if (en_trace)
			log_trace("calling glXImportContextEXT");
		remote_context= import_context_fn(cx->dpy, link_to);
		if (!remote_context)
			croak("Can't import remote GL context %d", link_to);
		
		// Get the visual ID used by the existing context
		//if (Success != query_context_info_fn(cx->dpy, cx->glctx, GLX_VISUAL_ID_EXT, &visual_id)) {
		//	free_context_fn(remote_context);
		//	croak("Can't retrieve visual ID of existing GL context");
		//}
		//// Was going to look up the VisualInfo for this ID, but don't see a way to do that.
		//// Instead, just make sure it is the same one as we created above.
		//if (visual_id != cx->xvisi->visualid) {
		//	free_context_fn(remote_context);
		//	croak("Visual of shared GL context does not match the one returned by glXChooseVisual");
		//}
		cx->glctx= glXCreateContext(cx->dpy, cx->xvisi, remote_context, direct);
		free_context_fn(cx->dpy, remote_context);
	}
	else {
		if (en_trace)
			log_trace("calling glXCreateContext");
		cx->glctx= glXCreateContext(cx->dpy, cx->xvisi, NULL, direct);
	}
	if (!cx->glctx)
		croak("glXCreateContext failed");

	get_context_id_fn= (PFNGLXGETCONTEXTIDEXTPROC) glXGetProcAddress("glXGetContextIDEXT");
	cx->glctx_id= get_context_id_fn? get_context_id_fn(cx->glctx) : 0;
}

void UIContext_teardown_glcontext(UIContext *cx) {
	PFNGLXFREECONTEXTEXTPROC free_context_fn;
	
	if (cx->target) {
		glXMakeCurrent(cx->dpy, None, NULL);
		cx->target= None;
	}
	
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


Bool UIContext_wait_event(
	UIContext *cx,
	XEvent *event,
	Bool (*callback)(Display*, XEvent*, XPointer),
	XPointer callback_arg,
	int max_wait_msec
) {
	struct timespec start_time, now;
	start_time.tv_sec= 0;
	start_time.tv_nsec= 0;
	struct timeval tv;

	while (!XCheckIfEvent(cx->dpy, event, callback, callback_arg)) {
		if (0 != clock_gettime(CLOCK_MONOTONIC, &now))
			croak("clock_gettime(CLOCK_MONOTONIC) failed");
		if (!start_time.tv_sec && !start_time.tv_nsec) {
			start_time.tv_sec=  now.tv_sec;
			start_time.tv_nsec= now.tv_nsec;
		}
		tv.tv_sec  =  (max_wait_msec / 1000)  - (now.tv_sec  - start_time.tv_sec);
		tv.tv_usec = ((max_wait_msec % 1000)*1000000 - (now.tv_nsec - start_time.tv_nsec));
		if (tv.tv_usec < 0) { tv.tv_usec += 1000000000; tv.tv_sec--; }
		if (tv.tv_sec  < 0) return 0;  // timeout
		
		if (UIContext_wait_xlib_socket(cx, tv) <= 0)
			return 0; // timeout, interrupted by signal, or other error
	}
	return 1;
}

void UIContext_glXMakeCurrent(UIContext *cx, int xid) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	CROAK_IF_NO_GLCONTEXT(cx);

	if (!glXMakeCurrent(cx->dpy, xid, cx->glctx))
		croak("glXMakeCurrent failed");
	cx->target= xid;
}

int UIContext_create_pixmap(UIContext *cx, int w, int h) {
	int xid, gl_xid;

	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	CROAK_IF_NO_GLCONTEXT(cx);

	xid= XCreatePixmap(cx->dpy, DefaultRootWindow(cx->dpy),
		w, h, cx->xvisi->depth);
	if (!xid)
		croak("XCreatePixmap failed");
	gl_xid= glXCreateGLXPixmap(cx->dpy, cx->xvisi, xid);
	XFreePixmap(cx->dpy, xid); // gl pixmap should hold its own reference?
	if (!gl_xid)
		croak("glXCreateGLXPixmap failed");
	
	return gl_xid;
}

void UIContext_destroy_pixmap(UIContext *cx, Pixmap xid) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	glXDestroyGLXPixmap(cx->dpy, xid);
}

Window UIContext_create_window(UIContext *cx, int x, int y, int w, int h) {
	int en_debug, en_trace;
	Window wnd;
	XSetWindowAttributes wndAttrs;
	Colormap cmap;
	Screen *s;

	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	en_debug= log_debug_enabled();
	en_trace= log_trace_enabled();

	if (en_trace)
		log_trace("calling XCreateColormap");
	cmap= XCreateColormap(cx->dpy, DefaultRootWindow(cx->dpy), cx->xvisi->visual, AllocNone);
	if (!cmap)
		croak("XCreateColormap failed");

	memset(&wndAttrs, 0, sizeof(wndAttrs));
	wndAttrs.background_pixel= 0;
	wndAttrs.border_pixel= 0;
	wndAttrs.colormap= cmap;
	wndAttrs.event_mask= ExposureMask; // | KeyPressMask;

	if (en_debug)
		log_debug("X11 screen is %dx%d", w, h);

	// Default missing window dimensions to screen size
	if (w <= 0 || h <= 0) {
		if (!(s= DefaultScreenOfDisplay(cx->dpy))) {
			XFreeColormap(cx->dpy, cmap);
			croak("DefaultScreenOfDisplay failed");
		}
		
		if (w <= 0) w= WidthOfScreen(s);
		if (h <= 0) h= HeightOfScreen(s);
	}
	
	if (en_trace)
		log_trace("calling XCreateWindow( {%d,%d,%d,%d} )", x, y, w, h);
	wnd= XCreateWindow(cx->dpy, DefaultRootWindow(cx->dpy),
		x, y, w, h, 0, cx->xvisi->depth,
		InputOutput, cx->xvisi->visual,
		CWBackPixel|CWBorderPixel|CWColormap, &wndAttrs);
	XFreeColormap(cx->dpy, cmap);
	if (!wnd)
		croak("XCreateWindow failed");
	
	return wnd;
}

void UIContext_destroy_window(UIContext *cx, Window xid) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	XDestroyWindow(cx->dpy, xid);
}

void UIContext_get_window_rect(
	UIContext *cx, Window wnd,
	int *x, int *y,	unsigned int *width, unsigned int *height
) {
	Window root;
	unsigned int border= 0, depth= 0;

	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	XGetGeometry(cx->dpy, wnd, &root, x, y, width, height, &border, &depth);
}

void UIContext_window_set_blank_cursor(UIContext *cx, Window wnd) {
	XColor black;
	static char noData[] = { 0,0,0,0,0,0,0,0 };
	Pixmap bitmapNoData;
	Cursor invisibleCursor;

	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);

	black.red = black.green = black.blue = 0;
	bitmapNoData= XCreateBitmapFromData(cx->dpy, wnd, noData, 8, 8);
	if (!bitmapNoData)
		croak("XCreateBitmapFromData failed");
	invisibleCursor= XCreatePixmapCursor(cx->dpy, bitmapNoData, bitmapNoData, &black, &black, 0, 0);
	XFreePixmap(cx->dpy, bitmapNoData);
	if (!invisibleCursor)
		croak("XCreatePixmapCursor failed");
	XDefineCursor(cx->dpy, wnd, invisibleCursor);
	XFreeCursor(cx->dpy, invisibleCursor);
}

void UIContext_XSetWMNormalHints(UIContext *cx, Window wnd, HV* hints) {
	XSizeHints *sh;
	SV** pval;
	
	sh= XAllocSizeHints();
	if (!sh) croak("XAllocSizeHints failed");
	#define LOADFIELD(field, bitflag) if (\
		(pval= hv_fetch(hints, #field, strlen(#field), 0)) && SvOK(*pval) \
		) { sh->flags |= bitflag; sh->field = SvIV(*pval); }
	LOADFIELD(x,            PPosition);
	LOADFIELD(y,            PPosition);
	LOADFIELD(width,        PSize);
	LOADFIELD(height,       PSize);
	LOADFIELD(min_width,    PMinSize);
	LOADFIELD(min_height,   PMinSize);
	LOADFIELD(max_width,    PMaxSize);
	LOADFIELD(max_height,   PMaxSize);
	LOADFIELD(width_inc,    PResizeInc);
	LOADFIELD(height_inc,   PResizeInc);
	LOADFIELD(min_aspect.x, PAspect);
	LOADFIELD(min_aspect.y, PAspect);
	LOADFIELD(max_aspect.x, PAspect);
	LOADFIELD(max_aspect.y, PAspect);
	LOADFIELD(base_width,   PBaseSize);
	LOADFIELD(base_height,  PBaseSize);
	LOADFIELD(win_gravity,  PWinGravity);
	#undef LOADFIELD
	XSetWMNormalHints(cx->dpy, wnd, sh);
	// any error is asynchronous
	XFree(sh);
}

static Bool WaitForWndMapped( Display *dpy, XEvent *event, XPointer arg ) {
	log_info("XEvent: %d %d (waiting for %d %d)",
		event->type, (int) event->xmap.window,
		MapNotify, (int)(Window) arg);
    return (event->type == MapNotify) && (event->xmap.window == (Window) arg);
}
void UIContext_XMapWindow(UIContext *cx, Window wnd, int wait_msec) {
	XEvent event;
	
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	CROAK_IF_NO_GLCONTEXT(cx);
	
	XMapWindow(cx->dpy, wnd);
	if (wait_msec) {
		if (!UIContext_wait_event(cx, &event, WaitForWndMapped, (XPointer) wnd, wait_msec))
			croak("Did not receive X11 MapNotify event");
	}
}

void UIContext_glXSwapBuffers(UIContext *cx) {
	CROAK_IF_XLIB_FATAL();
	CROAK_IF_NO_DISPLAY(cx);
	CROAK_IF_NO_TARGET(cx);

	glXSwapBuffers(cx->dpy, cx->target);
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
