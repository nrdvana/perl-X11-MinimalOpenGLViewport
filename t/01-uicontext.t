# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl X11-MinimalGLViewport.t'

#########################

use Test::More;
use IO::Handle;
use Log::Any::Adapter 'TAP';
sub errmsg(&) {	eval { shift->() };	defined $@? $@ : ''; }

use_ok('X11::MinimalGLViewport') or BAIL_OUT;

my $v= new_ok( 'X11::MinimalGLViewport', [], 'new viewport' );
isa_ok( $v->ui_context, 'X11::MinimalGLViewport::UIContext', 'has ui context' );

like(errmsg{ $v->ui_context->screen_wh }, qr/connect/i, 'screen dims unavailable before connect' );
$v->ui_context->connect(undef);
is( errmsg{my @wh= $v->ui_context->screen_wh }, '', 'got screen dims' );

is( errmsg{ $v->ui_context->setup_window(0, 0, 100, 100) }, '', 'setup_window' );
is_deeply( [ $v->ui_context->window_rect ], [0,0,100,100], 'created window correct size' );

# Test lack of an exception
is( errmsg { $v->ui_context->flip; }, '', 'flip' );

# Testing the XLib error handler has to be the last thing we test, because
# it forces a close of the program.
$v->ui_context->set_error_handler(sub {
	pass("Error was trapped");
	done_testing;
	exit 0;
});

# shutdown every socket, ensuring we lose the X server
for (3..50) {
	my $x= IO::Handle->new_from_fd($_, 'w+');
	shutdown($x, 2) if $x;
}
# Now trigger the error
$v->ui_context->flip;