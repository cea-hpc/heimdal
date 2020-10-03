/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"
#include "hdb_locl.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

/*! @mainpage Heimdal database backend library
 *
 * @section intro Introduction
 *
 * Heimdal libhdb library provides the backend support for Heimdal kdc
 * and kadmind. Its here where plugins for diffrent database engines
 * can be pluged in and extend support for here Heimdal get the
 * principal and policy data from.
 *
 * Example of Heimdal backend are:
 * - Berkeley DB 1.85
 * - Berkeley DB 3.0
 * - Berkeley DB 4.0
 * - New Berkeley DB
 * - LDAP
 *
 *
 * The project web page: http://www.h5l.org/
 *
 */

const int hdb_interface_version = HDB_INTERFACE_VERSION;

static struct hdb_method methods[] = {
    /* "db:" should be db3 if we have db3, or db1 if we have db1 */
#if HAVE_DB3
    { HDB_INTERFACE_VERSION, 1 /*is_file_based*/, 1 /*can_taste*/, NULL, NULL,
                                               "db:",	hdb_db3_create},
#elif HAVE_DB1
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "db:",	hdb_db1_create},
#endif
#if HAVE_DB1
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "db1:",	hdb_db1_create},
#endif
#if HAVE_DB3
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "db3:",	hdb_db3_create},
#endif
#if HAVE_DB1
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "mit-db:",	hdb_mitdb_create},
#endif
#if HAVE_LMDB
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "mdb:",	hdb_mdb_create},
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "lmdb:",	hdb_mdb_create},
#endif
#if HAVE_NDBM
    { HDB_INTERFACE_VERSION, 1, 0, NULL, NULL, "ndbm:",	hdb_ndbm_create},
#endif
#ifdef HAVE_SQLITE3
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "sqlite:", hdb_sqlite_create},
#endif
    /* The keytab interface can't use its hdb_open() method to "taste" a DB */
    { HDB_INTERFACE_VERSION, 1, 0, NULL, NULL, "keytab:",	hdb_keytab_create},
    /* The rest are not file-based */
#if defined(OPENLDAP) && !defined(OPENLDAP_MODULE)
    { HDB_INTERFACE_VERSION, 0, 0, NULL, NULL, "ldap:",	hdb_ldap_create},
    { HDB_INTERFACE_VERSION, 0, 0, NULL, NULL, "ldapi:",	hdb_ldapi_create},
#elif defined(OPENLDAP)
    { HDB_INTERFACE_VERSION, 0, 0, NULL, NULL, "ldap:",	NULL},
    { HDB_INTERFACE_VERSION, 0, 0, NULL, NULL, "ldapi:", NULL},
#endif
    { 0, 0, 0, NULL, NULL, NULL, NULL}
};

/**
 * Returns the Keys of `e' for `kvno', or NULL if not found.  The Keys will
 * remain valid provided that the entry is not mutated.
 *
 * @param context Context
 * @param e The HDB entry
 * @param kvno The kvno
 *
 * @return A pointer to the Keys for the requested kvno.
 */
const Keys *
hdb_kvno2keys(krb5_context context,
	      const hdb_entry *e,
	      krb5_kvno kvno)
{
    HDB_Ext_KeySet *hist_keys;
    HDB_extension *extp;
    size_t i;

    if (kvno == 0 || e->kvno == kvno)
	return &e->keys;

    extp = hdb_find_extension(e, choice_HDB_extension_data_hist_keys);
    if (extp == NULL)
	return 0;

    hist_keys = &extp->data.u.hist_keys;
    for (i = 0; i < hist_keys->len; i++) {
	if (hist_keys->val[i].kvno == kvno)
	    return &hist_keys->val[i].keys;
    }

    return NULL;
}

/* Based on remove_HDB_Ext_KeySet(), generated by the ASN.1 compiler */
static int
dequeue_HDB_Ext_KeySet(HDB_Ext_KeySet *data, unsigned int element, hdb_keyset *ks)
{
    if (element >= data->len) {
        ks->kvno = 0;
        ks->keys.len = 0;
        ks->keys.val = 0;
        ks->set_time = 0;
        return ASN1_OVERRUN;
    }
    *ks = data->val[element];
    data->len--;
    /* Swap instead of memmove()... changes the order of elements */
    if (element < data->len)
        data->val[element] = data->val[data->len];
    if (data->len == 0) {
        free(data->val);
        data->val = 0;
    }
    return 0;
}


