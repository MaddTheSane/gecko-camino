#!/usr/bin/perl -w
#
# This tool unpacks a full update package generated by make_full_update.sh
# Author: Benjamin Smedberg
#

# -----------------------------------------------------------------------------
# By default just assume that these tools exist on our path

use Getopt::Std;

my ($MAR, $BZIP2, $archive, @marentries, @marfiles);

if (defined($ENV{"MAR"})) {
    $MAR = $ENV{"MAR"};
}
else {
    $MAR = "mar";
}

if (defined($ENV{"BZIP2"})) {
    $BZIP2 = $ENV{"BZIP2"};
}
else {
    $BZIP2 = "bzip2";
}

sub print_usage
{
    print "Usage: unwrap_full_update.pl [OPTIONS] ARCHIVE\n\n";
    print "The contents of ARCHIVE will be unpacked into the current directory.\n\n";
    print "Options:\n";
    print "  -h show this help text\n";
}

my %opts;
getopts("h", \%opts);

if (defined($opts{'h'}) || scalar(@ARGV) != 1) {
    print_usage();
    exit 1;
}

$archive = $ARGV[0];
@marentries = `"$MAR" -t "$archive"`;

$? && die("Couldn't run \"$MAR\" -t");

shift @marentries;

system("$MAR -x \"$archive\"") == 0 || die "Couldn't run $MAR -x";

foreach (@marentries) {
    tr/\n\r//d;
    my @splits = split(/\t/,$_);
    my $file = $splits[2];

    system("mv \"$file\" \"$file.bz2\"") == 0 ||
      die "Couldn't mv \"$file\"";
    system("\"$BZIP2\" -d \"$file.bz2\"") == 0 ||
      die "Couldn't decompress \"$file\"";
}

