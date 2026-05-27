FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    cmake \
    ninja-build \
    qt6-base-dev \
    libgl1-mesa-dev \
    libxcb-xinerama0-dev \
    libxcb-cursor-dev \
    git \
    && rm -rf /var/lib/apt/lists/*