/**
 * Removes from `e' and optionally outputs the keyset for the requested `kvno'.
 *
 * @param context Context
 * @param e The HDB entry
 * @param kvno The key version number
 * @param ks A pointer to a variable of type hdb_keyset (may be NULL)
 *
 * @return Zero on success, an error code otherwise.
 */
krb5_error_code
hdb_remove_keys(krb5_context context,
                hdb_entry *e,
                krb5_kvno kvno,
                hdb_keyset *ks)
{
    HDB_Ext_KeySet *hist_keys;
    HDB_extension *extp;
    size_t i;

    if (kvno == 0 || e->kvno == kvno) {
        if (ks) {
            KerberosTime t;

            (void) hdb_entry_get_pw_change_time(e, &t);
            if (t) {
                if ((ks->set_time = malloc(sizeof(*ks->set_time))) == NULL)
                    return krb5_enomem(context);
                *ks->set_time = t;
            }
            ks->kvno = e->kvno;
            ks->keys = e->keys;
            e->keys.len = 0;
            e->keys.val = NULL;
            e->kvno = 0;
        } else {
            free_Keys(&e->keys);
        }
        return 0;
    }

    if (ks) {
        ks->kvno = 0;
        ks->keys.len = 0;
        ks->keys.val = 0;
        ks->set_time = 0;
    }

    extp = hdb_find_extension(e, choice_HDB_extension_data_hist_keys);
    if (extp == NULL)
	return 0;

    hist_keys = &extp->data.u.hist_keys;
    for (i = 0; i < hist_keys->len; i++) {
	if (hist_keys->val[i].kvno != kvno)
            continue;
        if (ks)
            return dequeue_HDB_Ext_KeySet(hist_keys, i, ks);
        return remove_HDB_Ext_KeySet(hist_keys, i);
    }
    return HDB_ERR_NOENTRY;
}

/**
 * Removes from `e' and outputs all the base keys for virtual principal and/or
 * key derivation.
 *
 * @param context Context
 * @param e The HDB entry
 * @param ks A pointer to a variable of type HDB_Ext_KeySet
 *
 * @return Zero on success, an error code otherwise.
 */
krb5_error_code
hdb_remove_base_keys(krb5_context context,
                     hdb_entry *e,
                     HDB_Ext_KeySet *base_keys)
{
    krb5_error_code ret;
    const HDB_Ext_KeyRotation *ckr;
    HDB_Ext_KeyRotation kr;
    size_t i, k;

    ret = hdb_entry_get_key_rotation(context, e, &ckr);
    if (ret == 0) {
        /*
         * Changing the entry's extensions invalidates extensions obtained
         * before the change.
         */
        ret = copy_HDB_Ext_KeyRotation(ckr, &kr);
        ckr = NULL;
    }
    base_keys->len = 0;
    if (ret == 0 &&
        (base_keys->val = calloc(kr.len, sizeof(base_keys->val[0]))) == NULL)
        ret = krb5_enomem(context);

    for (k = i = 0; ret == 0 && i < kr.len; i++) {
        const KeyRotation *krp = &kr.val[i];

        /*
         * WARNING: O(N * M) where M is number of keysets and N is the number
         *          of base keysets.
         *
         * In practice N will never be > 3 because the ASN.1 module imposes
         * that as a constraint, and M will generally be the same as N, so this
         * will be O(1) after all.
         */
        ret = hdb_remove_keys(context, e, krp->base_key_kvno,
                              &base_keys->val[k]);
        if (ret == 0)
            k++;
        else if (ret == HDB_ERR_NOENTRY)
            ret = 0;
    }
    if (ret == 0)
        base_keys->len = k;
    else
        free_HDB_Ext_KeySet(base_keys);
    free_HDB_Ext_KeyRotation(&kr);
    return 0;
}

/**
 * Removes from `e' and outputs all the base keys for virtual principal and/or
 * key derivation.
 *
 * @param context Context
 * @param e The HDB entry
 * @param is_current_keyset Whether to make the keys the current keys for `e'
 * @param ks A pointer to an hdb_keyset containing the keys to set
 *
 * @return Zero on success, an error code otherwise.
 */
