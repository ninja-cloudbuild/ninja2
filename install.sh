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
}

# Detect the OS type and install dependencies
function install_dependencies {
  # Install dependencies based on the OS type
  success "dependencies install begin: " 
  OS_ID=$(grep '^ID=' /etc/os-release | cut -d= -f2 | tr -d '"')

  case $OS_ID in
    ubuntu|debian)
      # sudo apt-get update
      # sudo apt-get install -y git cmake g++ gcc googletest libgmock-dev libssl-dev pkg-config uuid-dev grpc++ libprotobuf-dev protobuf-compiler-grpc ninja-build libyaml-cpp-dev
      ;;
    centos)
      # sudo yum update -y
      # sudo yum install -y epel-release
      # sudo yum groupinstall -y "Development Tools"
      # sudo yum install -y git cmake3 gtest gtest-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel
      ;;
    fedora)
      # sudo dnf update -y
      # sudo dnf group install -y "Development Tools"
      # sudo dnf install -y git cmake g++ gtest gtest-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel
      ;;
    *)
      failure "Unsupported OS: $OS_ID"
      exit 1
      ;;
  esac
}

# Function to install the built ninja binary
function install {
  if [ ! -f "ninja2.tar.gz" ]; then
    failure "ninja2.tar.gz package not found."
    exit 1
  fi
  install_dependencies
  tar -zxvf ninja2.tar.gz
  local install_path="/usr/bin/ninja"
  local backup_path="/usr/bin/ninja.prev"
  local config_file="/etc/ninja2.conf"
  local backup_config_file="/etc/ninja2.conf.prev"

  # Backup existing ninja if it exists
  if [ -f "$install_path" ]; then
    sudo mv "$install_path" "$backup_path"
    success "Backed up original ninja to $backup_path"
  fi
  # Backup existing /etc/ninja2.conf if it exists
  if [ -f "$config_file" ]; then
    sudo mv "$config_file" "$backup_config_file"
    success "Backed up original $config_file to $backup_config_file"
  fi

  # Install new ninja binary
  sudo cp "ninja2/ninja" "$install_path"
  sudo cp "ninja2/ninja2.conf" "$config_file"
  sudo cp -n ninja2/*.so.* /usr/local/lib/
  sudo ldconfig
  success "---------------------------------"
  success "New Ninja2 installed successfully at $install_path"
  success "---------------------------------"
  success "Ninja2 is the second generation of Ninja that supports CloudBuild(Distributed Build System) and Ninja2 is fully compatible with the Ninja."
  success "CloudBuild refer to https://gitee.com/cloudbuild888/cloudbuild"
  success "Copyright 2024 Mengning Software All rights reserved."
}

# Main script
case "$1" in
  start)
    install
    rm -rf ninja2.tar.gz ninja2/ install.sh
    ;;
  *)
    wget -c https://github.com/ninja-cloudbuild/ninja2/releases/download/v2.0.0/ninja2.tar.gz
    tar -zxvf ninja2.tar.gz
    install
    rm -rf ninja2.tar.gz ninja2/ install.sh
    ;;
esac

