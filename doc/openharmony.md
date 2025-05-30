# Compiling Harmony with Ninja2

## 一、本地编译

1.获取OpenHarmony源码

（1）基于docker编译

[harmony编译](https://gitee.com/cloudbuild888/cloudbuild/blob/master/doc/projects/openharmony.md)

（2）搭建环境编译

```sh
# 开发环境
Ubuntu18.04及以上版本，X86_64架构，内存推荐16 GB及以上。
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

### 二、分布式编译

1.启动buildbuddy

```sh
bazel run //enterprise/server:server
```

```sh
bazel run //enterprise/server/cmd/executor:executor
```


2.使用ninja2分布式编译

```sh
./build.sh --product-name rk3568 --ccache --ninja-args="-cgrpc://192.168.211.159:1985" --ninja-args="-r/home/openharmony/"
```