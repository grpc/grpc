Xcode Configs are sort of a black art, any time you have a set of rules, you
quickly hit a few exceptions.

The main goal of using these is as follow:

Edit your Project level build settings by removing as much as possible, and
then set the per Configuration settings to one of the project xcode config
files w/in the Project subfolder here.  Apple now recommends always building
with the "current" SDK and started being more aggressive at removing older
SDKs with each Xcode releases.  So set you SDK version and min supported OS
version in your project.  The configs will then set everything based off
those choices.


If you are building a Shared Library, Loadable Bundle (Framework) or UnitTest
you will need to apply a further Xcode Config file at the target level.  You do
this again by clearing most of the settings on the target, and just setting the
build config for that target to be the match from the Target subfolder here.

To see an example of this, look at CoverStory
(http://code.google.com/p/coverstory) or Vidnik
(http://code.google.com/p/vidnik).


The common exception...If you need to have a few targets build w/ different
SDKs, then you hit the most common of the exceptions.  For these, you'd need
the top level config not to set some things, the simplest way to do this seems
to be to remove as many of the settings from the project file, and make new
wrapper xcconfig files that inclue both the project level and target level
setting and set them on the targets (yes, this is like the MetroWerks days
where you can quickly explode in a what seems like N^2 (or worse) number of
config files.  With a little luck, future versions of Xcode might have some
support to make mixing SDKs easier.

Remember: When using the configs at any given layer, make sure you set them for
each build configuration you need (not just the active one).

Many of the build settings are more than just yes/no flags and take
a list of values that you may want to change at different levels.
Xcode doesn't allow you to "inherit" settings with includes so you always
end up overriding settings accidentally. To avoid this, we instead
allow you to define settings at different levels
(GENERAL, PLATFORM (iPhone/Mac), CONFIGURATION (Release/Debug).
We do this by setting a GTM version of the setting (so for OTHER_CFLAGS it's
GTM_XXX_OTHER_CFLAGS where xxx is GENERAL, PLATFORM or CONFIGURATION depending
at what level the flag is set. These are all merged together in the
GTMMerge.xcconfig. Do not modify the base setting (OTHER_CFLAGS) instead modify
the GTM one at the level you want it modified.

The major place this may affect you is that we have really tightened down on
the warnings. To make it easier for you to move your code onto the new
xcconfig files, we have split the warnings up into three categories, which in
general you can think of as easy, moderate and extreme. If you run into a lot
of warnings when you compile, look at changing the GTM_GENERAL_WARNING_CFLAGS
setting to only include lower levels (eg GTM_GENERAL_WARNING_CFLAGS1) and see
if that makes it easier on you. Look inside General.xcconfig and search for
GTM_GENERAL_WARNING_CFLAGS1 for more info.
