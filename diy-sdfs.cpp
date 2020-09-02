#define FUSE_USE_VERSION 31

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libgen.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <fnmatch.h>
#include <cstddef>
#include <pthread.h>
#include <unordered_map>
#include <chrono>
#include <sstream>
#include <unistd.h>


using namespace std;

unordered_map<string, unsigned long int> ump;
hash<string> hash_fn;
static unsigned int ncache_lifetime = 600;
static unsigned int interval_conf = 60;

struct gdtnfs_conf {
    char *mountpoint;
    char *logfile;
    char *configfile;
    int print_info;
    int foreground;
};

static struct gdtnfs_conf gdtnfs_conf;

#define GDTNFS_OPT(t, p, v) { t, offsetof(struct gdtnfs_conf, p), v }

static struct fuse_opt gdtnfs_opts[] = {
    GDTNFS_OPT("logfile=%s", logfile, 0),
    GDTNFS_OPT("configfile=%s", configfile, 0),
    GDTNFS_OPT("print_info", print_info, 1),
    GDTNFS_OPT("-d", foreground, 1),
    GDTNFS_OPT("debug", foreground, 1),
    GDTNFS_OPT("-f", foreground, 1),
    FUSE_OPT_KEY("-d", FUSE_OPT_KEY_KEEP),
    FUSE_OPT_KEY("debug", FUSE_OPT_KEY_KEEP),
    FUSE_OPT_KEY("-f", FUSE_OPT_KEY_KEEP),
    FUSE_OPT_END
};

struct pattern_t {
    string pattern;
    string path;
};


struct dir_t {
    string name;
    uintmax_t size;
    string mount_type;

    bool operator<(const dir_t& right) const {
        return size == right.size ? name < right.name : size > right.size;
    }
};


static FILE *logfp;
static const char *configfile;
static vector<pattern_t> target_patterns;
static vector<dir_t> target_dirs;
static string rootdir;
static mode_t default_umask;

#define USE_LOCK 1
#if USE_LOCK
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#define PRINT_DEBUG 0
#if PRINT_DEBUG
#define PRINT(fmt, ...) print_msg(__FUNCTION__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define PRINT(fmt, ...)
#endif

#define PRINT_ERR(fmt, ...) print_msg(__FUNCTION__, __LINE__, fmt, ##__VA_ARGS__)


static void print_dirs(void);
static void read_config(void);

static void print_msg(const char *function, int line, const char *fmt, ...)
{
    if(gdtnfs_conf.logfile){
        FILE *fp = logfp;
        time_t timer;
        time(&timer);

        fprintf(fp, "%10ld::", timer);
        fprintf(fp, "%s l.%03d ", function, line);

        va_list ap;
        va_start(ap, fmt);
        vfprintf(fp, fmt, ap);
        va_end(ap);

        fprintf(fp, "\n");
    }
}

bool exist_ump(string file_pass)
{
    auto time_now = chrono::system_clock::now();
    auto time_ms = chrono::time_point_cast<chrono::milliseconds>(time_now);
    long time = time_ms.time_since_epoch().count();
    auto itr = ump.find(file_pass);
	if (itr != ump.end())
	{
		itr->second = time;
		return true;
	}
	else
	{
		return false;
	}
}

int add_ump(string file_pass)
{
  auto time_now = chrono::system_clock::now();
  auto time_ms = chrono::time_point_cast<chrono::milliseconds>(time_now);
  long time = time_ms.time_since_epoch().count();
  ump[file_pass] = time;
  return 0;
}

vector<string> split_path(const string path, char seq) {
  vector<string> vec_path;
  stringstream ss_path(path);
  string buffer;
  while(getline(ss_path, buffer, seq)) {
	vec_path.push_back(buffer);
  }
  return vec_path;
}


static int file_exist(const char * filename, const char * pre)
{
    struct stat buf;
    char path[PATH_MAX] = {0};
    
    strcpy(path, pre);
    strncat(path, filename, PATH_MAX);
	
    vector<string> vec_path = split_path(path, '/'); 
	
	string s = "";
    for(auto itr = vec_path.begin(); itr != vec_path.end(); ++itr)
	{
	    if(*itr != "") {
	        s = s + "/" + *itr;
	        if(exist_ump(s)) {
	    	   return 0;
	        }
		}
	}
	
	
	s = "";
	for(auto itr = vec_path.begin(); itr != vec_path.end(); ++itr)
	{
        if(*itr != "") {
   	        s = s + "/" + *itr;
			char s_path[PATH_MAX] = {0};
			s.copy(s_path, s.length());
			if (lstat(s_path, &buf) != 0) {
			    add_ump(s);
				return 0;
			}
		}
	}

	return 1;

}

