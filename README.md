# DIY-SDFS (DIY-Sensor Data File System)

A NAS Integrated File System for On-site IoT Data Storage

## About

We propose a Network Attached Storag (NAS) integrated file system called the “Sensor Data File System (SDFS)”, which has advantages of being on-site, low-cost, and highly scalable. 

We developed the SDFS using FUSE (libfuse library), which is an interface for implementing file systems in user-space. SDFS not only allows multiple NAS to be treated as a single file system but also has functions that facilitate the ease of the addition of storage.

<img src="https://github.com/okayu1230z/data_field/blob/master/png/virtual_file_system.png" alt="vfs" title="Virtual File System">

## Overview of DIY-SDFS


SDFS has the following five features for the efficient management of time-series IoT data.
1) Multiple NAS can be handled as one file system.
2) Users can add a new NAS simply by rewriting the configuration file.
3) When the remaining capacity of the NAS decreases, it is automatically saved on another NAS.
4) Files with similar dates are saved on the same NAS.
5) Users can perform normal operations on one of the integrated NAS.

<img src="https://github.com/okayu1230z/data_field/blob/master/png/diy-sdfs_arc.png" alt="diy-sdfs_arch" title="DIY-SDFS architecture">



## Supported Platforms

- Linux (Ubuntu 16.04, 18.04)

## Installation
### Installation libfuse

FUSE Library is required to run DIY-SDFS.
Please configure DIY-SDFS after installing the library.

```
FUSE (Filesystem in Userspace) is an interface for userspace programs to export a filesystem to the Linux kernel. The FUSE project consists of two components: the fuse kernel module (maintained in the regular kernel repositories) and the libfuse userspace library (maintained in this repository). libfuse provides the reference implementation for communicating with the FUSE kernel module.
```

If you can not use Meson and Ninja, please use the method of using autotools described below.

#### Use Meson and Ninja

+ Use Meson and Ninja to install libfuse libraries
    * libfuse 3.1.1
    * https://github.com/libfuse/libfuse/tree/fuse-3.1.1

```
# Installation dependent packages
$ sudo apt update
$ sudo apt install zip
$ sudo apt install pkg-config
$ sudo apt install libfuse-dev python3 python3-pip ninja-build
$ pip3 install --user meson

# Download libfuse
$ mkdir ~/fuse_sources; cd ~/fuse_sources
$ wget https://github.com/libfuse/libfuse/releases/download/fuse-3.1.1/fuse-3.1.1.tar.gz

# Installation libfuse
$ tar xzvf fuse-3.1.1.tar.gz
$ cd fuse-3.1.1/
$ mkdir build; cd build
$ meson ..
$ ninja
$ pip3 install --user pytest
$ sudo python3 -m pytest test/
$ sudo ninja install

# Add path for shared library (may vary depending on environment)
$ sudo ln -s /usr/local/lib/x86_64-linux-gnu/libfuse3.so.3 /usr/lib/x86_64-linux-gnu/libfuse3.so.3
$ sudo ln -s /usr/local/lib/x86_64-linux-gnu/libfuse3.so /usr/lib/x86_64-linux-gnu/libfuse3.so

# To use allow_other option when executing FUSE
$ sudo vim /etc/fuse.conf
# Add the following at the end of the file
-----fuse.conf-----
user_allow_other
-----fuse.conf-----

# If the permission of /etc/fuse.conf is 0640 and other users cannot read it, change it to 0644
$ sudo chmod 644 /etc/fuse.conf
```

#### Use autotools

* If Meson and Ninja cannot be used, do as follows.

```
$ sudo apt-get install zip pkg-config libfuse-dev python3 python3-pip
$ mkdir ~/fuse_sources; cd ~/fuse_sources
$ wget https://github.com/libfuse/libfuse/releases/download/fuse-3.1.1/fuse-3.1.1.tar.gz
$ tar xzvf fuse-3.1.1.tar.gz
$ cd fuse-3.1.1/
$ ./configure
$ make
$ pip3 install --user pytest
$ sudo python3 -m pytest test/
$ sudo make install

$ sudo ln -s /usr/local/lib/libfuse3.so.3 /usr/lib/x86_64-linux-gnu/libfuse3.so.3
$ sudo ln -s /usr/local/lib/libfuse3.so /usr/lib/x86_64-linux-gnu/libfuse3.so
```

## How to implement DIY-SDFS

It is necessary to compile the program, describe the environment settings and configuration files.

```
# DIY-SDFS directory

DIY-SDFS
├── compile.sh           # Compile script
├── config.sh.sample     # config.sh sample
├── diy-sdfs.conf.sample # diy-sdfs.conf sample
├── diy-sdfs.cpp         # DIY-SDFS source code
├── mount.sh             # DIY-SDFS mount script
├── README.md
├── umount.sh            # DIY-SDFS unmount script
└── util/                # FUSE utility directory

```

