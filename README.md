Introducing the ASCAR QoS framework for Lustre.

This is the ASCAR client for Lustre. ASCAR, the Automated Contention
Management for High-Performance Storage Systems, is designed by the
Storage Systems Research Center (SSRC) in University of California,
Santa Cruz.

This client is based on Lustre 2.9.0 and follows the Lustre's license,
which is GPL version 2. For more information about ASCAR, please read
the project page at: TODO

Introduction
============

ASCAR is different than server-side schedulers like NRS because ASCAR
limits rate from the client.


The following installation instruction is based on the official guide:
[Walk-thru- Build Lustre MASTER on RHEL 6.4/CentOS 6.4 from Whamcloud
git](https://wiki.hpdd.intel.com/pages/viewpage.action?pageId=8126821)
(use this [archive](https://archive.fo/rfqVH) if the link stopped
working).  It has been tested with Lustre 2.4 on CentOS 6.7.

Requirements
============

ASCAR is based on Lustre, so make sure you have installed the kernel
and kernel-devel packages correctly and boot into the Lustre
kernel. The simplest way is to install the official 2.4 RPMs, which
can be found at
https://downloads.hpdd.intel.com/public/lustre/lustre-2.4.0/el6.

The following instructions are for reference only. You can skip them
if you already have Lustre up and running or if you prefer to use
other methods to install Lustre.

Download Lustre server RPMs:

    yum install -y w3m
    # download the server RPMs
    mkdir -p ~/lustre-rpm/server/RPMS; cd ~/lustre-rpm/server/RPMS
    w3m https://downloads.hpdd.intel.com/public/lustre/lustre-2.4.0/el6/server/RPMS/x86_64/ -dump | grep rpm | awk '{print $3}' | xargs -n 1 -I{} wget https://downloads.hpdd.intel.com/public/lustre/lustre-2.4.0/el6/server/RPMS/x86_64/{}
    # download the server SRPMs
    mkdir -p ~/lustre-rpm/server/SRPMS; cd ~/lustre-rpm/server/SRPMS
    w3m https://downloads.hpdd.intel.com/public/lustre/lustre-2.4.0/el6/server/SRPMS -dump | grep rpm | awk '{print $3}' | xargs -n 1 -I{} wget https://downloads.hpdd.intel.com/public/lustre/lustre-2.4.0/el6/server/SRPMS/{}


Install everything except for ZFS packages:

    cd ~/lustre-rpm/server/RPMS
    ls | grep -v zfs | xargs yum localinstall -y

Remove other non-Lustre kernels, reboot, then test it:

    /usr/lib64/lustre/tests/llmount.sh


Instructions for Building Lustre
================================

    yum -y groupinstall "Development Tools"
    yum -y install xmlto asciidoc elfutils-libelf-devel zlib-devel binutils-devel newt-devel python-devel hmaccalc perl-ExtUtils-Embed bison elfutils-devel audit-libs-devel

ASCAR only patches the client kernel modules so we can disable
everything else to accelerate the building:

    sh autogen.sh
    ./configure --disable-server --disable-snmp --disable-doc --disable-utils --disable-tests --disable-maintainer-mode --disable-liblustre
    make -j9
