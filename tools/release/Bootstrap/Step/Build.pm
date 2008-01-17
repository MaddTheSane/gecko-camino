#
# Build step. Calls tinderbox to produce en-US Firefox build.
#
package Bootstrap::Step::Build;

use File::Temp qw(tempfile);

use Bootstrap::Step;
use Bootstrap::Util qw(CvsCatfile SyncToStaging);

@ISA = ("Bootstrap::Step");

sub Execute {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $version = $config->GetVersion(longName => 0);
    my $buildDir = $config->Get(sysvar => 'buildDir');
    my $productTag = $config->Get(var => 'productTag');
    my $rc = $config->Get(var => 'rc');
    my $buildPlatform = $config->Get(sysvar => 'buildPlatform');
    my $logDir = $config->Get(sysvar => 'logDir');
    my $sysname = $config->SystemInfo(var => 'sysname');    
    my $rcTag = $productTag . '_RC' . $rc;

    if ($version eq 'nightly') {
        $this->Log(msg => 'Skip force-clobber for nightly mode');
    } else {
        my $lastBuilt = catfile($buildDir, $buildPlatform, 'last-built');
        if (! unlink($lastBuilt)) {
            $this->Log(msg => "Cannot unlink last-built file $lastBuilt: $!");
        } else {
            $this->Log(msg => "Unlinked $lastBuilt");
        }
    }

    my $buildLog = catfile($logDir, 'build_' . $rcTag . '-build.log');
 
    # For Cygwin only, ensure that the system mount point is binmode
    # This forces CVS to use Unix-style linefeed EOL characters.
    if ($sysname =~ /cygwin/i) {
        $this->Shell(
          cmd => 'mount',
          cmdArgs => ['-b', '-sc', '/cygdrive'],
          dir => $buildDir,
        );
    }
  
    $this->Shell(
      cmd => './build-seamonkey.pl',
      cmdArgs => ['--once', '--mozconfig', 'mozconfig', '--depend', 
                  '--config-cvsup-dir', 
                  catfile($buildDir, 'tinderbox-configs')],
      dir => $buildDir,
      logFile => $buildLog,
      timeout => 36000
    );

    if ($version eq 'nightly') {
        $this->Log(msg => 'Skip buildID storage for nightly mode');
    } else {
        $this->StoreBuildID();
    }
}

sub Verify {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $version = $config->GetVersion(longName => 0);
    my $buildDir = $config->Get(sysvar => 'buildDir');
    my $productTag = $config->Get(var => 'productTag');
    my $rc = $config->Get(var => 'rc');
    my $rcTag = $productTag.'_RC'.$rc;
    my $logDir = $config->Get(sysvar => 'logDir');

    if ($version eq 'nightly') {
        $this->Log(msg => 'Skip Verify for nightly mode');
        return;
    }

    my $buildLog = catfile($logDir, 'build_' . $rcTag . '-build.log');

    $this->CheckLog(
        log => $buildLog,
        notAllowed => 'tinderbox: status: failed',
    );

    my $logParser = new MozBuild::TinderLogParse(
        logFile => $buildLog,
    );

    if (! defined($logParser->GetBuildID())) {
        die("No buildID found in $buildLog");
    }
    if (! defined($logParser->GetPushDir())) {
        die("No pushDir found in $buildLog");
    }
}

