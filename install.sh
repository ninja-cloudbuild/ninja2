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

# Detect the OS type and install dependencies
function install_dependencies {
  # Install dependencies based on the OS type
  success "dependencies install begin: " 
  OS_ID=$(grep '^ID=' /etc/os-release | cut -d= -f2 | tr -d '"')

  case $OS_ID in
    ubuntu|debian)
    #   sudo apt-get update
    #   sudo apt-get install -y libyaml-cpp-dev
      ;;
    centos)
    #   sudo yum update -y
    #   sudo yum install -y epel-release
    #   sudo yum groupinstall -y "Development Tools"
    #   sudo yum install -y yaml-cpp yaml-cpp-devel
      ;;
    fedora)
    #   sudo dnf update -y
    #   sudo dnf group install -y "Development Tools"
    #   sudo dnf install -y yaml-cpp yaml-cpp-devel
      ;;
    *)
      failure "Unsupported OS: $OS_ID"
      exit 1
      ;;
  esac
}

# Function to install the built ninja binary
function install {
  if [ ! -f "ninja2/ninja" ]; then
    failure "Ninja binary not found."
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
  sudo cp "ninja2/ninja" "$install_path"
  success "---------------------------------"
  success "New Ninja2 installed successfully at $install_path"
  success "---------------------------------"
  success "Ninja2 is the second generation of Ninja that supports CloudBuild(Distributed Build System) and Ninja2 is fully compatible with the Ninja."
  success "CloudBuild refer to https://gitee.com/cloudbuild888/cloudbuild"
}

# Main script
wget -c https://github.com/ninja-cloudbuild/ninja2/releases/download/v2.0.0/ninja2.tar.gz
tar -zxvf ninja2.tar.gz
install