void print(unordered_map<string, unsigned long int> tmp_ump)
{
  for (auto itr = tmp_ump.begin(); itr != tmp_ump.end(); ++itr) {
	  cout << "key = " << itr->first << ", val = " << itr->second << "\n";
  }
  cout << "" << endl;
}

static int gdtnfs_fullpath_process(char fpath[PATH_MAX], const char *path)
{  
    int len = 0;

#if USE_LOCK
    pthread_mutex_lock(&mutex);
#endif
    size_t size = target_dirs.size();
    for (unsigned int i = 0; i < size; i++) {
        string dir_name = target_dirs[i].name;
        const char *dir_name_c = dir_name.c_str();
        PRINT("%s %s", path, dir_name_c);
        if(file_exist(path, dir_name_c)){
            PRINT("%s %s", path, fpath);
            strcpy(fpath, dir_name_c);
            strncat(fpath, path, PATH_MAX - strlen(fpath) + 1);
            len = strlen(fpath);
            break;
        }
    }
#if USE_LOCK
    pthread_mutex_unlock(&mutex);
#endif

    PRINT("%s -> %s", path, fpath);
    
    return len;
}


static int mkdir_parents_process(const char *path, mode_t mode)
{
    struct stat sb = {0};
    mode_t mode_mkdir = mode & ~default_umask;
    int ret = 0;

    ret = stat(path, &sb);
    if(ret == 0){
        if(!S_ISDIR(sb.st_mode)){
            PRINT_ERR("Error: Not a directory: %s", path);
            return -1;
        }
        return 0;
    }

    ret = mkdir(path, mode_mkdir);

    if(ret != 0){
        PRINT_ERR("Error: mkdir(%s) %s", path, strerror(errno));
        return -1;
    }

    return 0;
}


static int mkdir_parents(const char *path, mode_t mode)
{
    char buf[PATH_MAX] = {0};
    char *p = NULL;
    int ret = 0;

    strcpy(buf, path);

    for(p = strchr(buf + 1, '/'); p; p = strchr(p + 1, '/')){
        *p = '\0';
        ret = mkdir_parents_process(buf, mode);
        if(ret != 0){
            return -1;
        }
        *p = '/';
    }

    return 0;
}


static int check_fs_size(string path)
{
    const uintmax_t min_fs_size = 100UL * 1024 * 1024 * 1024;

    size_t size = target_dirs.size();
    for (unsigned int i = 0; i < size; i++) {
        if(path == target_dirs[i].name){
            if(target_dirs[i].size < min_fs_size){
                return -1;
            }else{
                return 0;
            }
        }
    }

    return -1;
}


static int check_pattern(char fpath[PATH_MAX], const char *path)
{
    int ret = -1;
    
#if USE_LOCK
    pthread_mutex_lock(&mutex);
#endif
    size_t size = target_patterns.size();
    for (unsigned int i = 0; i < size; i++) {
        const char *pattern = target_patterns[i].pattern.c_str();
        int matched = fnmatch(pattern, path, FNM_PATHNAME | FNM_LEADING_DIR);
        if(matched == 0){
            PRINT("OK_PATTERN: %s -> %s\n", pattern, path);
            if(check_fs_size(target_patterns[i].path) == 0){
                PRINT("OK_SIZE: %s -> %s\n", pattern, path);
                const char *target_path = target_patterns[i].path.c_str();
                strcpy(fpath, target_path);
                strncat(fpath, path, PATH_MAX - strlen(fpath) + 1);
                ret = 0;
            }

            break;
        }
    }
#if USE_LOCK
    pthread_mutex_unlock(&mutex);
#endif

    if(ret == -1){
        PRINT("NG: %s\n", path);
    }

    return ret;
}


