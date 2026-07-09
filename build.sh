#!/bin/bash

if [ ! -d "build" ]; then
    mkdir build
fi

cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel

if [ ! -d "$HOME/.local/bin" ]; then
	mkdir "$HOME/.local/bin"
fi
	
if [ -f "./bin/mkFileSearch" ]; then
    strip ./bin/mkFileSearch
    cp ./bin/mkFileSearch $HOME/.local/bin/
fi

if [ -f "./bin/mkFolderWidget" ]; then
    strip ./bin/mkFolderWidget
    cp ./bin/mkFolderWidget $HOME/.local/bin/
fi

if [ -f "./bin/mkLauncher" ]; then
    strip ./bin/mkLauncher
    cp ./bin/mkLauncher $HOME/.local/bin/
fi

if [ -f "./bin/mkTransactionHandler" ]; then
    strip ./bin/mkTransactionHandler
    cp ./bin/mkTransactionHandler $HOME/.local/bin/
fi
