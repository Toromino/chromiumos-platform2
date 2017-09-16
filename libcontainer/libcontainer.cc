/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#if USE_device_mapper
#include <libdevmapper.h>
#endif
#include <linux/loop.h>
#include <malloc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <libminijail.h>

#include "libcontainer/container_cgroup.h"
#include "libcontainer/libcontainer.h"

#define FREE_AND_NULL(ptr) \
  do {                     \
    free(ptr);             \
    ptr = nullptr;         \
  } while (0)

#define MAX_NUM_SETFILES_ARGS 128
#define MAX_RLIMITS 32  // Linux defines 15 at the time of writing.

namespace {

constexpr base::FilePath::CharType kLoopdevCtlPath[] =
    FILE_PATH_LITERAL("/dev/loop-control");
#if USE_device_mapper
constexpr base::FilePath::CharType kDevMapperPath[] =
    FILE_PATH_LITERAL("/dev/mapper/");
#endif

// Returns the path for |path_in_container| in the outer namespace.
base::FilePath GetPathInOuterNamespace(
    const base::FilePath& root, const base::FilePath& path_in_container) {
  if (path_in_container.IsAbsolute())
    return base::FilePath(root.value() + path_in_container.value());
  return root.Append(path_in_container);
}

}  // namespace

static int container_teardown(struct container* c);

static int strdup_and_free(char** dest, const char* src) {
  char* copy = strdup(src);
  if (!copy)
    return -ENOMEM;
  if (*dest)
    free(*dest);
  *dest = copy;
  return 0;
}

struct Mount {
  std::string name;
  base::FilePath source;
  base::FilePath destination;
  std::string type;
  std::string data;
  std::string verity;
  int flags;
  int uid;
  int gid;
  int mode;

  // True if mount should happen in new vfs ns.
  bool mount_in_ns;

  // True if target should be created if it doesn't exist.
  bool create;

  // True if target should be mounted via loopback.
  bool loopback;
};

struct container_device {
  char type; /* 'c' or 'b' for char or block */
  char* path;
  int fs_permissions;
  int major;
  int minor;
  int copy_minor; /* Copy the minor from existing node, ignores |minor| */
  int uid;
  int gid;
};

struct container_cgroup_device {
  int allow;
  char type;
  int major; /* -1 means all */
  int minor; /* -1 means all */
  int read;
  int write;
  int modify;
};

struct container_cpu_cgroup {
  int shares;
  int quota;
  int period;
  int rt_runtime;
  int rt_period;
};

struct container_rlimit {
  int type;
  uint32_t cur;
  uint32_t max;
};

/*
 * Structure that configures how the container is run.
 *
 * config_root - Path to the root of the container itself.
 * rootfs - Path to the root of the container's filesystem.
 * rootfs_mount_flags - Flags that will be passed to mount() for the rootfs.
 * premounted_runfs - Path to where the container will be run.
 * pid_file_path - Path to the file where the pid should be written.
 * program_argv - The program to run and args, e.g. "/sbin/init".
 * num_args - Number of args in program_argv.
 * uid - The uid the container will run as.
 * uid_map - Mapping of UIDs in the container, e.g. "0 100000 1024"
 * gid - The gid the container will run as.
 * gid_map - Mapping of GIDs in the container, e.g. "0 100000 1024"
 * alt_syscall_table - Syscall table to use or nullptr if none.
 * mounts - Filesystems to mount in the new namespace.
 * devices - Device nodes to create.
 * num_devices - Number of above.
 * cgroup_devices - Device node cgroup permissions.
 * num_cgroup_devices - Number of above.
 * run_setfiles - Should run setfiles on mounts to enable selinux.
 * cpu_cgparams - CPU cgroup params.
 * cgroup_parent - Parent dir for cgroup creation
 * cgroup_owner - uid to own the created cgroups
 * cgroup_group - gid to own the created cgroups
 * share_host_netns - Enable sharing of the host network namespace.
 * keep_fds_open - Allow the child process to keep open FDs (for stdin/out/err).
 * rlimits - Array of rlimits for the contained process.
 * num_rlimits - The number of elements in `rlimits`.
 * securebits_skip_mask - The mask of securebits to skip when restricting caps.
 * do_init - Whether the container needs an extra process to be run as init.
 * selinux_context - The SELinux context name the container will run under.
 * pre_start_hook - A function pointer to be called prior to calling execve(2).
 * pre_start_hook_payload - Parameter that will be passed to pre_start_hook().
 */
struct container_config {
  char* config_root;
  char* rootfs;
  unsigned long rootfs_mount_flags;
  char* premounted_runfs;
  char* pid_file_path;
  char** program_argv;
  size_t num_args;
  uid_t uid;
  char* uid_map;
  gid_t gid;
  char* gid_map;
  char* alt_syscall_table;
  std::vector<Mount> mounts;
  struct container_device* devices;
  size_t num_devices;
  struct container_cgroup_device* cgroup_devices;
  size_t num_cgroup_devices;
  char* run_setfiles;
  struct container_cpu_cgroup cpu_cgparams;
  char* cgroup_parent;
  uid_t cgroup_owner;
  gid_t cgroup_group;
  int share_host_netns;
  int keep_fds_open;
  struct container_rlimit rlimits[MAX_RLIMITS];
  int num_rlimits;
  int use_capmask;
  int use_capmask_ambient;
  uint64_t capmask;
  uint64_t securebits_skip_mask;
  int do_init;
  char* selinux_context;
  minijail_hook_t pre_start_hook;
  void* pre_start_hook_payload;
  int* inherited_fds;
  size_t inherited_fd_count;
};

struct container_config* container_config_create() {
  return new (std::nothrow) struct container_config();
}

static void container_free_program_args(struct container_config* c) {
  unsigned int i;

  if (!c->program_argv)
    return;
  for (i = 0; i < c->num_args; ++i) {
    FREE_AND_NULL(c->program_argv[i]);
  }
  FREE_AND_NULL(c->program_argv);
}

