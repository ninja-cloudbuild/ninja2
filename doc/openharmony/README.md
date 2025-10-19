# Compiling Harmony with Ninja2

## 一、本地编译

1.获取OpenHarmony源码

（1）基于docker编译

[harmony编译](https://gitee.com/cloudbuild888/cloudbuild/blob/master/doc/projects/openharmony.md)

（2）搭建环境编译

```sh
# 开发环境
Ubuntu18.04及以上版本，X86_64架构，内存推荐 16GB 及以上。
Ubuntu系统的用户名不能包含中文字符。

# 安装库和工具集
apt-get update
apt-get install binutils binutils-dev git git-lfs gnupg flex bison gperf build-essential zip curl zlib1g-dev gcc-multilib g++-multilib libc6-dev-i386 libc6-dev
apt-get install lib32ncurses5-dev x11proto-core-dev libx11-dev lib32z1-dev ccache libgl1-mesa-dev libxml2-utils xsltproc unzip m4 bc gnutls-bin python3.8 python3-pip ruby genext2fs device-tree-compiler make libffi-dev e2fsprogs pkg-config perl openssl libssl-dev libelf-dev libdwarf-dev u-boot-tools mtd-utils cpio doxygen liblz4-tool openjdk-8-jre gcc g++ texinfo dosfstools mtools default-jre default-jdk libncurses5 apt-utils wget scons python3.8-distutils tar rsync git-core libxml2-dev lib32z-dev grsync xxd libglib2.0-dev libpixman-1-dev kmod jfsutils reiserfsprogs xfsprogs squashfs-tools pcmciautils quota ppp libtinfo-dev libtinfo5 libncurses5-dev libncursesw5 libstdc++6 gcc-arm-none-eabi vim ssh locales libxinerama-dev libxcursor-dev libxrandr-dev libxi-dev

# Python 3.8设置为默认Python版本
apt install python3.8 -y
which python3.8
cd /usr/bin && rm python && ln -s /usr/bin/python3.8 python && python --version

# 安装编译工具
wget -c https://repo.huaweicloud.com/openharmony/os/4.1-Release/code-v4.1-Release.tar.gz
tar -zxvf code-v4.1-Release.tar.gz
cd cd OpenHarmony-v4.1-Release/OpenHarmony/
python3 -m pip install --user build/hb
vim ~/.bashrc
#将以下命令拷贝到.bashrc文件的最后一行，保存并退出。
#export PATH=~/.local/bin:$PATH
source ~/.bashrc

# 使用原ninja能编译成功
./build.sh --product-name rk3568 --ccache
```

2.编译ninja2

[ninja-cloudbuild/ninja2](https://github.com/ninja-cloudbuild/ninja2)

```sh
git clone https://github.com/ninja-cloudbuild/ninja2.git
cd ninja2
./build.sh build
```

3.使用ninja2编译OpenHarmony

```sh
cd /home/user/OpenHarmony-v4.1-Release/OpenHarmony/
sudo cp prebuilts/build-tools/linux-x86/bin/ninja prebuilts/build-tools/linux-x86/bin/ninja.prev
sudo mv /home/user/ninja2/build/ninja prebuilts/build-tools/linux-x86/bin/
# 开始编译
./build.sh --product-name rk3568 --ccache
```

## 二、分布式编译

`for openharmony-V4.1`

### 1.运行buildbuddy

1.1 clone

```sh
git clone https://github.com/buildbuddy-io/buildbuddy.git
# git@github.com:buildbuddy-io/buildbuddy.git
```

1.2 运行server

```sh
cd buildbuddy/
bazel build //enterprise/server:server
bazel run //enterprise/server:server
```

1.3 运行executor

```sh
cd buildbuddy/
bazel build //enterprise/server/cmd/executor:executor
bazel run //enterprise/server/cmd/executor:executor
```

### 2.挂载prebuilts

2.1 nfs server

```sh
# 1.file server
apt install nfs-kernel-server -y
# 确认能够读写
chmod 777 /prebuilts

# =========================== /etc/exports
# 添加
/........./OpenHarmony-v4.1-Release/OpenHarmony/prebuilts [clientIP]/24(rw,sync,no_root_squash,no_subtree_check)
# ip为允许挂载的客户端IP
# ===========================

# 2.生效并启动
exportfs -a
exportfs -rav
# Ubuntu/Debian
systemctl start nfs-kernel-server
systemctl enable nfs-kernel-server
systemctl status nfs-kernel-server

rpcinfo -p 127.0.0.1
# 防火墙
ufw allow 111/tcp
ufw allow 111/udp
ufw allow 2049/tcp
ufw allow 2049/udp

# 3.允许mountd服务
vim /etc/default/nfs-kernel-server
# =========================== /etc/default/nfs-kernel-server
# 添加一行
RPCMOUNTDOPTS="--manage-gids --port 30000"
# ===========================
systemctl restart nfs-kernel-server
ufw allow 30000/tcp
ufw allow 30000/udp

# 4.允许nlockmgr服务
rpcinfo -p 127.0.0.1 | grep nlockmgr
ufw allow 46558/udp
ufw allow 39029/tcp

# 5.其他
ufw reload
ufw status
```

2.2 executor挂载prebuilts

```sh
cd /tmp
apt install nfs-common -y
mkdir -p prebuilts

showmount -e [serverIP]

mount [serverIP]:/.../OpenHarmony-v4.1-Release/OpenHarmony/prebuilts /tmp/prebuilts
# 查看挂载信息
df -h

#开机自动挂载
# =========================== /etc/fstab
#在主机 B 上编辑 /etc/fstab，添加：
[serverIP]:/data/nfs_share  /xxx  nfs  defaults  0  0
# ===========================
# 重新加载fstab配置
mount -a 
```


> [!tip] 注
> 测试阶段，可以直接链接prebuilts目录；
> ln -s /.../OpenHarmony-v4.1-Release/OpenHarmony/prebuilts /tmp

### 3.编译

3.1 ninja2

```sh
git clone git@github.com:ninja-cloudbuild/ninja2.git
cd ninja2
./build.sh build
```

3.2 配置 $HOME/.ninja2.conf

```
# 将 .ninja2.conf 复制到$HOME目录中
cloud_build: true
grpc_url: "grpc://localhost:1985"  
share_build: false
```

3.3 .cloudbuild.yml

```
# xxx/.cloudbuild.yml
# 作用：配置 RBE（远程构建执行）的命令规则
# 将 .cloudbuild.yml 文件复制到鸿蒙项目根目录下
```

### 1.3.3 openharmony

```sh
rm prebuilts/build-tools/linux-x86/bin/ninja
ll prebuilts/build-tools/linux-x86/bin/ninja
cp /.../ninja2/build/bin/ninja prebuilts/build-tools/linux-x86/bin/
./build.sh --product-name rk3568 --ccache=false --ninja-args="-r/.../OpenHarmony-v4.1-Release/OpenHarmony"
```