krb5_error_code
hdb_install_keyset(krb5_context context,
                   hdb_entry *e,
                   int is_current_keyset,
                   const hdb_keyset *ks)
{
    krb5_error_code ret = 0;

    if (is_current_keyset) {
        if (e->keys.len &&
            (ret = hdb_add_current_keys_to_history(context, e)))
            return ret;
        free_Keys(&e->keys);
        if (ret == 0)
            ret = copy_Keys(&ks->keys, &e->keys);
        e->kvno = ks->kvno;
        if (ks->set_time)
            return hdb_entry_set_pw_change_time(context, e, *ks->set_time);
        return 0;
    }
    return hdb_add_history_keyset(context, e, ks);
}


krb5_error_code
hdb_next_enctype2key(krb5_context context,
		     const hdb_entry *e,
		     const Keys *keyset,
		     krb5_enctype enctype,
		     Key **key)
{
    const Keys *keys = keyset ? keyset : &e->keys;
    Key *k;

    for (k = *key ? (*key) + 1 : keys->val; k < keys->val + keys->len; k++) {
	if(k->key.keytype == enctype){
	    *key = k;
	    return 0;
	}
    }
    krb5_set_error_message(context, KRB5_PROG_ETYPE_NOSUPP,
			   "No next enctype %d for hdb-entry",
			  (int)enctype);
    return KRB5_PROG_ETYPE_NOSUPP; /* XXX */
}

krb5_error_code
hdb_enctype2key(krb5_context context,
		hdb_entry *e,
		const Keys *keyset,
		krb5_enctype enctype,
		Key **key)
{
    *key = NULL;
    return hdb_next_enctype2key(context, e, keyset, enctype, key);
}

void
hdb_free_key(Key *key)
{
    memset(key->key.keyvalue.data,
	   0,
	   key->key.keyvalue.length);
    free_Key(key);
    free(key);
}


krb5_error_code
hdb_lock(int fd, int operation)
{
    int i, code = 0;

    for(i = 0; i < 3; i++){
	code = flock(fd, (operation == HDB_RLOCK ? LOCK_SH : LOCK_EX) | LOCK_NB);
	if(code == 0 || errno != EWOULDBLOCK)
	    break;
	sleep(1);
    }
    if(code == 0)
	return 0;
    if(errno == EWOULDBLOCK)
	return HDB_ERR_DB_INUSE;
    return HDB_ERR_CANT_LOCK_DB;
}

krb5_error_code
hdb_unlock(int fd)
{
    int code;
    code = flock(fd, LOCK_UN);
    if(code)
	return 4711 /* XXX */;
    return 0;
}

void
hdb_free_entry(krb5_context context, hdb_entry_ex *ent)
{
    Key *k;
    size_t i;

    if (ent->free_entry)
	(*ent->free_entry)(context, ent);

    for(i = 0; i < ent->entry.keys.len; i++) {
	k = &ent->entry.keys.val[i];

	memset (k->key.keyvalue.data, 0, k->key.keyvalue.length);
    }
    free_hdb_entry(&ent->entry);
}

krb5_error_code
hdb_foreach(krb5_context context,
	    HDB *db,
	    unsigned flags,
	    hdb_foreach_func_t func,
	    void *data)
{
    krb5_error_code ret;
    hdb_entry_ex entry;
    ret = db->hdb_firstkey(context, db, flags, &entry);
    if (ret == 0)
	krb5_clear_error_message(context);
    while(ret == 0){
	ret = (*func)(context, db, &entry, data);
	hdb_free_entry(context, &entry);
	if(ret == 0)
	    ret = db->hdb_nextkey(context, db, flags, &entry);
    }
    if(ret == HDB_ERR_NOENTRY)
	ret = 0;
    return ret;
}

krb5_error_code
hdb_check_db_format(krb5_context context, HDB *db)
{
    krb5_data tag;
    krb5_data version;
    krb5_error_code ret, ret2;
    unsigned ver;
    int foo;

    ret = db->hdb_lock(context, db, HDB_RLOCK);
    if (ret)
	return ret;

    tag.data = (void *)(intptr_t)HDB_DB_FORMAT_ENTRY;
    tag.length = strlen(tag.data);
    ret = (*db->hdb__get)(context, db, tag, &version);
    ret2 = db->hdb_unlock(context, db);
    if(ret)
	return ret;
    if (ret2)
	return ret2;
    foo = sscanf(version.data, "%u", &ver);
    krb5_data_free (&version);
    if (foo != 1)
	return HDB_ERR_BADVERSION;
    if(ver != HDB_DB_FORMAT)
	return HDB_ERR_BADVERSION;
    return 0;
}