static void container_config_free_device(struct container_device* device) {
  FREE_AND_NULL(device->path);
}

void container_config_destroy(struct container_config* c) {
  size_t i;

  if (c == nullptr)
    return;
  FREE_AND_NULL(c->rootfs);
  container_free_program_args(c);
  FREE_AND_NULL(c->premounted_runfs);
  FREE_AND_NULL(c->pid_file_path);
  FREE_AND_NULL(c->uid_map);
  FREE_AND_NULL(c->gid_map);
  FREE_AND_NULL(c->alt_syscall_table);
  for (i = 0; i < c->num_devices; ++i) {
    container_config_free_device(&c->devices[i]);
  }
  FREE_AND_NULL(c->devices);
  FREE_AND_NULL(c->cgroup_devices);
  FREE_AND_NULL(c->run_setfiles);
  FREE_AND_NULL(c->cgroup_parent);
  FREE_AND_NULL(c->selinux_context);
  FREE_AND_NULL(c->inherited_fds);
  delete c;
}

int container_config_config_root(struct container_config* c,
                                 const char* config_root) {
  return strdup_and_free(&c->config_root, config_root);
}

const char* container_config_get_config_root(const struct container_config* c) {
  return c->config_root;
}

int container_config_rootfs(struct container_config* c, const char* rootfs) {
  return strdup_and_free(&c->rootfs, rootfs);
}

const char* container_config_get_rootfs(const struct container_config* c) {
  return c->rootfs;
}

void container_config_rootfs_mount_flags(struct container_config* c,
                                         unsigned long rootfs_mount_flags) {
  /* Since we are going to add MS_REMOUNT anyways, add it here so we can
   * simply check against zero later. MS_BIND is also added to avoid
   * re-mounting the original filesystem, since the rootfs is always
   * bind-mounted.
   */
  c->rootfs_mount_flags = MS_REMOUNT | MS_BIND | rootfs_mount_flags;
}

unsigned long container_config_get_rootfs_mount_flags(
    const struct container_config* c) {
  return c->rootfs_mount_flags;
}

int container_config_premounted_runfs(struct container_config* c,
                                      const char* runfs) {
  return strdup_and_free(&c->premounted_runfs, runfs);
}

const char* container_config_get_premounted_runfs(
    const struct container_config* c) {
  return c->premounted_runfs;
}

int container_config_pid_file(struct container_config* c, const char* path) {
  return strdup_and_free(&c->pid_file_path, path);
}

const char* container_config_get_pid_file(const struct container_config* c) {
  return c->pid_file_path;
}

int container_config_program_argv(struct container_config* c,
                                  const char** argv,
                                  size_t num_args) {
  size_t i;

  container_free_program_args(c);
  c->num_args = num_args;
  c->program_argv =
      reinterpret_cast<char**>(calloc(num_args + 1, sizeof(char*)));
  if (!c->program_argv)
    return -ENOMEM;
  for (i = 0; i < num_args; ++i) {
    if (strdup_and_free(&c->program_argv[i], argv[i]))
      goto error_free_return;
  }
  c->program_argv[num_args] = nullptr;
  return 0;

error_free_return:
  container_free_program_args(c);
  return -ENOMEM;
}

size_t container_config_get_num_program_args(const struct container_config* c) {
  return c->num_args;
}

const char* container_config_get_program_arg(const struct container_config* c,
                                             size_t index) {
  if (index >= c->num_args)
    return nullptr;
  return c->program_argv[index];
}

void container_config_uid(struct container_config* c, uid_t uid) {
  c->uid = uid;
}

uid_t container_config_get_uid(const struct container_config* c) {
  return c->uid;
}

int container_config_uid_map(struct container_config* c, const char* uid_map) {
  return strdup_and_free(&c->uid_map, uid_map);
}

void container_config_gid(struct container_config* c, gid_t gid) {
  c->gid = gid;
}

gid_t container_config_get_gid(const struct container_config* c) {
  return c->gid;
}

int container_config_gid_map(struct container_config* c, const char* gid_map) {
  return strdup_and_free(&c->gid_map, gid_map);
}

int container_config_alt_syscall_table(struct container_config* c,
                                       const char* alt_syscall_table) {
  return strdup_and_free(&c->alt_syscall_table, alt_syscall_table);
}

int container_config_add_rlimit(struct container_config* c,
                                int type,
                                uint32_t cur,
                                uint32_t max) {
  if (c->num_rlimits >= MAX_RLIMITS) {
    return -ENOMEM;
  }
  c->rlimits[c->num_rlimits].type = type;
  c->rlimits[c->num_rlimits].cur = cur;
  c->rlimits[c->num_rlimits].max = max;
  c->num_rlimits++;
  return 0;
}

int container_config_add_mount(struct container_config* c,
                               const char* name,
                               const char* source,
                               const char* destination,
                               const char* type,
                               const char* data,
                               const char* verity,
                               int flags,
                               int uid,
                               int gid,
                               int mode,
                               int mount_in_ns,
                               int create,
                               int loopback) {
  if (name == nullptr || source == nullptr || destination == nullptr ||
      type == nullptr)
    return -EINVAL;

  c->mounts.emplace_back(Mount{name,
                               base::FilePath(source),
                               base::FilePath(destination),
                               type,
                               data ? data : "",
                               verity ? verity : "",
                               flags,
                               uid,
                               gid,
                               mode,
                               mount_in_ns != 0,
                               create != 0,
                               loopback != 0});

  return 0;
}

int container_config_add_cgroup_device(struct container_config* c,
                                       int allow,
                                       char type,
                                       int major,
                                       int minor,
                                       int read,
                                       int write,
                                       int modify) {
  struct container_cgroup_device* dev_ptr;
  struct container_cgroup_device* current_dev;

  dev_ptr = reinterpret_cast<struct container_cgroup_device*>(
      realloc(c->cgroup_devices,
              sizeof(c->cgroup_devices[0]) * (c->num_cgroup_devices + 1)));
  if (!dev_ptr)
    return -ENOMEM;
  c->cgroup_devices = dev_ptr;

  current_dev = &c->cgroup_devices[c->num_cgroup_devices];
  memset(current_dev, 0, sizeof(struct container_cgroup_device));
  current_dev->allow = allow;
  current_dev->type = type;
  current_dev->major = major;
  current_dev->minor = minor;
  current_dev->read = read;
  current_dev->write = write;
  current_dev->modify = modify;
  ++c->num_cgroup_devices;

  return 0;
}

