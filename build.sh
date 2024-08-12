#!/bin/bash

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

# Function to clean build directory
function clean {
  if [ -d "build" ]; then
    rm -rf build
    success "Cleaned build directory."
  else
    echo "No build directory to clean."
  fi
}

# Detect the OS type and install dependencies
function install_dependencies {
  # Install dependencies based on the OS type
  success "dependencies install begin: " 
  OS_ID=$(grep '^ID=' /etc/os-release | cut -d= -f2 | tr -d '"')

  case $OS_ID in
    ubuntu|debian)
      sudo apt-get update
      sudo apt-get install -y git cmake g++ gcc googletest libgmock-dev libgoogle-glog-dev libssl-dev pkg-config uuid-dev grpc++ libprotobuf-dev protobuf-compiler-grpc ninja-build libyaml-cpp-dev
      ;;
    centos)
      sudo yum update -y
      sudo yum install -y epel-release
      sudo yum groupinstall -y "Development Tools"
      sudo yum install -y git cmake3 gtest gtest-devel glog glog-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel
      ;;
    fedora)
      sudo dnf update -y
      sudo dnf group install -y "Development Tools"
      sudo dnf install -y git cmake g++ gtest gtest-devel glog glog-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel
      ;;
    *)
      failure "Unsupported OS: $OS_ID"
      exit 1
      ;;
  esac
}

# Function to install the built ninja binary
function install {
  if [ ! -f "build/bin/ninja" ]; then
    failure "Ninja binary not found in build directory. Please build first."
    exit 1
  fi
  
  local install_path="/usr/bin/ninja"
  local backup_path="/usr/bin/ninja.prev"

  # Backup existing ninja if it exists
  if [ -f "$install_path" ]; then
    sudo mv "$install_path" "$backup_path"
    success "Backed up original ninja to $backup_path"
  fi

  # Install new ninja binary
  sudo cp "build/bin/ninja" "$install_path"
  success "New ninja installed at $install_path"
}

# Function to install dependencies and build the project
function build {
  success "begin build: "
  install_dependencies
  clean

  # Create build directory and build
  mkdir -p build
  cd build
  cmake -G Ninja ..
  ninja

  success "Build succeeded. Build output is located in $(pwd)"
}

# Main script
case "$1" in
  build)
    build
    ;;
  clean)
    clean
    ;;
  install)
    install
    ;;
  *)
    echo "Usage: $0 {build|clean|install}"
    exit 1
    ;;
esac

