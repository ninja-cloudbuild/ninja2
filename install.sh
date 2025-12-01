#!/bin/bash
# Copyright 2024 Mengning Software All rights reserved.

# Exit immediately if a command exits with a non-zero status
set -e

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Function to display success message
function success {
  echo -e "${GREEN}$1${NC}"
}

# Function to display failure message
function failure {
  echo -e "${RED}$1${NC}"
  exit 1
}


SERVICE_NAME="sharebuild-server"  # 服务名称
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"  # systemd服务文件路径

RUN_USER="root"  


# Detect the OS type and install dependencies
function install_dependencies {
  # Install dependencies based on the OS type
  success "dependencies install begin: " 
  OS_ID=$(grep '^ID=' /etc/os-release | cut -d= -f2 | tr -d '"')

  case $OS_ID in
    ubuntu|debian)
        # 1. 安装并配置Redis（已存在则跳过安装，仅检查配置）
      if ! dpkg -l | grep -q '^ii  redis-server'; then  # 检查Redis是否已安装
        sudo apt update && sudo apt install redis-server -y
        success "Redis已安装"
      else
        success "Redis已存在，跳过安装"
      fi

      # 检查并修改Redis绑定配置（确保开放局域网访问）
      if ! grep -q '^bind 0.0.0.0' /etc/redis/redis.conf; then  # 配置未生效
        sudo sed -i 's/^bind .*/bind 0.0.0.0/' /etc/redis/redis.conf
        sudo systemctl restart redis
        success "Redis已配置为开放局域网访问"
      else
        success "Redis配置已生效，跳过修改"
      fi


      # 2. 安装并配置NFS（已存在则跳过安装，仅检查共享规则）
      if ! dpkg -l | grep -q '^ii  nfs-kernel-server'; then  # 检查NFS服务是否已安装
        sudo apt install -y nfs-kernel-server
        success "NFS服务已安装"
      else
        success "NFS服务已存在，跳过安装"
      fi

      # 检查并添加NFS共享规则（避免重复添加）
      local nfs_rule='/home/ *(rw,root_squash,anonuid=1000,anongid=1000,insecure,async,no_subtree_check)'
      if ! grep -qF "$nfs_rule" /etc/exports; then  # 规则不存在
        echo "$nfs_rule" | sudo tee -a /etc/exports
        sudo exportfs -a  
        sudo systemctl restart nfs-kernel-server
        success "NFS共享规则已添加并生效"
      else
        success "NFS共享规则已存在，跳过添加"
      fi
      
     
      #sudo apt-get update
      #sudo apt-get install -y git cmake g++ gcc googletest libgmock-dev libssl-dev pkg-config uuid-dev grpc++ libprotobuf-dev protobuf-compiler-grpc ninja-build libyaml-cpp-dev 
      ;;
    centos)
      #sudo yum update -y
      #sudo yum install -y epel-release
      #sudo yum groupinstall -y "Development Tools"
      #sudo yum install -y git cmake3 gtest gtest-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel 
      ;;
    fedora)
      #sudo dnf update -y
      #sudo dnf group install -y "Development Tools"
      #sudo dnf install -y git cmake g++ gtest gtest-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel 
      ;;
    *)
      failure "Unsupported OS: $OS_ID"
      exit 1
      ;;
  esac
}

# Function to install the built ninja binary and configure ShareBuild service
function install {
  if [ ! -f "new_ninja2.tar.gz" ]; then
    failure "new_ninja2.tar.gz package not found."
    exit 1
  fi
  install_dependencies
  tar -zxvf new_ninja2.tar.gz
  local install_path="/usr/bin/ninja"
  local backup_path="/usr/bin/ninja.prev"
  local config_file="/etc/ninja2.conf"

  local sharebuid_install_path="/usr/bin/sharebuild-server"
  local android_ninja_install_path="/usr/bin/android_ninja"
  # Backup existing ninja if it exists
  if [ -f "$install_path" ]; then
    sudo mv "$install_path" "$backup_path"
    success "Backed up original ninja to $backup_path"
  fi

  # Install new ninja binary
  sudo cp "ninja2/ninja" "$install_path"

  # Install new sharebuild-server binary
  sudo cp "ninja2/sharebuild-server" "$sharebuid_install_path"

  # Install new android_ninja binary
  sudo cp "ninja2/android_ninja" "$android_ninja_install_path"

  if [ ! -f "$config_file" ]; then 
	  sudo cp "ninja2/ninja2.conf" "$config_file"
  fi
  sudo cp -n ninja2/*.so.* /usr/local/lib/
  sudo ldconfig


  # ==============================================
  # 新增：配置ShareBuild服务（自动启动+开机自启）
  # ==============================================
  success "\nConfiguring ShareBuild auto-start service..."

  # 1. 创建systemd服务文件
  sudo tee "$SERVICE_FILE" <<EOF > /dev/null
[Unit]
Description=ShareBuild Server (Distributed Build System)
After=network.target  
Wants=redis-server.service  

[Service]
User=$RUN_USER  
ExecStart=$sharebuid_install_path  
Restart=always  
RestartSec=3  
Environment="PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"  

[Install]
WantedBy=multi-user.target  
EOF

  # 2. 重新加载systemd配置
  sudo systemctl daemon-reload || failure "Failed to reload systemd config"

  # 3. 启用开机自启
  sudo systemctl enable "$SERVICE_NAME" || failure "Failed to enable $SERVICE_NAME service"

  # 4. 立即启动服务
  sudo systemctl start "$SERVICE_NAME" || failure "Failed to start $SERVICE_NAME service"

  # 5. 验证服务状态
  if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
    success "ShareBuild service started successfully (auto-start enabled)"
  else
    failure "ShareBuild service failed to start. Check logs: sudo journalctl -u $SERVICE_NAME"
  fi


  
  success "---------------------------------"
  success "New Ninja2 installed successfully at $install_path"
  success "New ShareBuild installed successfully at $sharebuid_install_path"
  success "New android_ninja installed successfully at $android_ninja_install_path"
  success "ShareBuild service name: $SERVICE_NAME"
  success "---------------------------------"
  success "Ninja2 is the second generation of Ninja that supports CloudBuild(Distributed Build System) and Ninja2 is fully compatible with the Ninja."
  success "CloudBuild refer to https://gitee.com/cloudbuild888/cloudbuild"
  success "Service management commands:"
  success "  - Check status: sudo systemctl status $SERVICE_NAME"
  success "  - Stop service: sudo systemctl stop $SERVICE_NAME"
  success "  - Restart service: sudo systemctl restart $SERVICE_NAME"
  success "Copyright 2024 Mengning Software All rights reserved."
}

# Main script
case "$1" in
  start)
    install
    rm -rf new_ninja2.tar.gz  install.sh
    ;;
  *)
    wget -c --no-check-certificate https://github.com/ninja-cloudbuild/ninja2/releases/download/v2.0.1/new_ninja2.tar.gz
    tar -zxvf new_ninja2.tar.gz
    install
    rm -rf new_ninja2.tar.gz  install.sh
    ;;
esac