int container_config_add_device(struct container_config* c,
                                char type,
                                const char* path,
                                int fs_permissions,
                                int major,
                                int minor,
                                int copy_minor,
                                int uid,
                                int gid,
                                int read_allowed,
                                int write_allowed,
                                int modify_allowed) {
  struct container_device* dev_ptr;
  struct container_device* current_dev;

  if (path == nullptr)
    return -EINVAL;
  /* If using a dynamic minor number, ensure that minor is -1. */
  if (copy_minor && (minor != -1))
    return -EINVAL;

  dev_ptr = reinterpret_cast<struct container_device*>(
      realloc(c->devices, sizeof(c->devices[0]) * (c->num_devices + 1)));
  if (!dev_ptr)
    return -ENOMEM;
  c->devices = dev_ptr;
  current_dev = &c->devices[c->num_devices];
  memset(current_dev, 0, sizeof(struct container_device));

  current_dev->type = type;
  if (strdup_and_free(&current_dev->path, path))
    goto error_free_return;
  current_dev->fs_permissions = fs_permissions;
  current_dev->major = major;
  current_dev->minor = minor;
  current_dev->copy_minor = copy_minor;
  current_dev->uid = uid;
  current_dev->gid = gid;
  if (read_allowed || write_allowed || modify_allowed) {
    if (container_config_add_cgroup_device(c,
                                           1,
                                           type,
                                           major,
                                           minor,
                                           read_allowed,
                                           write_allowed,
                                           modify_allowed))
      goto error_free_return;
  }
  ++c->num_devices;
  return 0;

error_free_return:
  container_config_free_device(current_dev);
  return -ENOMEM;
}

int container_config_run_setfiles(struct container_config* c,
                                  const char* setfiles_cmd) {
  return strdup_and_free(&c->run_setfiles, setfiles_cmd);
}

const char* container_config_get_run_setfiles(
    const struct container_config* c) {
  return c->run_setfiles;
}

int container_config_set_cpu_shares(struct container_config* c, int shares) {
  /* CPU shares must be 2 or higher. */
  if (shares < 2)
    return -EINVAL;

  c->cpu_cgparams.shares = shares;
  return 0;
}

int container_config_set_cpu_cfs_params(struct container_config* c,
                                        int quota,
                                        int period) {
  /*
   * quota could be set higher than period to utilize more than one CPU.
   * quota could also be set as -1 to indicate the cgroup does not adhere
   * to any CPU time restrictions.
   */
  if (quota <= 0 && quota != -1)
    return -EINVAL;
  if (period <= 0)
    return -EINVAL;

  c->cpu_cgparams.quota = quota;
  c->cpu_cgparams.period = period;
  return 0;
}

int container_config_set_cpu_rt_params(struct container_config* c,
                                       int rt_runtime,
                                       int rt_period) {
  /*
   * rt_runtime could be set as 0 to prevent the cgroup from using
   * realtime CPU.
   */
  if (rt_runtime < 0 || rt_runtime >= rt_period)
    return -EINVAL;

  c->cpu_cgparams.rt_runtime = rt_runtime;
  c->cpu_cgparams.rt_period = rt_period;
  return 0;
}

int container_config_get_cpu_shares(struct container_config* c) {
  return c->cpu_cgparams.shares;
}

int container_config_get_cpu_quota(struct container_config* c) {
  return c->cpu_cgparams.quota;
}

int container_config_get_cpu_period(struct container_config* c) {
  return c->cpu_cgparams.period;
}

int container_config_get_cpu_rt_runtime(struct container_config* c) {
  return c->cpu_cgparams.rt_runtime;
}

int container_config_get_cpu_rt_period(struct container_config* c) {
  return c->cpu_cgparams.rt_period;
}

int container_config_set_cgroup_parent(struct container_config* c,
                                       const char* parent,
                                       uid_t cgroup_owner,
                                       gid_t cgroup_group) {
  c->cgroup_owner = cgroup_owner;
  c->cgroup_group = cgroup_group;
  return strdup_and_free(&c->cgroup_parent, parent);
}

const char* container_config_get_cgroup_parent(struct container_config* c) {
  return c->cgroup_parent;
}

void container_config_share_host_netns(struct container_config* c) {
  c->share_host_netns = 1;
}

int get_container_config_share_host_netns(struct container_config* c) {
  return c->share_host_netns;
}

void container_config_keep_fds_open(struct container_config* c) {
  c->keep_fds_open = 1;
}

void container_config_set_capmask(struct container_config* c,
                                  uint64_t capmask,
                                  int ambient) {
  c->use_capmask = 1;
  c->capmask = capmask;
  c->use_capmask_ambient = ambient;
}

void container_config_set_securebits_skip_mask(struct container_config* c,
                                               uint64_t securebits_skip_mask) {
  c->securebits_skip_mask = securebits_skip_mask;
}

void container_config_set_run_as_init(struct container_config* c,
                                      int run_as_init) {
  c->do_init = !run_as_init;
}

int container_config_set_selinux_context(struct container_config* c,
                                         const char* context) {
  if (!context)
    return -EINVAL;
  c->selinux_context = strdup(context);
  if (c->selinux_context)
    return -ENOMEM;
  return 0;
}

void container_config_set_pre_execve_hook(struct container_config* c,
                                          int (*hook)(void*),
                                          void* payload) {
  c->pre_start_hook = hook;
  c->pre_start_hook_payload = payload;
}

