#
# Updates step. Configures patcher files.
# 
# 
package Bootstrap::Step::PatcherConfig;

use Config::General;
use File::Temp qw(tempfile);

use MozBuild::Util qw(MkdirWithPath);

use Bootstrap::Step;
use Bootstrap::Config;
use Bootstrap::Util qw(CvsCatfile GetBouncerPlatforms
                       GetBouncerToPatcherPlatformMap);

@ISA = ("Bootstrap::Step");

use strict;

my $RELEASE_CANDIDATE_CHANNELS = ['beta', 'betatest','DisableCompleteJump'];

sub Execute {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $logDir = $config->Get(sysvar => 'logDir');
    my $configBumpDir = $config->Get(var => 'configBumpDir');
    my $product = $config->Get(var => 'product');
    my $rc = $config->Get(var => 'rc');
    my $version = $config->GetVersion(longName => 0);
    my $oldVersion = $config->GetOldVersion(longName => 0);
    my $mozillaCvsroot = $config->Get(var => 'mozillaCvsroot');
    my $patcherConfig = $config->Get(var => 'patcherConfig');
    my $ftpServer = $config->Get(var => 'ftpServer');
    my $bouncerServer = $config->Get(var => 'bouncerServer');

    my $versionedConfigBumpDir = catfile($configBumpDir, 
                                          "$product-$version-rc$rc");

    # Create patcher config area in the config bump area.
    if (not -d $versionedConfigBumpDir) {
        MkdirWithPath(dir => $versionedConfigBumpDir) 
          or die("Cannot mkdir $versionedConfigBumpDir: $!");
    }

    # checkout config to bump
    $this->CvsCo(cvsroot => $mozillaCvsroot,
                 checkoutDir => 'patcher',
                 modules => [CvsCatfile('mozilla', 'tools', 'patcher-configs',
                                        $patcherConfig)],
                 logFile => catfile($logDir, 'patcherconfig-checkout.log'),
                 workDir => $versionedConfigBumpDir
    );

    # Do all the work...
    $this->BumpPatcherConfig();

    # verify that BumpPatcherConfig() actually did something.
    $this->Log(msg=> 'Ignoring shell value here because cvs diff returns a ' .
     'non-zero value if a diff exists; this is an assertion that a diff does ' .
     'exist');

    $this->Shell(
      cmd => 'cvs',
      cmdArgs => ['diff', $patcherConfig ],
      logFile => catfile($logDir, 'patcherconfig-diff.log'),
      ignoreExitValue => 1,
      dir => catfile($versionedConfigBumpDir, 'patcher'),
    ); 

    $this->Shell(
      cmd => 'cvs',
      cmdArgs => ['-d', $mozillaCvsroot,
                  'ci', '-m', "\"Automated configuration bump: $patcherConfig, "
                   .  "from $oldVersion to $version\"", $patcherConfig],
      logFile => catfile($logDir, 'patcherconfig-checkin.log'),
      dir => catfile($versionedConfigBumpDir, 'patcher'),
    ); 
}