sub Push {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $version = $config->GetVersion(longName => 0);
    my $productTag = $config->Get(var => 'productTag');
    my $rc = $config->Get(var => 'rc');
    my $logDir = $config->Get(sysvar => 'logDir');
    my $stagingUser = $config->Get(var => 'stagingUser');
    my $stagingServer = $config->Get(var => 'stagingServer');

    if ($version eq 'nightly') {
        $this->Log(msg => 'Skip Push for nightly mode');
        return;
    }

    my $rcTag = $productTag . '_RC' . $rc;
    my $buildLog = catfile($logDir, 'build_' . $rcTag . '-build.log');
    my $pushLog  = catfile($logDir, 'build_' . $rcTag . '-push.log');

    my $logParser = new MozBuild::TinderLogParse(
        logFile => $buildLog,
    );
    my $pushDir = $logParser->GetPushDir();
    if (! defined($pushDir)) {
        die("No pushDir found in $buildLog");
    }
    $pushDir =~ s!^http://ftp.mozilla.org/pub/mozilla.org!/home/ftp/pub!;

    my $candidateDir = $config->GetFtpCandidateDir(bitsUnsigned => 1);

    my $osFileMatch = $config->SystemInfo(var => 'osname');    

    # TODO - use a more generic function for this kind of remapping
    if ($osFileMatch eq 'win32')  {
      $osFileMatch = 'win';
    } elsif ($osFileMatch eq 'macosx') {
      $osFileMatch = 'mac';
    }

    $this->Shell(
      cmd => 'ssh',
      cmdArgs => ['-2', '-l', $stagingUser, $stagingServer,
                  'mkdir -p ' . $candidateDir],
      logFile => $pushLog,
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
      logFile => $pushLog,
    );

    $this->Shell(
      cmd => 'ssh',
      cmdArgs => ['-2', '-l', $stagingUser, $stagingServer,
                  'rsync', '-av', 
                  '--include=*' . $osFileMatch . '*',
                  '--exclude=*', 
                  $pushDir, 
                  $candidateDir],
      logFile => $pushLog,
    );

    SyncToStaging(); 
}

sub Announce {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $version = $config->GetVersion(longName => 0);
    my $product = $config->Get(var => 'product');
    my $productTag = $config->Get(var => 'productTag');
    my $rc = $config->Get(var => 'rc');
    my $logDir = $config->Get(sysvar => 'logDir');

    if ($version eq 'nightly') {
        $this->Log(msg => 'Skip Announce for nightly mode');
        return;
    }

    my $rcTag = $productTag . '_RC' . $rc;
    my $buildLog = catfile($logDir, 'build_' . $rcTag . '-build.log');

    my $logParser = new MozBuild::TinderLogParse(
        logFile => $buildLog,
    );
    my $buildID = $logParser->GetBuildID();
    my $pushDir = $logParser->GetPushDir();

    if (! defined($buildID)) {
        die("No buildID found in $buildLog");
    } 
    if (! defined($pushDir)) {
        die("No pushDir found in $buildLog");
    } 

    $this->SendAnnouncement(
      subject => "$product $version build step finished",
      message => "$product $version en-US build was copied to the candidates dir.\nBuild ID is $buildID\nPush Dir was $pushDir",
    );
}

sub StoreBuildID() {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $productTag = $config->Get(var => 'productTag');
    my $rc = $config->Get(var => 'rc');
    my $logDir = $config->Get(sysvar => 'logDir');
    my $stagingUser = $config->Get(var => 'stagingUser');
    my $stagingServer = $config->Get(var => 'stagingServer');

    my $rcTag = $productTag . '_RC' . $rc;
    my $buildLog = catfile($logDir, 'build_' . $rcTag . '-build.log');
    my $pushLog  = catfile($logDir, 'build_' . $rcTag . '-push.log');

    my $logParser = new MozBuild::TinderLogParse(
        logFile => $buildLog,
    );
    my $pushDir = $logParser->GetPushDir();
    if (! defined($pushDir)) {
        die("No pushDir found in $buildLog");
    }
    $pushDir =~ s!^http://ftp.mozilla.org/pub/mozilla.org!/home/ftp/pub!;

    # drop os-specific buildID file on FTP
    my $buildID = $logParser->GetBuildID();
    if (! defined($buildID)) {
        die("No buildID found in $buildLog");
    }
    if (! $buildID =~ /^\d+$/) {
        die("ASSERT: Build: build ID is not numerical: $buildID")
    }


    my $osFileMatch = $config->SystemInfo(var => 'osname');    

    my ($bh, $buildIDTempFile) = tempfile(DIR => '.');
    print $bh 'buildID=' . $buildID;
    $bh->close() || 
     die("Could not open buildID temp file $buildIDTempFile: $!");
    chmod(0644, $buildIDTempFile);

    my $buildIDFile = $osFileMatch . '_info.txt';
    $this->Shell(
      cmd => 'scp',
      cmdArgs => ['-p', $buildIDTempFile, 
                  $stagingUser . '@' . $stagingServer . ':' .
                  $pushDir . '/' . $buildIDFile],
      logFile => $pushLog,
    );
}

1;
