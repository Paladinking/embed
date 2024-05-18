@echo off

call vcvarsall /clean_env
call vcvarsall x64
python test.py --create VC64
call vcvarsall /clean_env
call vcvarsall x86
python test.py --create VC86
python test.py --create MINGW
python test.py --create GCC32
python test.py --create GCC64
