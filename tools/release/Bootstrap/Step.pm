#
# Base class for all Steps.
#

package Bootstrap::Step;

use IO::Handle;
use File::Spec::Functions;
use POSIX qw(strftime);
use File::Temp qw(tempfile);

use Bootstrap::Config;
use Bootstrap::Util qw(CvsCatfile);
use MozBuild::Util qw(RunShellCommand Email);

use base 'Exporter';

our @EXPORT = qw(catfile);

my $DEFAULT_LOGFILE = 'default.log';

sub new {
    my $proto = shift;
    my $class = ref($proto) || $proto;
    my $this = {};
    bless($this, $class);
    return $this;
}

sub Shell {
    my $this = shift;
    my %args = @_;
    my $cmd = $args{'cmd'};
    my $cmdArgs = exists($args{'cmdArgs'}) ? $args{'cmdArgs'} : [];
    my $dir = $args{'dir'};
    my $timeout = exists($args{'timeout'}) ? $args{'timeout'} :
     $Bootstrap::Util::DEFAULT_SHELL_TIMEOUT;
    my $ignoreExitValue = exists($args{'ignoreExitValue'}) ? 
     $args{'ignoreExitValue'} : 0;
    my $rv = '';
    my $config = new Bootstrap::Config();

    my $logFile = exists($args{'logFile'}) ? $args{'logFile'} : 
     catfile($config->Get(sysvar => 'logDir'), $DEFAULT_LOGFILE);

    if (ref($cmdArgs) ne 'ARRAY') {
        die("ASSERT: Bootstrap::Step::Shell(): cmdArgs is not an array ref\n");
    }

    my %runShellCommandArgs = (command => $cmd,
                               args => $cmdArgs,
                               timeout => $timeout,
                               logfile => $logFile);

    if ($config->Exists(var => 'dumpLogs')) {
        if ($config->Get(var => 'dumpLogs')) {
            $runShellCommandArgs{'output'} = 1;
        }
    }

    if ($dir) {
        $runShellCommandArgs{'dir'} = $dir;
    }

    $this->Log(msg => 'Running shell command' .
     (defined($dir) ? " in $dir" : '') . ':');
    $this->Log(msg => '  arg0: ' . $cmd); 
    my $argNum = 1;
    foreach my $arg (@{$cmdArgs}) {
        $this->Log(msg => '  arg' . $argNum . ': ' . $arg); 
        $argNum += 1;
    }
    $this->Log(msg => 'Starting time is ' . $this->CurrentTime());
    $this->Log(msg => 'Logging output to ' . $logFile);

    $this->Log(msg => 'Timeout: ' . $timeout);

    $rv = RunShellCommand(%runShellCommandArgs);

    my $exitValue = $rv->{'exitValue'};
    my $timedOut  = $rv->{'timedOut'};
    my $signalNum  = $rv->{'signalNum'};
    my $dumpedCore = $rv->{'dumpedCore'};
    if ($timedOut) {
        $this->Log(msg => "output: $rv->{'output'}") if $rv->{'output'};
        die('FAIL shell call timed out after ' . $timeout . ' seconds');
    }
    if ($signalNum) {
        $this->Log(msg => 'WARNING shell recieved signal ' . $signalNum);
    }
    if ($dumpedCore) {
        $this->Log(msg => "output: $rv->{'output'}") if $rv->{'output'};
        die("FAIL shell call dumped core");
    }
    if ($ignoreExitValue) {
        $this->Log(msg => "Exit value $rv->{'output'}, but ignoring as told");
    } elsif ($exitValue) {
        if ($exitValue != 0) {
            $this->Log(msg => "output: $rv->{'output'}") if $rv->{'output'};
            die("shell call returned bad exit code: $exitValue");
        }
    }

    if ($rv->{'output'} && not defined($logFile)) {
        $this->Log(msg => "output: $rv->{'output'}");
    }

    # current time
    $this->Log(msg => 'Ending time is ' . $this->CurrentTime());
}

