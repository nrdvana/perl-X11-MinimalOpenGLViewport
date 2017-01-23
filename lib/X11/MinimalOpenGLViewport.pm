package X11::MinimalOpenGLViewport;
use Moo 2;
use Log::Any '$log';
use Try::Tiny;
use Scalar::Util 'weaken';
use Carp;
require OpenGL;

our $VERSION= '0.00_00';

require XSLoader;
XSLoader::load('X11::MinimalOpenGLViewport', $VERSION);

our %_ConnectedInstances;

# ABSTRACT - Create an output-only OpenGL viewport on X11 with minimal dependencies

=head1 SYNOPSIS

  use OpenGL;
  use X11::MinimalOpenGLViewport;
  
  my $v= X11::MinimalOpenGLViewport->new();
  $v->connect;         # connect to X11 server
  $v->setup_window;    # create X11 window, default to size of the screen
  $v->project_frustum; # convenience for setting up standard GL_PROJECTION matrix
  
  while (1) {
    ...; # Perform your OpenGL rendering
    $v->show();   # calls glXSwapBuffers, and logs glGetError()
  }

=head1 DESCRIPTION

This module aims to be the quickest easiest way to get OpenGL oputput onto an
X11 window, with minimal dependencies and setup hassle.

It does not have an event loop, does not accept window input, and requires
no libraries other than OpenGL and XLib.  This helps it work in more places
than more complete solutions like SDL or GTK.  (but while I claim that, I
haven't actually tested on that many platforms yet, so the module might need
patches if you run on a non-standard system.)

This module might eventually be extended to provide more support for the X11
objects, or it could be rewritten to use XCB instead of XLib, but I probably
won't do that any time soon unless someone wants to assist.

=head1 ATTRIBUTES

=head2 mirror_x

If set to true, this reverses the window coordinates of the GL viewport and
the logical coordinates of the OpenGL projection.  Only takes effect if you
call the L</project_frustum> method.

This is automatically enabled if the window width given to L</setup_window> is negative.

=head2 mirror_y

Like mirror_x.

This is automatically enabled if the window height given to L</setup_window> is negative.

=head2 viewport_rect

Default value for L</project_frustum>'s first argument.

=head2 frustum_rect

Default value for L</project_frustum>'s second argument.

=head2 frustum_depth

Default value for the depth of C<glFrustum>, when L</project_frustum> is called.

=head2 on_error

  $v->on_error(sub {
    my ($viewport, $x_error_info, $is_fatal)= @_;
    ...
  });

X11 reports errors asynchronously, which can make it hard to associate
an error to its source.  This callback lets you know that an error was
associated with this particular connection, and gives you the XErrorInfo
to help track things down.

If the error was fatal, then C<$is_fatal> is true, C<$x_error_info> will
be C<undef>, and you won't be able to make any more XLib calls for the
remaindr of your program!  In this case you should clean up and exit.

Fatal errors affect all Viewports (and in fact, all other users of XLib,
but this module has no control over that) so all viewports will get
their C<on_error> called, followed by their C<on_disconnect> handler.

=head2 on_disconnect

Called any time you disconnect from the X server for any reason.

=cut

has _ui_context      => ( is => 'lazy', predicate => 1 );
sub _build__ui_context { X11::MinimalOpenGLViewport::UIContext->new; }

# Used by project_frustum
has mirror_x       => ( is => 'rw' );
has mirror_y       => ( is => 'rw' );
has viewport_rect  => ( is => 'rw' );
has frustum_rect   => ( is => 'rw' );
has frustum_depth  => ( is => 'rw', default => sub { 2500; } );

has on_error       => ( is => 'rw' );
has on_disconnect  => ( is => 'rw' );

=head1 METHODS

=head2 new

Standard Moo constructor.  No attributes are required.

=head2 connect

  $v->connect();  # defaults to $ENV{DISPLAY}, else ":0"
  $v->connect( $display_string );

Connect to X server.  Dies if it can't connect.

=cut

sub connect {
	my ($self, $display)= @_;
	$display= $ENV{DISPLAY} unless defined $display;
	$display= ':0' unless defined $display;
	$self->_ui_context->connect($display);
	weaken( $_ConnectedInstances{$self}= $self );
}

=head2 is_connected

Returns true if this object is connected to an XServer.
Does not check the connection.

=cut

sub is_connected {
	defined $_ConnectedInstances{shift()};
}

=head2 disconnect

Graceful teardown of OpenGL context, window, and X11 connection

=cut

sub disconnect {
	my $self= shift;
	$self->_ui_context->disconnect;
	delete $_ConnectedInstances{$self};
	$self->on_disconnect->($self) if $self->on_disconnect;
	return $self;
}