int container_config_inherit_fds(struct container_config* c,
                                 int* inherited_fds,
                                 size_t inherited_fd_count) {
  if (c->inherited_fds)
    return -EINVAL;
  c->inherited_fds =
      reinterpret_cast<int*>(calloc(inherited_fd_count, sizeof(int)));
  if (!c->inherited_fds)
    return -ENOMEM;
  memcpy(c->inherited_fds, inherited_fds, inherited_fd_count * sizeof(int));
  c->inherited_fd_count = inherited_fd_count;
  return 0;
}

/*
 * Container manipulation
 */
struct container {
  struct container_cgroup* cgroup;
  struct minijail* jail;
  pid_t init_pid;
  base::FilePath config_root;
  base::FilePath runfs;
  char* rundir;
  base::FilePath runfsroot;
  char* pid_file_path;
  char** ext_mounts; /* Mounts made outside of the minijail */
  size_t num_ext_mounts;
  std::vector<base::FilePath> loopdev_paths;
  char** device_mappers;
  size_t num_device_mappers;
  char* name;
};

struct container* container_new(const char* name, const char* rundir) {
  struct container* c = new (std::nothrow) container();
  if (!c)
    return nullptr;
  c->rundir = strdup(rundir);
  c->name = strdup(name);
  if (!c->rundir || !c->name) {
    container_destroy(c);
    return nullptr;
  }
  return c;
}

void container_destroy(struct container* c) {
  if (c->cgroup)
    container_cgroup_destroy(c->cgroup);
  if (c->jail)
    minijail_destroy(c->jail);
  FREE_AND_NULL(c->name);
  FREE_AND_NULL(c->rundir);
  delete c;
}

/*
 * Given a uid/gid map of "inside1 outside1 length1, ...", and an id
 * inside of the user namespace, return the equivalent outside id, or
 * return < 0 on error.
 */
static int get_userns_outside_id(const char* map, int id) {
  char *map_copy, *mapping, *saveptr1, *saveptr2;
  long inside, outside, length;
  int result = 0;
  errno = 0;

  if (!map)
    return id;

  if (asprintf(&map_copy, "%s", map) < 0)
    return -ENOMEM;

  mapping = strtok_r(map_copy, ",", &saveptr1);
  while (mapping) {
    inside = strtol(strtok_r(mapping, " ", &saveptr2), nullptr, 10);
    outside = strtol(strtok_r(nullptr, " ", &saveptr2), nullptr, 10);
    length = strtol(strtok_r(nullptr, "\0", &saveptr2), nullptr, 10);
    if (errno) {
      goto error_free_return;
    } else if (inside < 0 || outside < 0 || length < 0) {
      errno = EINVAL;
      goto error_free_return;
    }

    if (id >= inside && id <= (inside + length)) {
      result = (id - inside) + outside;
      goto exit;
    }

    mapping = strtok_r(nullptr, ",", &saveptr1);
  }
  errno = EINVAL;

error_free_return:
  result = -errno;
exit:
  free(map_copy);
  return result;
}

static int make_dir(const char* path, int uid, int gid, int mode) {
  if (mkdir(path, mode))
    return -errno;
  if (chmod(path, mode))
    return -errno;
  if (chown(path, uid, gid))
    return -errno;
  return 0;
}

static int touch_file(const char* path, int uid, int gid, int mode) {
  int rc;
  int fd = open(path, O_RDWR | O_CREAT, mode);
  if (fd < 0)
    return -errno;
  rc = fchown(fd, uid, gid);
  close(fd);

  if (rc)
    return -errno;
  return 0;
}

/* Make sure the mount target exists in the new rootfs. Create if needed and
 * possible.
 */
static int setup_mount_destination(const struct container_config* config,
                                   const Mount* mnt,
                                   const char* source,
                                   const char* dest) {
  int uid_userns, gid_userns;
  int rc;
  struct stat st_buf;

  rc = stat(dest, &st_buf);
  if (rc == 0) /* destination exists */
    return 0;

  /* Try to create the destination. Either make directory or touch a file
   * depending on the source type.
   */
  uid_userns = get_userns_outside_id(config->uid_map, mnt->uid);
  if (uid_userns < 0)
    return uid_userns;
  gid_userns = get_userns_outside_id(config->gid_map, mnt->gid);
  if (gid_userns < 0)
    return gid_userns;

  rc = stat(source, &st_buf);
  if (rc || S_ISDIR(st_buf.st_mode) || S_ISBLK(st_buf.st_mode))
    return make_dir(dest, uid_userns, gid_userns, mnt->mode);

  return touch_file(dest, uid_userns, gid_userns, mnt->mode);
}

/* Fork and exec the setfiles command to configure the selinux policy. */
static int RunSetfilesCommand(const struct container* c,
                              const struct container_config* config,
                              const std::vector<base::FilePath>& destinations) {
  if (!config->run_setfiles)
    return 0;

  int pid = fork();
  if (pid == 0) {
    size_t arg_index = 0;
    const char* argv[MAX_NUM_SETFILES_ARGS];
    const char* env[] = {
        nullptr,
    };

    base::FilePath context_path = c->runfsroot.Append("file_contexts");

    argv[arg_index++] = config->run_setfiles;
    argv[arg_index++] = "-r";
    argv[arg_index++] = c->runfsroot.value().c_str();
    argv[arg_index++] = context_path.value().c_str();
    if (arg_index + destinations.size() >= MAX_NUM_SETFILES_ARGS)
      _exit(-E2BIG);
    for (const auto& destination : destinations)
      argv[arg_index++] = destination.value().c_str();
    argv[arg_index] = nullptr;

    execve(
        argv[0], const_cast<char* const*>(argv), const_cast<char* const*>(env));

    /* Command failed to exec if execve returns. */
    _exit(-errno);
  }
  if (pid < 0)
    return -errno;

  int rc;
  int status;
  do {
    rc = waitpid(pid, &status, 0);
  } while (rc == -1 && errno == EINTR);
  if (rc < 0)
    return -errno;
  return status;
}

