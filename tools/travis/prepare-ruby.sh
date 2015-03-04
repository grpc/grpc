#!/bin/sh

set -ex


rvm reinstall $RUBY_VERSION
rvm use $RUBY_VERSION
gem install bundler