sub DESTROY {
	my $self= shift;
	# The connection will clean itself up, but we want the opportunity
	# to run the "on_disconnect" callback.
	$self->disconnect if $_ConnectedInstances{$self};
}

sub _rect { X11::MinimalOpenGLViewport::Rect->new(@_) }

=head2 setup_window

  $v->setup_window(); # defaults to $ENV{GEOMETRY}, else size of screen
  $v->setup_window([ $x, $y, $w, $h ]); # or specify your own size

Create an X11 window and initialize an OpenGL context on it.

Automatically calls L</connect> if not connected to a display yet.
If L</setup_window> has already been called this will destroy the current
window and then create a new one.

=cut

sub setup_window {
	my ($self, $rect)= @_;
	$self->connect unless $self->is_connected;
	
	my ($x, $y, $w, $h);
	if (defined $rect) {
		ref $rect or croak "Expected Rect object, arrayref, or hashref";
		($x, $y, $w, $h)= _rect($rect)->x_y_w_h;
	}
	# Pull defaults from GEOMETRY environment var
	elsif ($ENV{GEOMETRY} && $ENV{GEOMETRY} =~ /^(\d+)x(\d+)(?:\+(\d+)\+(\d+))?$/) {
		($x, $y, $w, $h)= ( $3, $4, $1, $2 );
	}
	
	# If w or h is negative, set the appropriate mirror flag
	if (defined $w && $w < 0) { $self->mirror_x(1); $w= -$w; }
	if (defined $h && $h < 0) { $self->mirror_y(1); $h= -$h; }
	
	# If X or Y are undef, default to 0.  If w or h are undef, set them to 0
	#  which will default to screen dims in the C code.
	$self->_ui_context->setup_window($x||0, $y||0, $w||0, $h||0);
}

=head2 window_rect

Returns the current rect of the window, live from the X server.

Throws an exception if called before L</setup_window>

=cut

sub window_rect {
	my $self= shift;
	return _rect($self->_ui_context->window_rect);
}

=head2 screen_dims

  my ($width, $height, $physical_width_mm, $physical_height_mm)
    = $v->screen_dims

Query X11 for the pixel dimensions and physical dimensions of the
default screen.  This module does not yet support multi-display
setups.

Throws an exception if called before L</connect>

=cut

sub screen_dims {
	my $self= shift;
	return $self->_ui_context->screen_metrics();
}

=head2 screen_pixel_aspect_ratio

  my $pixel_aspect= $v->screen_pixel_aspect_ratio();

Returns the ratio of the physical width of one pixel by the physical height
of one pixel.  If any of the measurements are missing it defaults to 1.0

Throws an exception if called before L</connect>

=cut

sub screen_pixel_aspect_ratio {
	my $self= shift;
	my ($screen_w, $screen_h, $screen_w_mm, $screen_h_mm)= $self->_ui_context->screen_metrics();
	return 1 if grep { $_ <= 0 } ($screen_w, $screen_h, $screen_w_mm, $screen_h_mm);
	return ($screen_w_mm * $screen_h) / ($screen_h_mm * $screen_w);
}

=head2 project_frustum

  $v->viewport_rect( ... );  # default is size of window
  $v->frustum_rect( ... );   # default is top=0.5, bottom=-0.5 with aspect-correct width
  $v->project_frustum();
  # -or-
  $v->project_frustum( $viewport_rect, $frustum_rect );

This method sets up a sensible OpenGL projection matrix.
It is not related to X11 and is just provided with this module for
your convenience.

Throws an exception if called before L</setup_window>

=cut

