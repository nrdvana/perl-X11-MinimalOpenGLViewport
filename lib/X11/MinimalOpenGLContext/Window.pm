package X11::MinimalOpenGLContext::Window;
use strict;
use warnings;
require Scalar::Util;

=head1 ATTRIBUTES

=head2 ctx

The instance of X11::MinimalOpenGLContext that created this window

=head2 xid

The X11 Window ID

=cut

sub ctx { $_[0][0] }
sub xid { $_[0][1] }

=head1 METHODS

=head2 new

Constructor, takes a reference to the context, and an optional rect describing
window placement.  The reference to the context is held as a weak reference.
If this object goes out of scope before the context, then it calls
XDestrowWindow.  If the context goes out of scope first, it closes the
connection which essentially frees the window even though this object might
still exist.  In that case the various methods of this object will throw
exceptions.

=cut

sub new {
	my ($class, $glc, $rect)= @_;
	my ($x, $y, $w, $h)= defined $rect? X11::MinimalOpenGLContext::Rect->new($rect)->x_y_w_h : ();
	my $xid= $glc->_ui_context->create_window($x||0, $y||0, $w||0, $h||0);
	my $self= bless [ $glc, $xid ], $class;
	Scalar::Util::weaken($self->[0]);
	return $self;
}

sub DESTROY {
	my $self= shift;
	# If weak reference still exists, then free the window
	$self->ctx->_ui_context->destroy_window($self->xid)
		if $self->ctx;
}

=head2 get_rect

Fetches the position of the window from X11 and returns a Rect object.

=cut

sub get_rect {
	my $self= shift;
	my ($x, $y, $w, $h)= $self->ctx->_ui_context->window_rect($self->xid);
	return X11::MinimalOpenGLContext::Rect->new($x, $y, $w, $h);
}

=head2 map_window

  $wnd->map_window($wait);

Make the window visible.  The optional C<$wait> parameter is a number of
seconds to wait for the X11 mesage saying the window is visible.  If zero
or undefined, the method returns immediately.

=cut

sub map_window {
	my ($self, $wait)= @_;
	$self->ctx->_ui_context->XMapWindow($self->xid, int($wait*1000));
}

=head2 set_wm_normal_hints

  $wnd->set_wm_normal_hints( \%fields );

This method applies window manager hints to the window.
See manual pages for C<XSizeHints> for the fields involved.

=cut

sub set_wm_normal_hints {
	my ($self, $hints)= @_;
	$self->ctx->_ui_context->XSetWMNormalHints($self->xid, $hints);
}

=head2 set_blank_cursor

Set the cursor image of the window to a blank image, essentially hiding the
cursor.

=cut

sub set_blank_cursor {
	my $self= shift;
	$self->ctx->_ui_context->window_set_blank_cursor($self->xid);
}

1;