sub Log {
    my $this = shift;
    my %args = @_;
    my $msg = $args{'msg'};
    print "log: $msg\n";
}

sub CheckLog {
    my $this = shift;
    my %args = @_;

    my $log = $args{'log'};
    my $notAllowed = $args{'notAllowed'};
    my $checkFor = $args{'checkFor'};
    my $checkForOnly = $args{'checkForOnly'};

    if (not defined($log)) {
        die("No log file specified");
    }

    open (FILE, "< $log") or die("Cannot open file $log: $!");
    my @contents = <FILE>;
    close FILE or die("Cannot close file $log: $!");
  
    if ($notAllowed) {
        my @errors = grep(/$notAllowed/i, @contents);
        if (@errors) {
            die("Errors in log ($log): \n\n @errors");
        }
    }
    if ($checkFor) {
        if (not grep(/$checkFor/i, @contents)) {
            die("$checkFor is not present in file $log");
        }
    }
    if ($checkForOnly) {
        if (not grep(/$checkForOnly/i, @contents)) {
            die("$checkForOnly is not present in file $log");
        }
        my @errors = grep(!/$checkForOnly/i, @contents);
        if (@errors) {
            die("Errors in log ($log): \n\n @errors");
        }
    }
}

sub CurrentTime {
    my $this = shift;

    return strftime("%T %D", localtime());
}

# Overridden by child if needed
sub Push {
    my $this = shift;
}

# Overridden by child if needed
sub Announce {
    my $this = shift;
}

sub SendAnnouncement {
    my $this = shift;
    my %args = @_;
    
    my $config = new Bootstrap::Config();

    my $blat = $config->Get(var => 'blat');
    my $sendmail = $config->Get(var => 'sendmail');
    my $from = $config->Get(var => 'from');
    my $to = $config->Get(var => 'to');
    my @ccList = $config->Exists(var => 'cc') ? split(/[,\s]+/, 
     $config->Get(var => 'cc')) : ();
    my $hostname = $config->SystemInfo(var => 'hostname');

    my $subject = $hostname . ' - ' . $args{'subject'};
    my $message = $hostname . ' - ' . $args{'message'};

    eval {
        Email(
          blat => $blat,
          sendmail => $sendmail,
          from => $from,
          to => $to,
          cc => \@ccList,
          subject => $subject,
          message => $message,
        );
    };
    if ($@) {
        die("Could not send announcement email: $@");
    }
}
    
sub GetBuildIDFromFTP() {
    my $this = shift;
    my %args = @_;

    my $os = $args{'os'};
    if (! defined($os)) {
        die("ASSERT: Bootstrap::Step::GetBuildID(): os is required argument");
    }
    my $releaseDir = $args{'releaseDir'};
    if (! defined($releaseDir)) {
        die("ASSERT: Bootstrap::Step::GetBuildID(): releaseDir is required argument");
    }

    my $config = new Bootstrap::Config();
    my $stagingUser = $config->Get(var => 'stagingUser');
    my $stagingServer = $config->Get(var => 'stagingServer');

    my ($bh, $buildIDTempFile) = tempfile(UNLINK => 1);
    $bh->close();
    $this->Shell(
      cmd => 'scp',
      cmdArgs => [$stagingUser . '@' . $stagingServer . ':' . 
                  $releaseDir .'/' . $os . '_info.txt',
                  $buildIDTempFile],
    );
    my $buildID;
    open(FILE, "< $buildIDTempFile") || 
     die("Could not open buildID temp file $buildIDTempFile: $!");
    while (<FILE>) {
      my ($var, $value) = split(/\s*=\s*/, $_, 2);
      if ($var eq 'buildID') {
          $buildID = $value;
      }
    }
    close(FILE) || 
     die("Could not close buildID temp file $buildIDTempFile: $!");
    if (! defined($buildID)) {
        die("Could not read buildID from temp file $buildIDTempFile: $!");
    }
    if (! $buildID =~ /^\d+$/) {
        die("ASSERT: BumpPatcherConfig: $buildID is non-numerical");
    }
    chomp($buildID);

    return $buildID;
}

