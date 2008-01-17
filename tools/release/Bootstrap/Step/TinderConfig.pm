##
# TinderConfig - creates config file for Tinderbox
##

package Bootstrap::Step::TinderConfig;

use Bootstrap::Step;
use Bootstrap::Config;
use Bootstrap::Util qw(CvsCatfile CvsTag);

use MozBuild::TinderLogParse;
use MozBuild::Util qw(MkdirWithPath);

@ISA = ("Bootstrap::Step");

sub Execute {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $configBumpDir = $config->Get(var => 'configBumpDir');
    my $productTag = $config->Get(var => 'productTag');
    my $version = $config->GetVersion(longName => 0);
    my $rc = int($config->Get(var => 'rc'));
    my $mozillaCvsroot = $config->Get(var => 'mozillaCvsroot');
    my $product = $config->Get(var => 'product');
    my $logDir = $config->Get(sysvar => 'logDir');
    my $branchTag = $config->Get(var => 'branchTag');
    my $osname = $config->SystemInfo(var => 'osname');

    my $releaseTag = $productTag . '_RELEASE';

    my $productConfigBumpDir = catfile($configBumpDir, 
                                       "$product-$version-rc$rc");

    if (-e $productConfigBumpDir) {
        die "ASSERT: Step::TinderConfig::Execute(): $productConfigBumpDir " .
         'already exists?';
    }

    MkdirWithPath(dir => $productConfigBumpDir)
      or die("Cannot mkdir $productConfigBumpDir: $!");

    my @branches = ();
    # tinderbox-configs tags are different on branches vs trunk
    # Do the right thing in both cases
    if ($branchTag eq 'HEAD') {
        push(@branches, ('release', 'l10n_release'));
    }
    else {
        push(@branches, $branchTag . '_release');
        push(@branches, $branchTag . '_l10n_release');
    }

    foreach my $branch (@branches) {
        $this->CvsCo(cvsroot => $mozillaCvsroot,
                     checkoutDir => $branch,
                     tag => $branch,
                     modules => [CvsCatfile('mozilla', 'tools',
                                  'tinderbox-configs', $product, $osname)],
                     logFile => catfile($logDir,
                      'build_config-checkout-' . $branch . '.log'),
                     workDir => $productConfigBumpDir
        );
  
        my @bumpConfigFiles = qw(tinder-config.pl mozconfig);

        foreach my $configFile (@bumpConfigFiles) {
            $config->Bump( configFile => 
             catfile($productConfigBumpDir, $branch, $configFile));
            $this->Shell(
              cmd => 'cvs',
              cmdArgs => ['-d', $mozillaCvsroot, 
                          'ci', '-m', 
                          '"Automated configuration bump, release for ' 
                           . $product  . ' ' . $version . "rc$rc" . '"', 
                          $configFile],
              logFile => catfile($logDir, 
               'build_config-checkin-' . $configFile . '-' . 
                $branch . '.log'),
              dir => catfile($productConfigBumpDir, $branch)
            );
        }

        if ($version eq 'nightly') {
            $this->Log(msg => 'Skip TinderConfig tagging for nightly mode');
            return;
        }

        my @tagNames = ($productTag . '_RELEASE',
                        $productTag . '_RC' . $rc);

        foreach my $configTagName (@tagNames) {
            # XXX - Don't like doing this this way (specifically, the logic 
            # change depending on the name of the branch in this loop...) 
            #
            # Also, the force argument to CvsTag() is interesting; we only
            # want to cvs tag -F a whatever_RELEASE tag if we're not tagging
            # the first RC; so, the logic is (rc > 1 && we're doing a _RELEASE
            # tag; also, we have to surround it in int(); otherwise, if it's
            # false, we get the empty string, which is undef which is bad.
            $configTagName .= '_l10n' if ($branch =~ /l10n/);

            my $rv = CvsTag(tagName => $configTagName,
                            force => int($rc > 1 && 
                                         $configTagName =~ /_RELEASE/),
                            files => \@bumpConfigFiles,
                            cvsDir => catfile($productConfigBumpDir,
                                              $branch),
                            logFile => catfile($logDir, 'build_config-tag-' . 
                                               $branch . '.log'),
                            output => 1
                           );

            if ($rv->{'timedOut'} || ($rv->{'exitValue'} != 0)) {
                $this->Log(msg => "CvsTag() in TinderConfig() failed; " .
                "tag: $configTagName, rv: $rv->{'exitValue'}, " .
                "timeout: $rv->{'timedOut'}, output: $rv->{'output'}");
                die("Bootstrap::Step::TinderConfig tag failed: "
                 . $rv->{'exitValue'});
            }
        }
    }
}

sub Verify {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $branchTag = $config->Get(var => 'branchTag');
    my $logDir = $config->Get(sysvar => 'logDir');
    
    my @branches = ();
    # tinderbox-configs tags are different on branches vs trunk
    # Do the right thing in both cases
    if ($branchTag eq 'HEAD') {
        push(@branches, ('release', 'l10n_release'));
    }
    else {
        push(@branches, $branchTag . '_release');
        push(@branches, $branchTag . '_l10n_release');
    }

    foreach my $branch (@branches) {
        $this->CheckLog(
            log => catfile($logDir, 
             'build_config-checkout-' . $branch . '.log'),
            notAllowed => 'fail',
        );

        $this->CheckLog(
            log => catfile($logDir, 
             'build_config-checkout-' . $branch . '.log'),
            notAllowed => 'aborted',
        );

        # In nightly mode we don't do any tagging, so there's nothing to verify
        if ($version eq 'nightly') {
            $this->Log(msg => 'Skipping tag verification for nightly mode.');
        } else {
            $this->CheckLog(
                log => catfile($logDir, 'build_config-tag-' . $branch . '.log'),
                checkFor => '^T',
            );
        }

        foreach my $configFile ('mozconfig', 'tinder-config.pl') {
            $this->CheckLog(
             log => catfile($logDir,
               'build_config-checkin-' . $configFile . '-' .  $branch . '.log'),
             notAllowed => 'fail',
            );
            $this->CheckLog(
             log => catfile($logDir,
               'build_config-checkin-' . $configFile . '-' .  $branch . '.log'),
             notAllowed => 'aborted',
            );
        }
    }
}
