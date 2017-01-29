# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl X11-MinimalOpenGLContext.t'

#########################

use Test::More;
use IO::Handle;
use Log::Any::Adapter 'TAP';
sub errmsg(&) {	eval { shift->() };	defined $@? $@ : ''; }

use_ok('X11::MinimalOpenGLContext') or BAIL_OUT;

my $v= new_ok( 'X11::MinimalOpenGLContext', [], 'new viewport' );

my $handler_ran= 0;
my $callback_ran= 0;

my $prev= \&X11::MinimalOpenGLContext::_X11_error_fatal;
defined $prev or die "wtf";
*X11::MinimalOpenGLContext::_X11_error_fatal= sub {
	note("Fatal error handler ran");
	$handler_ran= 1;
	$prev->();
};
$v->on_disconnect(sub {
	note("Disconnect callback ran");
	$callback_ran= 1;
});

is( errmsg{ $v->connect; }, '', 'connected' );
is( errmsg{ $v->setup_glcontext; }, '', 'initialized' );

# shutdown every socket, ensuring we lose the X server
note("Interrupting X11 connection to simulate lost server");
for (3..50) {
	my $x= IO::Handle->new_from_fd($_, 'w+');
	shutdown($x, 2) if $x;
}

ok( !$handler_ran, 'handler not run' );
ok( !$callback_ran, 'callback not run' );

# Now trigger the error
my $wnd;
like( errmsg{ $wnd= $v->setup_window; }, qr/fatal/i, 'X activity triggers fatal error' );

ok( $handler_ran, 'handler ran' );
ok( $callback_ran, 'callback ran' );

# All future X11 calls should also throw an error
like( errmsg{ $v->connect; }, qr/XLib/i, 'Future calls refuse to use XLib' );

done_testing;
