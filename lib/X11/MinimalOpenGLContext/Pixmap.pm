package X11::MinimalOpenGLContext::Pixmap;
use strict;
use warnings;
use Carp;
require Scalar::Util;

=head1 ATTRIBUTES

=head2 ctx

The instance of X11::MinimalOpenGLContext that created this window

=head2 xid

The X11 Window ID

=head2 w

The width of the pixmap

=head2 h

The height of the pixmap

=cut

sub ctx { $_[0][0] }
sub xid { $_[0][1] }
sub w   { $_[0][2] }
sub h   { $_[0][3] }

=head1 METHODS

=head2 new

Constructor, takes a reference to the context, ...

=cut

sub new {
	my ($class, $glc, $w, $h)= @_;
	defined $w && defined $h or croak "Width and height are required";
	my $xid= $glc->_ui_context->create_pixmap($w, $h);
	my $self= bless [ $glc, $xid, $w, $h ], $class;
	Scalar::Util::weaken($self->[0]);
	return $self;
}

sub DESTROY {
	my $self= shift;
	# If weak reference still exists, then free the window
	$self->ctx->_ui_context->destroy_pixmap($self->xid)
		if $self->ctx;
}

=head2 get_rect

Fetches the position of the window from X11 and returns a Rect object.

=cut

sub get_rect {
	my $self= shift;
	return X11::MinimalOpenGLContext::Rect->new(0, 0, $self->w, $self->h);
}

1;
