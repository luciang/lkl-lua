#ifdef LKL_FILE_APIS

#include "wapr_fileops.h"

#define BUF_SIZE 4096


static apr_status_t dir_cleanup(void *thedir)
{
	wapr_dir_t *dir = thedir;
	apr_status_t rc;

	rc = lkl_sys_close(dir->fd);
	return rc;
}

apr_status_t wapr_dir_open(wapr_dir_t **new, const char *dirname,
                          apr_pool_t *pool)
{
	int dir = lkl_sys_open(dirname,O_RDONLY|O_DIRECTORY|O_LARGEFILE, 0);

	if (dir < 0)
		return APR_EINVAL;
	(*new) = (wapr_dir_t *) apr_palloc(pool, sizeof(wapr_dir_t));
	(*new)->pool = pool;
	(*new)->dirname = apr_pstrdup(pool, dirname);
	(*new)->fd = dir;
	(*new)->size = 0;
	(*new)->offset = 0;
	(*new)->entry = NULL;
	(*new)->data = (struct __kernel_dirent*) apr_pcalloc(pool,BUF_SIZE);
	apr_pool_cleanup_register((*new)->pool, *new, dir_cleanup,
                          apr_pool_cleanup_null);

	return APR_SUCCESS;
}

apr_status_t wapr_dir_close(wapr_dir_t *thedir)
{
	return apr_pool_cleanup_run(thedir->pool, thedir, dir_cleanup);
}


apr_status_t wapr_dir_make(const char *path, apr_fileperms_t perm,
                          apr_pool_t *pool)
{
	apr_status_t rc;
	mode_t mode = wapr_unix_perms2mode(perm);

	rc = lkl_sys_mkdir(path, mode);
	return -rc;
}

#define PATH_SEPARATOR '/'

/* Remove trailing separators that don't affect the meaning of PATH. */
static const char *path_canonicalize (const char *path, apr_pool_t *pool)
{
    /* At some point this could eliminate redundant components.  For
     * now, it just makes sure there is no trailing slash. */
    apr_size_t len = strlen (path);
    apr_size_t orig_len = len;
    
    while ((len > 0) && (path[len - 1] == PATH_SEPARATOR))
        len--;
    
    if (len != orig_len)
        return apr_pstrndup (pool, path, len);
    else
        return path;
}


/* Remove one component off the end of PATH. */
static char *path_remove_last_component (const char *path, apr_pool_t *pool)
{
    const char *newpath = path_canonicalize (path, pool);
    int i;
    
    for (i = (strlen(newpath) - 1); i >= 0; i--) {
        if (path[i] == PATH_SEPARATOR)
            break;
    }

    return apr_pstrndup (pool, path, (i < 0) ? 0 : i);
}

apr_status_t wapr_dir_make_recursive(const char *path, apr_fileperms_t perm,
                                           apr_pool_t *pool) 
{
    apr_status_t apr_err = 0;
    
    apr_err = wapr_dir_make (path, perm, pool); /* Try to make PATH right out */
    
    if (apr_err == EEXIST) /* It's OK if PATH exists */
        return APR_SUCCESS;
    
    if (apr_err == ENOENT) { /* Missing an intermediate dir */
        char *dir;
        
        dir = path_remove_last_component(path, pool);
        /* If there is no path left, give up. */
        if (dir[0] == '\0') {
            return apr_err;
        }

        apr_err = wapr_dir_make_recursive(dir, perm, pool);
        
        if (!apr_err) 
            apr_err = wapr_dir_make (path, perm, pool);
    }

    return apr_err;
}

apr_status_t wapr_dir_remove(const char *path, apr_pool_t *pool)
{
	apr_status_t rc;

	rc = lkl_sys_rmdir(path);
	return -rc;
}


struct __kernel_dirent * wapr_readdir(wapr_dir_t *thedir)
{
	struct __kernel_dirent * de;

	if(thedir->offset >= thedir->size)
	{
		/* We've emptied out our buffer.  Refill it.  */
		int bytes = lkl_sys_getdents(thedir->fd, thedir->data, BUF_SIZE);
		if(bytes <= 0)
			return NULL;
		thedir->size = bytes;
		thedir->offset = 0;
	}
	de = (struct __kernel_dirent*) ((char*) thedir->data+thedir->offset);
	thedir->offset += de->d_reclen;

	return de;
}

apr_status_t wapr_dir_read(apr_finfo_t * finfo, apr_int32_t wanted, wapr_dir_t * thedir)
{

	apr_status_t rc = 0;

    // We're about to call a non-thread-safe readdir()
	thedir->entry = wapr_readdir(thedir);
	if (NULL == thedir->entry)
		rc = APR_ENOENT;
	finfo->fname = NULL;

	if (rc)
	{
		finfo->valid = 0;
		return rc;
	}

	if (thedir->entry->d_ino && thedir->entry->d_ino != -1)
		wanted &= ~APR_FINFO_INODE;

	wanted &= ~APR_FINFO_NAME;

	if (wanted)
	{
		char fspec[APR_PATH_MAX];
		int off;
		apr_cpystrn(fspec, thedir->dirname, sizeof(fspec));
		off = strlen(fspec);
		if ((fspec[off - 1] != '/') && (off + 1 < sizeof(fspec)))
			fspec[off++] = '/';
		apr_cpystrn(fspec + off, thedir->entry->d_name, sizeof(fspec) - off);
		rc = wapr_stat(finfo, fspec, APR_FINFO_LINK | wanted, thedir->pool);
		// We passed a stack name that will disappear
		finfo->fname = NULL;
	}

	if (wanted && (APR_SUCCESS == rc || APR_INCOMPLETE == rc))
	{
		wanted &= ~finfo->valid;
	}
	else
	{
        // We don't bail because we fail to stat, when we are only -required-
	//	* to readdir... but the result will be APR_INCOMPLETE

		finfo->pool = thedir->pool;
		finfo->valid = 0;

		if (thedir->entry->d_ino && thedir->entry->d_ino != -1)
		{
			finfo->inode = thedir->entry->d_ino;
			finfo->valid |= APR_FINFO_INODE;
		}
	}

	finfo->name = apr_pstrdup(thedir->pool, thedir->entry->d_name);
	finfo->valid |= APR_FINFO_NAME;

	if (wanted)
		return APR_INCOMPLETE;

	return APR_SUCCESS;
}

#endif
