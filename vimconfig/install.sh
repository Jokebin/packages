#!/bin/sh

sudo apt install vim cscope ctags -y
cp vimrc.conf ~/.vimrc
mkdir -p ~/.vim/bundle
mkdir -p ~/.vim/cscope_db

cp plugin ~/.vim -rf
git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim

git clone https://github.com/tomasr/molokai.git
mv molokai/colors ~/.vim
rm molokai -rf
