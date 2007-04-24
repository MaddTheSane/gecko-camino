#
# Repack step. Unpacks, modifies, repacks a Firefox en-US build.
# Primary use is for l10n (localization) builds.
#
package Bootstrap::Step::Repack;
use Bootstrap::Step;
use Bootstrap::Config;
use MozBuild::Util qw(MkdirWithPath);
@ISA = ("Bootstrap::Step");

sub Execute {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $l10n_buildDir = $config->Get(sysvar => 'l10n_buildDir');
    my $productTag = $config->Get(var => 'productTag');
    my $rc = $config->Get(var => 'rc');
    my $logDir = $config->Get(var => 'logDir');
    my $l10n_buildPlatform = $config->Get(sysvar => 'l10n_buildPlatform');
    my $rcTag = $productTag . '_RC' . $rc;

    my $buildLog = catfile($logDir, 'repack_' . $rcTag . '-build-l10n.log');
    my $lastBuilt = catfile($l10n_buildDir, $l10n_buildPlatform, 'last-built');
    unlink($lastBuilt) 
      or $this->Log(msg => "Cannot unlink last-built file $lastBuilt: $!");
    $this->Log(msg => "Unlinked $lastBuilt");

    $this->Shell(
      cmd => './build-seamonkey.pl',
      cmdArgs => ['--once', '--mozconfig', 'mozconfig', '--depend', 
                  '--config-cvsup-dir', 
                  catfile($l10n_buildDir, 'tinderbox-configs')],
      dir => $l10n_buildDir,
      logFile => $buildLog,
      timeout => 36000
    );
}

sub Verify {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $productTag = $config->Get(var => 'productTag');
    my $product = $config->Get(var => 'product');
    my $rc = $config->Get(var => 'rc');
    my $oldRc = $config->Get(var => 'oldRc');
    my $logDir = $config->Get(var => 'logDir');
    my $version = $config->Get(var => 'version');
    my $oldVersion = $config->Get(var => 'oldVersion');
    my $mozillaCvsroot = $config->Get(var => 'mozillaCvsroot');
    my $verifyDir = $config->Get(var => 'verifyDir');

    my $rcTag = $productTag.'_RC'.$rc;

    # l10n metadiff test

    my $verifyDirVersion = catfile($verifyDir, $product . '-' . $version);

    MkdirWithPath(dir => $verifyDirVersion)
      or die("Cannot mkdir $verifyDirVersion: $!");

    # check out l10n verification scripts
    foreach my $dir ('common', 'l10n') {
        $this->Shell(
          cmd => 'cvs',
          cmdArgs => ['-d', $mozillaCvsroot, 
                      'co', '-d', $dir, 
                      catfile('mozilla', 'testing', 'release', $dir)],
          dir => $verifyDirVersion,
          logFile => catfile($logDir, 
                               'repack_checkout-l10n_verification.log'),
        );
    }

    # Download current release
    $this->Shell(
      cmd => 'rsync',
      cmdArgs => ['-v', 
                  '-e', 'ssh', 
                  '--include=*.dmg',
                  '--include=*.exe',
                  '--include=*.tar.gz',
                  '--exclude=*',
                  'stage.mozilla.org:/home/ftp/pub/' . $product
                  . '/nightly/' . $version . '-candidates/rc' . $rc . '/*',
                  $product . '-' . $version . '-rc' . $rc . '/',
                 ],
      dir => catfile($verifyDirVersion, 'l10n'),
      logFile => 
        catfile($logDir, 'repack_verify-download_' . $version . '.log'),
      timeout => 3600
    );

    # Download previous release
    $this->Shell(
      cmd => 'rsync',
      cmdArgs => ['-v', 
                  '-e', 'ssh', 
                  '--include=*.dmg',
                  '--include=*.exe',
                  '--include=*.tar.gz',
                  '--exclude=*',
                  'stage.mozilla.org:/home/ftp/pub/' . $product
                  . '/nightly/' . $oldVersion . '-candidates/rc' 
                  . $oldRc . '/*',
                  $product . '-' . $oldVersion . '-rc' . $oldRc . '/',
                 ],
      dir => catfile($verifyDirVersion, 'l10n'),
      logFile => 
        catfile($logDir, 'repack_verify-download_' . $oldVersion . '.log'),
      timeout => 3600
    );

    my $newProduct = $product . '-' . $version . '-' . 'rc' . $rc;
    my $oldProduct = $product . '-' . $oldVersion . '-' . 'rc' . $oldRc;

    foreach my $product ($newProduct, $oldProduct) {
        MkdirWithPath(dir => catfile($verifyDirVersion, 'l10n', $product))
          or die("Cannot mkdir $verifyDirVersion/$product: $!");

        $this->Shell(
          cmd => './verify_l10n.sh',
          cmdArgs => [$product],
          dir => catfile($verifyDirVersion, 'l10n'),
          logFile => catfile($logDir, 
                             'repack_' . $product . '-l10n_verification.log'),
        );


        foreach my $rule ('^FAIL', '^Binary') {
            eval {
                $this->CheckLog(
                    log => $logDir . 
                           '/repack_' . $product . '-l10n_verification.log',
                    notAllowed => $rule,
                );
            };
            if ($@) {
                $this->Log('msg' => 
                           "WARN: $rule found in l10n metadiff output!");
            }
        }

        $this->CheckLog(
            log => $logDir . '/repack_' . $product . '-l10n_verification.log',
            notAllowed => '^Only',
        );
    }

    # generate metadiff
    $this->Shell(
      cmd => 'diff',
      cmdArgs => ['-r', 
                  catfile($newProduct, 'diffs'),
                  catfile($oldProduct, 'diffs'),
                 ],
      ignoreExitValue => 1,
      dir => catfile($verifyDirVersion, 'l10n'),
      logFile => catfile($logDir, 'repack_metadiff-l10n_verification.log'),
    );
}
sub Announce {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $product = $config->Get(var => 'product');
    my $productTag = $config->Get(var => 'productTag');
    my $version = $config->Get(var => 'version');
    my $rc = $config->Get(var => 'rc');
    my $logDir = $config->Get(var => 'logDir');

    my $rcTag = $productTag . '_RC' . $rc;
    my $buildLog = catfile($logDir, 'repack_' . $rcTag . '-build-l10n.log');

    my $logParser = new MozBuild::TinderLogParse(
        logFile => $buildLog,
    );
    my $buildID = $logParser->GetBuildID();
    my $pushDir = $logParser->GetPushDir();

    if (! defined($pushDir)) {
        die("No pushDir found in $buildLog");
    } 

    $this->SendAnnouncement(
      subject => "$product $version l10n repack step finished",
      message => "$product $version l10n builds are ready to be copied to the candidates directory.\nPush Dir is $pushDir",
    );
}

1;
