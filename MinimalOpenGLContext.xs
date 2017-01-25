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
window_rect(cx)
	UIContext * cx;
	INIT:
		int x, y;
		unsigned w, h;
	PPCODE:
		UIContext_get_window_rect(cx, &x, &y, &w, &h);
		XPUSHs(sv_2mortal(newSViv(x)));
		XPUSHs(sv_2mortal(newSViv(y)));
		XPUSHs(sv_2mortal(newSViv(w)));
		XPUSHs(sv_2mortal(newSViv(h)));

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

void
setup_glcontext(cx, link_to, direct)
	UIContext * cx
	int link_to
	int direct
	CODE:
		UIContext_setup_glcontext(cx, link_to, direct);

void
setup_window(cx, x, y, w, h)
	UIContext * cx
	int x
	int y
	int w
	int h
	CODE:
		UIContext_setup_window(cx, x, y, w, h);

void
flip(cx)
	UIContext * cx
	CODE:
		UIContext_flip(cx);

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
window_id(cx)
	UIContext * cx
	PPCODE:
		XPUSHs(sv_2mortal(newSViv(cx->wnd)));

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
