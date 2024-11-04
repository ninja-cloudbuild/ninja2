FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    sudo \
    apt-utils \
    git cmake g++ gcc googletest libgmock-dev libssl-dev pkg-config uuid-dev grpc++ libprotobuf-dev protobuf-compiler-grpc ninja-build libyaml-cpp-dev

WORKDIR /app

COPY . .

RUN ./build.sh build

CMD ["/bin/bash"]