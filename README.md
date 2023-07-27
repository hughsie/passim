# Passim

A local caching server. Named after the Latin word for “here, there and everywhere”.

## Introduction

Much of the software running on your computer that connects to other systems over the Internet needs
to periodically download *metadata* or information needed to perform other requests.

As part of running the fwupd/LVFS projects I've seen how *download this small file once per 24h*
turns into tens of millions of requests per day. Everybody downloads the same file from a CDN, and
although a CDN is not super-expensive, it's certainly not free. Everybody on your current network
(perhaps thousands of users) has to download the same 1MB blob of metadata from a CDN over a
perhaps-expensive internet link.

What if we could download the file from the CDN on one machine, and the next machine on the local
network that needs it instead downloads it from the first machine? We could put a limit on the
number of times it can be shared, and the maximum age so that we don't store yesterdays metadata
forever, and so that we don't turn a ThinkPad X220 into a machine distributing 1Gb/s to every other
machine in the office. We could cut the CDN traffic by at least one order of magnitude, but possibly
much more. This is better for the person paying the cloud bill, the person paying for the internet
connection, and the planet as a whole.

This is what `passim` tries to be. You add automatically or manually add files to the daemon which
stores them in `/var/lib/passim/data` with attributes set on each file for the `max-age` and
`share-limit`. When the file has been shared more than the share limit number of times, or is older
than the max age it is deleted and not advertised to other clients.

The daemon then advertises the availability of the file as a mDNS service subtype and provides a
tiny single-threaded HTTP v1.0 server that supplies the file over HTTP. Note, I said HTTP there --
not HTTPS -- i.e. so there's no SSL or encryption involved at all. Remember that.

The file is sent when requested from a URL like `http://192.168.1.1:27500/filename.xml.gz?sha256=hash`
-- any file requested without the checksum will not be supplied. Although this is a chicken-and-egg
problem where you don't know the payload checksum until you've checked the remote server, this is
easy solved using a tiny <100 byte request to the CDN for the payload checksum (or a .jcat file)
and then the multi-megabyte (or multi-gigabyte!) payload can be requested over mDNS.

## Sharing Considerations

Here we've assuming your local network (aka LAN) is a nice and friendly place, without evil people
trying to overwhelm your system or feed you fake files. Although we request files by their hash
(and thus can detect tampering) it still uses resources to send a file over the network.

We'll assume that any network with working mDNS (as implemented in Avahi) is good enough to get
metadata from other peers. If Avahi is not running, or mDNS is turned off on the firewall then
no files will be shared.

The cached index is shared with all users without any kind of authentication -- both over mDNS and
as a HTTP page on `http://localhost:27500/`.

So, **NEVER ADD FILES TO THE CACHE THAT YOU DO NOT WANT TO SHARE**. Even the filename may give more
clues than you wanted to provide, e.g. sharing `madison.json` might get you in trouble with a family
member. This can be more subtle; if you download a security update for a Lenovo P1 Gen 3 laptop
and share it with other laptops on your LAN -- it also tells the attacker your laptop model and also
that you're running a system firmware that isn't patched against the latest firmware bug.

My reccomendation here is only to advertise files that are common to all machines. For instance:

 * AdBlocker metadata
 * Firmware update metadata
 * Remote metadata for update frameworks, e.g. apt-get/dnf etc.

## Implementation Considerations

Any client **MUST** (and I'll go as far as to say it again, **MUST**) calculate the checksum of the
supplied file and verify that it matches. There is no authentication or signing verification done
so this step is non-optional. A malicious server could advertise the hash of `firmware.xml.gz` but
actually supply `evil-payload.exe` -- and you do not want that.

## Firewall Configuration

Port 27500 should be open by default, but if downloading files fails you can open the port using
firewalld:

    $ firewall-cmd --permanent --zone=public --add-port=27500/tcp

## Comparisons

The obvious comparison to make is IPFS. I'll try to make this as fair as possible, although I'm
obviously somewhat biased.

IPFS:

 * existing project that's existed for many years tested by many people
 * allows sharing with other users not on your local network
 * not packaged in any distributions and not trivial to install correctly
 * requires a significant time to find resources
 * does not prioritize local clients over remote clients
 * requires a internet<->IPFS "gateway" which cost $$$ for a large number of files

Passim:

 * new project that's not even finished
 * only allowed sharing with computers on your local network
 * returns results within 2s

One concern we had specifically with IPFS with firmware were ITAR/EAR legal considerations. e.g.
we can't share firmware containing strong encryption with users in some countries. From an ITAR/EAR
point of view Passim would be compliant (as it only shares locally) and IPFS would not be.

## Debugging

    $ curl -v http://localhost:27500/HELLO.md?sha256=22596363b3de40b06f981fb85d82312e8c0ed511 -L
    *   Trying 127.0.0.1:27500...
    * Connected to localhost (127.0.0.1) port 27500 (#0)
    > GET /HELLO.md?sha256=22596363b3de40b06f981fb85d82312e8c0ed511 HTTP/1.1
    > Host: localhost:27500
    > User-Agent: curl/8.0.1
    > Accept: */*
    >
    * HTTP 1.0, assume close after body
    < HTTP/1.0 303 See Other
    < Content-Length: 136
    < Location: http://192.168.122.39:27500/HELLO.md?sha256=22596363b3de40b06f981fb85d82312e8c0ed511
    <
    * Closing connection 0
    * Issue another request to this URL: 'http://192.168.122.39:27500/HELLO.md?sha256=22596363b3de40b06f981fb85d82312e8c0ed511'
    *   Trying 192.168.122.39:27500...
    * Connected to 192.168.122.39 (192.168.122.39) port 27500 (#1)
    > GET /HELLO.md?sha256=22596363b3de40b06f981fb85d82312e8c0ed511 HTTP/1.0
    > Host: 192.168.122.39:27500
    > User-Agent: curl/8.0.1
    > Accept: */*
    >
    * HTTP 1.0, assume close after body
    < HTTP/1.0 200 OK
    < Content-Length: 12
    <
    hello world
    * Closing connection 1

Using the CLI:

    $ passim dump
    7ea83bf1f9505f1e846cddef0d8ee49f6fd19361e2eb2f2e4f371b86382eacc2 HELLO.md (max-age: 86400, share-limit: 5)
    $ passim publish /var/lib/fwupd/metadata/lvfs/metadata.xml.xz 60 44
    7ea83bf1f9505f1e846cddef0d8ee49f6fd19361e2eb2f2e4f371b86382eacc2 HELLO.md (max-age: 86400, share-limit: 5)
    0157efe3cdab369a17b68facb187df1c559c91e2771c9094880ff2019ad84eaf metadata.xml.xz (max-age: 60, share-count: 0, share-limit: 44)

# TODO:

 - Use a file descriptor to add to the cache
 - Stop running as root
 - Self tests
 - Fix memory leaks
