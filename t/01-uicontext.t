# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl X11-MinimalOpenGLContext.t'

#########################

use Test::More;
use IO::Handle;
use Log::Any::Adapter 'TAP';
sub errmsg(&) {	eval { shift->() };	defined $@? $@ : ''; }

use_ok('X11::MinimalOpenGLContext') or BAIL_OUT;

sub log_error {
	diag explain $_[1];
}

my $v= new_ok( 'X11::MinimalOpenGLContext', [ on_error => \&log_error ], 'new viewport' );
isa_ok( $v->_ui_context, 'X11::MinimalOpenGLContext::UIContext', 'has ui context' );

like(errmsg{ $v->_ui_context->screen_metrics }, qr/connect/i, 'screen dims unavailable before connect' );
$v->_ui_context->connect(undef);
is( errmsg{my @metrics= $v->_ui_context->screen_metrics }, '', 'got screen dims' );

is( errmsg{ $v->_ui_context->setup_glcontext(0, 0) }, '', 'setup_glcontext' );

is( errmsg{ $v->_ui_context->setup_window(0, 0, 100, 100) }, '', 'setup_window' );
my $rect= [ $v->_ui_context->window_rect ];
ok( $rect->[2] > 0, 'can load window dimensions' );

# Test lack of an exception
is( errmsg { $v->_ui_context->flip; }, '', 'flip' );

is( errmsg{ $v->_ui_context->disconnect() }, '', 'disconnect' );
done_testing;
