#!/usr/bin/perl

use warnings;
use strict;
$ENV{"DALE_TEST_ARGS"} ||= "";
my $test_dir = $ENV{"DALE_TEST_DIR"} || ".";
$ENV{PATH} .= ":.";

use Data::Dumper;
use Test::More tests => 3;

my @res = `dalec $ENV{"DALE_TEST_ARGS"} $test_dir/t/src/sizeof-array-problem.dt -o sizeof-array-problem `;
is(@res, 0, 'No compilation errors');

@res = `./sizeof-array-problem`;
is($?, 0, 'Program executed successfully');

chomp for @res;

is_deeply(\@res, [
'100',
], 'Got expected results');

`rm sizeof-array-problem`;

1;