static int search_path(char fpath[PATH_MAX], const char *path)
{
    char buf[PATH_MAX] = {0};
    char tmp[PATH_MAX] = {0};
    char *dir_name = NULL;
    int len = 0;

    if(check_pattern(fpath, path) == 0){
        len = strlen(fpath);
    }else{
#if USE_LOCK
        pthread_mutex_lock(&mutex);
#endif
        const char *dir_name_c = target_dirs[0].name.c_str();
        strcpy(fpath, dir_name_c);
#if USE_LOCK
        pthread_mutex_unlock(&mutex);
#endif
        strncat(fpath, path, PATH_MAX - strlen(fpath) + 1);
        len = strlen(fpath);
    }
    
    strcpy(tmp, fpath);
    dir_name = dirname(tmp);
    strcpy(buf, dir_name);
    strncat(buf, "/", PATH_MAX - strlen(buf) + 1);
    PRINT("buf: %s", buf);
    int ret = mkdir_parents(buf, 0777);
    if(ret != 0){
        PRINT_ERR("Error: mkdir_parents(%s) %s", buf, strerror(errno));
    }
    PRINT("buf: %d", ret);

    return len;
}


static void gdtnfs_fullpath(char fpath[PATH_MAX], const char *path, int flag)
{
    int ret = gdtnfs_fullpath_process(fpath, path);
    
    PRINT("%s -> %s", path, fpath);

    if((ret == 0) && (flag == 1)){
        search_path(fpath, path);
    }
    
    
    PRINT("%s -> %s", path, fpath);
}


static void *config_thread(void *ptr)
{
    int ret = pthread_detach(pthread_self());
    if(ret != 0){
        PRINT_ERR("Error: pthread_detach() %s\n", strerror(errno));
    }

    while(1){
        read_config();
		sleep(interval_conf);
    }

    return NULL;
}