sub BumpPatcherConfig {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $product = $config->Get(var => 'product');
    my $configBumpDir = $config->Get(var => 'configBumpDir');
    my $version = $config->GetVersion(longName => 0);
    my $prettyVersion = $config->GetVersion(longName => 1);
    my $oldVersion = $config->GetOldVersion(longName => 0);
    my $rc = $config->Get(var => 'rc');
    my $oldRc = $config->Get(var => 'oldRc');
    my $localeInfo = $config->GetLocaleInfo();
    my $patcherConfig = $config->Get(var => 'patcherConfig');
    my $stagingUser = $config->Get(var => 'stagingUser');
    my $stagingServer = $config->Get(var => 'stagingServer');
    my $ftpServer = $config->Get(var => 'ftpServer');
    my $bouncerServer = $config->Get(var => 'bouncerServer');
    my $logDir = $config->Get(sysvar => 'logDir');

    my $versionedConfigBumpDir = catfile($configBumpDir, 
                                          "$product-$version-rc$rc");

    # First, parse the file.
    my $checkedOutPatcherConfig = catfile($versionedConfigBumpDir, 'patcher', 
                                           $patcherConfig);

    my $patcherConfigObj = new Config::General(-ConfigFile => 
     $checkedOutPatcherConfig);

    my %rawConfig = $patcherConfigObj->getall();
    die "ASSERT: BumpPatcherConfig(): null rawConfig" 
     if (0 == scalar(keys(%rawConfig)));

    my $appObj = $rawConfig{'app'}->{ucfirst($product)};
    die "ASSERT: BumpPatcherConfig(): null appObj" if (! defined($appObj));

    my $currentUpdateObj = $appObj->{'current-update'};

    # Add the release we're replacing to the past-releases array, but only if
    # it's a new release; we used to determine this by looking at the rc 
    # value, but that can be misleading because sometimes we may not get to
    # the update step before a respin; so what we really need to compare is 
    # whether our current version in bootstrap.cfg is in the to clause of the 
    # patcher config file/object; we now control this via doOnetimePatcherBumps.
    #
    # More complicated than it needs to be because it handles the (uncommon)
    # case that there is no past-update yet (e.g. Firefox 3.0)

    my $doOnetimePatcherBumps = ($currentUpdateObj->{'to'} ne $version);

    if ($doOnetimePatcherBumps) {
        my $pastUpdateObj = $appObj->{'past-update'};
        if (ref($pastUpdateObj) ne 'ARRAY') {
            my $oldSinglePastUpdateStr = $pastUpdateObj;
            $appObj->{'past-update'} = $pastUpdateObj = [];
            push(@{$pastUpdateObj}, $oldSinglePastUpdateStr);
        }

        my @pastUpdateChannels = (split(/[\s,]+/,
                                   $currentUpdateObj->{'testchannel'}),
                                  split(/[\s,]+/,
                                   $currentUpdateObj->{'channel'}));

        push(@{$pastUpdateObj}, join(' ', $currentUpdateObj->{'from'},
         $currentUpdateObj->{'to'}, @pastUpdateChannels));
    }

    # Now we can replace information in the "current-update" object; start
    # with the to/from versions, the rc channels, then the information for
    # the partial and complete update patches
    #
    # Only bump the to/from versions if we're really a new release. We used
    # to determine this by looking at the rc value, but now we use
    # doOnetimePatcherBump 
    
    if ($doOnetimePatcherBumps) {
        $currentUpdateObj->{'to'} = $version;
        $currentUpdateObj->{'from'} = $oldVersion;
    }

    $currentUpdateObj->{'details'} = 'http://%locale%.www.mozilla.com/%locale%/' .
                                      $product . '/' . $version . '/releasenotes/';

    $currentUpdateObj->{'rc'} = {};
    foreach my $c (@{$RELEASE_CANDIDATE_CHANNELS}) {
        $currentUpdateObj->{'rc'}->{$c} = "$rc";
    }

    my $rcStr = 'rc' . $rc;

    my $partialUpdate = {};
    $partialUpdate->{'url'} = 'http://' . $bouncerServer . '/?product=' .
                              $product. '-' . $version . '-partial-' . 
                              $oldVersion .
                             '&os=%bouncer-platform%&lang=%locale%';

    $partialUpdate->{'path'} = catfile($product, 'nightly', $version . 
                               '-candidates', $rcStr, $product. '-' . 
                               $oldVersion . '-' . $version .
                               '.%locale%.%platform%.partial.mar');

    $partialUpdate->{'betatest-url'} =
     'http://' . $stagingServer. '/pub/mozilla.org/' . $product . 
     '/nightly/' .  $version . '-candidates/' . $rcStr . '/' . $product . 
     '-' . $oldVersion .  '-' . $version . '.%locale%.%platform%.partial.mar';

    $partialUpdate->{'beta-url'} =
     'http://' . $ftpServer . '/pub/mozilla.org/' . $product. '/nightly/' . 
     $version . '-candidates/' . $rcStr . '/' . $product . '-' . $oldVersion . 
     '-' . $version . '.%locale%.%platform%.partial.mar';

    $currentUpdateObj->{'partial'} = $partialUpdate;

    # Now the same thing, only complete update
    my $completeUpdate = {};
    $completeUpdate->{'url'} = 'http://' . $bouncerServer . '/?product=' .
     $product . '-' . $version . 
     '-complete&os=%bouncer-platform%&lang=%locale%';

    $completeUpdate->{'path'} = catfile($product, 'nightly', $version . 
     '-candidates', $rcStr, $product . '-' . $version .
     '.%locale%.%platform%.complete.mar');

    $completeUpdate->{'betatest-url'} = 
     'http://' . $stagingServer . '/pub/mozilla.org/' . $product . 
     '/nightly/' .  $version . '-candidates/' . $rcStr .  '/' . $product . 
     '-' . $version .  '.%locale%.%platform%.complete.mar';

    $completeUpdate->{'beta-url'} = 
     'http://' . $ftpServer . '/pub/mozilla.org/' . $product. '/nightly/' .
     $version . '-candidates/' . $rcStr .  '/' . $product . '-' . $version .
     '.%locale%.%platform%.complete.mar';

    $currentUpdateObj->{'complete'} = $completeUpdate;

    # Now, add the new <release> stanza for the release we're working on

    my $releaseObj;
    $appObj->{'release'}->{$version} = $releaseObj = {};

    $releaseObj->{'schema'} = '1';
    $releaseObj->{'version'} = $releaseObj->{'extension-version'} = $version;
    $releaseObj->{'prettyVersion'} = $prettyVersion;

    my $linBuildId;
    my $winBuildId;
    my $macBuildId;

    # grab os-specific buildID file on FTP
    my $candidateDir = $config->GetFtpCandidateDir(bitsUnsigned => 0);
    foreach my $os ('linux', 'macosx', 'win32') {
        my $buildID = $this->GetBuildIDFromFTP(os => $os, releaseDir => $candidateDir);

        if ($os eq 'linux') {
            $linBuildId = "$buildID";
        } elsif ($os eq 'macosx') {
            $macBuildId = "$buildID";
        } elsif ($os eq 'win32') {
            $winBuildId = "$buildID";
        } else {
            die("ASSERT: BumpPatcherConfig(): unknown OS $os");
        }
    }

    $releaseObj->{'platforms'} = { 'linux-i686' => $linBuildId,
                                   'win32' => $winBuildId,
                                   'mac' => $macBuildId };

    $releaseObj->{'locales'} = join(' ', sort (keys(%{$localeInfo})));

    $releaseObj->{'completemarurl'} = 
     'http://' . $stagingServer . '/pub/mozilla.org/' . $product. 
     '/nightly/' .  $version . '-candidates/' . $rcStr . '/' . $product . '-'. 
     $version . '.%locale%.%platform%.complete.mar',

    # Compute locale exceptions; 
    # $localeInfo is hash ref of locales -> array of platforms the locale
    # is for.
    #
    # To calculate the exceptions to this rule, create a hash with 
    # all known platforms in it. It's a hash so we can easily delete() keys
    # out of it. Then, we iterate through all the platforms we should build
    # this locale for, and delete them from the list of all known locales.
    #
    # If we should build it for all the loclaes, then this hash should be
    # empty after this process. If it's not, those are the platforms we would
    # __NOT__ build this locale for. But, that doesn't matter; what we're 
    # interested in is that it is such a locale, so we can create a list of
    # platforms we *do* build it for; such information is in the original
    # localeInfo hash, so this is a lot of work to make sure that we do
    # the right thing here.

    $releaseObj->{'exceptions'} = {};

    my %platformMap = GetBouncerToPatcherPlatformMap();
    foreach my $locale (keys(%{$localeInfo})) {
        my $allPlatformsHash = {};
        foreach my $platform (GetBouncerPlatforms()) {
            $allPlatformsHash->{$platform} = 1;
        }

        foreach my $localeSupportedPlatform (@{$localeInfo->{$locale}}) {
            die 'ASSERT: BumpPatcherConfig(): platform in locale, but not in' .
             ' all locales? Invalid platform?' if 
             (!exists($allPlatformsHash->{$localeSupportedPlatform}));

            delete $allPlatformsHash->{$localeSupportedPlatform};
        }
      
        my @supportedPatcherPlatforms = ();
        foreach my $platform (@{$localeInfo->{$locale}}) {
            push(@supportedPatcherPlatforms, $platformMap{$platform});
        }

        if (keys(%{$allPlatformsHash}) > 0) {
            $releaseObj->{'exceptions'}->{$locale} =
             join(', ', sort(@supportedPatcherPlatforms));
        }
    }

    $patcherConfigObj->save_file($checkedOutPatcherConfig);
}

sub Verify {
    my $this = shift;
    return;
}

1;
