#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

// These defines are for the C file that we include.
// It is not a separate library like most people do, just
// a separate file for readability and syntax hilighting.

#define LOG_PKG "X11::MinimalOpenGLContext"

#define log_info_enabled()  log_enabled("is_info")
#define log_debug_enabled() log_enabled("is_debug")
#define log_trace_enabled() log_enabled("is_trace")

static int log_enabled(const char *method) {
	int enabled= 0;
	SV *ret;
	SV *log= get_sv(LOG_PKG "::log", 0);
	if (!log) croak("we require $" LOG_PKG "::log to be set");
	
	dSP; ENTER; SAVETMPS;
	
	PUSHMARK(SP);
	XPUSHs(log);
	PUTBACK;
	
	int count= call_method(method, G_SCALAR);
	
	SPAGAIN;
	if (count > 0) {
		ret= POPs;
		enabled= SvTRUE(ret);
	}
	PUTBACK;
	
	FREETMPS; LEAVE;
	return enabled;
}

#define log_error(x...) log_write("error", sv_2mortal(newSVpvf(x)))
#define log_info(x...) log_write("info", sv_2mortal(newSVpvf(x)))
#define log_debug(x...) log_write("debug", sv_2mortal(newSVpvf(x)))
#define log_trace(x...) log_write("trace", sv_2mortal(newSVpvf(x)))

static void log_write(const char *method, SV *message) {
	SV *log= get_sv(LOG_PKG "::log", 0);
	if (!log) croak("we require $" LOG_PKG "::log to be set");
	
	dSP; ENTER; SAVETMPS;
	
	PUSHMARK(SP);
	XPUSHs(log);
	XPUSHs(message);
	PUTBACK;
	
	call_method(method, G_DISCARD);
	
	FREETMPS; LEAVE;
}

#include "uicontext.c"

MODULE = X11::MinimalOpenGLContext		PACKAGE = X11::MinimalOpenGLContext::UIContext

SV *
new(pkg)
	const char * pkg
	CODE:
		if (!sv_derived_from(ST(0), "X11::MinimalOpenGLContext::UIContext"))
			Perl_croak("Expected package name deriving from X11::MinimalOpenGLContext::UIContext");
		UIContext *cx = UIContext_new();
		RETVAL = sv_setref_pv(newSV(0), pkg, (void*) cx);
	OUTPUT:
		RETVAL

void
DESTROY(cx)
	UIContext * cx
	CODE:
		UIContext_free(cx);

void
screen_metrics(cx)
	UIContext * cx;
	INIT:
		int w, h, w_mm, h_mm;
	PPCODE:
		UIContext_get_screen_metrics(cx, &w, &h, &w_mm, &h_mm);
		XPUSHs(sv_2mortal(newSViv(w)));
		XPUSHs(sv_2mortal(newSViv(h)));
		XPUSHs(sv_2mortal(newSViv(w_mm)));
		XPUSHs(sv_2mortal(newSViv(h_mm)));

void
connect(cx, display)
	UIContext * cx
	const char * display 
	CODE:
		UIContext_connect(cx, display);

void
disconnect(cx)
	UIContext * cx
	CODE:
		UIContext_disconnect(cx);

int
get_xlib_socket(cx)
	UIContext * cx
	CODE:
		RETVAL= UIContext_get_xlib_socket(cx);
	OUTPUT:
		RETVAL

void
XFlush(cx)
	UIContext * cx
	CODE:
		XFlush(cx->dpy);

void
setup_glcontext(cx, link_to, direct)
	UIContext * cx
	int link_to
	int direct
	CODE:
		UIContext_setup_glcontext(cx, link_to, direct);

void
teardown_glcontext(cx)
	UIContext * cx
	CODE:
		UIContext_teardown_glcontext(cx);

int
create_pixmap(cx, w, h)
	UIContext * cx
	int w
	int h
	CODE:
		RETVAL= UIContext_create_pixmap(cx, w, h);
	OUTPUT:
		RETVAL

void
destroy_pixmap(cx, xid)
	UIContext * cx
	int xid
	CODE:
		UIContext_destroy_pixmap(cx, xid);

int
create_window(cx, x, y, w, h)
	UIContext * cx
	int x
	int y
	int w
	int h
	CODE:
		RETVAL= UIContext_create_window(cx, x, y, w, h);
	OUTPUT:
		RETVAL

void
destroy_window(cx, xid)
	UIContext * cx
	int xid
	CODE:
		UIContext_destroy_window(cx, xid);

void
window_rect(cx, wnd)
	UIContext * cx
	int wnd
	INIT:
		int x, y;
		unsigned w, h;
	PPCODE:
		UIContext_get_window_rect(cx, wnd, &x, &y, &w, &h);
		XPUSHs(sv_2mortal(newSViv(x)));
		XPUSHs(sv_2mortal(newSViv(y)));
		XPUSHs(sv_2mortal(newSViv(w)));
		XPUSHs(sv_2mortal(newSViv(h)));

void
window_set_blank_cursor(cx, wnd)
	UIContext * cx
	int wnd
	CODE:
		UIContext_window_set_blank_cursor(cx, wnd);

void
XSetWMNormalHints(cx, wnd, hints)
	UIContext * cx
	int wnd
	HV* hints
	CODE:
		UIContext_XSetWMNormalHints(cx, wnd, hints);

void
XMapWindow(cx, wnd, wait_msec)
	UIContext * cx
	int wnd
	int wait_msec
	CODE:
		UIContext_XMapWindow(cx, wnd, wait_msec);

void
glXMakeCurrent(cx, xid)
	UIContext * cx
	int xid
	CODE:
		UIContext_glXMakeCurrent(cx, xid);

void
glXSwapBuffers(cx)
	UIContext * cx
	CODE:
		UIContext_glXSwapBuffers(cx);

SV*
display(cx)
	UIContext * cx
	PPCODE:
		XPUSHs(sv_2mortal(newSVpvf("%p", cx->dpy)));

SV*
glctx_id(cx)
	UIContext * cx
	PPCODE:
		XPUSHs(sv_2mortal(newSViv(cx->glctx_id)));

SV*
has_glcontext(cx)
	UIContext * cx
	PPCODE:
		XPUSHs(sv_2mortal(newSViv(cx->glctx? 1 : 0)));

SV*
current_gl_target(cx)
	UIContext * cx
	PPCODE:
		XPUSHs(sv_2mortal(newSViv(cx->target)));

void
glx_version(cx)
	UIContext * cx
	PPCODE:
		XPUSHs(sv_2mortal(newSViv(cx->glx_version_major)));
		XPUSHs(sv_2mortal(newSViv(cx->glx_version_minor)));

SV*
glx_extensions(cx)
	UIContext * cx
	PPCODE:
		XPUSHs(sv_2mortal(newSVpv(cx->glx_extensions? cx->glx_extensions : "", 0)));

void
get_xlib_error_codes(dest)
	HV * dest
	CODE:
		UIContext_get_xlib_error_codes(dest);
