/*
 * Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation
 *
 * Author: Nikos Mavroyanopoulos
 *
 * This file is part of GNUTLS.
 *
 * The GNUTLS library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#ifndef AUTH_SRP_H
# define AUTH_SRP_H

#include <gnutls_auth.h>


typedef int gnutls_srp_server_credentials_function(gnutls_session_t,
						   const char *username,
						   gnutls_datum_t * salt,
						   gnutls_datum_t *
						   verifier,
						   gnutls_datum_t *
						   generator,
						   gnutls_datum_t * prime);

typedef int gnutls_srp_client_credentials_function(gnutls_session_t,
						   unsigned int times,
						   char **username,
						   char **password);


typedef struct {
    char *username;
    char *password;
    gnutls_srp_client_credentials_function *get_function;
} srp_client_credentials_st;

#define gnutls_srp_client_credentials_t srp_client_credentials_st*

typedef struct {
    char *password_file;
    char *password_conf_file;
    /* callback function, instead of reading the
     * password files.
     */
    gnutls_srp_server_credentials_function *pwd_callback;
} srp_server_cred_st;

#define gnutls_srp_server_credentials_t srp_server_cred_st*

/* these structures should not use allocated data */
typedef struct srp_server_auth_info_st {
    char username[MAX_SRP_USERNAME];
} *srp_server_auth_info_t;

extern const gnutls_datum_t gnutls_srp_1024_group_prime;
extern const gnutls_datum_t gnutls_srp_1024_group_generator;
extern const gnutls_datum_t gnutls_srp_1536_group_prime;
extern const gnutls_datum_t gnutls_srp_1536_group_generator;
extern const gnutls_datum_t gnutls_srp_2048_group_prime;
extern const gnutls_datum_t gnutls_srp_2048_group_generator;


#ifdef ENABLE_SRP

int _gnutls_proc_srp_server_hello(gnutls_session_t state,
				  const opaque * data, size_t data_size);
int _gnutls_gen_srp_server_hello(gnutls_session_t state, opaque * data,
				 size_t data_size);

int _gnutls_gen_srp_server_kx(gnutls_session_t, opaque **);
int _gnutls_gen_srp_client_kx(gnutls_session_t, opaque **);

int _gnutls_proc_srp_server_kx(gnutls_session_t, opaque *, size_t);
int _gnutls_proc_srp_client_kx(gnutls_session_t, opaque *, size_t);

typedef struct srp_server_auth_info_st srp_server_auth_info_st;

#endif				/* ENABLE_SRP */

#endif
