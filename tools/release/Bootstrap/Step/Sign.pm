#
# Sign step. Wait for signed builds to appear.
# 
package Bootstrap::Step::Sign;
use Bootstrap::Step;
use Bootstrap::Config;
@ISA = ("Bootstrap::Step");

use LWP::Simple;

sub Execute {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $product = $config->Get(var => 'product');
    my $version = $config->Get(var => 'version');
    my $rc = $config->Get(var => 'rc');
    my $stagingServer = $config->Get(var => 'stagingServer');

    my $logFile = 'win32_signing_rc' . $rc . '.log';
    my $url = 'http://' . $stagingServer . '/pub/mozilla.org/' . $product . 
     '/nightly/' .  $version . '-candidates/' . 'rc' . $rc . '/' . $logFile;

    $this->Log(msg => 'Looking for url ' . $url);

    while (! head($url)) {
        sleep(10);
    }

    $this->Log(msg => 'Found signing log');
}

sub Verify {}

sub Announce {
    my $this = shift;

    my $config = new Bootstrap::Config();
    my $product = $config->Get(var => 'product');
    my $version = $config->GetVersion(longName => 0);

    $this->SendAnnouncement(
      subject => "$product $version sign step finished",
      message => "$product $version win32 builds have been signed and copied to the candidates dir.",
    );
}

1;
