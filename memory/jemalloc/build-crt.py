#!/usr/bin/env python

import os, sys

if 'MAKEFLAGS' in os.environ:
  del os.environ['MAKEFLAGS']
os.chdir(sys.argv[1])
sys.exit(os.system('nmake dll_ mt RETAIL_DLL_NAME=mozcrt19 RETAIL_LIB_NAME=msvcrt'))
