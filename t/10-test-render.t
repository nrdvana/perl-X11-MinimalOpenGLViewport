# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl X11-MinimalOpenGLViewport.t'

#########################

use Test::More;
use Time::HiRes 'sleep';
use OpenGL qw(:glfunctions :glconstants);
use Log::Any::Adapter 'TAP';
sub errmsg(&) {	eval { shift->() };	defined $@? $@ : ''; }

use_ok('X11::MinimalOpenGLViewport') or BAIL_OUT;

my $v= new_ok( 'X11::MinimalOpenGLViewport', [on_error => sub { use DDP; p $_[1]; }], 'new viewport' );

is( errmsg{ $v->setup_window([0, 0, 500, 500]); }, '', 'create window' );
is( errmsg{ $v->project_frustum(); },            '', 'setup frustum' );
sleep .2;
glClearColor(0, 1, 0, 1);
glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
$v->show;
sleep .2;
glClearColor(1, 0, 0, 1);
glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
$v->show;
sleep .2;
glClearColor(0, 0, 1, 1);
glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
$v->show;
sleep .2;

is( errmsg{ $v->disconnect }, '', 'disconnect' );

is( errmsg{ $v->setup_window([100, 100, 400, 200]); }, '', 'create window' );
is( errmsg{ $v->project_frustum(); },                '', 'setup frustum' );
sleep .2;
glClearColor(0, 1, 0, 1);
glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
$v->show;
sleep .2;
glClearColor(1, 1, 0, 1);
glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

# Circle should touch top and bottom of window
glBegin(GL_TRIANGLE_FAN);
use Math::Trig 'deg2rad';
glColor3f(0, 0, 0);
glVertex2d(0, 0);
for (0..360) {
	glVertex2d(cos(deg2rad($_))*.5, sin(deg2rad($_))*.5);
}
glEnd();
$v->show;
sleep 1;
# Circle should touch top and bottom of window
glBegin(GL_TRIANGLE_FAN);
use Math::Trig 'deg2rad';
glColor3f(0, 0, 0);
glVertex2d(0, 0);
for (0..360) {
	glVertex2d(cos(deg2rad($_))*.5, sin(deg2rad($_))*.5);
}
glEnd();
$v->show;
sleep 2;

done_testing;