krb5_error_code
hdb_init_db(krb5_context context, HDB *db)
{
    krb5_error_code ret, ret2;
    krb5_data tag;
    krb5_data version;
    char ver[32];

    ret = hdb_check_db_format(context, db);
    if(ret != HDB_ERR_NOENTRY)
	return ret;

    ret = db->hdb_lock(context, db, HDB_WLOCK);
    if (ret)
	return ret;

    tag.data = (void *)(intptr_t)HDB_DB_FORMAT_ENTRY;
    tag.length = strlen(tag.data);
    snprintf(ver, sizeof(ver), "%u", HDB_DB_FORMAT);
    version.data = ver;
    version.length = strlen(version.data) + 1; /* zero terminated */
    ret = (*db->hdb__put)(context, db, 0, tag, version);
    ret2 = db->hdb_unlock(context, db);
    if (ret) {
	if (ret2)
	    krb5_clear_error_message(context);
	return ret;
    }
    return ret2;
}

/*
 * `default_dbmethod' is the last resort default.
 *
 * In hdb_create() we may try all the `methods[]' until one succeeds or all
 * fail.
 */
#if defined(HAVE_LMDB)
static struct hdb_method default_dbmethod =
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "", hdb_mdb_create };
#elif defined(HAVE_DB3)
static struct hdb_method default_dbmethod =
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "", hdb_db3_create };
#elif defined(HAVE_DB1)
static struct hdb_method default_dbmethod =
    { HDB_INTERFACE_VERSION, 1, 1, NULL, NULL, "", hdb_db1_create };
#elif defined(HAVE_NDBM)
static struct hdb_method default_dbmethod =
    { HDB_INTERFACE_VERSION, 0, 1, NULL, NULL, "", hdb_ndbm_create };
#else
static struct hdb_method default_dbmethod =
    { 0, 0, 0, NULL, NULL, NULL, NULL};
#endif

static int
is_pathish(const char *s)
{
    if (s[0] == '/' ||
        strncmp(s, "./", sizeof("./") - 1) == 0 ||
        strncmp(s, "../", sizeof("../") - 1) == 0)
        return 1;
#ifdef WIN32
    if (s[0] == '\\' || (isalpha(s[0]) && s[0] == ':') ||
        strncmp(s, ".\\", sizeof(".\\") - 1) == 0 ||
        strncmp(s, "\\\\", sizeof("\\\\") - 1) == 0)
        return 1;
#endif
    return 0;
}

static const struct hdb_method *
has_method_prefix(const char *filename)
{
    const struct hdb_method *h;

    for (h = methods; h->prefix != NULL; ++h)
	if (strncmp(filename, h->prefix, strlen(h->prefix)) == 0)
	    return h;
    return NULL;
}

/*
 * find the relevant method for `filename', returning a pointer to the
 * rest in `rest'.
 * return NULL if there's no such method.
 */

static const struct hdb_method *
find_method(const char *filename, const char **rest)
{
    const struct hdb_method *h = has_method_prefix(filename);

    *rest = h ? filename + strlen(h->prefix) : filename;
    return h;
}

struct cb_s {
    const char *residual;
    const char *filename;
    const struct hdb_method *h;
};

static krb5_error_code KRB5_LIB_CALL
callback(krb5_context context, const void *plug, void *plugctx, void *userctx)
{
    const struct hdb_method *h = (const struct hdb_method *)plug;
    struct cb_s *cb_ctx = (struct cb_s *)userctx;

    if (strncmp(cb_ctx->filename, h->prefix, strlen(h->prefix)) == 0) {
	cb_ctx->residual = cb_ctx->filename + strlen(h->prefix) + 1;
	cb_ctx->h = h;
	return 0;
    }
   return KRB5_PLUGIN_NO_HANDLE;
}

