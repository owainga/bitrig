/* $OpenBSD: myproposal.h,v 1.40 2014/04/30 19:07:48 naddy Exp $ */

/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef WITH_OPENSSL

#define KEX_SERVER_KEX		\
	"curve25519-sha256@libssh.org," \
	"ecdh-sha2-nistp256," \
	"ecdh-sha2-nistp384," \
	"ecdh-sha2-nistp521," \
	"diffie-hellman-group-exchange-sha256," \
	"diffie-hellman-group14-sha1" \

#define KEX_CLIENT_KEX KEX_SERVER_KEX "," \
	"diffie-hellman-group-exchange-sha1," \
	"diffie-hellman-group1-sha1"

#define	KEX_DEFAULT_PK_ALG	\
	"ecdsa-sha2-nistp256-cert-v01@openssh.com," \
	"ecdsa-sha2-nistp384-cert-v01@openssh.com," \
	"ecdsa-sha2-nistp521-cert-v01@openssh.com," \
	"ssh-ed25519-cert-v01@openssh.com," \
	"ssh-rsa-cert-v01@openssh.com," \
	"ssh-dss-cert-v01@openssh.com," \
	"ssh-rsa-cert-v00@openssh.com," \
	"ssh-dss-cert-v00@openssh.com," \
	"ecdsa-sha2-nistp256," \
	"ecdsa-sha2-nistp384," \
	"ecdsa-sha2-nistp521," \
	"ssh-ed25519," \
	"ssh-rsa," \
	"ssh-dss"

#define	KEX_SERVER_ENCRYPT \
	"aes128-ctr,aes192-ctr,aes256-ctr," \
	"aes128-gcm@openssh.com,aes256-gcm@openssh.com," \
	"chacha20-poly1305@openssh.com"

#define KEX_CLIENT_ENCRYPT KEX_SERVER_ENCRYPT "," \
	"arcfour256,arcfour128," \
	"aes128-cbc,3des-cbc,blowfish-cbc,cast128-cbc," \
	"aes192-cbc,aes256-cbc,arcfour,rijndael-cbc@lysator.liu.se"

#define	KEX_SERVER_MAC \
	"umac-64-etm@openssh.com," \
	"umac-128-etm@openssh.com," \
	"hmac-sha2-256-etm@openssh.com," \
	"hmac-sha2-512-etm@openssh.com," \
	"umac-64@openssh.com," \
	"umac-128@openssh.com," \
	"hmac-sha2-256," \
	"hmac-sha2-512" \

#define KEX_CLIENT_MAC KEX_SERVER_MAC "," \
	"hmac-md5-etm@openssh.com," \
	"hmac-sha1-etm@openssh.com," \
	"hmac-ripemd160-etm@openssh.com," \
	"hmac-sha1-96-etm@openssh.com," \
	"hmac-md5-96-etm@openssh.com," \
	"hmac-md5," \
	"hmac-sha1," \
	"hmac-ripemd160," \
	"hmac-ripemd160@openssh.com," \
	"hmac-sha1-96," \
	"hmac-md5-96"

#else

#define KEX_SERVER_KEX		\
	"curve25519-sha256@libssh.org"
#define	KEX_DEFAULT_PK_ALG	\
	"ssh-ed25519-cert-v01@openssh.com," \
	"ssh-ed25519"
#define	KEX_SERVER_ENCRYPT \
	"aes128-ctr,aes192-ctr,aes256-ctr," \
	"chacha20-poly1305@openssh.com"
#define	KEX_SERVER_MAC \
	"umac-64-etm@openssh.com," \
	"umac-128-etm@openssh.com," \
	"hmac-sha2-256-etm@openssh.com," \
	"hmac-sha2-512-etm@openssh.com," \
	"umac-64@openssh.com," \
	"umac-128@openssh.com," \
	"hmac-sha2-256," \
	"hmac-sha2-512"

#define KEX_CLIENT_KEX KEX_SERVER_KEX
#define	KEX_CLIENT_ENCRYPT KEX_SERVER_ENCRYPT
#define KEX_CLIENT_MAC KEX_SERVER_MAC "," \
	"hmac-sha1-etm@openssh.com," \
	"hmac-sha1"

#endif /* WITH_OPENSSL */

#define	KEX_DEFAULT_COMP	"none,zlib@openssh.com,zlib"
#define	KEX_DEFAULT_LANG	""

#define KEX_CLIENT \
	KEX_CLIENT_KEX, \
	KEX_DEFAULT_PK_ALG, \
	KEX_CLIENT_ENCRYPT, \
	KEX_CLIENT_ENCRYPT, \
	KEX_CLIENT_MAC, \
	KEX_CLIENT_MAC, \
	KEX_DEFAULT_COMP, \
	KEX_DEFAULT_COMP, \
	KEX_DEFAULT_LANG, \
	KEX_DEFAULT_LANG

#define KEX_SERVER \
	KEX_SERVER_KEX, \
	KEX_DEFAULT_PK_ALG, \
	KEX_SERVER_ENCRYPT, \
	KEX_SERVER_ENCRYPT, \
	KEX_SERVER_MAC, \
	KEX_SERVER_MAC, \
	KEX_DEFAULT_COMP, \
	KEX_DEFAULT_COMP, \
	KEX_DEFAULT_LANG, \
	KEX_DEFAULT_LANG