sub project_frustum {
	my ($self, $viewport_rect, $frustum_rect)= @_;
	my $pixel_aspect= $self->screen_pixel_aspect_ratio();
	my $window_rect= $self->window_rect;
	my $mirror_x= $self->mirror_x? 1 : 0;
	my $mirror_y= $self->mirror_y? 1 : 0;
	
	$log->debug("initializing viewport");
	my ($x, $y, $w, $h)= _rect($viewport_rect || $self->viewport_rect || {})->x_y_w_h;
	$x ||= 0;
	$y ||= 0;
	$w ||= $window_rect->w - $x;
	$h ||= $window_rect->h - $y;
	# Calculate the viewport from the opposite side of the screen if mirror is in effect
	if ($x && $mirror_x) { $x= $window_rect->w - $w - $x; }
	if ($y && $mirror_y) { $y= $window_rect->h - $h - $y; }
	OpenGL::glViewport($x, $y, $w, $h);
	
	$log->debug("setting up projection matrix");
	my ($fx, $fy, $fw, $fh)= _rect($frustum_rect || $self->frustum_rect || {})->x_y_w_h;
	# If horizontal dimension is missing...
	if (!$fw) {
		# If vertical is also missing, default to 1
		$fh ||= 1;
		# Calculate the horizontal using pixel aspect and the viewport width
		$fw= $pixel_aspect * $fh * ($w / $h);
	} elsif (!$fh) {
		$fh= $fw * ($h / $w) / $pixel_aspect;
	}
	# If edge missing, use half the size
	$fx= $fw * -.5 unless defined $fx;
	$fy= $fh * -.5 unless defined $fy;
	
	OpenGL::glMatrixMode(OpenGL::GL_PROJECTION());
	OpenGL::glLoadIdentity();
	
	OpenGL::glFrustum(
		($mirror_x? ($fx+$fw, $fx) : ($fx, $fx+$fw)),
		($mirror_y? ($fy+$fh, $fy) : ($fy, $fy+$fh)),
		1, $self->frustum_depth * 2);
	OpenGL::glTranslated(0, 0, -$self->frustum_depth);
	
	# If mirror is in effect, need to tell OpenGL which way the camera is
	OpenGL::glFrontFace($mirror_x == $mirror_y? OpenGL::GL_CCW() : OpenGL::GL_CW());
	OpenGL::glMatrixMode(OpenGL::GL_MODELVIEW());
}

=head2 swap_buffers

  $v->swap_buffers()

Pass-through to glXSwapBuffers();

Throws an exception if called before L</setup_window>

=cut

sub swap_buffers {
	shift->_ui_context->flip();
}

=head2 show

Convenience method to call C<< $v->swap_buffers() >>
and log the results of C<< $v->gl_get_errors() >> to Log::Any

Throws an exception if called before L</setup_window>

=cut

sub show {
	my $self= shift;
	$self->swap_buffers;
	my $e= $self->get_gl_errors;
	$log->error("OpenGL error bits: ", join(', ', values %$e))
		if $e;
	return !$e;
}

=head2 get_gl_errors

Convenience method to call glGetError repeatedly and build a
hash of the symbolic names of the error constants.

Throws an exception if called before L</setup_window>

=cut

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

our %_X11_error_code_byname;
our %_X11_error_code_byval;
sub _X11_error_code_byval {
	if (!keys %_X11_error_code_byname) {
		X11::MinimalOpenGLViewport::UIContext::get_error_codes(\%_X11_error_code_byname);
		%_X11_error_code_byval= reverse %_X11_error_code_byname;
	}
	return \%_X11_error_code_byval;
}

# This is called for recoverable errors on the X11 stream
# most notably when a window has been closed and is no longer valid.
sub _X11_error {
	my ($err)= @_;
	$err->{error_code_name}= _X11_error_code_byval()->{$err->{error_code}} || '(unknown)';
	# iterate through all connections to see which one the error applies to.
	for (values %_ConnectedInstances) {
		if ($_->_has_ui_context && $_->_ui_context->display eq $err->{display}) {
			$_->on_error->($_, $err, 0)
				if $_->on_error;
			last;
		}
	}
}

# This is called when XLib encounters a fatal error (like lost XServer)
# After this, XLib is no longer usable.
# TODO: switch to XCB which is better designed than XLib.
sub _X11_error_fatal {
	$log->error("Fatal X11 error.");
	my @close= values %_ConnectedInstances;
	for my $v (@close) {
		try { $v->on_error->($v, undef, 1) } catch { warn $_; }
			if $v->on_error;
		try { $v->disconnect; } catch { warn $_; };
	}
}

package X11::MinimalOpenGLViewport::Rect;
use strict;
use warnings;
use Carp;

sub new {
	my $class= shift;
	my $self= (@_ == 1 && ref $_[0])?
		(ref($_[0]) eq 'HASH'? { %{$_[0]} }
		: ref($_[0])->isa($class)? { %{$_[0]} }
		: ref($_[0]) eq 'ARRAY'? { x => $_[0][0], y => $_[0][1], w => $_[0][2], h => $_[0][3] }
		: croak "Expected arrayref or hashref or x,y,w,h in Rect constructor"
		)
		: { x => $_[0], y => $_[1], w => $_[2], h => $_[3] };
	return bless $self, $class;
}

sub x { shift->{x} }
sub y { shift->{y} }
sub w { shift->{w} }
sub h { shift->{h} }
sub x_y_w_h {
	my $self= shift;
	@{$self}{qw/ x y w h /};
}

1;
