FROM debian

ARG DEBIAN_FRONTEND=noninteractive
RUN echo set bell-style none > /etc/inputrc

RUN apt update && apt install -y wget clang curl unzip git libc6-dev-i386-cross libc6-dev-i386 && rm -rf /var/lib/apt/lists/*

RUN wget https://github.com/clangd/clangd/releases/download/18.1.3/clangd-linux-18.1.3.zip && unzip clangd-linux-18.1.3.zip && rm clangd-linux-18.1.3.zip && mv clangd_18.1.3 clangd

RUN wget https://github.com/neovim/neovim/releases/latest/download/nvim-linux64.tar.gz && tar -xf nvim-linux64.tar.gz && rm nvim-linux64.tar.gz && mv nvim-linux64 nvim

ENV PATH=/clangd/bin:/nvim/bin:$PATH

RUN adduser me
USER me
WORKDIR /home/me

RUN mkdir .config && mkdir .config/nvim && cd .config/nvim && wget https://raw.githubusercontent.com/Paladinking/Scripts/master/data/init.vim

WORKDIR embed/tests