static char *
make_sym(const char *prefix)
{
    char *s, *sym;

    errno = 0;
    if (prefix == NULL || prefix[0] == '\0')
        return NULL;
    if ((s = strdup(prefix)) == NULL)
        return NULL;
    if (strchr(s, ':') != NULL)
        *strchr(s, ':') = '\0';
    if (asprintf(&sym, "hdb_%s_interface", s) == -1)
        sym = NULL;
    free(s);
    return sym;
}

static const char *hdb_plugin_deps[] = { "hdb", "krb5", NULL };

krb5_error_code
hdb_list_builtin(krb5_context context, char **list)
{
    const struct hdb_method *h;
    size_t len = 0;
    char *buf = NULL;

    for (h = methods; h->prefix != NULL; ++h) {
	if (h->prefix[0] == '\0')
	    continue;
	len += strlen(h->prefix) + 2;
    }

    len += 1;
    buf = malloc(len);
    if (buf == NULL) {
	return krb5_enomem(context);
    }
    buf[0] = '\0';

    for (h = methods; h->prefix != NULL; ++h) {
        if (h->create == NULL) {
            struct cb_s cb_ctx;
            char *f;
	    struct heim_plugin_data hdb_plugin_data;

	    hdb_plugin_data.module = "krb5";
	    hdb_plugin_data.min_version = HDB_INTERFACE_VERSION;
	    hdb_plugin_data.deps = hdb_plugin_deps;
	    hdb_plugin_data.get_instance = hdb_get_instance;

            /* Try loading the plugin */
            if (asprintf(&f, "%sfoo", h->prefix) == -1)
                f = NULL;
            if ((hdb_plugin_data.name = make_sym(h->prefix)) == NULL) {
                free(buf);
                free(f);
                return krb5_enomem(context);
            }
            cb_ctx.filename = f;
            cb_ctx.residual = NULL;
            cb_ctx.h = NULL;
            (void)_krb5_plugin_run_f(context, &hdb_plugin_data, 0,
                                     &cb_ctx, callback);
            free(f);
            free(rk_UNCONST(hdb_plugin_data.name));
            if (cb_ctx.h == NULL || cb_ctx.h->create == NULL)
                continue;
        }
	if (h != methods)
	    strlcat(buf, ", ", len);
	strlcat(buf, h->prefix, len);
    }
    *list = buf;
    return 0;
}

krb5_error_code
_hdb_keytab2hdb_entry(krb5_context context,
		      const krb5_keytab_entry *ktentry,
		      hdb_entry_ex *entry)
{
    entry->entry.kvno = ktentry->vno;
    entry->entry.created_by.time = ktentry->timestamp;

    entry->entry.keys.val = calloc(1, sizeof(entry->entry.keys.val[0]));
    if (entry->entry.keys.val == NULL)
	return ENOMEM;
    entry->entry.keys.len = 1;

    entry->entry.keys.val[0].mkvno = NULL;
    entry->entry.keys.val[0].salt = NULL;

    return krb5_copy_keyblock_contents(context,
				       &ktentry->keyblock,
				       &entry->entry.keys.val[0].key);
}

static krb5_error_code
load_config(krb5_context context, HDB *db)
{
    db->enable_virtual_hostbased_princs =
        krb5_config_get_bool_default(context, NULL, FALSE, "hdb",
                                     "enable_virtual_hostbased_princs",
                                     NULL);
    db->virtual_hostbased_princ_ndots =
        krb5_config_get_int_default(context, NULL, 1, "hdb",
                                    "virtual_hostbased_princ_mindots",
                                    NULL);
    db->virtual_hostbased_princ_maxdots =
        krb5_config_get_int_default(context, NULL, 0, "hdb",
                                    "virtual_hostbased_princ_maxdots",
                                    NULL);
    db->new_service_key_delay =
        krb5_config_get_time_default(context, NULL, 0, "hdb",
                                     "new_service_key_delay", NULL);
    /*
     * XXX Needs freeing in the HDB backends because we don't have a
     * first-class hdb_close() :(
     */
    db->virtual_hostbased_princ_svcs =
      krb5_config_get_strings(context, NULL, "hdb",
                              "virtual_hostbased_princ_svcs", NULL);
    /* Check for ENOMEM */
    if (db->virtual_hostbased_princ_svcs == NULL
        && krb5_config_get_string(context, NULL, "hdb",
                                  "virtual_hostbased_princ_svcs", NULL)) {
        return krb5_enomem(context);
    }
    return 0;
}