static void start_config_thread(void)
{
    pthread_t th;
    int ret = pthread_create(&th, NULL, &config_thread, NULL);
    if(ret != 0){
        fprintf(stderr, "Error: pthread_create %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

bool delete_ump(string file_pass)
{
  auto itr = ump.find(file_pass);
  if (itr != ump.end())
	{
	  ump.erase(itr);
	  return true;
	}
  else
	{
	  return false;
	}
}


static void organize_ump(unsigned int memory_span)
{
    auto time_now = chrono::system_clock::now();
	auto time_ms = chrono::time_point_cast<chrono::milliseconds>(time_now);
	long time = time_ms.time_since_epoch().count();
	vector<string> vec_erase;

	for (auto itr = ump.begin(); itr != ump.end(); itr++)
	{
  	    if(time - itr->second > memory_span)
		{
		    vec_erase.push_back(itr->first);
		}
	}

	for(auto itr = vec_erase.begin(); itr != vec_erase.end(); ++itr)
	{
	  	PRINT(" ** delete ncache %s", *itr);
	    delete_ump(*itr);
	}
	
	return;
}


static void *ncache_thread(void *ptr)
{
    int ret = pthread_detach(pthread_self());
    if(ret != 0){
        PRINT_ERR("Error: pthread_detach() of ncache_thread %s\n", strerror(errno));
    }
	
	unsigned int *interval = &ncache_lifetime;
	unsigned int memory_span = (*interval) * 1000;

	PRINT(" ncache interval : %d", *interval);
	
	while(1) {
	    sleep(*interval);
		organize_ump(memory_span);
	}
}

static void start_ncache_thread(void)
{
    pthread_t ncache_th;
    unsigned int ncache_interval = ncache_lifetime;
	PRINT("ncache lifetime : %d", ncache_lifetime);
    int ret = pthread_create(&ncache_th, NULL, &ncache_thread, &ncache_interval);
    if(ret != 0) {
  	    fprintf(stderr, "Error: pthread_create %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void *gdtnfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void) conn;
    cfg->use_ino = 1;

    cfg->entry_timeout = 0;
    cfg->attr_timeout = 0;
    cfg->negative_timeout = 0;

    start_config_thread();
	start_ncache_thread();
	PRINT("start_ncache_thread");
	
    return NULL;
}


static int gdtnfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void) fi;
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    res = lstat(fpath, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_access(const char *path, int mask)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    res = access(fpath, mask);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_readlink(const char *path, char *buf, size_t size)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    res = readlink(fpath, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


static int find_vector(vector<string> vec, string str)
{
    size_t len = vec.size();
    for (size_t i = 0; i < len; i++){
        if(vec[i] == str){
            return 1;
        }
    }
    return 0;
}


static int gdtnfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi,
               enum fuse_readdir_flags flags)
{
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;
    (void) flags;
    char fpath[PATH_MAX] = {0};
    vector<string> pre_dirs;
    int flag = 0;
    int errsv = 0;

    PRINT("call %s", path);
    
#if USE_LOCK
    pthread_mutex_lock(&mutex);
#endif
    size_t size = target_dirs.size();
    for (unsigned int i = 0; i < size; i++) {
        memset(fpath, 0, sizeof(fpath));
        strcpy(fpath, target_dirs[i].name.c_str());
        strncat(fpath, path, PATH_MAX);
        
        PRINT("for %d: %s %s", i, path, fpath);
        
        errno = 0;
        dp = opendir(fpath);
        errsv = errno;
        if (dp != NULL) {
            flag = 1;
            PRINT("for %d: if_before errro:%s", i, strerror(errsv));

            while ((de = readdir(dp)) != NULL) {
                string dir_name;
                dir_name = string(de->d_name);
                if(find_vector(pre_dirs, dir_name) == 0){
                    pre_dirs.push_back(dir_name);
                    
                    struct stat st;
                    memset(&st, 0, sizeof(st));
                    st.st_ino = de->d_ino;
                    st.st_mode = de->d_type << 12;
                    PRINT("name: %s", de->d_name);
                    if (filler(buf, de->d_name, &st, 0, (fuse_fill_dir_flags)0)) {
                        break;
                    }
                }
            }
        }
        closedir(dp);
    }
#if USE_LOCK
    pthread_mutex_unlock(&mutex);
#endif
    
    if(flag != 1){
        return -errsv;
    }
    
    return 0;
    
}


static int gdtnfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 1);
	
    if (S_ISREG(mode)) {
        res = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(fpath, mode);
    else
        res = mknod(fpath, mode, rdev);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_mkdir(const char *path, mode_t mode)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);

	gdtnfs_fullpath(fpath, path, 1);
	res = mkdir(fpath, mode);
	
	string s_path = fpath;
    delete_ump(s_path);

	if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_unlink(const char *path)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    PRINT("unlink %s", fpath);
    res = unlink(fpath);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_rmdir(const char *path)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    res = rmdir(fpath);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_symlink(const char *from, const char *to)
{
    int res;
    char fto[PATH_MAX] = {0};
    
    PRINT("Before: %s %s", from, to);

    gdtnfs_fullpath(fto, to, 1);
    PRINT("After: %s %s", from, fto);
    res = symlink(from, fto);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_rename(const char *from, const char *to, unsigned int flags)
{
    int res;
    char ffrom[PATH_MAX] = {0};
    char fto[PATH_MAX] = {0};
    
    PRINT("call %s %s", from, to);
    PRINT("flags = %d", flags);
    gdtnfs_fullpath(ffrom, from, 0);
    gdtnfs_fullpath(fto, to, 1);

	string s_path = fto;
    delete_ump(s_path);

    if (flags)
        return -EINVAL;

    PRINT("rename(%s, %s)", ffrom, fto);
    res = rename(ffrom, fto);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_link(const char *from, const char *to)
{
    int res;
    char ffrom[PATH_MAX] = {0};
    char fto[PATH_MAX] = {0};
    
    PRINT("Before: %s %s", from, to);

    gdtnfs_fullpath(ffrom, from, 0);
    gdtnfs_fullpath(fto, to, 1);

    PRINT("After: %s %s", ffrom, fto);
    res = link(ffrom, fto);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_chmod(const char *path, mode_t mode,
             struct fuse_file_info *fi)
{
    (void) fi;
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    res = chmod(fpath, mode);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_chown(const char *path, uid_t uid, gid_t gid,
             struct fuse_file_info *fi)
{
    (void) fi;
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    res = lchown(fpath, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_truncate(const char *path, off_t size,
            struct fuse_file_info *fi)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);

    if (fi != NULL)
        res = ftruncate(fi->fh, size);
    else
        res = truncate(fpath, size);
    if (res == -1)
        return -errno;

    return 0;
}


#ifdef HAVE_UTIMENSAT
static int gdtnfs_utimens(const char *path, const struct timespec ts[2],
               struct fuse_file_info *fi)
{
    (void) fi;
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);

    /* don't use utime/utimes since they follow symlinks */
    res = utimensat(0, fpath, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
        return -errno;

    return 0;
}
#endif


static int gdtnfs_create(const char *path, mode_t mode,
              struct fuse_file_info *fi)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 1);

	string s_path = fpath;
    delete_ump(s_path);

	
    res = open(fpath, fi->flags, mode);

    if (res == -1)
        return -errno;

    fi->fh = res;
    return 0;
}


static int gdtnfs_open(const char *path, struct fuse_file_info *fi)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);

    res = open(fpath, fi->flags);
    if (res == -1)
        return -errno;

    fi->fh = res;
    return 0;
}


static int gdtnfs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    int fd;
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);

    if(fi == NULL)
        fd = open(fpath, O_RDONLY);
    else
        fd = fi->fh;
    
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    if(fi == NULL)
        close(fd);
    return res;
}


static int gdtnfs_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);

    (void) fi;
    if(fi == NULL)
        fd = open(fpath, O_WRONLY);
    else
        fd = fi->fh;
    
    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    if(fi == NULL)
        close(fd);
    return res;
}


