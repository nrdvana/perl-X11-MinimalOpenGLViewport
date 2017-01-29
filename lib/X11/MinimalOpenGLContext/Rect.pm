package X11::MinimalOpenGLContext::Rect;
use strict;
use warnings;
use Carp;

=head1 ATTRIBUTES

The rectangle is represented by

=over

=item x

=item y

=item w

=item h

=back

=cut

sub x { shift->{x} }
sub y { shift->{y} }
sub w { shift->{w} }
sub h { shift->{h} }

=head1 METHODS

=head2 new

Construct a rectangle from either an array

   [ $x, $y, $w, $h ]

or from a hash

   { x => $x, y => $y, w => $w, h => $h }

=cut

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

=head2 x_y_w_h

Returns the 4 elements of the rectangle as a list.

=cut

sub x_y_w_h {
	my $self= shift;
	@{$self}{qw/ x y w h /};
}

1;
