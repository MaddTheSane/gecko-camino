#
# Config object for release automation
#

package Bootstrap::Config;

use strict;

use POSIX "uname";
use File::Copy qw(move);

use Bootstrap::Util qw(GetLocaleManifest CvsCatfile);
use Bootstrap::Util qw(GetFtpNightlyDir);

# shared static config
my %config;

# single shared instance
my $singleton = undef;

sub new {
    my $proto = shift;
    my $class = ref($proto) || $proto;

    return $singleton if defined $singleton;

    my $this = {};
    bless($this, $class);
    $this->Parse();
    $singleton = $this;
    
    return $this;
}

sub Parse {
    my $this = shift;
    
    open(CONFIG, "< bootstrap.cfg") 
      || die("Can't open config file bootstrap.cfg");

    while (<CONFIG>) {
        # no comments or empty lines
        next if ($_ =~ /^#/ || $_ =~ /^\s*$/);
        s/^\s+//; # no leading white
        s/\s+$//; # no trailing white
        chomp $_; # no newline
        my ($var, $value) = split(/\s*=\s*/, $_, 2);
        $this->Set(var => $var, value => $value);
    }
    close(CONFIG);
}

##
# Get checks to see if a variable exists and returns it.
# Returns scalar
#
# This method supports system-specific overrides, or "sysvar"s.
#  For example, if a caller is on win32 and does 
#  Get(sysvar => "buildDir") and win32_buildDir exists, the value of 
#  win32_buildDir will be returned. If not, the value of buildDir will
#  be returned. Otherwise, the die() assertion will be hit.
##

sub Get {
    my $this = shift;

    my %args = @_;
    my $var = $args{'var'};
    # sysvar will attempt to prepend the OS name to the requested var
    my $sysvar = $args{'sysvar'};

    if ((! defined($args{'var'})) && (! defined($args{'sysvar'}))) {
      die "ASSERT: Bootstep::Config::Get(): null var requested";
    } elsif ((defined($args{'var'})) && (defined($args{'sysvar'}))) {
      die "ASSERT: Bootstep::Config::Get(): both var and sysvar requested";
    }

    if (defined($args{'sysvar'})) {
        # look for a system specific var first
        my $osname = $this->SystemInfo(var => 'osname');
        my $sysvarOverride = $osname . '_' . $sysvar;

        if ($this->Exists(var => $sysvarOverride)) {
            return $config{$sysvarOverride};
        } elsif ($this->Exists(var => $sysvar)) {
            return $config{$sysvar};
        } else {
            die("No such system config variable: $sysvar");
        }
    } elsif ($this->Exists(var => $var)) {
        return $config{$var};
    } else {
        die("No such config variable: $var");
    }
}

sub Set {
    my $this = shift;

    my %args = @_;

    die "ASSERT: Config::Set(): null var and/or value\n" if
     (!exists($args{'var'}) || !exists($args{'value'}));

    die "ASSERT: Config::Set(): Cannot set null var\n" if
     (!defined($args{'var'}) || 
     (defined($args{'var'}) && $args{'var'} =~ /^\s*$/));

    my $var = $args{'var'};
    my $value = $args{'value'};
    my $force = exists($args{'force'}) ? $args{'force'} : 0;

    die "ASSERT: Config::Set(): $var already exists ($value)\n" if 
     (!$force && exists($config{$var}));

    die "ASSERT: Config::Set(): Attempt to set null value for var $var\n" if 
     (!$force && (!defined($value) || $value =~ /^\s*$/));

    return ($config{$var} = $value);
}
 
sub GetLocaleInfo {
    my $this = shift;

    if (! $this->Exists(var => 'localeInfo')) {
        my $localeFileTag = $this->Get(var => 'productTag') . '_RELEASE';
        $config{'localeInfo'} = GetLocaleManifest(
         app => $this->Get(var => 'appName'),
         cvsroot => $this->Get(var => 'mozillaCvsroot'),
         tag => $localeFileTag);
    }

    return $this->Get(var => 'localeInfo');
}

##
# GetFtpCandidateDir - construct the FTP path for pushing builds & updates to
# returns scalar
#
# mandatory argument:
#    bitsUnsigned - boolean - 1 for unsigned, 0 for signed
#      adds "unsigned/" prefix for windows and version >= 2.0
##

sub GetFtpCandidateDir {
    my $this = shift;
    my %args = @_;

    if (! defined($args{'bitsUnsigned'})) {
      die "ASSERT: Bootstep::Config::GetFtpCandidateDir(): bitsUnsigned is a required argument";
    }
    my $bitsUnsigned = $args{'bitsUnsigned'};

    my $version = $this->Get(var => 'version');
    my $build = $this->Get(var => 'build');

    my $candidateDir = CvsCatfile(GetFtpNightlyDir(), $version . '-candidates', 'build' . $build ) . '/';

    my $osFileMatch = $this->SystemInfo(var => 'osname');

    if ($bitsUnsigned && ($osFileMatch eq 'win32')  && ($version ge '2.0')) {
        $candidateDir .= 'unsigned/';
    }

    return $candidateDir;  
}

sub GetLinuxExtension {
    my $this = shift;

    # We are assuming tar.bz2 to help minimize bootstrap.cfg variables in
    # the future. tar.gz support can probably be removed once we stop
    # building/releasing products that use it.
    my $useTarGz = $this->Exists(var => 'useTarGz') ?
     $this->Get(var => 'useTarGz') : 0;
    return ($useTarGz) ? 'gz' : 'bz2';
}

sub GetVersion {
    my $this = shift;
    my %args = @_;
    
    if (! defined($args{'longName'})) {
      die "ASSERT: Bootstep::Config::GetVersion(): longName is a required argument";
    }
    my $longName = $args{'longName'};

    my $version = $this->Get(var => 'version');
    my $longVersion = $version;
    $longVersion =~ s/a([0-9]+)$/ Alpha $1/;
    $longVersion =~ s/b([0-9]+)$/ Beta $1/;
    $longVersion =~ s/rc([0-9]+)$/ RC $1/;

    return ($longName) ? $longVersion : $version;
}

# Sometimes we need the application version to be different from what we "call"
# the build, eg public release candidates for a major release (3.0 RC1). The var
# appVersion is an optional definition used for $appName/config/version.txt, and
# hence in the filenames coming off the tinderbox.
sub GetAppVersion {
    my $this = shift;

    return ($this->Exists(var => 'appVersion')) ? 
      $this->Get(var => 'appVersion') : $this->GetVersion(longName => 0);
}

sub GetOldVersion {
    my $this = shift;
    my %args = @_;
    
    if (! defined($args{'longName'})) {
      die "ASSERT: Bootstep::Config::GetOldVersion(): longName is a required argument";
    }
    my $longName = $args{'longName'};

    my $oldVersion = $this->Get(var => 'oldVersion');
    my $oldLongVersion = $oldVersion;
    $oldLongVersion =~ s/a([0-9]+)$/ Alpha $1/;
    $oldLongVersion =~ s/b([0-9]+)$/ Beta $1/;
    $oldLongVersion =~ s/rc([0-9]+)$/ RC $1/;

    return ($longName) ? $oldLongVersion : $oldVersion;
}

# Like GetAppVersion(), but for the previous release 
# eg we're doing 3.0RC2 and need to refer to 3.0RC1
sub GetOldAppVersion {
    my $this = shift;

    return ($this->Exists(var => 'oldAppVersion')) ?
      $this->Get(var => 'oldAppVersion') : $this->GetOldVersion(longName => 0);
}

##
# Exists checks to see if a config variable exists.
# Returns boolean (1 or 0)
#
# This method supports system-specific overrides, or "sysvar"s.
#  For example, if a caller is on win32 and does 
#  Exists(sysvar => "win32_buildDir") and only buildDir exists, a 0
#  will be returned. There is no "fallback" as in the case of Get.
##

sub Exists {
    my $this = shift;
    my %args = @_;
    my $var = $args{'var'};
    # sysvar will attempt to prepend the OS name to the requested var
    my $sysvar = $args{'sysvar'};

    if ((! defined($args{'var'})) && (! defined($args{'sysvar'}))) {
      die "ASSERT: Bootstep::Config::Get(): null var requested";
    } elsif ((defined($args{'var'})) && (defined($args{'sysvar'}))) {
      die "ASSERT: Bootstep::Config::Get(): both var and sysvar requested";
    }

    if (defined($args{'sysvar'})) {
        # look for a system specific var first
        my $osname = $this->SystemInfo(var => 'osname');
        my $sysvarOverride = $osname . '_' . $sysvar;

        if (exists($config{$sysvarOverride})) {
            return 1;
        } elsif (exists($config{$sysvar})) {
            return 1;
        } else {
            return 0;
        }
    } else {
        return exists($config{$var});
    }
}

sub SystemInfo {
    my $this = shift;
    my %args = @_;

    my $var = $args{'var'};

    my ($sysname, $hostname, $release, $version, $machine ) = uname;

    if ($var eq 'sysname') {
        return $sysname;
    } elsif ($var eq 'hostname') {
        return $hostname;
    } elsif ($var eq 'release') {
        return $release;
    } elsif ($var eq 'version') {
        return $version;
    } elsif ($var eq 'machine') {
        return $machine;
    } elsif ($var eq 'osname') {
        if ($sysname =~ /cygwin/i || $sysname =~ /mingw32/i) {
            return 'win32';
        } elsif ($sysname =~ /darwin/i) {
            return 'macosx';
        } elsif ($sysname =~ /linux/i) {
            return 'linux';
        } else {
            die("Unrecognized OS: $sysname");
        }
    } else {
        die("No system info named $var");
    }
}

##
# Bump - modifies config files
#
# Searches and replaces lines of the form:
#   # CONFIG: $BuildTag = '%productTag%_RELEASE';
#   $BuildTag = 'FIREFOX_1_5_0_9_RELEASE';
#
# The comment containing "CONFIG:" is parsed, and the value in between %%
# is treated as the key. The next line will be overwritten by the value
# matching this key in the private %config hash.
#
# If any of the requested keys are not found, this function calls die().
##

sub Bump {
    my $this = shift;
    my %args = @_;

    my $config = new Bootstrap::Config();

    my $configFile = $args{'configFile'};
    if (! defined($configFile)) {
        die('ASSERT: Bootstrap::Config::Bump - configFile is a required argument');
    }

    my $tmpFile = $configFile . '.tmp';

    open(INFILE, "< $configFile") 
     or die ("Bootstrap::Config::Bump - Could not open $configFile for reading: $!");
    open(OUTFILE, "> $tmpFile") 
     or die ("Bootstrap::Config::Bump - Could not open $tmpFile for writing: $!");

    my $skipNextLine = 0;
    my $KEY_REGEX = qr/ ([\w\-]+) /x;
    foreach my $line (<INFILE>) {
        if ($skipNextLine) {
            $skipNextLine = 0;
            next;
        } elsif ($line =~ /^# CONFIG:\s+/) {
            print OUTFILE $line;
            $skipNextLine = 1;
            my $interpLine = $line;
            $interpLine =~ s/^#\s+CONFIG:\s+//;
            foreach my $variable (grep(/%/,split(/(%$KEY_REGEX?%)/, $line))) {
                my $key = $variable;
                if (! ($key =~ s/.*%($KEY_REGEX)%.*/$1/)) {
                    die("ASSERT: could not parse $variable");
                }

                $key =~ s/^%(.*)%$/$1/;

                if ($key =~ /^\s*$/) {
                    die("ASSERT: could not get key from $variable");
                }

                if (! $config->Exists(sysvar => $key)) {
                    die("ASSERT: no replacement found for $key");
                }
                my $value = $config->Get(sysvar => $key);
                $interpLine =~ s/\%$key\%/$value/g;
            }
            print OUTFILE $interpLine;
        } else {
            print OUTFILE $line;
        }
    }

    close(INFILE) or die ("Bootstrap::Config::Bump - Could not close $configFile for reading: $!");
    close(OUTFILE) or die ("Bootstrap::Config::Bump - Could not close $tmpFile for writing: $!");

    move($tmpFile, $configFile)
     or die("Cannot rename $tmpFile to $configFile: $!");

}

1;
