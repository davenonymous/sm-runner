# vim: set sts=2 ts=8 sw=2 tw=99 et:
# Copyright 2026 davenonymous
#
# Configures the AMBuild build of smrunner and SourceMod's logic library,
# for example:
#   python configure.py --sm-path=/build/sourcemod --mms-path=/build/metamod \
#     --sp-build=/build/sourcepawn-build --libs-path=/build/libs --targets=x86,x86_64 --enable-optimize
import sys

try:
  from ambuild2 import run
except ImportError:
  sys.stderr.write('AMBuild 2.2 or later must be installed to build this project.\n')
  sys.exit(1)

parser = run.BuildParser(sourcePath=sys.path[0], api='2.2')
parser.options.add_argument('--sm-path', type=str, dest='sm_path', default=None,
                            help='Path to SourceMod (with its sourcepawn submodule)')
parser.options.add_argument('--mms-path', type=str, dest='mms_path', default=None,
                            help='Path to Metamod:Source (for SourceHook headers)')
parser.options.add_argument('--sp-build', type=str, dest='sp_build', default=None,
                            help='Build folder of SourcePawn\'s own configure.py (for its static libraries)')
parser.options.add_argument('--libs-path', type=str, dest='libs_path', default=None,
                            help='Folder of the vcpkg libraries, x86/ and x64/ (libcurl, Mbed TLS, nlohmann/json)')
parser.options.add_argument('--enable-debug', action='store_const', const='1', dest='debug',
                            help='Enable debugging symbols')
parser.options.add_argument('--enable-optimize', action='store_const', const='1', dest='opt',
                            help='Enable optimization')
parser.options.add_argument('--targets', type=str, dest='targets', default='x86,x86_64',
                            help='Target architectures, separated by commas')
parser.Configure()