sub CvsCo {
    my $this = shift;
    my %args = @_;

    # Required arguments
    die "ASSERT: Bootstrap::Util::CvsCo(): null cvsroot" if
     (!exists($args{'cvsroot'}));
    my $cvsroot = $args{'cvsroot'};

    die "ASSERT: Bootstrap::Util::CvsCo(): null modules" if
     (!exists($args{'modules'}));
    my $modules = $args{'modules'};

    die "ASSERT: Bootstrap::Util::CvsCo(): bad modules data" if
     (ref($modules) ne 'ARRAY');

    # Optional arguments
    my $logFile = $args{'logFile'};
    my $tag = exists($args{'tag'}) ? $args{'tag'} : 0; 
    my $date = exists($args{'date'}) ? $args{'date'} : 0;
    my $checkoutDir = exists($args{'checkoutDir'}) ? $args{'checkoutDir'} : 0;
    my $workDir = exists($args{'workDir'}) ? $args{'workDir'} : 0;
    my $ignoreExitValue = exists($args{'ignoreExitValue'}) ?
        $args{'ignoreExitValue'} : 0;
    my $timeout = exists($args{'timeout'}) ? $args{'timeout'} :
     $Bootstrap::Util::DEFAULT_SHELL_TIMEOUT;

    my $config = new Bootstrap::Config();

    my $useCvsCompression = 0;
    if ($config->Exists(var => 'useCvsCompression')) {
        $useCvsCompression = $config->Get(var => 'useCvsCompression');
    }

    my @cmdArgs;
    push(@cmdArgs, '-z3') if ($useCvsCompression);
    push(@cmdArgs, ('-d', $cvsroot));
    push(@cmdArgs, 'co');
    # Don't use a tag/branch if pulling from HEAD
    push(@cmdArgs, ('-r', $tag)) if ($tag && $tag ne 'HEAD');
    push(@cmdArgs, ('-D', $date)) if ($date);
    push(@cmdArgs, ('-d', $checkoutDir)) if ($checkoutDir);
    push(@cmdArgs, @{$modules});

    my %cvsCoArgs = (cmd => 'cvs',
                     cmdArgs => \@cmdArgs,
                     dir => $workDir,
                     timeout => $timeout,
                     ignoreExitValue => $ignoreExitValue,
    );
    if ($logFile) {
        $cvsCoArgs{'logFile'} = $logFile;
    }

    $this->Shell(%cvsCoArgs);
}

sub CreateCandidatesDir() {
    my $this = shift;

    my $config = new Bootstrap::Config();

    my $stagingUser = $config->Get(var => 'stagingUser');    
    my $stagingServer = $config->Get(var => 'stagingServer');    
    my $candidateDir = $config->GetFtpCandidateDir(bitsUnsigned => 1);

    $this->Shell(
      cmd => 'ssh',
      cmdArgs => ['-2', '-l', $stagingUser, $stagingServer,
                  'mkdir -p ' . $candidateDir],
    );

    # Make sure permissions are created on the server correctly;
    #
    # Note the '..' at the end of the chmod string; this is because
    # Config::GetFtpCandidateDir() returns the full path, including the
    # rcN directories on the end. What we really want to ensure
    # have the correct permissions (from the mkdir call above) is the
    # firefox/nightly/$version-candidates/ directory.
    #
    # XXX - This is ugly; another solution is to fix the umask on stage, or
    # change what GetFtpCandidateDir() returns.

    my $chmodArg = CvsCatfile($config->GetFtpCandidateDir(bitsUnsigned => 0), 
     '..');

    $this->Shell(
      cmd => 'ssh',
      cmdArgs => ['-2', '-l', $stagingUser, $stagingServer,
                  'chmod 0755 ' . $chmodArg],
    );
}

1;