/* Find a free loop device and attach it. */
static int LoopdevSetup(const base::FilePath& source,
                        base::FilePath* loopdev_path_out) {
  base::ScopedFD source_fd(open(source.value().c_str(), O_RDONLY | O_CLOEXEC));
  if (!source_fd.is_valid())
    return -errno;

  base::ScopedFD control_fd(
      open(kLoopdevCtlPath, O_RDWR | O_NOFOLLOW | O_CLOEXEC));
  if (!control_fd.is_valid())
    return -errno;

  while (true) {
    int num = ioctl(control_fd.get(), LOOP_CTL_GET_FREE);
    if (num < 0)
      return -errno;

    base::FilePath loopdev_path(base::StringPrintf("/dev/loop%i", num));
    base::ScopedFD loop_fd(
        open(loopdev_path.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (!loop_fd.is_valid())
      return -errno;

    if (ioctl(loop_fd.get(), LOOP_SET_FD, source_fd.get()) == 0) {
      *loopdev_path_out = loopdev_path;
      break;
    }

    if (errno != EBUSY)
      return -errno;
  }

  return 0;
}

/* Detach the specified loop device. */
static int LoopdevDetach(const base::FilePath& loopdev) {
  base::ScopedFD fd(
      open(loopdev.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  if (!fd.is_valid())
    return -errno;
  if (ioctl(fd.get(), LOOP_CLR_FD) < 0)
    return -errno;

  return 0;
}

/* Create a new device mapper target for the source. */
static int DeviceMapperSetup(const base::FilePath& source,
                             const std::string& verity_cmdline,
                             base::FilePath* dm_path_out,
                             std::string* dm_name_out) {
#if USE_device_mapper
  // Normalize the name into something unique-esque.
  std::string dm_name =
      base::StringPrintf("cros-containers-%s", source.value().c_str());
  base::ReplaceChars(dm_name, "/", "_", &dm_name);

  // Get the /dev path for the higher levels to mount.
  base::FilePath dm_path = base::FilePath(kDevMapperPath).Append(dm_name);

  // Insert the source path in the verity command line.
  std::string verity = verity_cmdline;
  base::ReplaceSubstringsAfterOffset(&verity, 0, "@DEV@", source.value());

  // Extract the first three parameters for dm-verity settings.
  char ttype[20];
  unsigned long long start, size;
  int n;
  if (sscanf(verity.c_str(), "%llu %llu %10s %n", &start, &size, ttype, &n) !=
      3) {
    return -errno;
  }

  /* Finally create the device mapper. */
  std::unique_ptr<struct dm_task, decltype(&dm_task_destroy)> dmt(
      dm_task_create(DM_DEVICE_CREATE), &dm_task_destroy);
  if (dmt == nullptr)
    return -errno;

  if (!dm_task_set_name(dmt.get(), dm_name.c_str()))
    return -errno;

  if (!dm_task_set_ro(dmt.get()))
    return -errno;

  if (!dm_task_add_target(dmt.get(), start, size, ttype, verity.c_str() + n))
    return -errno;

  uint32_t cookie = 0;
  if (!dm_task_set_cookie(dmt.get(), &cookie, 0))
    return -errno;

  if (!dm_task_run(dmt.get()))
    return -errno;

  /* Make sure the node exists before we continue. */
  dm_udev_wait(cookie);

  *dm_path_out = dm_path;
  *dm_name_out = dm_name;
#endif
  return 0;
}

/* Tear down the device mapper target. */
static int dm_detach(const char* dm_name) {
  int ret = 0;
#if USE_device_mapper
  struct dm_task* dmt;

  dmt = dm_task_create(DM_DEVICE_REMOVE);
  if (dmt == nullptr)
    goto error;

  if (!dm_task_set_name(dmt, dm_name))
    goto error;

  if (!dm_task_run(dmt))
    goto error;

  goto exit;

error:
  ret = -errno;
exit:
  dm_task_destroy(dmt);
#endif
  return ret;
}

/*
 * Unmounts anything we mounted in this mount namespace in the opposite order
 * that they were mounted.
 */
static int unmount_external_mounts(struct container* c) {
  int ret = 0;

  while (c->num_ext_mounts) {
    c->num_ext_mounts--;
    if (!c->ext_mounts[c->num_ext_mounts])
      continue;
    if (umount(c->ext_mounts[c->num_ext_mounts]))
      ret = -errno;
    FREE_AND_NULL(c->ext_mounts[c->num_ext_mounts]);
  }
  FREE_AND_NULL(c->ext_mounts);

  for (const auto& loopdev_path : c->loopdev_paths) {
    int rc = LoopdevDetach(loopdev_path);
    if (rc)
      ret = rc;
  }
  c->loopdev_paths.clear();

  while (c->num_device_mappers) {
    c->num_device_mappers--;
    if (dm_detach(c->device_mappers[c->num_device_mappers]))
      ret = -errno;
    FREE_AND_NULL(c->device_mappers[c->num_device_mappers]);
  }
  FREE_AND_NULL(c->device_mappers);

  return ret;
}

/*
 * Match mount_one in minijail, mount one mountpoint with
 * consideration for combination of MS_BIND/MS_RDONLY flag.
 */
static int mount_external(const char* src,
                          const char* dest,
                          const char* type,
                          unsigned long flags,
                          const void* data) {
  int remount_ro = 0;

  /*
   * R/O bind mounts have to be remounted since 'bind' and 'ro'
   * can't both be specified in the original bind mount.
   * Remount R/O after the initial mount.
   */
  if ((flags & MS_BIND) && (flags & MS_RDONLY)) {
    remount_ro = 1;
    flags &= ~MS_RDONLY;
  }

  if (mount(src, dest, type, flags, data) == -1)
    return -1;

  if (remount_ro) {
    flags |= MS_RDONLY;
    if (mount(src, dest, nullptr, flags | MS_REMOUNT, data) == -1)
      return -1;
  }

  return 0;
}

static int do_container_mount(struct container* c,
                              const struct container_config* config,
                              const Mount* mnt) {
  int rc = 0;

  base::FilePath dest = GetPathInOuterNamespace(c->runfsroot, mnt->destination);

  /*
   * If it's a bind mount relative to rootfs, append source to
   * rootfs path, otherwise source path is absolute.
   */
  base::FilePath source;
  if ((mnt->flags & MS_BIND) && !mnt->source.IsAbsolute()) {
    source = GetPathInOuterNamespace(c->runfsroot, mnt->source);
  } else if (mnt->loopback && !mnt->source.IsAbsolute() &&
             !c->config_root.empty()) {
    source = GetPathInOuterNamespace(c->config_root, mnt->source);
  } else {
    source = mnt->source;
  }

  // Only create the destinations for external mounts, minijail will take
  // care of those mounted in the new namespace.
  if (mnt->create && !mnt->mount_in_ns) {
    rc = setup_mount_destination(
        config, mnt, source.value().c_str(), dest.value().c_str());
    if (rc)
      return rc;
  }
  if (mnt->loopback) {
    /* Record this loopback file for cleanup later. */
    base::FilePath loop_source = source;
    rc = LoopdevSetup(loop_source, &source);
    if (rc)
      return rc;

    /* Save this to cleanup when shutting down. */
    c->loopdev_paths.push_back(source);
  }
  if (!mnt->verity.empty()) {
    /* Set this device up via dm-verity. */
    std::string dm_name;
    base::FilePath dm_source = source;
    rc = DeviceMapperSetup(dm_source, mnt->verity, &source, &dm_name);
    if (rc)
      return rc;

    /* Save this to cleanup when shutting down. */
    rc = strdup_and_free(&c->device_mappers[c->num_device_mappers],
                         dm_name.c_str());
    if (rc)
      return rc;
    c->num_device_mappers++;
  }
  if (mnt->mount_in_ns) {
    /* We can mount this with minijail. */
    rc = minijail_mount_with_data(
        c->jail,
        source.value().c_str(),
        mnt->destination.value().c_str(),
        mnt->type.c_str(),
        mnt->flags,
        mnt->data.empty() ? nullptr : mnt->data.c_str());
    if (rc)
      return rc;
  } else {
    /* Mount this externally and unmount it on exit. */
    if (mount_external(source.value().c_str(),
                       dest.value().c_str(),
                       mnt->type.c_str(),
                       mnt->flags,
                       mnt->data.empty() ? nullptr : mnt->data.c_str())) {
      return -errno;
    }
    /* Save this to unmount when shutting down. */
    rc = strdup_and_free(&c->ext_mounts[c->num_ext_mounts],
                         dest.value().c_str());
    if (rc)
      return rc;
    c->num_ext_mounts++;
  }

  return 0;
}

static int do_container_mounts(struct container* c,
                               const struct container_config* config) {
  unsigned int i;
  int rc = 0;

  unmount_external_mounts(c);
  /*
   * Allocate space to track anything we mount in our mount namespace.
   * This over-allocates as it has space for all mounts.
   */
  c->ext_mounts = reinterpret_cast<char**>(
      calloc(config->mounts.size(), sizeof(*c->ext_mounts)));
  if (!c->ext_mounts)
    return -errno;
  c->device_mappers = reinterpret_cast<char**>(
      calloc(config->mounts.size(), sizeof(*c->device_mappers)));
  if (!c->device_mappers)
    return -errno;

  for (i = 0; i < config->mounts.size(); ++i) {
    rc = do_container_mount(c, config, &config->mounts[i]);
    if (rc)
      goto error_free_return;
  }

  return 0;

error_free_return:
  unmount_external_mounts(c);
  return rc;
}

static int ContainerCreateDevice(const struct container* c,
                                 const struct container_config* config,
                                 const struct container_device* dev,
                                 int minor) {
  mode_t mode = dev->fs_permissions;
  switch (dev->type) {
    case 'b':
      mode |= S_IFBLK;
      break;
    case 'c':
      mode |= S_IFCHR;
      break;
    default:
      return -EINVAL;
  }

  int uid_userns = get_userns_outside_id(config->uid_map, dev->uid);
  if (uid_userns < 0)
    return uid_userns;
  int gid_userns = get_userns_outside_id(config->gid_map, dev->gid);
  if (gid_userns < 0)
    return gid_userns;

  base::FilePath path =
      GetPathInOuterNamespace(c->runfsroot, base::FilePath(dev->path));
  if (mknod(path.value().c_str(), mode, makedev(dev->major, minor)) &&
      errno != EEXIST) {
    return -errno;
  }
  if (chown(path.value().c_str(), uid_userns, gid_userns))
    return -errno;
  if (chmod(path.value().c_str(), dev->fs_permissions))
    return -errno;

  return 0;
}

static int mount_runfs(struct container* c,
                       const struct container_config* config) {
  static const mode_t root_dir_mode = 0660;
  const char* rootfs = config->rootfs;
  char* runfs_template = nullptr;
  int uid_userns, gid_userns;

  if (asprintf(&runfs_template, "%s/%s_XXXXXX", c->rundir, c->name) < 0)
    return -ENOMEM;

  char* runfs_path = mkdtemp(runfs_template);
  if (!runfs_path) {
    free(runfs_template);
    return -errno;
  }
  c->runfs = base::FilePath(runfs_path);
  free(runfs_template);

  uid_userns = get_userns_outside_id(config->uid_map, config->uid);
  if (uid_userns < 0)
    return uid_userns;
  gid_userns = get_userns_outside_id(config->gid_map, config->gid);
  if (gid_userns < 0)
    return gid_userns;

  /* Make sure the container uid can access the rootfs. */
  if (chmod(c->runfs.value().c_str(), 0700))
    return -errno;
  if (chown(c->runfs.value().c_str(), uid_userns, gid_userns))
    return -errno;

  c->runfsroot = c->runfs.Append("root");

  if (mkdir(c->runfsroot.value().c_str(), root_dir_mode))
    return -errno;
  if (chmod(c->runfsroot.value().c_str(), root_dir_mode))
    return -errno;

  if (mount(rootfs,
            c->runfsroot.value().c_str(),
            "",
            MS_BIND | (config->rootfs_mount_flags & MS_REC),
            nullptr)) {
    return -errno;
  }

  /* MS_BIND ignores any flags passed to it (except MS_REC). We need a
   * second call to mount() to actually set them.
   */
  if (config->rootfs_mount_flags &&
      mount(rootfs,
            c->runfsroot.value().c_str(),
            "",
            (config->rootfs_mount_flags & ~MS_REC),
            nullptr)) {
    return -errno;
  }

  return 0;
}

static int device_setup(struct container* c,
                        const struct container_config* config) {
  int rc;
  size_t i;

  c->cgroup->ops->deny_all_devices(c->cgroup);

  for (i = 0; i < config->num_cgroup_devices; i++) {
    const struct container_cgroup_device* dev = &config->cgroup_devices[i];
    rc = c->cgroup->ops->add_device(c->cgroup,
                                    dev->allow,
                                    dev->major,
                                    dev->minor,
                                    dev->read,
                                    dev->write,
                                    dev->modify,
                                    dev->type);
    if (rc)
      return rc;
  }

  for (i = 0; i < config->num_devices; i++) {
    const struct container_device* dev = &config->devices[i];
    int minor = dev->minor;

    if (dev->copy_minor) {
      struct stat st_buff;
      if (stat(dev->path, &st_buff) < 0)
        continue;
      minor = minor(st_buff.st_rdev);
    }
    if (minor >= 0) {
      rc = ContainerCreateDevice(c, config, dev, minor);
      if (rc)
        return rc;
    }
  }

  for (const auto& loopdev_path : c->loopdev_paths) {
    struct stat st;

    rc = stat(loopdev_path.value().c_str(), &st);
    if (rc < 0)
      return -errno;
    rc = c->cgroup->ops->add_device(
        c->cgroup, 1, major(st.st_rdev), minor(st.st_rdev), 1, 0, 0, 'b');
    if (rc)
      return rc;
  }

  return 0;
}

static int setexeccon(void* payload) {
  char* init_domain = reinterpret_cast<char*>(payload);
  char* exec_path;
  pid_t tid = syscall(SYS_gettid);
  int fd;
  int rc = 0;

  if (tid == -1) {
    return -errno;
  }

  if (asprintf(&exec_path, "/proc/self/task/%d/attr/exec", tid) < 0) {
    return -errno;
  }

  fd = open(exec_path, O_WRONLY | O_CLOEXEC);
  free(exec_path);
  if (fd == -1) {
    return -errno;
  }

  if (write(fd, init_domain, strlen(init_domain)) !=
      (ssize_t)strlen(init_domain)) {
    rc = -errno;
  }

  close(fd);
  return rc;
}

int container_start(struct container* c,
                    const struct container_config* config) {
  int rc = 0;

  if (!c)
    return -EINVAL;
  if (!config)
    return -EINVAL;
  if (!config->program_argv || !config->program_argv[0])
    return -EINVAL;

  // This will run in all the error cases.
  base::ScopedClosureRunner teardown(
      base::Bind(base::IgnoreResult(&container_teardown), base::Unretained(c)));

  if (config->config_root)
    c->config_root = base::FilePath(config->config_root);
  if (config->premounted_runfs) {
    c->runfs.clear();
    c->runfsroot = base::FilePath(config->premounted_runfs);
  } else {
    rc = mount_runfs(c, config);
    if (rc)
      return rc;
  }

  c->jail = minijail_new();
  if (!c->jail)
    return -ENOMEM;

  rc = do_container_mounts(c, config);
  if (rc)
    return rc;

  int cgroup_uid = get_userns_outside_id(config->uid_map, config->cgroup_owner);
  if (cgroup_uid < 0)
    return cgroup_uid;
  int cgroup_gid = get_userns_outside_id(config->gid_map, config->cgroup_group);
  if (cgroup_gid < 0)
    return cgroup_gid;

  c->cgroup = container_cgroup_new(
      c->name, "/sys/fs/cgroup", config->cgroup_parent, cgroup_uid, cgroup_gid);
  if (!c->cgroup)
    return -errno;

  /* Must be root to modify device cgroup or mknod */
  if (getuid() == 0) {
    rc = device_setup(c, config);
    if (rc)
      return rc;
  }

  /* Potentially run setfiles on mounts configured outside of the jail */
  const base::FilePath kDataPath("/data");
  const base::FilePath kCachePath("/cache");
  std::vector<base::FilePath> destinations;
  for (const auto& mnt : config->mounts) {
    if (mnt.mount_in_ns)
      continue;
    if (mnt.flags & MS_RDONLY)
      continue;

    /* A hack to avoid setfiles on /data and /cache. */
    if (mnt.destination == kDataPath || mnt.destination == kCachePath)
      continue;

    destinations.emplace_back(
        GetPathInOuterNamespace(c->runfsroot, mnt.destination));
  }
  if (!destinations.empty()) {
    rc = RunSetfilesCommand(c, config, destinations);
    if (rc)
      return rc;
  }

  /* Setup CPU cgroup params. */
  if (config->cpu_cgparams.shares) {
    rc = c->cgroup->ops->set_cpu_shares(c->cgroup, config->cpu_cgparams.shares);
    if (rc)
      return rc;
  }
  if (config->cpu_cgparams.period) {
    rc = c->cgroup->ops->set_cpu_quota(c->cgroup, config->cpu_cgparams.quota);
    if (rc)
      return rc;
    rc = c->cgroup->ops->set_cpu_period(c->cgroup, config->cpu_cgparams.period);
    if (rc)
      return rc;
  }
  if (config->cpu_cgparams.rt_period) {
    rc = c->cgroup->ops->set_cpu_rt_runtime(c->cgroup,
                                            config->cpu_cgparams.rt_runtime);
    if (rc)
      return rc;
    rc = c->cgroup->ops->set_cpu_rt_period(c->cgroup,
                                           config->cpu_cgparams.rt_period);
    if (rc)
      return rc;
  }

  /* Setup and start the container with libminijail. */
  if (config->pid_file_path) {
    c->pid_file_path = strdup(config->pid_file_path);
    if (!c->pid_file_path)
      return -ENOMEM;
  } else if (!c->runfs.empty()) {
    if (asprintf(&c->pid_file_path,
                 "%s/container.pid",
                 c->runfs.value().c_str()) < 0)
      return -ENOMEM;
  }

  if (c->pid_file_path)
    minijail_write_pid_file(c->jail, c->pid_file_path);
  minijail_reset_signal_mask(c->jail);

  /* Setup container namespaces. */
  minijail_namespace_ipc(c->jail);
  minijail_namespace_vfs(c->jail);
  if (!config->share_host_netns)
    minijail_namespace_net(c->jail);
  minijail_namespace_pids(c->jail);
  minijail_namespace_user(c->jail);
  if (getuid() != 0)
    minijail_namespace_user_disable_setgroups(c->jail);
  minijail_namespace_cgroups(c->jail);
  rc = minijail_uidmap(c->jail, config->uid_map);
  if (rc)
    return rc;
  rc = minijail_gidmap(c->jail, config->gid_map);
  if (rc)
    return rc;

  /* Set the UID/GID inside the container if not 0. */
  rc = get_userns_outside_id(config->uid_map, config->uid);
  if (rc < 0)
    return rc;
  else if (config->uid > 0)
    minijail_change_uid(c->jail, config->uid);
  rc = get_userns_outside_id(config->gid_map, config->gid);
  if (rc < 0)
    return rc;
  else if (config->gid > 0)
    minijail_change_gid(c->jail, config->gid);

  rc = minijail_enter_pivot_root(c->jail, c->runfsroot.value().c_str());
  if (rc)
    return rc;

  /* Add the cgroups configured above. */
  for (int i = 0; i < NUM_CGROUP_TYPES; i++) {
    if (c->cgroup->cgroup_tasks_paths[i]) {
      rc = minijail_add_to_cgroup(c->jail, c->cgroup->cgroup_tasks_paths[i]);
      if (rc)
        return rc;
    }
  }

  if (config->alt_syscall_table)
    minijail_use_alt_syscall(c->jail, config->alt_syscall_table);

  for (int i = 0; i < config->num_rlimits; i++) {
    const struct container_rlimit* lim = &config->rlimits[i];
    rc = minijail_rlimit(c->jail, lim->type, lim->cur, lim->max);
    if (rc)
      return rc;
  }

  if (config->selinux_context) {
    rc = minijail_add_hook(c->jail,
                           &setexeccon,
                           config->selinux_context,
                           MINIJAIL_HOOK_EVENT_PRE_EXECVE);
    if (rc)
      return rc;
  }

  if (config->pre_start_hook) {
    rc = minijail_add_hook(c->jail,
                           config->pre_start_hook,
                           config->pre_start_hook_payload,
                           MINIJAIL_HOOK_EVENT_PRE_EXECVE);
    if (rc)
      return rc;
  }

  for (int i = 0; i < config->inherited_fd_count; i++) {
    rc = minijail_preserve_fd(
        c->jail, config->inherited_fds[i], config->inherited_fds[i]);
    if (rc)
      return rc;
  }

  /* TODO(dgreid) - remove this once shared mounts are cleaned up. */
  minijail_skip_remount_private(c->jail);

  if (!config->keep_fds_open)
    minijail_close_open_fds(c->jail);

  if (config->use_capmask) {
    minijail_use_caps(c->jail, config->capmask);
    if (config->use_capmask_ambient) {
      minijail_set_ambient_caps(c->jail);
    }
    if (config->securebits_skip_mask) {
      minijail_skip_setting_securebits(c->jail, config->securebits_skip_mask);
    }
  }

  if (!config->do_init)
    minijail_run_as_init(c->jail);

  rc = minijail_run_pid_pipes_no_preload(c->jail,
                                         config->program_argv[0],
                                         config->program_argv,
                                         &c->init_pid,
                                         nullptr,
                                         nullptr,
                                         nullptr);
  if (rc)
    return rc;

  // The container has started successfully, no need to tear it down anymore.
  ignore_result(teardown.Release());
  return 0;
}

const char* container_root(struct container* c) {
  return c->runfs.value().c_str();
}

int container_pid(struct container* c) {
  return c->init_pid;
}

static int container_teardown(struct container* c) {
  int ret = 0;

  unmount_external_mounts(c);
  if (!c->runfsroot.empty() && !c->runfs.empty()) {
    /* |c->runfsroot| may have been mounted recursively. Thus use
     * MNT_DETACH to "immediately disconnect the filesystem and all
     * filesystems mounted below it from each other and from the
     * mount table". Otherwise one would need to unmount every
     * single dependent mount before unmounting |c->runfsroot|
     * itself.
     */
    if (umount2(c->runfsroot.value().c_str(), MNT_DETACH))
      ret = -errno;
    if (rmdir(c->runfsroot.value().c_str()))
      ret = -errno;
  }
  if (c->pid_file_path) {
    if (unlink(c->pid_file_path))
      ret = -errno;
    FREE_AND_NULL(c->pid_file_path);
  }
  if (c->runfs.empty()) {
    if (rmdir(c->runfs.value().c_str()))
      ret = -errno;
  }
  return ret;
}

int container_wait(struct container* c) {
  int rc;

  do {
    rc = minijail_wait(c->jail);
  } while (rc == -EINTR);

  // If the process had already been reaped, still perform teardown.
  if (rc == -ECHILD || rc >= 0) {
    rc = container_teardown(c);
  }
  return rc;
}

int container_kill(struct container* c) {
  if (kill(c->init_pid, SIGKILL) && errno != ESRCH)
    return -errno;
  return container_wait(c);
}