static int gdtnfs_statfs(const char *path, struct statvfs *stbuf)
{
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);

    res = statvfs(fpath, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}


static int gdtnfs_release(const char *path, struct fuse_file_info *fi)
{
    //(void) path;
    char fpath[PATH_MAX] = {0};
    
    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    
    close(fi->fh);
    return 0;
}



static int gdtnfs_fsync(const char *path, int isdatasync,
             struct fuse_file_info *fi)
{
    char fpath[PATH_MAX] = {0};
    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    
    (void) isdatasync;
    (void) fi;
    return 0;
}


#ifdef HAVE_POSIX_FALLOCATE
static int gdtnfs_fallocate(const char *path, int mode,
            off_t offset, off_t length, struct fuse_file_info *fi)
{
    int fd;
    int res;
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);

    (void) fi;

    if (mode)
        return -EOPNOTSUPP;

    if(fi == NULL)
        fd = open(fpath, O_WRONLY);
    else
        fd = fi->fh;
    
    if (fd == -1)
        return -errno;

    res = -posix_fallocate(fd, offset, length);

    if(fi == NULL)
        close(fd);
    return res;
}
#endif


#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int gdtnfs_setxattr(const char *path, const char *name, const char *value,
            size_t size, int flags)
{
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    int res = lsetxattr(fpath, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}


static int gdtnfs_getxattr(const char *path, const char *name, char *value,
            size_t size)
{
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    int res = lgetxattr(fpath, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}


static int gdtnfs_listxattr(const char *path, char *list, size_t size)
{
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    int res = llistxattr(fpath, list, size);
    if (res == -1)
        return -errno;
    return res;
}


static int gdtnfs_removexattr(const char *path, const char *name)
{
    char fpath[PATH_MAX] = {0};

    PRINT("call %s", path);
    gdtnfs_fullpath(fpath, path, 0);
    int res = lremovexattr(fpath, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */


// for C++
static struct fuse_operations gdtnfs_oper = {
    gdtnfs_getattr,
    gdtnfs_readlink,
    gdtnfs_mknod,
    gdtnfs_mkdir,
    gdtnfs_unlink,
    gdtnfs_rmdir,
    gdtnfs_symlink, //NULL, // gdtnfs_symlink, // symlink
    gdtnfs_rename,
    gdtnfs_link, //  NULL, // gdtnfs_link, // link
    gdtnfs_chmod,
    gdtnfs_chown,
    gdtnfs_truncate,
    gdtnfs_open,
    gdtnfs_read,
    gdtnfs_write,
    gdtnfs_statfs,
    NULL, // flush
    gdtnfs_release,
    gdtnfs_fsync,
#ifdef HAVE_SETXATTR
    gdtnfs_setxattr,
    gdtnfs_getxattr,
    gdtnfs_listxattr,
    gdtnfs_removexattr,
#else
    NULL, // setxattr
    NULL, // getxattr
    NULL, // listxattr
    NULL, // removexattr
#endif
    NULL, // opendir
    gdtnfs_readdir,
    NULL, // releasedir
    //gdtnfs_releasedir,
    NULL, // fsyncdir
    gdtnfs_init,
    NULL, // destroy
    gdtnfs_access,
    gdtnfs_create,
    NULL, // lock
#ifdef HAVE_UTIMENSAT
    gdtnfs_utimens,
#else
    NULL, // utimens
#endif
    NULL, // bmap
    NULL, // ioctl
    NULL, // poll
    NULL, // write_buf
    NULL, // read_buf
    NULL, // flock
#ifdef HAVE_POSIX_FALLOCATE
    gdtnfs_fallocate,
#else
    NULL, // fallocate
#endif
};


static int check_same_dir(string path)
{
    size_t size = target_dirs.size();
    for (size_t i = 0; i < size; i++){
        if(target_dirs[i].name == path){
            return -1;
        }
    }

    return 0;
}


static void print_dirs(void)
{
    printf("mountpoint: %s\n", rootdir.c_str());

#if USE_LOCK
    pthread_mutex_lock(&mutex);
#endif
    size_t size = target_patterns.size();
    printf("target_patterns: %zu\n", size);
    for (unsigned int i = 0; i < size; i++) {
        string pattern = target_patterns[i].pattern;
        string path = target_patterns[i].path;
        printf("    %s, %s\n", pattern.c_str(), path.c_str());
    }

    size = target_dirs.size();
    printf("target_dirs: %zu\n", size);
    for (unsigned int i = 0; i < size; i++) {
        string dir_name = target_dirs[i].name;
        uintmax_t disk_size = target_dirs[i].size;
        printf("    %s, %lu\n", dir_name.c_str(), disk_size);
    }
#if USE_LOCK
    pthread_mutex_unlock(&mutex);
#endif
}


static void read_config(void)
{
    FILE *configfp;
    char buf[PATH_MAX] = {0};
    char pattern[PATH_MAX] = {0};
    char path[PATH_MAX] = {0};

#if USE_LOCK
    pthread_mutex_lock(&mutex);
#endif

    target_patterns.clear();
    target_dirs.clear();

    configfp = fopen(configfile, "r");
    if(configfp == NULL){
        PRINT_ERR("Error: configfp %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    while(fgets(buf, PATH_MAX, configfp) != NULL){
        if(buf[0] == '\n' || buf[0] == '#'){
            continue;
        }
        PRINT("%s", buf);
        sscanf(buf, "%s%s", pattern, path);

        struct statvfs statvfs_buf = {0};

        int ret = statvfs(path, &statvfs_buf);
        if (ret != 0){
            PRINT_ERR("Error: statvfs() %s: %s", strerror(errno), path);
        }else{
            uintmax_t fs_size = statvfs_buf.f_frsize * statvfs_buf.f_bavail;
            
            pattern_t pattern_buf = {(string)pattern, (string)path};
            target_patterns.push_back(pattern_buf);
            
            if(check_same_dir(path) == 0){
                dir_t dir_buf = {(string)path, fs_size};
                target_dirs.push_back(dir_buf);
            } 
        }  
    }

    fclose(configfp);

    if(target_dirs.size() == 0){
        PRINT_ERR("Error: target_dirs.size(): 0");
        exit(EXIT_FAILURE);
    }

    sort(target_dirs.begin(), target_dirs.end());
#if USE_LOCK
    pthread_mutex_unlock(&mutex);
#endif

    if(gdtnfs_conf.print_info == 1){
        print_dirs();
    }
}


static int gdtnfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{

    switch (key) {
        case FUSE_OPT_KEY_NONOPT:
            gdtnfs_conf.mountpoint = realpath(arg, NULL);
            if (!gdtnfs_conf.mountpoint) {
                fprintf(stderr, "Error: bad mount point '%s': %s\n", arg, strerror(errno));
                return -1;
            }
            return 1;
    }

    return 1;
}

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    gdtnfs_conf.print_info = 0;
    if(fuse_opt_parse(&args, &gdtnfs_conf, gdtnfs_opts, gdtnfs_opt_proc) == -1){
        exit(EXIT_FAILURE);
    }

    if (!gdtnfs_conf.mountpoint) {
        fprintf(stderr, "Error: no mountpoint specified\n");
        exit(EXIT_FAILURE);
    }

    rootdir = string(gdtnfs_conf.mountpoint);

    if (gdtnfs_conf.logfile) {
        const char *logfile = gdtnfs_conf.logfile;
        fprintf(stderr, "logfile: %s\n", logfile);

        logfp = fopen(logfile, "a");
        if (logfp == NULL) {
            fprintf(stderr, "Error: logfp %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        setvbuf(logfp, NULL, _IOLBF, 0);
        PRINT_ERR("logfile: %s", logfile);
    }

    if (gdtnfs_conf.configfile) {
        configfile = gdtnfs_conf.configfile;
    }else {
        configfile = "/home/user/prog/fuse/okd/gdtnfs.conf";
    }

    PRINT_ERR("configfile: %s", configfile);
    read_config();
    print_dirs();

    default_umask =  umask(0);
    return fuse_main(args.argc, args.argv, &gdtnfs_oper, NULL);
}