### Environmental setting

It is necessary to prepare environment setting file (config.sh) and setting file (gdtnfs.conf).

```
$ git clone https://github.com/watalabo/DIY-SDFS.git
$ cd DIY-SDFS
$ cp config.sh.sample config.sh
$ cp diy-sdfs.conf.sample diy-sdfs.conf
```

It is necessary to describe the environment setting file (config.sh).
Describe the following three points in this file with absolute paths.

* DIY-SDFS mount point (MNT_DIR)
    * ex.）`/mnt/sdfs`
* Log file location (LOG_FILE)
    * ex.）`/var/log/diy-sdfs.log`
* Configuration file location (CONFIG_FILE)
    * ex.）`/usr/local/bin/DIY-SDFS/diy-sdfs.conf`

```
$ vim config.sh
-----config.sh-----
MNT_DIR=/mnt/sdfs
LOG_FILE=/var/log/diy-sdfs.log
CONFIG_FILE=/usr/local/bin/DIY-SDFS/diy-sdfs.conf
-----config.sh-----
```

Describe the configuration file (gdtnfs.conf). See the paper for details of this file.

In this example, it is assumed that three NAS units are mounted on `/mnt/nas01`, `/mnt/nas02` and `/mnt/nas03`, respectively.

```
$ vim gdtnfs.conf
-----gdtnfs.conf-----
/*/2017 /mnt/nas01
/*/2018 /mnt/nas02
/*/2019 /mnt/nas03
-----gdtnfs.conf-----
```

### Compilation DIY-SDFS

A compile script is provided for compiling.
The compile script describes the following compile commands.

```
# compile
$ ./compile.sh diy-sdfs
```


### Execution DIY-SDFS

If there are no problems with the library or the configuration file, the SDFS will be mounted on the mount point and the multiple directories will appear as one directory.

```
# Execution
$ ./mount.sh
```

In the execution script, DIY-SDFS is executed with the following options.
* ./gdtnfs：DIY-SDFS executable

* option
    * -s：DIY-SDFS (FUSE program) runs in a single thread
    * -o auto_unmount：Unmount automatically when DIY-SDFS terminates (including abnormal termination)
    * -o allow_other：Users other than the SDFS execution user can use SDFS
        * `user_allow_other` needs to be described in `/etc/fuse.conf`
    * -o logfile=${LOG_FILE}：To specify log file
    * -o configfile=${CONFIG_FILE}：To sopecify a configuration file
* argument
    * ${MNT_DIR}：DIY-SDFS mount point



### How to stop DIY-SDFS

DIY-SDFS (FUSE program) is stopped (unmounted) using the `fusermount3` command instead of the` umount` command.
A stop script is prepared for stopping.

```
# Stop
./umount.sh
```

## How to use DIY-SDFS

In DIY-SDFS, a format for storing sensor data is standardized as
an DIY-SDFS path. Specifically, when DIY-SDFS is mounted on /sdfs, it is standardized in the following format.

```
/sdfs/[sensor type]/[year]/[month]/[day]/[name]
```

For example, if the acceleration sensor data on September
10th, 2019 is acc.csv, the DIY-SDFS path is following.

```
/sdfs/acc/2019/09/10/acc.csv
```

In SDFS, existing file management software such as cp, mv, and rsync can be used. They are used to manage the DIY-SDFS path.

Reading the configuration file of the path conversion mechanism is implemented as a thread and is executed periodically. 
The thread acquires the NAS mount point information described in the configuration file and checks the remaining capacity of each mounted NAS.
The configuration file is given as a pair of the path pattern of the directory on SDFS, and the mount point of the corresponding NAS.
The path pattern is described as an absolute path with the mount point as the root. Users can utilize wildcards.

SDFS is mounted on /sdfs, and three NAS are mounted as /mnt/nas01, /mnt/nas02, and /mnt/nas03. At this point, if the configuration file /etc/sdfs.conf contains the following, the files are saved in the order they were written until the capacity is exceeded.

```
/ /mnt/nas01
/ /mnt/nas02
/ /mnt/nas03
```

In SDFS, users can select the NAS to save according to the type, year, and month of the acquired sensor data. If the setting file is described as follows, sensor data from January to September 2017 will be saved in /mnt/nas01. In addition, sensor data from October to December 2017 and 2018 will be saved to /mnt/nas02, and sensor data for 2019 will be saved to /mnt/nas03.

```
/*/2017 /mnt/nas01
/*/2017/10 /mnt/nas02
/*/2017/11 /mnt/nas02
/*/2017/12 /mnt/nas02
/*/2018 /mnt/nas02
/*/2019 /mnt/nas03
```
