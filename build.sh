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
      sudo apt-get install -y git cmake g++ gcc googletest libgmock-dev libssl-dev pkg-config uuid-dev grpc++ libprotobuf-dev protobuf-compiler-grpc ninja-build libyaml-cpp-dev
      ;;
    centos)
      sudo yum update -y
      sudo yum install -y epel-release
      sudo yum groupinstall -y "Development Tools"
      sudo yum install -y git cmake3 gtest gtest-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel
      ;;
    fedora)
      sudo dnf update -y
      sudo dnf group install -y "Development Tools"
      sudo dnf install -y git cmake g++ gtest gtest-devel openssl openssl-devel pkgconfig uuid-devel grpc-devel protobuf protobuf-devel protobuf-compiler ninja-build yaml-cpp yaml-cpp-devel
      ;;
    *)
      failure "Unsupported OS: $OS_ID"
      exit 1
      ;;
  esac
}

# Function to install the built ninja binary
function install {
  if [ ! -f "build/bin/ninja2.tar.gz" ]; then
    failure "Please build and package first."
    exit 1
  fi
  cp "install.sh" "build/bin/"
  (cd "build/bin/" && ./install.sh start)
}

# Function to package the built ninja binary
function package {
  if [ ! -f "build/bin/ninja" ]; then
    failure "Ninja binary not found in build directory. Please build first."
    exit 1
  fi
  
  # package new ninja binary
  mkdir -p build/bin/ninja2
  cp "build/bin/ninja" "build/bin/ninja2/"
  cp $(ldd ./build/bin/ninja | awk '{print $3}' | grep '/lib')  ./build/bin/ninja2/

  cat <<EOL > "build/bin/ninja2/ninja2.conf"
# Copyright 2024 Mengning Software All rights reserved.  
# /etc/ninja2.conf example

cloudbuild: false  
grpc_url: "grpc://localhost:1985"  
sharebuild: false  
EOL

  (cd build/bin/ && tar -zcvf ninja2.tar.gz ninja2/* && rm -rf ninja2)
  success "New Ninja2 package ninja2.tar.gz at build/bin/"
}

# Function to install dependencies and build the project
function build {
  # Create build directory and build
  mkdir -p build
  if [ ! -f "build/build.ninja" ]; then
    (cd build && cmake -G Ninja ..)
  fi
  (cd build && ninja)

  success "Build successfully. Build output is located in build/bin/"
}

# Main script
case "$1" in
  all)
    install_dependencies
    clean
    build
    package
    install
    ;;
  build)
    build
    ;;
  package|pkg)
    package
    ;;
  install)
    install
    ;;
  clean)
    clean
    ;;
  *)
    echo "Usage: $0 {all|build|package|install|clean}"
    success "Please use ./build.sh all for the first compilation."
    exit 1
    ;;
esac