/**
 * Create a handle for a Kerberos database
 *
 * Create a handle for a Kerberos database backend specified by a
 * filename.  Doesn't actually create or even open an HDB file(s);
 * you have to call the hdb_open() open method of the resulting HDB
 * to open the database, and you have to use O_CREAT to create it.
 *
 * If `filename' does not have a backend type prefix, all file-based
 * backends will be tried until one succeeds or all fail, and if the
 * HDB exists for some backend, that will be used.  A build-time
 * default backend type will be used if the `filename' does not exist.
 *
 * Note that the actual filename may have a suffix added, such as
 * ".db".  Also, for backends such as "ldap:" and "ldapi:" the
 * `filename' is more like a URI.
 *
 * @param [in] context Context
 * @param [out] db HDB handle output
 * @param [in] filename The name of the HDB
 *
 * @return Zero on success else a krb5 error code.
 */

krb5_error_code
hdb_create(krb5_context context, HDB **db, const char *filename)
{
    krb5_error_code ret;
    struct cb_s cb_ctx;

    *db = NULL;
    if (filename == NULL)
	filename = HDB_DEFAULT_DB;
    cb_ctx.h = find_method(filename, &cb_ctx.residual);
    cb_ctx.filename = filename;

    if (cb_ctx.h == NULL || cb_ctx.h->create == NULL) {
	struct heim_plugin_data hdb_plugin_data;

        /*
         * `filename' does not start with a known HDB backend prefix.
         *
         * Try plugins.
         */
	hdb_plugin_data.module = "krb5";
	hdb_plugin_data.min_version = HDB_INTERFACE_VERSION;
	hdb_plugin_data.deps = hdb_plugin_deps;
	hdb_plugin_data.get_instance = hdb_get_instance;

        if ((hdb_plugin_data.name = make_sym(filename)) == NULL)
            return krb5_enomem(context);

        (void)_krb5_plugin_run_f(context, &hdb_plugin_data, 0 /* flags */,
                                 &cb_ctx, callback);

        free(rk_UNCONST(hdb_plugin_data.name));
    }

    if (cb_ctx.h == NULL || cb_ctx.h->create == NULL) {
        int pathish = is_pathish(filename);
        /*
         * `filename' does not start with a known HDB backend prefix and it
         * wasn't handled by any plugin.
         *
         * If it's "filename-ish", try all builtin HDB backends that are
         * local-file-ish, but use hdb_open() to see if the HDB exists and stop
         * when a backend is found for which the HDB exists.
         */
        if (!pathish) {
            krb5_set_error_message(context, ret = ENOTSUP,
                                   "No database support for %s",
                                   cb_ctx.filename);
            return ret;
        }
        for (cb_ctx.h = methods; cb_ctx.h->prefix != NULL; cb_ctx.h++) {
            if (cb_ctx.h->is_file_based && !pathish)
                continue;
            if (!cb_ctx.h->can_taste)
                continue;
            /* Taste the file */
            ret = (*cb_ctx.h->create)(context, db, filename);
            if (ret == 0)
                ret = (*db)->hdb_open(context, *db, O_RDONLY, 0);
            if (ret == 0) {
                (void) (*db)->hdb_close(context, *db);
                break;
            }
            if (*db)
                (*db)->hdb_destroy(context, *db);
            *db = NULL;
        }
    }
    if (cb_ctx.h == NULL || cb_ctx.h->prefix == NULL)
        cb_ctx.h = &default_dbmethod;
    if (cb_ctx.h == NULL || cb_ctx.h->prefix == NULL) {
        krb5_set_error_message(context, ENOTSUP,
                               "Could not determine default DB backend for %s",
                               filename);
        return ENOTSUP;
    }
    if (!*db)
        ret = (*cb_ctx.h->create)(context, db, cb_ctx.residual);
    if (ret == 0 && *db)
        ret = load_config(context, *db);
    if (ret && *db) {
        (*db)->hdb_destroy(context, *db);
        *db = NULL;
    }
    return ret;
}

uintptr_t KRB5_CALLCONV
hdb_get_instance(const char *libname)
{
    static const char *instance = "libhdb";

    if (strcmp(libname, "hdb") == 0)
	return (uintptr_t)instance;
    else if (strcmp(libname, "krb5") == 0)
	return krb5_get_instance(libname);

    return 0;
}
