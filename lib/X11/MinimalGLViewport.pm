package X11::MinimalGLViewport;
use Moo 2;
use Log::Any '$log';
use Try::Tiny;
require OpenGL;

our $VERSION= 0.00_00;

require XSLoader;
XSLoader::load('X11::MinimalGLViewport', $VERSION);

has mirror_x       => ( is => 'rw', default => sub { (($ENV{MIRROR}||'') =~ /x/)? 1 : 0 } );
has mirror_y       => ( is => 'rw', default => sub { (($ENV{MIRROR}||'') =~ /y/)? 1 : 0 } );
has viewport_depth => ( is => 'rw', default => sub { 2500; } );
has ui_context     => ( is => 'lazy' );
has on_connect     => ( is => 'rw' );
has on_disconnect  => ( is => 'rw' );

=pod

sub BUILD {
	my $self= shift;
	if (!defined $self->window_rect) {
		$ENV{GEOMETRY} =~ /^(\d+)x(\d+)(?:\+(\d+)\+(\d+))?$/
			if defined $ENV{GEOMETRY};
		$log->debug("Setting window rect to ".join(',', $3||0, $4||0, $1||0, $2||0));
		$self->window_rect( Math::BBox->new( $3||0, $4||0, $1||0, $2||0 ) );
	}
}

sub window_rect {
	if ($_[0]->ui_context->is_ready) {
		return Math::BBox->new( $_[0]->ui_context->window_rect );
	} else {
		$_[0]{window_rect}= $_[1] if @_ > 1;
		return $_[0]{window_rect};
	}
}
sub screen_w {
	$_[0]->ui_context->screen_w;
}
sub screen_h {
	$_[0]->ui_context->screen_h;
}

=cut

sub _build_ui_context {
	$log->debug("creating UIContext");
	X11::MinimalGLViewport::UIContext->new();
}

=pod

sub setup_centered_viewport {
	$log->debug("initializing viewport");
	my $self= shift;
	my ($w, $h)= ($self->window_rect->wh);

	glViewport(0, 0, $w, $h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	apply3DFrustum(0, 0, $w, $h, $self->viewport_depth);
	if ($self->mirror_x && $self->mirror_y) {
		glScalef(1, -1, 1);
		glScalef(-1, 1, 1);
		glFrontFace(GL_CCW);
	} elsif ($self->mirror_x) {
		glScalef(-1, 1, 1);
		glFrontFace(GL_CW);
	} elsif ($self->mirror_y) {
		glScalef(1, -1, 1);
		glFrontFace(GL_CW);
	} else {
		glFrontFace(GL_CCW);
	}
	glMatrixMode(GL_MODELVIEW);
}

sub try_connect {
	my $self= shift;
	if (!$self->ui_context->is_ready) {
		$log->debug("creating X11 window");
		
		try {
			$self->ui_context->setup_window($self->window_rect->xywh);
			1;
		} catch {
			$log->warn("error: $_");
			0;
		} or return;
		$self->setup_centered_viewport;
		$self->on_connect->()
			if $self->on_connect;
	}
	return 1;
}

=cut

sub begin_render {
	my $self= shift;
	$self->try_connect;
	OpenGL::glClear(OpenGL::GL_COLOR_BUFFER_BIT());
	1;
}

sub end_render {
	my $self= shift;
	$self->ui_context->flip();
	my $e= $self->get_gl_errors;
	$log->error("OpenGL error bits: ", join(', ', values %$e))
		if $e;
	return !$e;
}

my %_gl_err_msg= (
	OpenGL::GL_INVALID_ENUM()      => "Invalid Enum",
	OpenGL::GL_INVALID_VALUE()     => "Invalid Value",
	OpenGL::GL_INVALID_OPERATION() => "Invalid Operation",
	OpenGL::GL_STACK_OVERFLOW()    => "Stack Overflow",
	OpenGL::GL_STACK_UNDERFLOW()   => "Stack Underflow",
	OpenGL::GL_OUT_OF_MEMORY()     => "Out of Memory",
#	OpenGL::GL_TABLE_TOO_LARGE()   => "Table Too Large",
);

sub get_gl_errors {
	my $self= shift;
	my (%errors, $e);
	$errors{$e}= $_gl_err_msg{$e} || "(unrecognized) ".$e
		while (($e= OpenGL::glGetError()) != OpenGL::GL_NO_ERROR());
	return (keys %errors)? \%errors : undef;
}

1;