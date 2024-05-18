@echo off


cl ..\embed.c /Fe:embed-VC64.exe

python test.py --create VC64
