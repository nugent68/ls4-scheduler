#!/usr/bin/perl -w

# Test client for neat_yale.pl

use Env;
use Cwd;
use FileHandle;
use IO::Socket;
use IO::Select;

sub get_config_info {
    
    my $cfgfile = "$LS4_ROOT/questlib/quest_neat.cfg";
    my $neatcfg = new FileHandle "<  $cfgfile"
	or die "Could not open $cfgfile: $!";
    my $nmatch = 0;
    while(<$neatcfg>) {
	if(/^NEAT_COMMAND_PORT\s+(\S+)/) {
	    $neat_command_port = $1;
	    $nmatch++;
        } 
	if(/^NEAT_HOSTNAME\s+(\S+)/) {
	    $neat_hostname = $1;
	    $nmatch++;
        }
	if(/^TIMEOUT\s+(\S+)/) {
	    $timeout = $1/1000.;
	    $nmatch++;
        }
    }
    if (!defined($neat_command_port)) {
	printf STDERR "Couldn't find NEAT_COMMAND_PORT in $cfgfile\n";
    }
    if (!defined($neat_hostname)) {
	printf STDERR "Couldn't find NEAT_HOSTNAME in $cfgfile\n";
    }
    if (!defined($timeout)) {
	printf STDERR "Couldn't find TIMEOUT in $cfgfile\n";
    }
    $neatcfg->close or die "Couldn't close cfgfile:$!";
	
    if ($nmatch != 3) {
	die "Ports undefined";
    }
    return $nmatch;
}
  

sub connect_neat_command_socket {

    $comsock = new IO::Socket::INET (
                                 PeerAddr =>	$neat_hostname,
                                 PeerPort =>	$neat_command_port,
                                 Proto =>	'tcp',
                                );
    die "Could not create neat_command_socket: $!\n" unless $comsock;
    $tsel = new IO::Select();
    $tsel->add($comsock);

}    

sub sel_write {
    my ($sel) = @_;
    if (@s=$sel->can_write($timeout)) {
	chomp($_);
	$w=$s[0];
	printf $w "$_\0";
    } else {
	printf STDERR "timeout on socket write\n";
    }
}

sub sel_read {
    my ($sel) = @_;
    if (@s=$sel->can_read($timeout)) {
	$w=$s[0];
	return <$w>;
    } else {
	printf STDERR "timeout on socket read\n";
	return -2;
    }
}

get_config_info;
if ($#ARGV == 0) {
    $neat_hostname = $ARGV[0];
    printf STDERR "Using host $neat_hostname\n";
}


while(<STDIN>) {
connect_neat_command_socket;
    if ($_ =~ /^e/) {
	sel_write($tsel);
	$ret = sel_read($tsel);
	printf STDERR "client: expose:\n";
	printf STDERR "client: t1=$ret\n";
	$ret = sel_read($tsel);
	printf STDERR "client: expose:\n";
	printf STDERR "client: t2=$ret\n";
    } elsif ($_ =~ /^q/) {
	    printf STDERR "closing socket\n";
	    close($comsock);
	    exit(0);
    } else {
	sel_write($tsel);
	$ret = sel_read($tsel);
	printf STDERR "client: $_\n";
	if (/^x$/ || /^shutdown/) {
	    last;
	}
	if (defined($ret)) {
	    printf STDERR "client: ret=$ret\n";
	} else {
	    printf STDERR "client: ret is undefined\n";
	}
    }
close($comsock);
}
printf STDERR "closing socket\n";


