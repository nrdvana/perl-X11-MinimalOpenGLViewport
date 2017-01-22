use 5.008009;
use ExtUtils::MakeMaker;
# See lib/ExtUtils/MakeMaker.pm for details of how to influence
# the contents of the Makefile that is written.
WriteMakefile(
    NAME              => 'X11::MinimalGLViewport',
    VERSION_FROM      => 'lib/X11/MinimalGLViewport.pm', # finds $VERSION
    PREREQ_PM         => { OpenGL => 0, 'Log::Any' => 0 }, # e.g., Module::Name => 1.1
    ($] >= 5.005 ?     ## Add these new keywords supported since 5.005
      (ABSTRACT_FROM  => 'lib/X11/MinimalGLViewport.pm', # retrieve abstract from module
       AUTHOR         => 'Michael Conrad <mike@nrdvana.net>') : ()),
    TEST_REQUIRES     => { 'Log::Any::Adapter::TAP' => 0 },
    LIBS              => ['-lGL -lX11'], # e.g., '-lm'
    DEFINE            => '-Wall', # e.g., '-DHAVE_SOMETHING'
    INC               => '-I.', # e.g., '-I. -I/usr/include/other'
    #OBJECT            => '$(O_FILES)', # link all the C files too
);
